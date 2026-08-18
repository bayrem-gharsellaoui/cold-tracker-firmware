# ColdTracker

Firmware for ColdTracker devices

## Milestone 1 — Engineering foundation

### Goal: get a tiny ColdTracker firmware running reproducibly on all supported targets.

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

- 
- What is the difference between a west workspace and a west project and a zephyr module?