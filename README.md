# Revive-Link

**An offline, encrypted LoRa mesh network for sending SOS alerts with GPS location when cellular networks are down.**

Revive-Link lets people in a disaster area, or anyone in danger, send an SOS with their location by pressing one button on a small handheld device. The alert travels over LoRa radio, hopping through other handheld nodes when needed, until it reaches a base station. No mobile network, Wi-Fi or internet is required.

Capstone project, Department of Computer Science and Engineering, Dayananda Sagar University, Bengaluru (2025–2026).
Indian patent application No. **202641020566** (published 6 Mar 2026).

![Hardware prototype](docs/images/hardware.jpg)

---

## Features

- **One-button SOS** with live GPS coordinates (or `NO_FIX` when no satellite lock)
- **Long-range LoRa radio** at 433 MHz, independent of any infrastructure
- **Multi-hop relaying**: handhelds forward packets toward the base station (up to 5 hops)
- **Route discovery** with AODV-style route request / route reply (RREQ / RREP) messages
- **Reliable delivery**: the base station acknowledges every SOS; handhelds retry up to 3 times (5 s timeout each) and start route discovery after the second failure
- **Duplicate suppression** so relayed packets are not processed or forwarded twice
- **Heartbeats** every 30 s so the base station knows which nodes are alive
- **AES-128-GCM encryption and authentication** of every payload (ESP32 `mbedtls`); tampered or wrongly keyed packets are rejected
- **Base station OLED display** showing the SOS node and its GPS location, plus a red alert LED
- **PC dashboard** that shows alerts in a table and on a map (currently a simulation, see below)

---

## System architecture

![System data flow](docs/images/system_flow.png)

```
[Handheld node] --LoRa--> [Handheld node (relay)] --LoRa--> [Base station] --> OLED / serial / dashboard
   SOS button                 forwards packets                decrypts, verifies,
   GPS module                 (hop_count + 1)                 sends ACK
```

1. A user presses the SOS button on a handheld node.
2. The node builds the message `<node>|SOS|<lat>|<lon>`, encrypts it with AES-128-GCM and broadcasts it over LoRa.
3. Any handheld that hears a packet addressed to the base station forwards it, increasing the hop count.
4. The base station decrypts and verifies the packet, shows the alert on its OLED display and sends an encrypted ACK back.
5. If no ACK arrives, the handheld retries and starts route discovery.

---

## Repository structure

```
revive-link/
├── firmware/
│   ├── base_station/
│   │   ├── base_station.ino      Base station firmware
│   │   ├── mesh_config.h         Node ID, LoRa pins, packet format, timing
│   │   └── secrets.example.h     Template for the AES key file
│   └── handheld/
│       ├── handheld.ino          Handheld SOS node firmware
│       ├── mesh_config.h
│       └── secrets.example.h
├── dashboard/
│   ├── dashboard.py              PC dashboard (table + map)
│   └── requirements.txt
└── docs/images/                  Hardware photo and system diagram
```

---

## Hardware

| Component | Handheld node | Base station |
|---|---|---|
| Microcontroller | ESP32 (WROOM) | ESP32 (WROOM) |
| Radio | LoRa SX1278 module, 433 MHz | LoRa SX1278 module, 433 MHz |
| GPS | NEO-6M GPS module with antenna | – |
| Display | – | SSD1306 OLED, 128×64, I2C |
| Input / output | SOS push button, blue and green LEDs | Red alert LED |

### Pin connections

**Handheld node**

| Signal | ESP32 pin |
|---|---|
| LoRa SCK / MISO / MOSI | 18 / 19 / 23 |
| LoRa CS / RST / IRQ (DIO0) | 5 / 12 / 13 |
| GPS RX / TX (UART2) | 16 / 17 |
| SOS button (to GND, internal pull-up) | 27 |
| Blue LED (sending) | 2 |
| Green LED (delivered) | 15 |

**Base station**

| Signal | ESP32 pin |
|---|---|
| LoRa SCK / MISO / MOSI | 18 / 19 / 23 |
| LoRa CS / RST / IRQ (DIO0) | 5 / 14 / 26 |
| OLED SDA / SCL (address 0x3C) | 21 / 22 |
| Red LED (SOS alert) | 2 |

