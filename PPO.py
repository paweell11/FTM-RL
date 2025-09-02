import numpy as np
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



import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

BDUR_ARMS = list(range(1, 11))   
MINDELTA = list(range(1, 11))   
FTMS_ARMS = list(range(1, 11))   
PERIOD_ARMS = list(range(1, 16))   
ASAP_ARMS = [False, True]
NUM_CLASSES = [len(BDUR_ARMS), len(MINDELTA), len(FTMS_ARMS), len(PERIOD_ARMS), len(ASAP_ARMS)]

def build_state(e: Env):
    sr = (e.successes / e.attempts) if e.attempts else 0.0
    n = float(e.nWifi) / 50.0
    dr = float(e.dataRate) / 100.0
    return np.array([sr, n, dr], dtype=np.float32)

class PolicyValueNet(keras.Model):
    def __init__(self, state_dim=3, hidden=64):
        super().__init__()
        self.d1 = layers.Dense(hidden, activation='tanh')
        self.d2 = layers.Dense(hidden, activation='tanh')
        # 5 głów polityki:
        self.logits_bdur = layers.Dense(NUM_CLASSES[0])
        self.logits_mind = layers.Dense(NUM_CLASSES[1])
        self.logits_ftms = layers.Dense(NUM_CLASSES[2])
        self.logits_period = layers.Dense(NUM_CLASSES[3])
        self.logits_asap = layers.Dense(NUM_CLASSES[4])
        # wartość:
        self.v = layers.Dense(1)

    def call(self, x, training=False):
        h = self.d2(self.d1(x))
        logits_list = [
            self.logits_bdur(h),
            self.logits_mind(h),
            self.logits_ftms(h),
            self.logits_period(h),
            self.logits_asap(h),
        ]
        v = tf.squeeze(self.v(h), axis=-1)
        return logits_list, v

def logprob_and_entropy(logits_list, actions_idx):
    """
    logits_list: lista Tensors [(B,C_k), ...] dla 5 głów
    actions_idx: Tensor (B,5) int64
    Zwraca: (logp_sum, entropy_sum) – oba (B,)
    """
    total_logp = tf.zeros(tf.shape(actions_idx)[0], dtype=tf.float32)
    total_ent  = tf.zeros_like(total_logp)
    for k, logits in enumerate(logits_list):
        a_k = actions_idx[:, k]
        nll = tf.nn.sparse_softmax_cross_entropy_with_logits(labels=a_k, logits=logits)
        logp = -nll
        total_logp += logp
        p = tf.nn.softmax(logits)
        ent = -tf.reduce_sum(p * tf.math.log(tf.clip_by_value(p, 1e-8, 1.0)), axis=1)
        total_ent += ent
    return total_logp, total_ent

class RolloutBuffer:
    def __init__(self):
        self.states = []
        self.actions_idx = []
        self.logps = []
        self.values = []
        self.rewards = []
        self.next_values = []
    def add(self, s, a_idx, logp, v, r, v_next):
        self.states.append(s)
        self.actions_idx.append(a_idx)
        self.logps.append(logp)
        self.values.append(v)
        self.rewards.append(r)
        self.next_values.append(v_next)
    def __len__(self): return len(self.states)
    def as_tensors(self):
        S = tf.convert_to_tensor(np.stack(self.states).astype(np.float32))
        A = tf.convert_to_tensor(np.stack(self.actions_idx).astype(np.int64))
        LP = tf.convert_to_tensor(np.array(self.logps, dtype=np.float32))
        V = tf.convert_to_tensor(np.array(self.values, dtype=np.float32))
        R = tf.convert_to_tensor(np.array(self.rewards, dtype=np.float32))
        VN = tf.convert_to_tensor(np.array(self.next_values, dtype=np.float32))
        return S, A, LP, V, R, VN
    def clear(self): self.__init__()

