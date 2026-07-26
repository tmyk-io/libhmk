# AGENTS.md

## Cursor Cloud specific instructions

`libhmk` is embedded **C firmware** for Hall-effect keyboards, built with **PlatformIO**
driven by `setup.py`. Standard build steps are in `README.md`; only the non-obvious
cloud notes are captured here.

- **PlatformIO CLI lives at `~/.local/bin/pio`** (installed via `pip --user`), which is
  not on the default non-interactive PATH. Prefix commands with
  `export PATH="$HOME/.local/bin:$PATH"` or call `~/.local/bin/pio` directly.
- **Build flow:** `python3 setup.py -k <keyboard>` generates `platformio.ini`, then
  `pio run` compiles. Available keyboards live under `keyboards/` (`he16`, `he60`,
  `he60-v2`, `m256-whe`). Output lands in `.pio/build/<keyboard>/firmware.{bin,elf}`.
- **First `pio run` is slow** (downloads the ARM toolchain + STM32 platform + TinyUSB);
  subsequent builds are cached under `~/.platformio`.
- `platformio.ini`, `.pio/`, and `include/metadata.h` are generated and gitignored — do
  not commit them.
- There is **no physical keyboard** in the cloud environment, so firmware can be built
  but not flashed/run on hardware here. Verification = a successful `pio run`.
