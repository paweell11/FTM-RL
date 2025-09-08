# FTM-RL

### Overviev
This repository implements reinforcement learning (RL) to tune Wi-fi Fine Tune Measurements (FTM) parameters. Simulations run in ns-3, using ns3-ai for C++ ⬌ Python integration and ns3-ftm to handle FTM sessions. An agent (eg., Thompson Sampling or PPO) updates FTM parameters every N sessions based on success/attemps statistics. The goal is to improve FTM accuracy.

### Results
<img width="953" height="438" alt="image" src="https://github.com/user-attachments/assets/ac300a18-1df3-4b81-b92f-ac0378a7ede0" />
<img width="953" height="438" alt="image" src="https://github.com/user-attachments/assets/9b88adf9-1800-4e95-808e-8178a7ed273c" />



### Installation
1. **Clone the repositories**
```bash
git clone https://github.com/paweell11/FTM-RL.git

git clone https://github.com/tkn-tub/wifi-ftm-ns3.git

cd wifi-ftm-ns3/ns-allinone-3.33-FTM-SigStr/ns-3.33/contrib/  

git clone --depth 1 --branch v1.1.0 https://github.com/hust-diangroup/ns3-ai.git
```

2. **Move patched ns-3 to the target directory**
```bash
cd ../../../..
mv wifi-ftm-ns3/ns-allinone-3.33-FTM-SigStr/ns-3.33 $NS3_DIR
```
3. **Copy scenario files into the `scratch` directory**

```bash
cp $PROJECT_DIR/scenario.cc $NS3_DIR/scratch
cp $PROJECT_DIR/ThompsonSampling.py $NS3_DIR/scratch
cp $PROJECT_DIR/PPO.py $NS3_DIR/scratch
```

4. **Build ns-3** 
```bash
cd $NS3_DIR

./waf configure -d optimized --enable-examples --enable-tests --disable-werror --disable-python
./waf

cd $NS3_DIR/contrib/ns3-ai/py_interface
pip3 install . 
```
or (if you are not in venv)
```bash
pip3 install . --user
```

5. **Run program** 
```bash
cd $NS3_DIR

python scratch/ThompsonSampling.py
python scratch/PPO.py
```

If ./waf fails (e.g. with Python 3.10), add ```#include <limits>``` to $NS3_DIR/src/core/helper/csv-reader.cc.

