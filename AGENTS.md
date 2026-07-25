# AGENTS.md

## Cursor Cloud specific instructions

`libhmk` is embedded C firmware for Hall-effect keyboards, built with PlatformIO (Python-driven). It is not a runnable service; "running" it means cross-compiling firmware for a target board. See `README.md` for the full flow.

- Python tooling runs in a project-local venv at `.venv` (PlatformIO + `pydantic`). Use `.venv/bin/pio` / `.venv/bin/python`, or activate with `source .venv/bin/activate`.
- Build a board: `python setup.py -k <keyboard>` generates `platformio.ini` (git-ignored), then `pio run`. Valid `<keyboard>` values are the directories under `keyboards/` (currently `he16`, `he60`, `he60-v2`, `m256-whe`).
- Outputs land in `.pio/build/<keyboard>/firmware.bin` and `firmware.elf`. First build downloads the ARM GCC toolchain + TinyUSB into `~/.platformio`; subsequent builds are fast.
- Flashing to real hardware (DFU/WebUSB DFU) is not possible in this headless environment — a successful `pio run` is the verification step.
