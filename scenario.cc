#include <chrono>
#include <map>
#include <string>

#include "ns3/applications-module.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ftm-header.h"
#include "ns3/ftm-error-model.h"
#include "ns3/ftm-session.h"
#include "ns3/internet-module.h"
#include "ns3/mac48-address.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/node-container.h"
#include "ns3/node-list.h"
#include "ns3/ssid.h"
#include "ns3/wifi-net-device.h"
#include "ns3/yans-wifi-helper.h"

#include "ns3/core-module.h"
#include "ns3/ns3-ai-module.h"
#include "ns3/system-path.h" 

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ftm-optimal");


struct Env
{
  uint8_t  ftmNumberOfBurstsExponent;
  uint8_t  ftmBurstDuration;
  uint8_t  ftmMinDeltaFtm;
  uint16_t ftmPartialTsfTimer;
  bool     ftmPartialTsfNoPref;
  bool     ftmAsap;
  uint8_t  ftmFtmsPerBurst;
  uint16_t ftmBurstPeriod;
  uint32_t attempts;   
  uint32_t successes;  
}Packed;

struct Act
{
  uint8_t  ftmNumberOfBurstsExponent;
  uint8_t  ftmBurstDuration;
  uint8_t  ftmMinDeltaFtm;
  uint16_t ftmPartialTsfTimer;
  bool     ftmPartialTsfNoPref;
  bool     ftmAsap;
  uint8_t  ftmFtmsPerBurst;
  uint16_t ftmBurstPeriod;
  bool     apply;
}Packed;


class FTMControl : public Ns3AIRL<Env, Act>
{
public:
    FTMControl(uint16_t id);
    Act GetFTMParams(const Env& env);
};

FTMControl::FTMControl(uint16_t id) : Ns3AIRL<Env, Act>(id)
{
    SetCond(2, 0);  
}

Act FTMControl::GetFTMParams(const Env& env)
{
    auto envPtr = EnvSetterCond();
    *envPtr = env;
    SetCompleted();

    auto actPtr = ActionGetterCond();
    Act result = *actPtr;
    GetCompleted();

    return result;
}

namespace {
  // co ile sesji zmieniać parametry:
  static uint32_t g_changeEvery = 10;
  static uint32_t g_sessionsSinceChange = 0;

  static uint32_t g_sessionsTotal = 0; // wszystkie zakończone sesje
  static uint32_t g_sessionsOk = 0; // udane sesje

  static FTMControl* g_ftmCtrl = nullptr;
}


void SetFtmParams (FtmParams ftmParams);


static void ApplyFtmFromPython()
{
  if (!g_ftmCtrl) return;

  Env env{};    
  env.attempts = g_sessionsTotal;
  env.successes = g_sessionsOk;                     
  Act act = g_ftmCtrl->GetFTMParams(env);

  if (!act.apply) return;

  FtmParams p;
  p.SetNumberOfBurstsExponent(act.ftmNumberOfBurstsExponent);
  p.SetBurstDuration(act.ftmBurstDuration);
  p.SetMinDeltaFtm(act.ftmMinDeltaFtm);
  p.SetPartialTsfTimer(act.ftmPartialTsfTimer);
  p.SetPartialTsfNoPref(act.ftmPartialTsfNoPref);
  p.SetAsap(act.ftmAsap);
  p.SetFtmsPerBurst(act.ftmFtmsPerBurst);
  p.SetBurstPeriod(act.ftmBurstPeriod);

  SetFtmParams(p);
  
  g_sessionsTotal = 0;
  g_sessionsOk    = 0;
  

  std::cout << "[t=" << Simulator::Now().GetSeconds() << "s] APPLIED FTM: "
            << "BDur="    << int(act.ftmBurstDuration)
            << " MinΔ="   << int(act.ftmMinDeltaFtm)
            << " PerBurst=" << int(act.ftmFtmsPerBurst)
            << " Period=" << act.ftmBurstPeriod
            << " ASAP="   << act.ftmAsap
            << std::endl;
}




/***** Functions declarations *****/

void ChangePower (Ptr<Node> staNode, uint8_t powerLevel);
void GetWarmupFlows (Ptr<FlowMonitor> monitor);
void InstallTrafficGenerator (Ptr<ns3::Node> fromNode, Ptr<ns3::Node> toNode, uint32_t port,
                              DataRate offeredLoad, uint32_t packetSize, double startTime, double stopTime);
