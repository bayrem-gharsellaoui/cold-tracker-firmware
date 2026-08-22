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

## Milestone 2 - OTA Update

Includes:
- mcuboot bootloader integration
- ota ovelay configuration
- sysbuild configuration
- board partitions understanding
- manual flashing of new firmware version in slot1
- manual upgrade and confirmation via mcuboot shell commands

- Make note of the needed commands:
    ```bash
    west build -b nucleo_u575zi_q -p --sysbuild -- -DEXTRA_CONF_FILE=overlay-ota.conf

    west flash --runner openocd

    # Make changes and bump version

    west flash \
        -d build \
        --domain application \
        --file build/application/zephyr/zephyr.signed.bin \
        --file-type bin \
        --flash-address 0x171000 \
        --verify

    west build -b esp32_devkitc/esp32/procpu -p --sysbuild -- -DEXTRA_CONF_FILE=overlay-ota.conf

    # Erase flash to start cleanly on ESP32
    west flash -d build --runner esp32 --erase

    west flash

    # Make changes and bump version

    west flash \
        -d build \
        --domain application \
        --runner esp32 \
        --bin-file build/application/zephyr/zephyr.signed.bin \
        --esp-app-address 0x171000 \
        --no-erase

    uart:~$ mcuboot request_upgrade

    uart:~$ mcuboot confirm

    python3 -m http.server 4242 --bind 0.0.0.0
    ```

### USB Networking

1. Connect **the Nucleo USB device port** to the laptop and make sure the USB Ethernet interface appears in **Settings → Network**.
2. Open the gear icon for that interface, then under **IPv4** select:
**Shared to other computers**
You can disable IPv6 for this project to keep things simple. Click Apply.
3. In a terminal run:

    ```bash
    sudo iptables -P FORWARD ACCEPT
    ```
4. Reset the board or run this on zephyr shell: `kernel reboot`
5. ColdTracker starts DHCP automatically and NetworkManager gives it an address, gateway, and DNS information. You should see something along the lines of:

```
Network connectivity established and IP address assigned
ColdTracker is online
Connected to example.com:80
HTTP status: 200
```
