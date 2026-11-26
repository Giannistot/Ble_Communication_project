# ESP32-C6 BLE Performance Benchmarking Tool (GATT Indications)

## Overview

This repository contains a benchmarking framework for evaluating Bluetooth Low Energy (BLE 5.3) connection quality using ESP32-C6 microcontrollers and the NimBLE host stack. The project measures real-time **throughput**, **packet loss**, and **latency** at the application layer by simulating a continuous data stream.

This branch implements **Native Stop-and-Wait Flow Control** using **GATT Indications**. Unlike Notifications, Indications enforce application-layer reliability directly through the BLE stack: the Peripheral sends one packet and waits for an automatic GATT Confirmation (`ATT_HANDLE_VALUE_CFM`) from the Central before transmitting the next one.

## System Architecture

The topology consists of a Point-to-Point (Star) connection between two ESP32-C6 nodes:

- **Central Node (Node 20) / Client:** Acts as the data receiver, throughput/packet-loss calculator, and automatic GATT Confirmation responder.
- **Peripheral Node (Node 21) / Server:** Acts as the data generator (dummy payload) and latency calculator.

### GATT Server Structure (Service & Characteristics)

Because GATT Indications include built-in acknowledgments at the ATT/GATT protocol layer, no separate ACK characteristic is required. The Peripheral hosts a single custom BLE Service with one characteristic:

- **Service UUID:** `2e75afd8-1981-4b3c-b5f5-256539a37d3f`
- **Data Characteristic (Indicate):** `54bbb94b-b2e8-44b4-82cf-94bf26d02f9d`
  - Used by the Peripheral to stream dummy data packets to the Central with guaranteed delivery confirmation.

## Hardware & Software Requirements

- **Hardware:** 2x ESP32-C6-WROOM-1 development boards.
- **Environment:** Visual Studio Code with the PlatformIO extension.
- **Dependencies:** `h2zero/NimBLE-Arduino` library.
- **PlatformIO Configuration (`platformio.ini`):**
  No custom build flags are required. Because Stop-and-Wait limits transmission to a single packet in flight at any time, the default NimBLE ACL buffer configuration is more than sufficient.

## Node Configuration & Responsibilities

### 1. Peripheral / Server (Node 21)

This node acts as the data source and is responsible for measuring network latency.

- **Data Generation:** Constructs a **238-byte** payload containing a 4-byte sequence ID and 234 bytes of dummy data (`DATA_SIZE = 238`, MTU = 517).
- **Native Stop-and-Wait Flow Control:** Uses `pCharacteristic->indicate()` paired with a thread-safe atomic flag (`std::atomic<bool> waiting_for_confirm`). Since NimBLE's `indicate()` is asynchronous (non-blocking), the flag prevents the `loop()` from issuing overlapping calls while yielding CPU time (`delay(1)`) to the NimBLE host task.
- **Confirmation Handling & Latency Calculation:** Records the exact microsecond timestamp (`esp_timer_get_time()`) before calling `indicate()`. When the Central's GATT Confirmation arrives, NimBLE triggers the `onStatus()` callback with status code `BLE_HS_EDONE`. The Peripheral then calculates the Round Trip Time (RTT), logs the one-way latency (`RTT / 2`), and clears `waiting_for_confirm` to allow the next packet.

### 2. Central / Client (Node 20)

This node connects to the Peripheral, sinks the data, and computes throughput and reliability metrics.

- **Fail-Fast Connection:** Executes a structured connection sequence (Connect -> Secure Connection with Passkey -> Exchange MTU -> Discover Service -> Discover Characteristic -> Subscribe to Indications). If any step fails, it cleanly deletes the client instance and restarts scanning.
- **Automatic GATT Confirmation:** Subscribes specifically to Indications (`pChr->subscribe(false, notifyCallback)`). Upon receiving each packet, the NimBLE stack automatically transmits the GATT Confirmation back to the Peripheral without requiring manual application-layer writes.
- **Throughput Calculation:** Atomically accumulates the received payload bytes (excluding the 4-byte ID) via `std::atomic<uint32_t>`. Every 1000ms, it swaps the counter to zero (`.exchange(0)`) and outputs real-time throughput in kilobits per second (kbps).
- **Packet Loss Detection:** Compares the 4-byte sequence ID of each incoming Indication against `expected_packet_id` to detect and log any sequence gaps.

## How It Works (Communication Flow)

1. **Boot & Advertising:** The Peripheral initializes the GATT server with the `INDICATE` property and begins advertising.
2. **Scanning & Connection:** The Central scans for the target Service UUID, connects with the configured `CONN_INTERVAL`, and establishes an encrypted link (Passkey `123456`) with an MTU of 517 bytes.
3. **PHY Negotiation:** Upon connection, both nodes request the configured PHY level (`1M`, `2M`, `Coded S=2`, or `Coded S=8` via `#define PHY_LEVEL`).
4. **Subscription:** The Central writes `0x0002` to the CCCD descriptor (`subscribe(false, ...)`), enabling GATT Indications.
5. **Stop-and-Wait Data Streaming:**
   - The Peripheral's `onSubscribe` callback detects `subValue & 2` and starts transmission.
   - The Peripheral sends 1 packet via `indicate()` and sets `waiting_for_confirm = true`.
   - The Central receives the packet in `notifyCallback`, updates throughput/loss stats, and its BLE stack automatically replies with a GATT Confirmation.
   - The Peripheral's `onStatus()` receives `BLE_HS_EDONE`, logs the latency, and resets `waiting_for_confirm = false`, triggering the next packet.

6. **Benchmarking Loop:**

   * The Central processes incoming packets, calculates packet loss, updates total received bytes, and fires back ACKs.

   * The Peripheral receives ACKs, calculates latency, and shifts the sliding window to allow the next batch of packets to be transmitted.
