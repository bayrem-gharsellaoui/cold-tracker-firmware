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

#### Format code
```bash
clang-format --style=file:${ZEPHYR_BASE}/.clang-format -i src/*.c include/*.h
```

#### Build and execute on `native_sim`
```bash
west build -b native_sim -p
./build/zephyr/zephyr.exe
```

#### Build and flash `nucleo_u575zi_q`
```bash
west build -b nucleo_u575zi_q -p
west flash --runner openocd
```

#### Build and flash `xiao_esp32c3`
```bash
west build -b xiao_esp32c3 -p
west flash --reset-type watchdog-reset
```

## Milestone 2 - OTA Update

Includes:
- mcuboot bootloader integration
- ota ovelay configuration
- sysbuild configuration
- board partitions understanding
- manual flashing of new firmware version in slot1
- manual upgrade and confirmation via mcuboot shell commands
- in stm32 we need to erase the flash before we write to it

### nucleo_u575zi_q

1. Build mcuboot and application using sysbuild

```bash
west build -b nucleo_u575zi_q -p --sysbuild -- -DEXTRA_CONF_FILE=overlay-ota.conf
```

2. Full chip erase and flash the initial MCUboot + application

The first time make sure to erase the flash to start cleanly

```bash
west flash -d build --runner openocd --erase
west flash --runner openocd
```

3. Make changes and bump version

```
VERSION_MAJOR = 0
VERSION_MINOR = 1
PATCHLEVEL = 1
VERSION_TWEAK = 0
EXTRAVERSION =
```

Then rebuild:

```bash
west build -b nucleo_u575zi_q -p --sysbuild -- -DEXTRA_CONF_FILE=overlay-ota.conf
```

4. Manually flash the new application in the slot1

```bash
west flash \
    -d build \
    --domain application \
    --runner openocd \
    --no-rebuild \
    --file build/application/zephyr/zephyr.signed.bin \
    --file-type bin \
    --flash-address 0x080FA000 \
    --verify
```

5. Request a TEST upgrade

```bash
uart:~$ mcuboot request_upgrade
uart:~$ kernel reboot
```

The new application should now boot.

6. Confirm the new application

```bash
uart:~$ mcuboot confirm
```

If the application is not confirmed and the board is rebooted again, MCUboot should revert to the previous version.

### xiao_esp32c3

1. Build mcuboot and application using sysbuild

```bash
west build -b xiao_esp32c3 -p --sysbuild -- -DEXTRA_CONF_FILE=overlay-ota.conf
```

2. Flash mcuboot and application

The first time make sure to erase the flash to start cleanly

```bash
west flash --reset-type watchdog-reset --erase

west flash --reset-type watchdog-reset
```

3. Make changes and bump version

```
VERSION_MAJOR = 0
VERSION_MINOR = 1
PATCHLEVEL = 1
VERSION_TWEAK = 0
EXTRAVERSION =
```

Then rebuild:

```bash
west build -b xiao_esp32c3 -p --sysbuild -- -DEXTRA_CONF_FILE=overlay-ota.conf
```

4. Manually flash the new application in the slot1

```bash
west flash \
    -d build \
    --domain application \
    --runner esp32 \
    --no-rebuild \
    --bin-file build/application/zephyr/zephyr.signed.bin \
    --esp-app-address 0x1E1000 \
    --reset-type watchdog-reset \
    --no-erase
```

5. Request a TEST upgrade

```bash
uart:~$ mcuboot request_upgrade
uart:~$ kernel reboot
```

The new application should now boot.

6. Confirm the new application

```bash
uart:~$ mcuboot confirm
```

If the application is not confirmed and the board is rebooted again, MCUboot should revert to the previous version.

### USB Networking

#### Linux

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

#### Windows (Not tested)

1. Connect **the Nucleo USB device port** to the laptop and make sure the USB Ethernet adapter appears in **Control Panel → Network and Internet → Network Connections**.

2. Right-click the adapter that currently provides Internet access, such as **Wi-Fi**, then select:

**Properties → Sharing**

Enable:

**Allow other network users to connect through this computer's Internet connection**

Then select the **USB-NCM Ethernet adapter** connected to the Nucleo and click Apply.

3. Reset the board or run this on the Zephyr shell: `kernel reboot`

4. ColdTracker starts DHCP automatically and Windows Internet Connection Sharing gives it an address, gateway, and DNS information. You should see something along the lines of:

```
Network connectivity established and IP address assigned
ColdTracker is online
Connected to example.com:80
HTTP status: 200
```

### Update shell command

```bash
user@debian:/workdir/application/build/application/zephyr$ python3 -m http.server 4242 --bind 0.0.0.0
Serving HTTP on 0.0.0.0 port 4242 (http://0.0.0.0:4242/) ...
```

```bash
uart:~$ update http://192.168.1.104:4242/coldtracker-xiao_esp32c3.signed.bin
uart:~$ mcuboot request_upgrade
uart:~$ kernel reboot
uart:~$ mcuboot confirm
uart:~$ kernel reboot
```

```bash
uart:~$ update https://github.com/bayrem-gharsellaoui/cold-tracker-firmware/releases/download/0.2.0/coldtracker-xiao_esp32c3.signed.bin
uart:~$ mcuboot request_upgrade
uart:~$ kernel reboot
uart:~$ mcuboot confirm
uart:~$ kernel reboot
```

### SWO Logging

```bash
west build -p always -b nucleo_u575zi_q -- -DEXTRA_CONF_FILE=overlay-swo.conf
```

```bash
west build -p always -b nucleo_u575zi_q -- -DEXTRA_CONF_FILE="overlay-swo.conf;overlay-ppp.conf"
```


```bash
STM32_Programmer_CLI -c port=SWD -SWV freq=160 portnumber=0 -RA
```

```bash
sudo pppd /dev/ttyACM0 921600 \
    192.168.7.1:192.168.7.2 \
    local \
    noauth \
    nocrtscts \
    debug \
    nodetach
```

## Build snippets

### Minimal

```bash
west build -b native_sim -p
west build -b nucleo_u575zi_q -p
west build -b xiao_esp32c3 -p
```

### Native simulator networking

```bash
west build -b native_sim -p -S net -S nsos -- -DSNIPPET_ROOT="$PWD"
```

### Nucleo NCM

```bash
west build -b nucleo_u575zi_q -p -S net -S ncm -- -DSNIPPET_ROOT="$PWD"
```

### Nucleo PPP + SWO

```bash
west build -b nucleo_u575zi_q -p -S net -S ppp -S swo -- -DSNIPPET_ROOT="$PWD"
```

### XIAO Wi-Fi

```bash
west build -b xiao_esp32c3 -p -S net -S wifi -- -DSNIPPET_ROOT="$PWD"
```

### Full OTA variants

```bash
west build -b nucleo_u575zi_q -p --sysbuild -- -DSNIPPET_ROOT="$PWD" -Dapplication_SNIPPET="net;ncm;ota"
```

```bash
west build -b nucleo_u575zi_q -p --sysbuild -- -DSNIPPET_ROOT="$PWD" -Dapplication_SNIPPET="net;ppp;swo;ota"
```

```bash
west build -b xiao_esp32c3 -p --sysbuild -- -DSNIPPET_ROOT="$PWD" -Dapplication_SNIPPET="net;wifi;ota"
```
