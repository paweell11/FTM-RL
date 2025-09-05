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
        ('nWifi', c_uint32),
        ('dataRate', c_uint32),
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


class BetaBernoulliTS:
    def __init__(self, arms):
        self.arms = list(arms)                 
        self.alpha = {a: 1.0 for a in self.arms}
        self.beta  = {a: 1.0 for a in self.arms}
        self.prev_arm = None                   

    def update_from_segment(self, attempts, successes):
        if self.prev_arm is None:
            return
        if attempts is None or attempts == 0:
            return
        successes = min(successes, attempts)
        failures = attempts - successes
        self.alpha[self.prev_arm] += successes
        self.beta[self.prev_arm]  += failures

    def select_arm(self):
        best_arm = None
        best_draw = -1.0
        for a in self.arms:
            draw = random.betavariate(self.alpha[a], self.beta[a])
            if draw > best_draw:
                best_draw = draw
                best_arm = a
        self.prev_arm = best_arm
        return best_arm


def ftm_success_rate(e: Env):
    return (e.successes / e.attempts) if e.attempts else None

def update_all_samplers_from_env(e: Env):
    attempts, successes = e.attempts, e.successes
    for s in SAMPLERS.values():
        s.update_from_segment(attempts, successes)

def set_params_with_ts(a: Act):
    chosen = {
        "ftmBurstDuration": SAMPLERS["ftmBurstDuration"].select_arm(),
        "ftmMinDeltaFtm": SAMPLERS["ftmMinDeltaFtm"].select_arm(),
        "ftmAsap": SAMPLERS["ftmAsap"].select_arm(),
        "ftmFtmsPerBurst": SAMPLERS["ftmFtmsPerBurst"].select_arm(),
        "ftmBurstPeriod": SAMPLERS["ftmBurstPeriod"].select_arm(),
    }
    a.ftmNumberOfBurstsExponent = 1
    a.ftmBurstDuration = chosen["ftmBurstDuration"]
    a.ftmMinDeltaFtm = chosen["ftmMinDeltaFtm"]
    a.ftmPartialTsfTimer = 0
    a.ftmPartialTsfNoPref = True
    a.ftmAsap = chosen["ftmAsap"]
    a.ftmFtmsPerBurst = chosen["ftmFtmsPerBurst"]
    a.ftmBurstPeriod = chosen["ftmBurstPeriod"]
    a.apply = True

    return chosen



SAMPLERS = {
    "ftmBurstDuration": BetaBernoulliTS(arms=range(1, 11)),   
    "ftmMinDeltaFtm": BetaBernoulliTS(arms=range(1, 11)),   
    "ftmAsap": BetaBernoulliTS(arms=[False, True]),  
    "ftmFtmsPerBurst": BetaBernoulliTS(arms=range(1, 11)),   
    "ftmBurstPeriod": BetaBernoulliTS(arms=range(1, 16)),   
}



mempool_key  = 1234
mem_size     = 4096
memblock_key = 2333
ns3_path     = '.'
exp_name     = 'scenario'

exp = Experiment(mempool_key, mem_size, exp_name, ns3_path)

try:
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

            update_all_samplers_from_env(e)
   
            a = data.act
            chosen = set_params_with_ts(a)

            print("PY sent (TS):",
                chosen["ftmBurstDuration"],
                chosen["ftmMinDeltaFtm"],
                chosen["ftmFtmsPerBurst"],
                chosen["ftmBurstPeriod"],
                chosen["ftmAsap"])
    pro.wait()

except Exception as ex:
    print("Something wrong")
    print(ex)
finally:
    del exp
