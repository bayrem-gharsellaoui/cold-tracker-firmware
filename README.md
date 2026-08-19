# ColdTracker

Firmware for ColdTracker devices

[![GitHub Format workflow status](https://github.com/bayrem-gharsellaoui/cold-tracker-firmware/workflows/Format/badge.svg)](https://github.com/bayrem-gharsellaoui/cold-tracker-firmware/actions/workflows/format.yml)
[![GitHub Build workflow status](https://github.com/bayrem-gharsellaoui/cold-tracker-firmware/workflows/Build/badge.svg)](https://github.com/bayrem-gharsellaoui/cold-tracker-firmware/actions/workflows/build.yml)
[![Zephyr RTOS](https://img.shields.io/badge/zephyr-v4.4.2-blue)](https://github.com/zephyrproject-rtos/zephyr/releases/tag/v4.4.2)
[![Zephyr SDK](https://img.shields.io/github/v/release/zephyrproject-rtos/sdk-ng?label=sdk-ng)](https://github.com/zephyrproject-rtos/sdk-ng/releases)


## Milestone 1 - Infrastructure

Includes:
- GitHub repo
- west workspace/project layout
- devcontainer + VS Code
- application VERSION
- firmware version / build / commit hash
- STM32 target
- ESP32 target
- native_sim
- GitHub Actions
- build matrix
- formatting/check workflow
- README badges

End result: the same basic ColdTracker firmware runs on the three targets and prints its version/build information.

### Notes:

- Make sure you have enough free space on your disk (64GB)
- What is the difference between a west workspace and a west project and a zephyr module?
- Make note of the needed commands:
    ```bash
    clang-format --style=file:${ZEPHYR_BASE}/.clang-format -i src/*.c

    west build -b native_sim -p
    ./build/zephyr/zephyr.exe

    west build -b nucleo_u575zi_q -p
    west flash --runner openocd

    west build -b esp32_devkitc/esp32/procpu -p
    west flash
    ```

