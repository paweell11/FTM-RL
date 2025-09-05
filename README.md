# FTM-RL

### Results
<img width="954" height="438" alt="image" src="https://github.com/user-attachments/assets/68a98899-ae53-495f-82e3-624630c0c81c" />
<img width="954" height="438" alt="image" src="https://github.com/user-attachments/assets/2092e77d-d57d-428a-b874-f1d3f54c2a39" />



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

If ./waf fails (e.g. with Python 3.10), add #include <limits> to $NS3_DIR/src/core/helper/csv-reader.cc.

