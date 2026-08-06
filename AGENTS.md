# AGENTS.md

Hall-effect keyboard firmware (C) built with PlatformIO. Python (`setup.py` + pydantic) generates the PlatformIO project; agents typically verify by compiling, not by flashing.

## Commands

```bash
pip install -r requirements.txt          # pydantic only
pip install --upgrade platformio         # PlatformIO Core (not in requirements.txt)
python setup.py -k <keyboard>            # generate platformio.ini
pio run                                  # build → .pio/build/<keyboard>/firmware.{bin,elf}
pio run --target upload                  # flash (needs hardware + DFU); not available headless
```

Valid `<keyboard>` values are directories under `keyboards/` (`he16`, `he35`, `he60`, `he60-v2`, `m256-whe`).

If `pio` is missing from a non-interactive shell, ensure `~/.local/bin` is on `PATH` (user pip installs land there).

## Verification

CI (`.github/workflows/build.yml`) for each keyboard under `keyboards/`:

1. `pip install -r requirements.txt`
2. `pip install --upgrade platformio`
3. `python setup.py -k <keyboard>`
4. `pio run`

There is no unit-test suite. In cloud/headless environments, a successful `pio run` is the verification step. First build downloads the ARM toolchain and deps into `~/.platformio` and is slow; later builds use the cache.

## Important paths

| Path | Role |
|------|------|
| `keyboards/<name>/keyboard.json` | Keyboard metadata (firmware + configurator) |
| `src/`, `include/` | Shared firmware sources and headers |
| `src/hardware/`, `include/hardware/`, `hardware/` | MCU drivers and board defs |
| `scripts/` | Codegen, validation, driver registry |
| `scripts/schema/keyboard.py` | pydantic schema for `keyboard.json` |
| `setup.py` | Writes `platformio.ini` |
| `linker/` | Linker scripts |

**Generated / gitignored — do not commit:** `platformio.ini`, `.pio/`, `include/metadata.h`.

## Notes

- Switch keyboards by re-running `python setup.py -k <keyboard>` before `pio run`.
- Contributions target the `dev` branch (see README); pair firmware `dev` with hmkconf `dev`.
- C builds use `-Werror` and related warnings via generated PlatformIO flags; fix warnings rather than silencing them casually.
- `.clang-format` exists (LLVM-based) but is not enforced in CI.

## Docs

- [README.md](README.md) — features, build, porting, contribution branch
- Per-keyboard notes: `keyboards/*/README.md`
- Web configurator: [hmkconf](https://github.com/tmyk-io/hmkconf)

## カスタム Cursor Skills（tmyk-io/cursor-skills）

個人用スキル集 `tmyk-io/cursor-skills` を利用します。Cloud Agent の Build 時に `.cursor/install-skills.sh` が同リポジトリを clone し、スキルを `~/.cursor/skills` へ配置します。**Environment では `hmkconf` と `libhmk` の2つだけ選べばよく、`cursor-skills` を付ける必要はありません。**

- 起動: `.cursor/environment.json` の `install` → `.cursor/install-skills.sh` → `cursor-skills` の `scripts/cloud-install.sh`（`~/.cursor/skills` へ rsync）
- 収録スキル: `agents-md-creator` / `doc-updater` / `commit-push` / `dig` / `writing-plans`（各 `~/.cursor/skills/<name>/SKILL.md`）
- **反映タイミング**: skills は Build 時点のスナップショットです。`cursor-skills` を更新したら、この Environment を **Rebuild** してください。一覧は次セッションから反映されます（同一セッション内では該当 `SKILL.md` を直接読めます）
- スキルの追加・更新は `cursor-skills` 側で行い、そちらの `scripts/sync-from-local.sh` で同期します（このリポジトリにはスキル本体を置きません）