---

## Setup

### 1. Arduino IDE

1. Install the **ESP32 board package** (Boards Manager → "esp32" by Espressif). `mbedtls` is included with it.
2. Install these libraries (Library Manager):
   - **LoRa** by Sandeep Mistry
   - **TinyGPSPlus** by Mikal Hart
   - **Adafruit SSD1306** and **Adafruit GFX Library** (base station only)

### 2. Encryption key

The AES key is not stored in this repository. In **both** `firmware/base_station/` and `firmware/handheld/`:

1. Copy `secrets.example.h` and rename the copy to `secrets.h`.
2. Replace the zeros with your own random 16-byte key and 12-byte IV.
3. Use the **same** values on every device.

`secrets.h` is listed in `.gitignore` and is never uploaded.

### 3. Node IDs

In `firmware/handheld/mesh_config.h`, set a different ID for each handheld before flashing it:

```cpp
#define THIS_NODE NODE_1   // NODE_1, NODE_2 or NODE_3
```

The base station uses `BASE_ID` (already set).

### 4. Flash and run

1. Open `firmware/base_station/base_station.ino`, select your ESP32 board and port, and upload.
2. Open `firmware/handheld/handheld.ino` and upload it to each handheld (changing `THIS_NODE` each time).
3. Open the Serial Monitor at **115200 baud** to see packets, relays, ACKs and decrypted messages.
4. Press the SOS button on a handheld: the base station's OLED shows `!! SOS !!` with the node and its GPS location, and the handheld's green LED lights up when the ACK arrives.

### 5. Dashboard (optional)

```bash
cd dashboard
pip install -r requirements.txt
python dashboard.py
```

The dashboard currently runs in **simulation mode**: it generates sample SOS alerts every 10 seconds to demonstrate the table and map view. Connecting it to the base station's serial output is planned (see below).

---

## Packet format

Every LoRa packet has a fixed size of 85 bytes:

| Field | Size | Description |
|---|---|---|
| `src` | 1 byte | Sender node ID |
| `dst` | 1 byte | Destination node ID (`0` = base, `0xFF` = broadcast) |
| `hop_count` | 1 byte | Incremented by every relay (max 5) |
| `seq` | 2 bytes | Sequence number |
| `payload` | 64 bytes | AES-128-GCM ciphertext |
| `tag` | 16 bytes | GCM authentication tag |

Message types (inside the encrypted payload): `SOS`, `ACK`, `HB` (heartbeat), `RREQ`, `RREP`.

---

## Security notes and future work

This is a working prototype. The following known limitations are documented here openly, with the planned fixes:

1. **Nonce reuse.** The GCM nonce is derived from a fixed base IV and the sequence number, which restarts at 0 on every boot and is the same across nodes that share one key. Reusing a nonce with the same key weakens both confidentiality and authentication in AES-GCM.
   *Planned fix:* generate a random 12-byte nonce per packet (`esp_random()`) and send it in the packet header, or include the node ID and a persistent boot counter in the nonce.
2. **Unauthenticated header.** `src`, `dst`, `seq` and `hop_count` are not passed to GCM as additional authenticated data (AAD), so they could be modified without detection.
   *Planned fix:* pass the fixed header fields as AAD (excluding `hop_count`, which relays must change).
3. **Shared static key.** All nodes use one pre-shared key, so one captured device exposes the network.
   *Planned fix:* per-node keys or a lightweight key-exchange step during provisioning.
4. **Simplified routing.** Route discovery follows the AODV RREQ / RREP pattern, but forwarding is hop-limited flooding toward the base station; the route table is not yet used to pick a next hop.
5. **Dashboard.** Read SOS lines from the base station over USB serial (`pyserial`) to replace the simulated alerts.

---

## Status

- Hardware prototype built and tested with handheld nodes and a base station
- Indian patent application No. 202641020566: filed 22 Feb 2026, published 6 Mar 2026

## Team

[Add team member names and roles]

## License

All rights reserved. This project is the subject of a pending patent application.
