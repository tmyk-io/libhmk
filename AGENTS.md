# AGENTS.md

## Cursor Cloud specific instructions

`libhmk` is a C firmware library for Hall-effect keyboards, built with PlatformIO. There is no runnable "app"; "running" it means building firmware for a target keyboard.

- Use `python3` (there is no `python` alias in this environment). Python deps (`pydantic`) come from `requirements.txt` and are installed by the Cloud startup update script.
- PlatformIO (`pio`) is preinstalled. Build flow (see `README.md` / `.github/workflows/build.yml`):
  1. `python3 setup.py -k <keyboard>` generates a gitignored `platformio.ini`. Valid keyboards are the directories under `keyboards/` (e.g. `he16`, `he60`, `he60-v2`, `m256-whe`).
  2. `pio run` compiles; artifacts land in `.pio/build/<keyboard>/firmware.bin` and `firmware.elf`.
- First `pio run` downloads the toolchain (arm-none-eabi-gcc), the `ststm32` platform, and TinyUSB; subsequent builds are fast and cached under `~/.platformio`.
- `platformio.ini`, `.pio/`, and `include/metadata.h` are generated and gitignored — do not commit them.
- Flashing firmware to hardware (DFU) is not possible in this environment; builds are the extent of local verification.
- The upstream development branch is `dev` (PRs target `dev` upstream); this fork's default branch is `main`.
