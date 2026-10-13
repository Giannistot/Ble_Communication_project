# 📡 ESP32 BLE Performance & Telemetry System

A robust Bluetooth Low Energy (BLE) communication framework built for ESP32 microcontrollers using the **NimBLE** stack. This project is currently in the experimental phase, designed to evaluate and measure real-time network performance metrics under various configurable parameters (Payload Size, Connection Intervals, PHY, etc.).

## 📑 Table of Contents
- [Architecture](#-architecture)
- [Key Features](#-key-features)
- [Requirements](#-requirements)
- [Quick Start](#-quick-start)
- [Expected Output](#-expected-output)

---

## 🏗️ Architecture

The system operates on a Master/Slave (Central/Peripheral) topology, divided into two dedicated components:

### 1. Peripheral (`/peripheral`) | *BLE Server & Data Source*
* **Role:** Acts as the BLE Server and continuously transmits data.
* **Connection Control:** Dictates the connection parameters (`updateConnParams`) upon connection to establish a baseline interval (e.g., 100 ms).
* **Data Generation:** Generates and transmits **variable-length dummy payloads** (e.g., 40 bytes up to 240+ bytes, depending on the test scenario) containing unique Packet IDs via BLE Notifications.
* **Latency Tracking:** Utilizes a dedicated `WRITE_NO_RSP` characteristic to receive ACKs from the Central. It matches the echoed Packet IDs with local timestamps to calculate real-time One-Way Latency (RTT/2) with microsecond precision.

### 2. Central (`/central`) | *BLE Client & Telemetry Receiver*
* **Role:** Acts as the BLE Client (Master) and data sink.
* **Continuous Scanning:** Uses aggressive scanning parameters to quickly discover the Peripheral.
* **Echo/ACK Mechanism:** Instantly extracts the 4-byte Packet ID from incoming notifications and echoes it back to the Peripheral to facilitate latency calculations.
* **Telemetry Engine:** Computes Network Throughput (kbps), Packets Received, and Packet Loss (by comparing expected vs. received IDs), outputting the metrics precisely every ~1000 ms.

---

## ✨ Key Features

* 🛡️ **Connection Guard Time:** Implements a strict 5-second delay post-connection on the Peripheral side. This allows BLE CCCD handshakes and service discoveries to fully stabilize before data transmission begins, preventing initial packet drops.
* ⏱️ **Real-Time Analytics:** Simultaneously measures bandwidth capability (on the Central) and transmission latency (on the Peripheral).
* 🎛️ **Highly Configurable:** Built to test different BLE constraints. Users can easily modify buffer sizes, intervals, and payload structures in the code to observe how they affect throughput and packet loss.

---

## 🛠️ Requirements

**Hardware:**
* 2 × ESP32 Microcontrollers (Compatible with ESP32-C6 and other standard ESP32 variants).

**Software:**
* [PlatformIO](https://platformio.org/) or [Arduino IDE](https://www.arduino.cc/en/software).
* `h2zero/NimBLE-Arduino` library (A highly optimized, memory-efficient BLE stack).

---

## 🚀 Quick Start

1. **Configure Parameters:** Set your desired `BUFFER_SIZE_BYTES` in both files (e.g., 40, 240) depending on the experiment.
2. **Flash the Devices:** Upload `/Peripheral_Device_21/main_2.cpp` to the first ESP32 and `/Central_Device_20/main.cpp` to the second.
3. **Monitor:** Open two Serial Monitor instances at a `115200` baud rate.
4. **Boot Sequence:**
   * Power on / Reset the **Peripheral** device first.
   * Immediately Power on / Reset the **Central** device.
5. **Observe:** After a successful connection and a 5-second Guard Time, data transmission will automatically begin.

---

## 📊 Expected Output Behavior

Since this is an experimental setup with variable payload sizes and intervals, the exact numbers will fluctuate based on your configurations. 

* **On the Central Device (Telemetry):**
  After the connection succeeds, the Serial Monitor will output a continuous stream updating exactly every 1000 ms. Each line will display the current runtime, the total number of packets received within that second, the calculated throughput in kps (kilobits per second), and any detected packet loss.

* **On the Peripheral Device (Latency):**
  Following the connection confirmation, the Serial Monitor will print a rapid stream of individual Packet IDs alongside their calculated One-Way Latency (in milliseconds).
