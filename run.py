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

# ---------- Thompson Sampling dla ftmBurstDuration (1..10) ----------
class BetaBernoulliTS:
    def __init__(self, arms):
        self.arms = list(arms)                 # np. [1,2,...,10]
        self.alpha = {a: 1.0 for a in self.arms}
        self.beta  = {a: 1.0 for a in self.arms}
        self.prev_arm = None                   # ostatnio zastosowane ramię

    def update_from_segment(self, attempts, successes):
        """Zaktualizuj posterior dla poprzednio użytego ramienia."""
        if self.prev_arm is None:
            return
        if attempts is None or attempts == 0:
            return
        # pilnuj spójności
        successes = min(successes, attempts)
        failures = attempts - successes
        self.alpha[self.prev_arm] += successes
        self.beta[self.prev_arm]  += failures

    def select_arm(self):
        """Wylosuj z Beta i wybierz ramię o największej próbce."""
        best_arm = None
        best_draw = -1.0
        for a in self.arms:
            draw = random.betavariate(self.alpha[a], self.beta[a])
            if draw > best_draw:
                best_draw = draw
                best_arm = a
        self.prev_arm = best_arm
        return best_arm
# --------------------------------------------------------------------

def ftm_success_rate(e: Env):
    return (e.successes / e.attempts) if e.attempts else None

def set_params_with_ts(a: Act, sampler: BetaBernoulliTS):
    a.ftmBurstDuration = sampler.select_arm()

    a.ftmNumberOfBurstsExponent = 1
    a.ftmMinDeltaFtm            = 4
    a.ftmPartialTsfTimer        = 0
    a.ftmPartialTsfNoPref       = True
    a.ftmAsap                   = True
    a.ftmFtmsPerBurst           = 2
    a.ftmBurstPeriod            = 2
    a.apply                     = True

mempool_key  = 1234
mem_size     = 4096
memblock_key = 2333
ns3_path     = '.'
exp_name     = 'scenario'

# 10 wariantów DURATION: 1..10
sampler = BetaBernoulliTS(arms=range(1, 11))

exp = Experiment(mempool_key, mem_size, exp_name, ns3_path)

try:
    exp.reset()
    rl = Ns3AIRL(memblock_key, Env, Act)
    pro = exp.run(setting={}, show_output=True)

    while not rl.isFinish():
        with rl as data:
            if data is None:
                continue

            # 1) feedback z ostatniego segmentu -> aktualizacja posteriora
            e = data.env
            sr = ftm_success_rate(e)
            if sr is None:
                print("PY recv: attempts=0 (brak SR)")
            else:
                print(f"PY recv: attempts={e.attempts} succ={e.successes} rate={sr:.3f}")
                sampler.update_from_segment(attempts=e.attempts, successes=e.successes)

            # 2) wybór nowej akcji (tylko ftmBurstDuration przez TS)
            a = data.act
            set_params_with_ts(a, sampler)

            print("PY sent:",
                  a.ftmBurstDuration,
                  a.ftmMinDeltaFtm,
                  a.ftmFtmsPerBurst,
                  a.ftmBurstPeriod,
                  a.ftmAsap)

    pro.wait()

except Exception as ex:
    print("Something wrong")
    print(ex)
finally:
    del exp
