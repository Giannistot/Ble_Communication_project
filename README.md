# ESP32-C6 BLE Performance Benchmarking Tool

## Overview

This repository contains a complete benchmarking framework for evaluating Bluetooth Low Energy (BLE 5.3) connection quality using ESP32-C6 microcontrollers and the NimBLE host stack. The project is designed to measure real-time **throughput**, **packet loss**, and **latency** at the application layer by simulating a continuous data stream.

The system utilizes an Application-Layer Flow Control mechanism (Sliding Window) to maximize bandwidth utilization while preventing buffer overflows on the BLE controller.

## System Architecture

The topology consists of a Point-to-Point (Star) connection between two ESP32-C6 nodes:

* **Central Node (Node 20) / Client:** Runs `main.cpp`. Acts as the data receiver and acknowledgment transmitter.

* **Peripheral Node (Node 21) / Server:** Runs `main.cpp`. Acts as the data generator (dummy payload) and latency calculator.

### GATT Server Structure (Service & Characteristics)

The Peripheral hosts a custom BLE Service containing two distinct characteristics:

* **Service UUID:** `2e75afd8-1981-4b3c-b5f5-256539a37d3f`

* **Data Characteristic (Notify):** `54bbb94b-b2e8-44b4-82cf-94bf26d02f9d`

  * Used by the Peripheral to stream dummy data packets to the Central.

* **ACK Characteristic (Write No Response):** `11111111-2222-3333-4444-555555555555`

  * Used by the Central to immediately send back the received Packet ID to the Peripheral.

## Hardware & Software Requirements

* **Hardware:** 2x ESP32-C6-WROOM-1 development boards.

* **Environment:** Visual Studio Code with the PlatformIO extension.

* **Dependencies:** `h2zero/NimBLE-Arduino` library.

* **PlatformIO Configuration (`platformio.ini`):**
  The system relies on the default NimBLE ACL buffer count (`CONFIG_BT_NIMBLE_ACL_BUF_COUNT=24`). This default size is sufficient to safely support the application's sliding window size (`WINDOW = 10`) without dropping packets at the controller level. No custom build flags are required for this baseline configuration.

## Node Configuration & Responsibilities

### 1. Peripheral / Server (`main_2.cpp` - Node 21)

This node acts as the data source and is responsible for measuring network latency.

* **Data Generation:** Constructs a 30-byte payload containing a 4-byte sequence ID and 26 bytes of dummy data.

* **Sliding Window Protocol:** Manages the transmission flow using a `WINDOW` of 10 in-flight packets. It will pause transmission if 10 packets have been sent without receiving their corresponding ACKs, yielding CPU time to allow the NimBLE stack to process incoming data.

* **Deadlock Prevention (Timeout):** Includes a fail-safe `ACK_TIMEOUT_MS` (200ms). If the window is full and no ACKs arrive within this timeframe, the window resets to prevent system stalls.

* **Latency Calculation:** Records the exact microsecond timestamp (`esp_timer_get_time()`) when a packet is successfully handed to the BLE stack. When the Central writes the ID back to the ACK characteristic, the Peripheral calculates the Round Trip Time (RTT) and logs the one-way latency.

### 2. Central / Client (`main.cpp` - Node 20)

This node connects to the Peripheral, sinks the data, and computes throughput and reliability metrics.

* **Fail-Fast Connection:** Executes a rigid 4-step connection protocol (Connect -> Discover Service -> Discover Characteristics -> Subscribe). If any step fails, it cleanly deletes the client instance and restarts scanning.

* **Throughput Calculation:** Accumulates the exact number of payload bytes received (excluding the 4-byte ID). Every 1000ms, it calculates and outputs the real-time throughput in kilobits per second (kbps).

* **Packet Loss Detection:** Compares the sequence ID of the incoming notification against the `expected_packet_id`. Any gap in the sequence is immediately logged as a lost packet.

* **Immediate Acknowledgment:** Extracts the 4-byte packet ID from the incoming notification and immediately writes it back to the Peripheral's ACK Characteristic using a `Write Without Response` command.

## How It Works (Communication Flow)

1. **Boot & Advertising:** The Peripheral initializes the GATT server and begins advertising.

2. **Scanning & Connection:** The Central scans for the specific Service UUID, discovers the Peripheral, and initiates a secure connection (Security Auth enabled).

3. **PHY Negotiation:** Upon connection, the Central automatically requests an upgrade to the 1M PHY (or 2M / Coded PHY based on the `#define PHY_LEVEL` configuration).

4. **Subscription:** The Central subscribes to the Data Characteristic notifications.

5. **Data Streaming:**

   * The Peripheral's `onSubscribe` callback triggers, activating the transmission loop.

   * Packets are pushed out via notifications up to the `WINDOW` limit.

6. **Benchmarking Loop:**

   * The Central processes incoming packets, calculates packet loss, updates total received bytes, and fires back ACKs.

   * The Peripheral receives ACKs, calculates latency, and shifts the sliding window to allow the next batch of packets to be transmitted.
