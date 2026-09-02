This is the current **ColdTracker course roadmap**, including the two bonus milestones.

### Milestone 1 - Infrastructure

Build a reproducible development foundation before implementing product features.

Students will set up the **Zephyr development environment using a Dev Container** based on the official Zephyr Docker image, create the west workspace/application topology, add the initial application and shell, and bring up the three supported targets:

- `nucleo_u575zi_q`
- `xiao_esp32c3`
- `native_sim`

They'll also add formatting and multi-target build workflows with GitHub Actions, README status badges, and a release workflow that builds and attaches firmware artifacts to GitHub Releases.

**Outcome:** Anyone can clone ColdTracker, open the Dev Container, build all three targets, and reproduce the same builds locally and in CI.

---

### Milestone 2 - Networking

Give every ColdTracker target a way to access a network, even when the MCU doesn't contain dedicated networking hardware.

Different transports demonstrate that the Zephyr networking stack isn't tied to Wi-Fi:

- ESP32-C3 → **Wi-Fi**
- `native_sim` → **native/offloaded sockets**
- STM32 → **USB NCM**
- Boards without suitable USB/networking → **PPP over UART/ST-Link VCP**
- When PPP occupies UART → **SWO for logging**
- **Telnet shell** for interactive access when UART isn't available

This milestone also introduces **Zephyr snippets**, allowing networking features such as Wi-Fi, NCM, PPP, SWO, Telnet, etc. to be composed rather than creating huge board configurations.

**Outcome:** Each target can connect to a network, obtain an IP address, and communicate over the network.

---

### Milestone 3 - OTA Updates

Introduce OTA unusually early to demonstrate that **firmware update architecture should be considered at the beginning of product development**, not bolted on at the end.

Students introduce **MCUboot**, flash partitioning, image signing, secondary image slots, and the boot/update lifecycle.

OTA is built progressively:

**manual secondary image flashing → MCUboot swap → local HTTP download → HTTPS/TLS → GitHub Release artifact → HTTP redirect handling**

ColdTracker gets an OTA snippet and a command such as:

```text
coldtracker> update <url>
```

OTA applies to the two hardware targets; `native_sim` is deliberately excluded.

**Outcome:** A deployed ColdTracker can download a signed firmware image from a GitHub Release, install it through MCUboot, reboot, and run the new firmware.

---

### Milestone 4 - Temperature & Time

ColdTracker finally starts performing its primary product function.

Each board exposes a common **Devicetree temperature-sensor alias**, while the application accesses every sensor through the same Zephyr **Sensor subsystem API**.

The actual implementation can differ underneath:

```text
STM32 die temperature ─┐
ESP32 temperature     ─┼─→ Zephyr Sensor API → ColdTracker
native_sim hwmon      ─┘
```

For `native_sim`, the host CPU temperature is obtained through the Zephyr hwmon support.

Students also introduce **SNTP**, synchronize UTC time, and associate timestamps with measurements.

A shell command is used to request measurements instead of continuously flooding the logs.

**Outcome:** The same application-level interface produces a real, timestamped temperature measurement on all three targets.

---

### Milestone 5 - Firebase & Telemetry

Connect ColdTracker to the cloud using the **Firebase Realtime Database REST API**.

Students learn HTTPS/TLS, JSON, Firebase's data model and REST operations such as `GET`, `PUT`, `POST`, `PATCH`, and `DELETE`.

Initially, known/random test data is sent so the cloud communication can be debugged independently.

ColdTracker then reads its device configuration from Firebase at startup.

Finally, introduce the **producer–consumer pattern**:

```text
Temperature producer
        ↓
      k_msgq
        ↓
Firebase consumer
        ↓
 HTTPS / JSON
        ↓
 Firebase RTDB
```

The producer periodically creates timestamped samples while the consumer independently sends them to Firebase.

**Outcome:** ColdTracker periodically measures temperature and uploads timestamped telemetry to Firebase while retrieving its configuration from the cloud.

---

### Milestone 6 - Persistent Storage & Offline Operation

Ask the important question:

> What happens to our configuration and telemetry if the network disappears or ColdTracker loses power?

Students first study the **flash/memory layout** of each target, including the consequences of the MCUboot partitions created in Milestone 3.

Separate persistent areas are then introduced for:

- **Device configuration/identity → Zephyr Settings subsystem**
- **Temperature history → FCB**

Device ID becomes part of the persistent device settings rather than being handled as a special independent mechanism.

The volatile queue from M5 can now disappear:

```text
Sensor producer
      ↓
     FCB
      ↓
Firebase consumer
      ↓
Firebase
```

The producer continues recording measurements regardless of Firebase connectivity, while the consumer uploads stored records whenever possible.

Diagnostic shell commands can expose settings, storage information and stored samples.

**Outcome:** ColdTracker survives connectivity loss and reboot without losing its configuration or locally recorded temperature history.

---

### Milestone 7 - Web Dashboard

Complete the IoT loop by giving FreshRoute something useful to interact with.

Build a deliberately simple dashboard using only:

**HTML + Bootstrap + vanilla JavaScript**

No React/Vue, no frontend build system, almost no custom CSS and minimal JavaScript.

The dashboard can contain:

- Device ID/information
- Current temperature
- Measurement timestamp
- Device/last-seen status
- Recent measurements
- Optional **Chart.js** temperature-history graph

Finally, deploy it using **Firebase Hosting**.

The complete data path is now visible:

```text
Physical temperature
       ↓
Zephyr sensor
       ↓
ColdTracker sample
       ↓
Persistent storage
       ↓
JSON / HTTPS
       ↓
Firebase RTDB
       ↓
JavaScript
       ↓
FreshRoute dashboard
```

**Outcome:** FreshRoute can open a hosted webpage and remotely see the state and temperature history of its ColdTracker device.

---

## ⭐ Bonus Milestone 8 - Realtime Communication / SSE

Move beyond REST request/response communication and introduce **Firebase Server-Sent Events (SSE)**.

ColdTracker maintains a long-lived connection to Firebase, allowing it to receive changes such as remote configuration updates without waiting for a reboot or continuously polling.

This introduces more advanced networking concepts such as persistent connections, stream/event parsing, reconnection and connection lifetime management.

Updated settings can then be persisted through the Settings subsystem.

**Outcome:** ColdTracker can react to Firebase changes in real time while maintaining a persistent cloud connection.

---

## ⭐ Bonus Milestone 9 - Resilience & Recovery

Strengthen the persistent producer–consumer architecture from M6.

The problem becomes:

> ColdTracker rebooted. The FCB still contains its telemetry-but which records were already successfully delivered to Firebase?

Students introduce a **persistent consumer checkpoint/index** representing the last successfully uploaded data.

The lifecycle becomes conceptually:

```text
              ┌──────── FCB ─────────┐
Sensor ──────→│ sample               │
              │ sample               │
              │ sample               │
              └──────────┬───────────┘
                         ↓
                 Firebase consumer
                         ↓
                     Firebase
                         ↓ success
              Persist upload position
```

After an unexpected reboot, ColdTracker can restore its state and resume synchronization from the appropriate location.

This is also the milestone where you can deliberately test failures: network interruption, Firebase outage, reboot during synchronization, etc.

**Outcome:** ColdTracker can recover from interruptions and continue synchronizing its persistent telemetry reliably.
