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
        ('attempts', c_uint32),  
        ('successes', c_uint32),  
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

def ftm_success_rate(e: Env):
    return (e.successes / e.attempts) if e.attempts else None

def set_random_params(a: Act):
    a.ftmNumberOfBurstsExponent = 1
    a.ftmBurstDuration = random.randint(1, 10)
    a.ftmMinDeltaFtm = random.randint(1, 10)
    a.ftmPartialTsfTimer = 0
    a.ftmPartialTsfNoPref = True
    a.ftmAsap = random.choice([True, False])
    a.ftmFtmsPerBurst = random.randint(1, 10)
    a.ftmBurstPeriod = random.randint(1, 15)
    a.apply = True

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
                    continue  
                
                e = data.env
                sr = ftm_success_rate(e)
                if sr is None:
                    print("PY recv: attempts=0 (brak SR)")
                else:
                    print(f"PY recv: attempts={e.attempts} succ={e.successes} rate={sr:.3f}")

                a = data.act
                set_random_params(a)

                print("PY sent:",
                    a.ftmBurstDuration,
                    a.ftmMinDeltaFtm,
                    a.ftmFtmsPerBurst,
                    a.ftmBurstPeriod,
                    a.ftmAsap)

        pro.wait()


except Exception as e:
    print('Something wrong')
    print(e)
finally:
    del exp
