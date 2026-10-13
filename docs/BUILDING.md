# Building

## Tools

- ESP-IDF v6.1
- Target: esp32

## Build

Open an ESP-IDF terminal (or activate the environment with `export.sh` / `export.ps1`), then from the repository root:

    idf.py set-target esp32
    idf.py build

Outputs land in `build/`: `.bin` images, `.elf`, `.map` and `flasher_args.json` with the flash arguments. CI publishes the same files with the relative layout that `flasher_args.json` expects, plus size reports and a `SHA.txt` recording the built commit.

## Host tests

Available once `tests/host/CMakeLists.txt` exists:

    cmake -S tests/host -B build/host -G Ninja
    cmake --build build/host
    ctest --test-dir build/host --output-on-failure

## CI

`.github/workflows/gp-ci.yml` builds inside the `espressif/idf:v6.1` container on pull requests into Testing and Maintaining and on pushes to Testing, Maintaining and `ci/**`. Artifacts are labelled with the candidate SHA. CI never flashes hardware.

## Troubleshooting

- `idf.py: command not found`: the ESP-IDF environment is not activated in this shell.
- Wrong target after switching: run `idf.py set-target esp32` again, which regenerates `sdkconfig`.
- Do not commit machine-specific paths or `sdkconfig` changes that only reflect your local setup.