void LogSuccessRate ();
void PopulateArpCache ();
void SetPosition (Ptr<MobilityModel> mobilityModel, Vector3D pos);
void SetFtmParams (FtmParams ftmParams);
void FtmBurst (uint32_t staId, Ptr <WifiNetDevice> device, Mac48Address apAddress);
void FtmSessionOver (FtmSession session);

/***** Global variables and constants *****/

#define RTT_TO_DISTANCE 0.00015
#define MAX_DISTANCE 1000.0
#define DEFAULT_TX_POWER 16.0206

std::map<uint32_t, uint64_t> warmupFlows;
std::ostringstream logOutput;

uint64_t ftmReqSent = 0;
uint64_t ftmReqRec = 0;

double fuzzTime = 5.;
double ftmIntervalTime = 1.0;
double ftmParamsSwitch = 0.0;
double logInterval = 0.5;
double warmupTime = 10.;
double simulationTime = 50.;
bool hiddenCrossScenario = false;


/***** Main with scenario definition *****/

int
main (int argc, char *argv[])
{
  // Initialize default simulation parameters
  std::string csvPath = "results.csv";
  std::string logPath = "log.csv";
  std::string ftmMapPath = "";
  std::string lossModel = "LogDistance";
  std::string mobilityModel = "Distance";
  std::string pcapName = "ftm-pcap";

  uint32_t nWifi = 1;
  double distance = 10.;
  double delta = 0.;          // difference between 2 power levels in dB
  double powerInterval = 4.;  // mean (exponential) interval between power change

  bool ampdu = true;
  bool enableRtsCts = false;
  uint32_t packetSize = 1500;
  uint32_t dataRate = 10;
  uint32_t channelWidth = 20;
  uint32_t minGI = 800;

  double area = 40.;
  double nodeSpeed = 1.4;
  double nodePause = 20.;

  uint8_t ftmNumberOfBurstsExponent = 1;
  uint8_t ftmBurstDuration = 6;
  uint8_t ftmMinDeltaFtm = 4;
  uint16_t ftmPartialTsfTimer = 0;
  bool ftmPartialTsfNoPref = true;
  bool ftmAsap = true;
  uint8_t ftmFtmsPerBurst = 2;
  uint16_t ftmBurstPeriod = 2;

  // Parse command line arguments
  CommandLine cmd;
  cmd.AddValue ("ampdu", "Use AMPDU (boolean flag)", ampdu);
  cmd.AddValue ("area", "Size of the square in which stations are wandering (m) - only for RWPM mobility type", area);
  cmd.AddValue ("channelWidth", "Channel width (MHz)", channelWidth);
  cmd.AddValue ("csvPath", "Path to output CSV file", csvPath);
  cmd.AddValue ("dataRate", "Traffic generator data rate (Mb/s)", dataRate);
  cmd.AddValue ("delta", "Power change (dBm)", delta);
  cmd.AddValue ("distance", "Distance between AP and STAs (m) - only for Distance mobility type", distance);
  cmd.AddValue ("enableRtsCts", "Flag set to enable CTS/RTS protocol", enableRtsCts);
  cmd.AddValue ("ftmIntervalTime", "Interval between FTM bursts (s)", ftmIntervalTime);
  cmd.AddValue ("ftmMap", "Path to FTM wireless error map", ftmMapPath);
  cmd.AddValue ("ftmNumberOfBurstsExponent", "Number of bursts exponent", ftmNumberOfBurstsExponent);
  cmd.AddValue ("ftmBurstDuration", "Burst duration", ftmBurstDuration);
  cmd.AddValue ("ftmMinDeltaFtm", "Minimum delta FTM", ftmMinDeltaFtm);
  cmd.AddValue ("ftmPartialTsfTimer", "Partial TSF timer", ftmPartialTsfTimer);
  cmd.AddValue ("ftmPartialTsfNoPref", "Partial TSF no preference", ftmPartialTsfNoPref);
  cmd.AddValue ("ftmAsap", "ASAP capable", ftmAsap);
  cmd.AddValue ("ftmFtmsPerBurst", "FTMs per burst", ftmFtmsPerBurst);
  cmd.AddValue ("ftmBurstPeriod", "Burst period", ftmBurstPeriod);
  cmd.AddValue ("ftmParamsSwitch", "Time to switch FTM parameters (s)", ftmParamsSwitch);
  cmd.AddValue ("fuzzTime", "Maximum fuzz value (s)", fuzzTime);
  cmd.AddValue ("hiddenCrossScenario", "Flag set to enable hidden cross scenario", hiddenCrossScenario);
  cmd.AddValue ("powerInterval", "Interval between power change (s)", powerInterval);
  cmd.AddValue ("logPath", "Path to log file", logPath);
  cmd.AddValue ("logInterval", "Interval between log entries (s)", logInterval);
  cmd.AddValue ("lossModel", "Propagation loss model (LogDistance, Nakagami)", lossModel);
  cmd.AddValue ("minGI", "Shortest guard interval (ns)", minGI);
  cmd.AddValue ("mobilityModel", "Mobility model (Distance, RWPM, Hidden)", mobilityModel);
  cmd.AddValue ("nodeSpeed", "Maximum station speed (m/s) - only for RWPM mobility type",nodeSpeed);
  cmd.AddValue ("nodePause","Maximum time station waits in newly selected position (s) - only for RWPM mobility type",nodePause);
  cmd.AddValue ("nWifi", "Number of stations", nWifi);
  cmd.AddValue ("packetSize", "Packets size (B)", packetSize);
  cmd.AddValue ("pcapName", "Name of a PCAP file generated from the AP", pcapName);
  cmd.AddValue ("simulationTime", "Duration of simulation (s)", simulationTime);
  cmd.AddValue ("warmupTime", "Duration of warmup stage (s)", warmupTime);
  cmd.Parse (argc, argv);

  if (mobilityModel == "Hidden")
    {
      nWifi = hiddenCrossScenario ? 4 * nWifi : 2 * nWifi;
    }

  FtmParams defaultFtmParams;
  defaultFtmParams.SetNumberOfBurstsExponent(1);
  defaultFtmParams.SetBurstDuration(6);
  defaultFtmParams.SetMinDeltaFtm(4);
  defaultFtmParams.SetPartialTsfTimer(0);
  defaultFtmParams.SetPartialTsfNoPref(true);
  defaultFtmParams.SetAsap(true);
  defaultFtmParams.SetFtmsPerBurst(2);
  defaultFtmParams.SetBurstPeriod(2);

  FtmParams userFtmParams;
  userFtmParams.SetNumberOfBurstsExponent(ftmNumberOfBurstsExponent);
  userFtmParams.SetBurstDuration(ftmBurstDuration);
  userFtmParams.SetMinDeltaFtm(ftmMinDeltaFtm);
  userFtmParams.SetPartialTsfTimer(ftmPartialTsfTimer);
  userFtmParams.SetPartialTsfNoPref(ftmPartialTsfNoPref);
  userFtmParams.SetAsap(ftmAsap);
  userFtmParams.SetFtmsPerBurst(ftmFtmsPerBurst);
  userFtmParams.SetBurstPeriod(ftmBurstPeriod);

  // Print simulation settings to screen
  std::cout << std::endl
            << "Simulating an IEEE 802.11ax devices with the following settings:" << std::endl
            << "- frequency band: 5 GHz" << std::endl
            << "- total data rate: " << dataRate << " Mb/s" << std::endl
            << "- FTM error map: " << !ftmMapPath.empty () << std::endl
            << "- FTM interval: " << ftmIntervalTime << " s" << std::endl
            << "- power delta: " << delta << " dBm" << std::endl
            << "- power interval: " << powerInterval << " s" << std::endl
            << "- channel width: " << channelWidth << " Mhz" << std::endl
            << "- shortest guard interval: " << minGI << " ns" << std::endl
            << "- packets size: " << packetSize << " B" << std::endl
            << "- AMPDU: " << ampdu << std::endl
            << "- RTS/CTS protocol enabled: " << enableRtsCts << std::endl
            << "- number of stations: " << nWifi << std::endl
            << "- simulation time: " << simulationTime << " s" << std::endl
            << "- warmup time: " << warmupTime << " s" << std::endl
            << "- max fuzz time: " << fuzzTime << " s" << std::endl
            << "- FTM params switch time: " << ftmParamsSwitch << " s" << std::endl
            << "- log interval: " << logInterval << " s" << std::endl
            << "- loss model: " << lossModel << std::endl;

  if (mobilityModel == "Distance" || mobilityModel == "Hidden")
    {
      std::cout << "- mobility model: " << mobilityModel << std::endl
                << "- distance: " << distance << " m" << std::endl
                << std::endl;
    }
  else if (mobilityModel == "RWPM")
    {
      std::cout << "- mobility model: " << mobilityModel << std::endl
                << "- area: " << area << " m" << std::endl
                << "- max node speed: " << nodeSpeed << " m/s" << std::endl
                << "- max node pause: " << nodePause << " s" << std::endl
                << std::endl;
    }

  std::cout << "FTM user parameters:" << std::endl
            << "- number of bursts exponent: " << (uint32_t) ftmNumberOfBurstsExponent << std::endl
            << "- burst duration: " << (uint32_t) ftmBurstDuration << std::endl
            << "- minimum delta FTM: " << (uint32_t) ftmMinDeltaFtm << std::endl
            << "- partial TSF timer: " << (uint32_t) ftmPartialTsfTimer << std::endl
            << "- partial TSF no preference: " << ftmPartialTsfNoPref << std::endl
            << "- ASAP capable: " << ftmAsap << std::endl
            << "- FTMs per burst: " << (uint32_t) ftmFtmsPerBurst << std::endl
            << "- burst period: " << ftmBurstPeriod << std::endl
            << std::endl;

  // Load FTM map and configure FTM
  if (!ftmMapPath.empty ())
    {
      Ptr<WirelessFtmErrorModel::FtmMap> ftmMap = CreateObject<WirelessFtmErrorModel::FtmMap> ();
      ftmMap->LoadMap (ftmMapPath);
      Config::SetDefault ("ns3::WirelessFtmErrorModel::FtmMap", PointerValue (ftmMap));
    }

  Time::SetResolution (Time::PS);
  Config::SetDefault ("ns3::RegularWifiMac::QosSupported", BooleanValue (true));
  Config::SetDefault ("ns3::RegularWifiMac::FTM_Enabled", BooleanValue (true));
  Config::SetDefault ("ns3::WiredFtmErrorModel::Channel_Bandwidth",
                      StringValue ("Channel_" + std::to_string (channelWidth) + "_MHz"));

  // if (ftmParamsSwitch > 0.0)
  //   {
  //     SetFtmParams (defaultFtmParams);
  //     Simulator::Schedule (Seconds (warmupTime + ftmParamsSwitch), &SetFtmParams, userFtmParams);
  //   }
  // else
  //   {
  //     SetFtmParams (userFtmParams);
  //   }


  int memblock_key = 2333;
  static FTMControl ftm(memblock_key);
  g_ftmCtrl = &ftm;

  SetFtmParams(defaultFtmParams);

  // double stopTime = warmupTime + simulationTime;
  // Simulator::Schedule(Seconds(warmupTime + 0.1), &UpdateFtmParams, &ftm, stopTime);




  // Create AP and stations
  NodeContainer wifiApNode (1);
  NodeContainer wifiStaNodes (nWifi);

  // Configure mobility
  MobilityHelper mobility;

  if (mobilityModel == "Distance")
    {
      mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
      mobility.Install (wifiApNode);
      mobility.Install (wifiStaNodes);

      // Place AP at (distance, 0)
      Ptr<MobilityModel> mobilityAp = wifiApNode.Get (0)->GetObject<MobilityModel> ();
      mobilityAp->SetPosition (Vector3D (distance, 0., 0.));
    }
  else if (mobilityModel == "Hidden")
    {
      mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
      mobility.Install (wifiApNode);
      mobility.Install (wifiStaNodes);

      // Place AP at (0, 0)
      Ptr<MobilityModel> mobilityAp = wifiApNode.Get (0)->GetObject<MobilityModel> ();
      mobilityAp->SetPosition (Vector3D (0., 0., 0.));

      // Place Stations on both sides of AP, in (-distance, 0) and (distance, 0)
      int orientation_x = 0;
      int orientation_y = 0;
      Ptr<MobilityModel> mobilityStation;
      for (int i = 0; i < nWifi; i++)
        {
          if (hiddenCrossScenario)
            {
              orientation_y = i % 4 < 2 ? 1 : -1;
            }
          orientation_x = i % 2 == 0 ? 1 : -1;
          mobilityStation = wifiStaNodes.Get (i)->GetObject<MobilityModel> ();
          mobilityStation->SetPosition (Vector3D (orientation_x * distance, orientation_y * distance, 0.));
        }
    }
  else if (mobilityModel == "RWPM")
    {
      // Place AP at (0, 0)
      mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
      mobility.Install (wifiApNode);

      // Place nodes randomly in square extending from (0, 0) to (area, area)
      ObjectFactory pos;
      pos.SetTypeId ("ns3::RandomRectanglePositionAllocator");
      std::stringstream ssArea;
      ssArea << "ns3::UniformRandomVariable[Min=0.0|Max=" << area;
      pos.Set ("X", StringValue (ssArea.str () + "|Stream=2]"));
      pos.Set ("Y", StringValue (ssArea.str () + "|Stream=3]"));

      Ptr<PositionAllocator> taPositionAlloc = pos.Create ()->GetObject<PositionAllocator> ();
      mobility.SetPositionAllocator (taPositionAlloc);

      // Set random pause (from 0 to nodePause [s]) and speed (from 0 to nodeSpeed [m/s])
      std::stringstream ssSpeed;
      ssSpeed << "ns3::UniformRandomVariable[Min=0.0|Max=" << nodeSpeed << "|Stream=4]";
      std::stringstream ssPause;
      ssPause << "ns3::UniformRandomVariable[Min=0.0|Max=" << nodePause << "|Stream=5]";

      mobility.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                                 "Speed", StringValue (ssSpeed.str ()),
                                 "Pause", StringValue (ssPause.str ()),
                                 "PositionAllocator", PointerValue (taPositionAlloc));

      mobility.Install (wifiStaNodes);

      for (uint32_t j = 0; j < wifiStaNodes.GetN (); ++j)
        {
          Ptr<MobilityModel> mobilityModel = wifiStaNodes.Get (j)->GetObject<MobilityModel> ();

          if (nodeSpeed == 0.)
            {
              Simulator::Schedule (Seconds (fuzzTime), &SetPosition, mobilityModel, mobilityModel->GetPosition ());
            }

          mobilityModel->SetPosition (Vector3D (0., 0., 0.));
        }
    }
  else
    {
      std::cerr << "Selected incorrect mobility model!";
      return 2;
    }

  // Print position of each node
  std::cout << "Node positions:" << std::endl;

  // AP position
  Ptr<MobilityModel> position = wifiApNode.Get (0)->GetObject<MobilityModel> ();
  Vector pos = position->GetPosition ();
  std::cout << "AP:\tx=" << pos.x << ", y=" << pos.y << std::endl;

  // Stations positions
  for (auto node = wifiStaNodes.Begin (); node != wifiStaNodes.End (); ++node)
    {
      position = (*node)->GetObject<MobilityModel> ();
      pos = position->GetPosition ();
      std::cout << "Sta " << (*node)->GetId () << ":\tx=" << pos.x << ", y=" << pos.y << std::endl;
    }

  std::cout << std::endl;

  // Configure wireless channel
  YansWifiPhyHelper phy;
  YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default ();

  if (lossModel == "Nakagami")
    {
      // Add Nakagami fading to the default log distance model
      channelHelper.AddPropagationLoss ("ns3::NakagamiPropagationLossModel");
    }
  else if (lossModel != "LogDistance")
    {
      std::cerr << "Selected incorrect loss model!";
      return 1;
    }

  phy.Set ("ChannelWidth", UintegerValue (channelWidth));
  phy.SetChannel (channelHelper.Create ());

  // Configure two power levels
  phy.Set ("TxPowerLevels", UintegerValue (2));
  phy.Set ("TxPowerStart", DoubleValue (DEFAULT_TX_POWER - delta));
  phy.Set ("TxPowerEnd", DoubleValue (DEFAULT_TX_POWER));

  // Configure MAC layer
  WifiMacHelper mac;
  WifiHelper wifi;

  wifi.SetStandard (WIFI_STANDARD_80211ax_5GHZ);
  wifi.SetRemoteStationManager ("ns3::IdealWifiManager");

  // Enable or disable CTS/RTS
  uint64_t ctsThrLow = 100;
  uint64_t ctsThrHigh = 100000000; // Arbitrarly large value, 100 MB for now
  UintegerValue ctsThr = (enableRtsCts ? UintegerValue (ctsThrLow) : UintegerValue (ctsThrHigh));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", ctsThr);

  // Set SSID
  Ssid ssid = Ssid ("ns3-80211ax");
  mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid), "MaxMissedBeacons",
               UintegerValue (1000)); // prevents exhaustion of association IDs

  // Create and configure Wi-Fi interfaces
  NetDeviceContainer staDevice;
  staDevice = wifi.Install (phy, mac, wifiStaNodes);

  mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));

  NetDeviceContainer apDevice;
  apDevice = wifi.Install (phy, mac, wifiApNode);
  
  // Manage AMPDU aggregation
  if (!ampdu)
    {
      Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
                   UintegerValue (0));
    }

  // Set shortest GI
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval",
               TimeValue (NanoSeconds (minGI)));

  // Install an Internet stack
  InternetStackHelper stack;
  stack.Install (wifiApNode);
  stack.Install (wifiStaNodes);

  // Configure IP addressing
  Ipv4AddressHelper address ("192.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer staNodeInterface = address.Assign (staDevice);
  Ipv4InterfaceContainer apNodeInterface = address.Assign (apDevice);

  // PopulateArpCache
  PopulateArpCache ();

  // Configure applications
  DataRate applicationDataRate = DataRate (0.1 * 1e6);
  uint32_t portNumber = 9;

  for (uint32_t j = 0; j < wifiStaNodes.GetN (); ++j)
    {
      InstallTrafficGenerator (wifiStaNodes.Get (j), wifiApNode.Get (0), portNumber++,
                               applicationDataRate, packetSize, 0., warmupTime);
    }

  applicationDataRate = DataRate (dataRate * 1e6 / nWifi);

  for (uint32_t j = 0; j < wifiStaNodes.GetN (); ++j)
    {
      InstallTrafficGenerator (wifiStaNodes.Get (j), wifiApNode.Get (0), portNumber++,
                               applicationDataRate, packetSize, warmupTime, warmupTime + simulationTime);
    }

  // Install FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
  Simulator::Schedule (Seconds (warmupTime), &GetWarmupFlows, monitor);



  // Generate PCAP at AP
  if (!pcapName.empty ())
    {
      std::string outDir = "../../pcaps";         
      ns3::SystemPath::MakeDirectories(outDir);
      std::string base = outDir + "/" + pcapName;

      phy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
      phy.EnablePcap(base, apDevice.Get(0), true);
    }

  // Schedule all power changes
  double time;
  bool maxPower;

  // The interval between each change follows the exponential distribution
  Ptr<ExponentialRandomVariable> x = CreateObject<ExponentialRandomVariable> ();
  x->SetAttribute ("Mean", DoubleValue (powerInterval));
  x->SetStream (1);

  for (uint32_t j = 0; j < wifiStaNodes.GetN (); ++j)
  {
    time = warmupTime;
    maxPower = false;

    while (time < simulationTime)
    {
      time += x->GetValue ();
      Simulator::Schedule (Seconds (time), &ChangePower, wifiStaNodes.Get (j), maxPower);
      maxPower = !maxPower;
    }
  }

  // Setup FTM bursts
  Ptr<UniformRandomVariable> offset = CreateObject<UniformRandomVariable> ();
  offset->SetAttribute ("Min", DoubleValue (0.));
  offset->SetAttribute ("Max", DoubleValue (ftmIntervalTime));
  offset->SetStream (2);

  for (uint32_t j = 0; j < wifiStaNodes.GetN (); ++j)
    {
      Simulator::Schedule (Seconds (warmupTime + offset->GetValue ()), &FtmBurst, j,
                           staDevice.Get (j)->GetObject<WifiNetDevice>(),
                           Mac48Address::ConvertFrom (apDevice.Get (0)->GetAddress ()));
    }

  // Log FTM success rate
  logOutput << "time,ftmSuccessRate" << std::endl;
  Simulator::Schedule (Seconds (warmupTime), &LogSuccessRate);

  // Define simulation stop time
  Simulator::Stop (Seconds (warmupTime + simulationTime));

  // Record start time
  std::cout << "Starting simulation..." << std::endl;
  auto start = std::chrono::high_resolution_clock::now ();

  Simulator::Run ();

  // Record stop time and count duration
  auto finish = std::chrono::high_resolution_clock::now ();
  std::chrono::duration<double> elapsed = finish - start;

  std::cout << "Done!" << std::endl
            << "Elapsed time: " << elapsed.count () << " s" << std::endl
            << std::endl;

  // Calculate per-flow throughput and Jain's fairness index
  double nWifiReal = 0;
  double jainsIndexN = 0.;
  double jainsIndexD = 0.;

  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  std::cout << "Results: " << std::endl;

  for (auto &stat : stats)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (stat.first);

      if (t.destinationPort < 9 + wifiStaNodes.GetN ())
        {
          continue;
        }

      double flow = (8 * stat.second.rxBytes - warmupFlows[stat.first]) / (1e6 * simulationTime);

      if (flow > dataRate / (50 * nWifi))
        {
          nWifiReal += 1;
          jainsIndexN += flow;
          jainsIndexD += flow * flow;
        }

      std::cout << "Flow " << stat.first << " (" << t.sourceAddress << " -> "
                << t.destinationAddress << ")\tThroughput: " << flow << " Mb/s" << std::endl;
    }

  double totalThr = jainsIndexN;
  double fairnessIndex = jainsIndexN * jainsIndexN / (nWifiReal * jainsIndexD);

  // Print results
  std::cout << std::endl
            << "Network throughput: " << totalThr << " Mb/s" << std::endl
            << "Jain's fairness index: " << fairnessIndex << std::endl
            << std::endl;

  // Gather results in CSV format
  double velocity = mobilityModel == "RWPM" ? nodeSpeed : 0.;
  double ftmSuccessRate = ftmReqRec / (double) ftmReqSent;

  std::ostringstream csvOutput;
  csvOutput << mobilityModel << ',' << velocity << ',' << distance << "," << nWifi << ',' << nWifiReal << ','
            << RngSeedManager::GetRun () << ',' << totalThr << ',' << ftmSuccessRate << std::endl;

  // Print results to std output
  std::cout << "mobility,velocity,distance,nWifi,nWifiReal,seed,throughput,ftmSuccessRate"
            << std::endl
            << csvOutput.str ();

  // Print results to file
  std::ofstream outputFile (csvPath);
  outputFile << csvOutput.str ();
  std::cout << std::endl << "Simulation data saved to: " << csvPath << std::endl << std::endl;

  std::ofstream logFile (logPath);
  logFile << logOutput.str ();
  std::cout << "Log data saved to: " << logPath << std::endl;

  //Clean-up
  Simulator::Destroy ();

  return 0;
}

