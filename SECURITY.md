# Security

## Secrets architecture (2026-08-17)

No real API key lives in TRACKED source. Lookup chain for every secret
(highest priority first):

1. **NVS** — runtime storage (AI config saved from the UI wins; NVS
   namespace `ai` dual-slot, `weather` for OWM key/coords)
2. **SPIFFS `/env.cfg`** — device-side file, `KEY=VALUE` lines
   (see `examples/pda2/env.cfg.example`; parsed by
   `examples/pda2/env_secrets.cpp`). Not part of the repo.
3. **`examples/pda2/config_keys.h`** — compile-time dev values,
   **gitignored** (copy from `config_keys.h.example`)
4. Built-in default (usually empty)

When a value is already present in NVS, the lower layers are ignored
for that key.

## Git history warning

Older commits (before 2026-08-17) contain a real OpenRouter key string
in `examples/pda2/openai_api.h`. It has been removed from HEAD and
should be treated as COMPROMISED:

- The key was revoked / must be rotated on OpenRouter before any use.
- Before pushing to a PUBLIC remote, run `git filter-repo` (or
  `git filter-branch`) to purge the string from history, then force
  push; a plain push is also blocked by GitHub push protection.
- `config_keys.h` and `/env.cfg` must never be added to git.

## Release checklist

- [ ] No secrets in `git diff HEAD` (run `git grep` for key patterns)
- [ ] History purged (filter-repo) if pushing publicly
- [ ] OpenRouter / OpenWeatherMap keys rotated
