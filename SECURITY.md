# Security

## Dev-only API Key exception (user decision)

- A real OpenRouter API key is temporarily committed as
  `AI_KEY_DEFAULT` in `examples/pda2/openai_api.h` to keep on-device AI
  verification fast during development (deliberate exception, see
  `memory/api-key-dev-exception.md`).
- The key is a free-tier key and has been rotated on OpenRouter before
  this decision was made; it will be rotated again before any release.
- **Before pushing to a public remote / making a release you MUST:**
  1. delete the `AI_KEY_DEFAULT` string (replace with `""`),
  2. remove `-DAI_KEY_DEFAULT_COMPILED` from `[env:pda2]` in `platformio.ini`,
  3. rotate the key on OpenRouter,
  4. consider `git filter-repo` to purge the string from history.
- Compensating control in place: the build emits
  `#warning "Dev-only API Key in source..."` on every compile while the
  key is compiled in (C1). No CI secret scanning is configured yet (C3).

## Other secrets

- WiFi credentials are stored in NVS at runtime only; nothing else is
  hard-coded in source.