/***** Function definitions *****/

void
ChangePower (Ptr<Node> staNode, uint8_t powerLevel)
{
  // Change power in STA
  Config::Set ("/NodeList/" + std::to_string (staNode->GetId ()) +
                   "/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/DefaultTxPowerLevel",
               UintegerValue (powerLevel));
}

void
GetWarmupFlows (Ptr<FlowMonitor> monitor)
{
  for (auto &stat : monitor->GetFlowStats ())
    {
      warmupFlows.insert (std::pair<uint32_t, double> (stat.first, 8 * stat.second.rxBytes));
    }
}

void
InstallTrafficGenerator (Ptr<ns3::Node> fromNode, Ptr<ns3::Node> toNode, uint32_t port,
                         DataRate offeredLoad, uint32_t packetSize, double startTime, double stopTime)
{
  // Get sink address
  Ptr<Ipv4> ipv4 = toNode->GetObject<Ipv4> ();
  Ipv4Address addr = ipv4->GetAddress (1, 0).GetLocal ();

  // Define type of service
  uint8_t tosValue = 0x70; //AC_BE

  // Add random fuzz to app start time
  if (startTime == 0.)
    {
      Ptr<UniformRandomVariable> fuzz = CreateObject<UniformRandomVariable> ();
      fuzz->SetAttribute ("Min", DoubleValue (0.));
      fuzz->SetAttribute ("Max", DoubleValue (fuzzTime));
      fuzz->SetStream (0);
      startTime += fuzz->GetValue ();
    }

  // Configure source and sink
  InetSocketAddress sinkSocket (addr, port);
  sinkSocket.SetTos (tosValue);
  PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory", sinkSocket);

  OnOffHelper onOffHelper ("ns3::UdpSocketFactory", sinkSocket);
  onOffHelper.SetConstantRate (offeredLoad, packetSize);

  // Configure applications
  ApplicationContainer sinkApplications (packetSinkHelper.Install (toNode));
  ApplicationContainer sourceApplications (onOffHelper.Install (fromNode));

  sinkApplications.Start (Seconds (startTime));
  sinkApplications.Stop (Seconds (stopTime));
  sourceApplications.Start (Seconds (startTime));
  sourceApplications.Stop (Seconds (stopTime));
}

