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

Older commits (before 2026-08-17) contained a real OpenRouter key string
in `examples/pda2/openai_api.h` (plus quotes in some review docs). It has
been removed from HEAD and **treated as COMPROMISED**:

- The key was revoked / must be rotated on OpenRouter before any use.
- **Local history purged 2026-08-21** via `git filter-repo --replace-text`
  (rules: full-key literal, `sk-or-v1-<64hex>` regex, bare-hex literal →
  `REDACTED-OPENROUTER-KEY`). Verified: every blob in the object database
  scanned — 0 occurrences of the key fragment. Old→new commit map kept at
  `.git/filter-repo/commit-map` (docs cite pre-rewrite hashes).
- The GitHub remote (`origin/HD-V2-250915`, `origin/master`) was verified
  to ALREADY be clean (all reachable blobs scanned, 0 hits) — it had been
  rewritten from another machine before 2026-08-21. Local rewrite produced
  different hashes, so pushing HD-V2-250915 requires `--force` (expected).
- Full pre-rewrite backup (contains the key): the remote was force-pushed
  and blob-verified clean on 2026-08-21, so the backup is now redundant and
  can be deleted: `E:\cpp_works\T-Deck-Pro-pre-filter-backup-2026-08-21.bundle`
- OpenRouter key ROTATED 2026-08-21; the new key lives in `/env.cfg` (device
  SPIFFS + repo-root `data/env.cfg`, both gitignored). `config_keys.h` was
  cleared to a template the same day (OWM key/coords moved to `/env.cfg` too).
- `config_keys.h` and `/env.cfg` must never be added to git.

## Release checklist

- [ ] No secrets in `git diff HEAD` (run `git grep` for key patterns)
- [x] History purged (filter-repo, 2026-08-21)
- [x] OpenRouter key rotated (2026-08-21; OWM key never leaked — env-chain only)
- [x] Rewritten `HD-V2-250915` force-pushed and blob-verified (2026-08-21)
