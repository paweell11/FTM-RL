import random
from ctypes import *
from py_interface import *

class Env(Structure):
    _pack_ = 1
    _fields_ = [
        ('ftmNumberOfBurstsExponent', c_uint8),
        ('ftmBurstDuration', c_uint8),
        ('ftmMinDeltaFtm', c_uint8),
        ('ftmPartialTsfTimer', c_uint16),
        ('ftmPartialTsfNoPref', c_bool),
        ('ftmAsap', c_bool),
        ('ftmFtmsPerBurst', c_uint8),
        ('ftmBurstPeriod', c_uint16),
    ]

class Act(Structure):
    _pack_ = 1
    _fields_ = [
        ('ftmNumberOfBurstsExponent', c_uint8),
        ('ftmBurstDuration', c_uint8),
        ('ftmMinDeltaFtm', c_uint8),
        ('ftmPartialTsfTimer', c_uint16),
        ('ftmPartialTsfNoPref', c_bool),
        ('ftmAsap', c_bool),
        ('ftmFtmsPerBurst', c_uint8),
        ('ftmBurstPeriod', c_uint16),
        ('apply', c_bool), 
    ]


mempool_key = 1234
mem_size = 4096
memblock_key = 2333
ns3_path = '.'
exp_name = 'scenario'

exp = Experiment(mempool_key, mem_size, exp_name, ns3_path)

try:
    for i in range(1):  # liczba symulacji
        exp.reset()
        rl = Ns3AIRL(memblock_key, Env, Act)

        pro = exp.run(setting={}, show_output=True)

        while not rl.isFinish():
            with rl as data:
                if data is None:
                    break  
                # ustaw akcję
                data.act.ftmNumberOfBurstsExponent = 1
                data.act.ftmBurstDuration = random.randint(1, 10)
                data.act.ftmMinDeltaFtm = random.randint(1, 10)
                data.act.ftmPartialTsfTimer = 0
                data.act.ftmPartialTsfNoPref = True
                data.act.ftmAsap = random.choice([True, False])
                data.act.ftmFtmsPerBurst = random.randint(1, 10)
                data.act.ftmBurstPeriod = random.randint(1, 15)
                data.act.apply = True
                print("PY sent:",
                    data.act.ftmBurstDuration,
                    data.act.ftmMinDeltaFtm,
                    data.act.ftmFtmsPerBurst,
                    data.act.ftmBurstPeriod,
                    data.act.ftmAsap)

        pro.wait()


except Exception as e:
    print('Something wrong')
    print(e)
finally:
    del exp