void
LogSuccessRate ()
{
    static uint64_t lastReqSent = 0;
    static uint64_t lastReqRec = 0;

    uint64_t reqSent = ftmReqSent - lastReqSent;
    uint64_t reqRec = ftmReqRec - lastReqRec;
    double successRate = reqRec / (double) reqSent;
    lastReqSent = ftmReqSent;
    lastReqRec = ftmReqRec;

    logOutput << Simulator::Now ().GetSeconds () - warmupTime << "," << successRate << std::endl;
    Simulator::Schedule (Seconds (logInterval), &LogSuccessRate);
}

void
PopulateArpCache ()
{
  Ptr<ArpCache> arp = CreateObject<ArpCache> ();
  arp->SetAliveTimeout (Seconds (3600 * 24));

  for (auto i = NodeList::Begin (); i != NodeList::End (); ++i)
    {
      Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol> ();
      ObjectVectorValue interfaces;
      ip->GetAttribute ("InterfaceList", interfaces);

      for (auto j = interfaces.Begin (); j != interfaces.End (); j++)
        {
          Ptr<Ipv4Interface> ipIface = (*j).second->GetObject<Ipv4Interface> ();
          Ptr<NetDevice> device = ipIface->GetDevice ();
          Mac48Address addr = Mac48Address::ConvertFrom (device->GetAddress ());

          for (uint32_t k = 0; k < ipIface->GetNAddresses (); k++)
            {
              Ipv4Address ipAddr = ipIface->GetAddress (k).GetLocal ();
              if (ipAddr == Ipv4Address::GetLoopback ())
                {
                  continue;
                }

              ArpCache::Entry *entry = arp->Add (ipAddr);
              Ipv4Header ipv4Hdr;
              ipv4Hdr.SetDestination (ipAddr);

              Ptr<Packet> p = Create<Packet> (100);
              entry->MarkWaitReply (ArpCache::Ipv4PayloadHeaderPair (p, ipv4Hdr));
              entry->MarkAlive (addr);
            }
        }
    }

  for (auto i = NodeList::Begin (); i != NodeList::End (); ++i)
    {
      Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol> ();
      ObjectVectorValue interfaces;
      ip->GetAttribute ("InterfaceList", interfaces);

      for (auto j = interfaces.Begin (); j != interfaces.End (); j++)
        {
          Ptr<Ipv4Interface> ipIface = (*j).second->GetObject<Ipv4Interface> ();
          ipIface->SetAttribute ("ArpCache", PointerValue (arp));
        }
    }
}

