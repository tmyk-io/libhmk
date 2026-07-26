# AGENTS.md

## Cursor Cloud specific instructions

`libhmk` is C firmware for Hall-effect keyboards, built with **PlatformIO** driven by Python codegen scripts. There is no runnable app: building the firmware is the way to validate the environment. CI equivalent is `.github/workflows/build.yml`.

Python deps (pydantic + PlatformIO Core) are installed into a local virtualenv at `.venv` (untracked, created by the startup update script). PlatformIO is **not** on the global `PATH`; invoke it via the venv.

Build flow (per keyboard):

- `.venv/bin/python setup.py -k <keyboard>` generates `platformio.ini` (gitignored).
- `.venv/bin/pio run` compiles it. Output: `.pio/build/<keyboard>/firmware.bin` and `firmware.elf`.
- Valid `<keyboard>` values are the directories under `keyboards/` (currently `he60`, `he60-v2`, `he16`, `m256-whe`).

Non-obvious notes:

- The first `pio run` downloads the MCU toolchain/framework (STM32Cube for `he60`, AT32 for the others), which can take several minutes; subsequent builds are fast and cached under `~/.platformio`.
- `platformio.ini` is regenerated per keyboard by `setup.py`, so switch keyboards by re-running `setup.py -k <other>` before `pio run`.
- Flashing/running on hardware (DFU upload) needs a physical keyboard and is not possible in this environment.
