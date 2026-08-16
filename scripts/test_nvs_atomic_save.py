#!/usr/bin/env python3
"""Executable state-machine test for the dual-slot NVS atomic save.

Mirrors the ALGORITHM in examples/pda2/openai_api.cpp (openai_load_config /
openai_save_config) against an injectable KV store, so the ten spec cases
in tests/test_nvs_atomic_save.md run with real assertions instead of
being hand-executed (main review 1.2 / copilot finding 1.6).

This is a host-side algorithm check, NOT the compiled firmware: the
C++ implementation and this model must stay in sync; if you change the
C++ side, update the model here and re-run.

Usage: python scripts/test_nvs_atomic_save.py
"""
import sys

DEFAULT_BASE = "https://openrouter.ai/api/v1/chat/completions"
DEFAULT_MODEL = "deepseek/deepseek-v4-flash-0731"
DEFAULT_KEY = "sk-or-v1-default"


class KV:
    """Injectably-failing key-value store (models Preferences)."""

    def __init__(self):
        self.d = {}
        self.fail_next_puts = 0    # fail the next N put* calls
        self.fail_at = None        # fail exactly the put with this global index
        self.corrupt_puts = 0      # truncate the next N put* values
        self.put_calls = 0

    def put(self, key, val):
        self.put_calls += 1
        if self.fail_next_puts > 0:
            self.fail_next_puts -= 1
            return 0
        if self.fail_at == self.put_calls:
            return 0
        if self.corrupt_puts > 0:
            self.corrupt_puts -= 1
            self.d[key] = val[: max(0, len(val) // 2)]   # truncated write
            return 1
        self.d[key] = val
        return 1

    def get(self, key, default=None):
        return self.d.get(key, default)


def cfg_key(field, slot):
    return "%s.%d" % (field, slot)


def active_slot(kv):
    return 0 if kv.get("active", 0) == 0 else 1


def load_config(kv):
    """Mirror of openai_load_config's fallback chain."""
    slot = active_slot(kv)
    slot_init = (kv.get(cfg_key("base", slot)) is not None or
                 kv.get(cfg_key("model", slot)) is not None or
                 kv.get(cfg_key("key", slot)) is not None)
    if slot_init:
        return (kv.get(cfg_key("base", slot), ""),
                kv.get(cfg_key("model", slot), ""),
                kv.get(cfg_key("key", slot), ""))
    if kv.get("base") is not None or kv.get("model") is not None or \
       kv.get("key") is not None:
        return (kv.get("base", ""), kv.get("model", ""), kv.get("key", ""))
    return (DEFAULT_BASE, DEFAULT_MODEL, DEFAULT_KEY)


def save_config(kv, base, model, key):
    """Mirror of openai_save_config: stage to inactive slot, verify, flip."""
    active = active_slot(kv)
    nxt = 1 - active
    staged = (kv.put(cfg_key("base", nxt), base) > 0 and
              kv.put(cfg_key("model", nxt), model) > 0 and
              kv.put(cfg_key("key", nxt), key) > 0 and
              kv.get(cfg_key("base", nxt), "") == base and
              kv.get(cfg_key("model", nxt), "") == model and
              kv.get(cfg_key("key", nxt), "") == key)
    if not staged:
        return False, "NVS write failed"
    if kv.put("active", nxt) == 0:
        return False, "NVS commit failed"
    return True, None


# ---------------------------------------------------------------- test cases
passed = 0
failed = 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("PASS %s" % name)
    else:
        failed += 1
        print("FAIL %s  %s" % (name, detail))
        sys.exit(1)


# 1. normal save
kv = KV()
ok, err = save_config(kv, "https://a/v1/chat/completions", "m/x", "k" * 20)
check("1 normal save", ok and err is None and
      load_config(kv)[0].startswith("https://a/"))

# 2. first save lands in slot 1 (active defaults to 0)
kv = KV()
save_config(kv, "B1", "M1", "K1")
check("2 first save -> slot 1", kv.get("active") == 1 and
      kv.get("base.1") == "B1" and kv.get("base.0") is None)

# 3. fail on first put -> abort, no partial slot, active unchanged
kv = KV()
save_config(kv, "B0", "M0", "K0")          # slot 1 active
kv.fail_next_puts = 1
ok, err = save_config(kv, "B1", "M1", "K1")
check("3 put fail -> abort", not ok and err == "NVS write failed" and
      kv.get("active") == 1 and load_config(kv) == ("B0", "M0", "K0"))

# 4. fail on third put
kv = KV()
save_config(kv, "B0", "M0", "K0")
kv.fail_next_puts = 3
ok, err = save_config(kv, "B1", "M1", "K1")
check("4 third put fail", not ok and err == "NVS write failed" and
      load_config(kv) == ("B0", "M0", "K0"))   # active never flipped

# 5. truncated write -> read-back mismatch
kv = KV()
save_config(kv, "B0", "M0", "K0")
kv.corrupt_puts = 1
ok, err = save_config(kv, "B1", "M1", "K1")
check("5 truncation detected", not ok and err == "NVS write failed" and
      load_config(kv) == ("B0", "M0", "K0"))

# 6. active flip fails -> old slot stays live
kv = KV()
save_config(kv, "B0", "M0", "K0")          # consumes global puts 1-4, active=1
kv.fail_at = 8                             # second save's flip is global put #8
ok, err = save_config(kv, "B1", "M1", "K1")
check("6 commit fail", not ok and err == "NVS commit failed" and
      kv.get("active") == 1 and load_config(kv) == ("B0", "M0", "K0"))

# 7. power-loss model: any observed state is one complete save
kv = KV()
save_config(kv, "B0", "M0", "K0")
# simulate: staging complete, flip not yet applied (or applied)
kv2 = KV()
for k, v in kv.d.items():
    kv2.d[k] = v
kv2.d["base.0"], kv2.d["model.0"], kv2.d["key.0"] = "B9", "M9", "K9"
got_a = load_config(kv)     # before flip
kv2.d["active"] = 0         # after flip
got_b = load_config(kv2)
check("7 power-loss states", got_a == ("B0", "M0", "K0") and
      got_b == ("B9", "M9", "K9"))

# 8. three consecutive saves alternate slots, load = latest
kv = KV()
save_config(kv, "B1", "M1", "K1")      # -> slot 1, active=1
save_config(kv, "B2", "M2", "K2")      # -> slot 0, active=0
save_config(kv, "B3", "M3", "K3")      # -> slot 1, active=1
check("8 slot alternation", load_config(kv) == ("B3", "M3", "K3") and
      kv.get("active") == 1)

# 9. legacy flat keys fallback (no slot initialized)
kv = KV()
kv.d["base"], kv.d["model"], kv.d["key"] = "BLEG", "MLEG", "KLEG"
check("9 legacy fallback", load_config(kv) == ("BLEG", "MLEG", "KLEG"))
save_config(kv, "B1", "M1", "K1")
check("9b legacy -> slot", load_config(kv) == ("B1", "M1", "K1"))

# 10. empty base in an INITIALIZED slot is read back as empty
kv = KV()
save_config(kv, "", "M1", "K1")
check("10 empty base round-trips", load_config(kv) == ("", "M1", "K1"))

print("PASS: all %d cases" % passed)