void
SetPosition (Ptr<MobilityModel> mobilityModel, Vector3D pos)
{
  mobilityModel->SetPosition (pos);
}

void
SetFtmParams (FtmParams ftmParams)
{
  Ptr<FtmParamsHolder> ftmParamsHolder = CreateObject<FtmParamsHolder> ();
  ftmParamsHolder->SetFtmParams (ftmParams);
  Config::SetDefault ("ns3::FtmSession::DefaultFtmParams", PointerValue (ftmParamsHolder));
}

void
FtmBurst (uint32_t staId, Ptr<WifiNetDevice> device, Mac48Address apAddress)
{
  Ptr<RegularWifiMac> staMac = device->GetMac ()->GetObject<RegularWifiMac> ();
  Ptr<FtmSession> session = staMac->NewFtmSession (apAddress);

  if (session != NULL)
    {
      Ptr<WirelessSigStrFtmErrorModel> errorModel = CreateObject<WirelessSigStrFtmErrorModel> (RngSeedManager::GetRun ());
      errorModel->SetNode (device->GetNode ());

      session->SetFtmErrorModel (errorModel);
      session->SetSessionOverCallback (MakeCallback (&FtmSessionOver));
      session->SessionBegin ();

      ftmReqSent++;
    }

  Simulator::Schedule (Seconds (ftmIntervalTime), &FtmBurst, staId, device, apAddress);
}

void 
FtmSessionOver (FtmSession session)
{
  g_sessionsTotal++;

  double distance = session.GetMeanRTT () * RTT_TO_DISTANCE;
  if (distance != 0 && distance < MAX_DISTANCE)
  {
    ftmReqRec++;
    g_sessionsOk++;
  }

  // zmiana co N sesji (łącznie)
  g_sessionsSinceChange++;
  if (g_sessionsSinceChange >= g_changeEvery)
  {
    g_sessionsSinceChange = 0;
    ApplyFtmFromPython();
  }
}