class PPOAgentKeras:
    def __init__(self, state_dim=3, lr=3e-4, gamma=0.99, clip_eps=0.2, ent_coef=0.01, vf_coef=0.5, epochs=4):
        self.gamma  = gamma
        self.clip_eps = clip_eps
        self.ent_coef = ent_coef
        self.vf_coef = vf_coef
        self.epochs = epochs
        self.net = PolicyValueNet(state_dim=state_dim)
        self.opt = keras.optimizers.Adam(learning_rate=lr)

    @tf.function
    def _forward(self, states):
        return self.net(states, training=False)

    def select_action(self, state_vec):
        s = tf.convert_to_tensor(state_vec[None, :], dtype=tf.float32)
        logits_list, v = self._forward(s)
        actions = []
        for logits in logits_list:
            sample = tf.random.categorical(logits, num_samples=1)  # (1,1)
            a = int(sample[0, 0].numpy())
            actions.append(a)
        # policz łączny logP wybranej akcji
        A_tensor = tf.convert_to_tensor(np.array(actions, dtype=np.int64)[None, :])
        logp, _  = logprob_and_entropy([l for l in logits_list], A_tensor)
        return actions, float(v.numpy()[0]), float(logp.numpy()[0])

    @tf.function
    def _train_step(self, S, A, LP_old, V_old, RET, ADV):
        with tf.GradientTape() as tape:
            logits_list, V_new = self.net(S, training=True)
            LOGP_new, ENT = logprob_and_entropy(logits_list, A)
            ratio = tf.exp(LOGP_new - LP_old)
            surr1 = ratio * ADV
            surr2 = tf.clip_by_value(ratio, 1.0 - self.clip_eps, 1.0 + self.clip_eps) * ADV
            policy_loss = -tf.reduce_mean(tf.minimum(surr1, surr2))
            value_loss = 0.5 * tf.reduce_mean(tf.square(V_new - RET))
            entropy = tf.reduce_mean(ENT)
            loss = policy_loss + self.vf_coef * value_loss - self.ent_coef * entropy
        grads = tape.gradient(loss, self.net.trainable_variables)
        grads, _ = tf.clip_by_global_norm(grads, 0.5)
        self.opt.apply_gradients(zip(grads, self.net.trainable_variables))
        return policy_loss, value_loss, entropy

    def update(self, buffer: RolloutBuffer):
        if len(buffer) == 0:
            return
        S, A, LP_old, V_old, R, V_next = buffer.as_tensors()
        RET = R + self.gamma * V_next
        ADV = RET - V_old
        ADV = (ADV - tf.reduce_mean(ADV)) / (tf.math.reduce_std(ADV) + 1e-8)
        for _ in range(self.epochs):
            self._train_step(S, A, LP_old, V_old, RET, ADV)
        buffer.clear()

def fill_act_from_indices(a: Act, idxs):
    i_bdur, i_mind, i_ftms, i_period, i_asap = idxs
    a.ftmNumberOfBurstsExponent = 1
    a.ftmBurstDuration = BDUR_ARMS[i_bdur]
    a.ftmMinDeltaFtm = MINDELTA[i_mind]
    a.ftmPartialTsfTimer = 0
    a.ftmPartialTsfNoPref = True
    a.ftmAsap = ASAP_ARMS[i_asap]
    a.ftmFtmsPerBurst = FTMS_ARMS[i_ftms]
    a.ftmBurstPeriod = PERIOD_ARMS[i_period]
    a.apply = True


def main():
    random.seed(0)
    np.random.seed(0)
    tf.random.set_seed(0)

    mempool_key = 1235
    mem_size = 4096
    memblock_key = 2334
    ns3_path = '.'
    exp_name = 'scenario'

    agent = PPOAgentKeras(state_dim=3, lr=3e-4, gamma=0.99, clip_eps=0.2, ent_coef=0.01, vf_coef=0.5, epochs=4)
    buffer = RolloutBuffer()
    BATCH_SEG = 64  # co tyle segmentów robimy update

    exp = Experiment(mempool_key, mem_size, exp_name, ns3_path)

    last = None  

    try:
        exp.reset()
        rl  = Ns3AIRL(memblock_key, Env, Act)
        pro = exp.run(setting={}, show_output=True)

        while not rl.isFinish():
            with rl as data:
                if data is None:
                    continue

                # Stan z feedbacku poprzedniej akcji:
                e = data.env
                s_now = build_state(e)

                # Jeśli mamy poprzednią akcję -> zapis przejścia z nagrodą
                if last is not None:
                    attempts, successes = e.attempts, e.successes
                    r = (successes / attempts) if attempts > 0 else 0.0
                    # V(next)
                    logits_list, v_next_tf = agent._forward(tf.convert_to_tensor(s_now[None, :], dtype=tf.float32))
                    v_next = float(v_next_tf.numpy()[0])
                    buffer.add(last['s'], last['a_idx'], last['logp'], last['v'], r, v_next)
                    print(f"PY recv: attempts={attempts} succ={successes} rate={r:.3f}")

                # Wybierz nową akcję i wyślij do C++
                a_idx, v_now, logp_now = agent.select_action(s_now)
                a = data.act
                fill_act_from_indices(a, a_idx)

                print("PY sent PPO:",
                      BDUR_ARMS[a_idx[0]], MINDELTA[a_idx[1]], FTMS_ARMS[a_idx[2]],
                      PERIOD_ARMS[a_idx[3]], ASAP_ARMS[a_idx[4]])

                last = dict(s=s_now, a_idx=a_idx, logp=logp_now, v=v_now)

                # Aktualizacja co batch
                if len(buffer) >= BATCH_SEG:
                    agent.update(buffer)

        # po epizodzie – ostatni update (jeśli coś w buforze)
        if len(buffer) > 0:
            agent.update(buffer)

        pro.wait()

    except Exception as ex:
        print("Something wrong")
        print(ex)
    finally:
        del exp

if __name__ == "__main__":
    main()
