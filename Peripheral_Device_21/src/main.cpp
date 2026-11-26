#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include <atomic>

#define SERVICE_UUID "2e75afd8-1981-4b3c-b5f5-256539a37d3f"
#define CHARACTERISTIC_UUID "54bbb94b-b2e8-44b4-82cf-94bf26d02f9d"

//Print latency only every N packets. Serial.printf on every ACK causes blocking.
//the NimBLE host task (115200 baud) and reduces throughput.
#define LATENCY_PRINT_EVERY 1

//If no ACK arrives for a certain number of ms while the window is full, we break the deadlock.
//#define ACK_TIMEOUT_MS 200

//Select value of phy level 1:1M (1Mbps), 2:2M(2Mbps), 3:Coded_2 (500kbps), 4:Coded_8 (125kbps)
#define PHY_LEVEL 4

#define DATA_SIZE 104

uint8_t dummy_data_full[DATA_SIZE];
int64_t timestamps_log[4096] = {0};

uint32_t packet_id = 0;
std::atomic<bool> waiting_for_confirm(false);
std::atomic<int64_t> packet_start_time(0);

volatile bool deviceConnected = false;
volatile bool subscribed = false;
volatile unsigned long last_ack_time = 0;

static NimBLEServer* pServer;
NimBLECharacteristic* pCharacteristic = NULL;
NimBLECharacteristic* pAckCharacteristic = NULL;

//---------Callback του data characteristic: ξέρουμε πότε το Central έκανε subscribe---------
class dataCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        if (subValue & 2) { // notifications ενεργοποιήθηκαν
            packet_id = 0;
            waiting_for_confirm = false;
            //last_acked_id = 0;
            //last_ack_time = millis();
            //memset(timestamps_log, 0, sizeof(timestamps_log));
            subscribed = true; 
            Serial.println(">>> Central subscribed to INDICATIONS, starting transmission");
        } else {
            subscribed = false;
            waiting_for_confirm = false;
        }
    }

    void onStatus(NimBLECharacteristic* pChar, int code) override{
        if(code == BLE_HS_EDONE)
        {
            int64_t end_time = esp_timer_get_time();
            int64_t start_time = packet_start_time.load();

            if (start_time > 0 && (packet_id % LATENCY_PRINT_EVERY) == 0) {
                int64_t rtt_us = end_time - start_time;
                float latency_ms = rtt_us / 2000.0; // One-way latency (RTT/2) σε ms
                Serial.printf("ID: %u | Latency: %.2f ms\n", packet_id, latency_ms);
                
            }
            waiting_for_confirm = false;
        }else if(code !=0){
            Serial.printf(">>> Indication error code: %d\n", code);
            waiting_for_confirm = false;
        }
    }
};

//---------Callback του Server---------
class serverCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        Serial.println(">>> Device connected.");
        //Serial.printf("Conn interval: %u units (x1.25ms)\n", connInfo.getConnInterval());

        int rc = -1;

        switch (PHY_LEVEL) {
            case 1:
                rc = ble_gap_set_prefered_le_phy(connInfo.getConnHandle(), BLE_GAP_LE_PHY_1M_MASK, BLE_GAP_LE_PHY_1M_MASK, 0);
                Serial.println(">>> Peripheral explicitly requested 1M PHY.");
                break;
            case 2:
                rc = ble_gap_set_prefered_le_phy(connInfo.getConnHandle(), BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
                Serial.println(">>> Peripheral explicitly requested 2M PHY.");
                break;
            case 3:
                rc = ble_gap_set_prefered_le_phy(connInfo.getConnHandle(), BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_S2);
                Serial.println(">>> Peripheral explicitly requested Coded PHY (S=2).");
                break;
            case 4:
                rc = ble_gap_set_prefered_le_phy(connInfo.getConnHandle(), BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_S8);
                Serial.println(">>> Peripheral explicitly requested Coded PHY (S=8).");
                break;
        }

        if (rc != 0) {
            Serial.printf(">>> Failed to send PHY request. Error: %d\n", rc);
        }
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        subscribed = false;
        Serial.printf(">>> Device %d disconnected. Reason code: %d.\n", connInfo.getConnHandle(), reason);
        NimBLEDevice::startAdvertising();
    }

    void onPhyUpdate(NimBLEConnInfo& connInfo, uint8_t txPhy, uint8_t rxPhy) {
        Serial.printf(">>> [PERIPHERAL CONFIRMATION] PHY Updated! TX PHY: %d | RX PHY: %d\n", txPhy, rxPhy);
    }
};

void setup() {
    Serial.begin(921600);
    Serial.print("Starting NimBle Server...\n");

    NimBLEDevice::init("First_NimBle_21");
    NimBLEDevice::setSecurityAuth(false, true , true);
    NimBLEDevice::setSecurityPasskey(123456);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

    NimBLEDevice::setMTU(517);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new serverCallbacks);

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID,
                                                     NIMBLE_PROPERTY::INDICATE);
    pCharacteristic->setCallbacks(new dataCallbacks());

    // Χαρακτηριστικό λήψης ACK (Write No Response)
    //pAckCharacteristic = pService->createCharacteristic(ACK_CHARACTERISTIC_UUID,
    //                                                    NIMBLE_PROPERTY::WRITE_NR);
    //pAckCharacteristic->setCallbacks(new ackCallbacks());

    memset(dummy_data_full, 0, sizeof(dummy_data_full));

    for (int i = 4; i < sizeof(dummy_data_full); i++) {
        if (i % 3 == 0)      dummy_data_full[i] = 175;
        else if (i % 3 == 1) dummy_data_full[i] = 217;
        else                 dummy_data_full[i] = 33;
    }

    pServer->start();

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->enableScanResponse(true);

    // advertising interval 144*0.625 = 90ms
    pAdvertising->setMinInterval(144);
    pAdvertising->setMaxInterval(144);

    pAdvertising->start();
}

void loop() {
    if (!subscribed) {
        delay(1); // Give time to the idle task 
        return;
    }

    if (!waiting_for_confirm.load()) {

        //packet_id++;
        uint32_t next_packet_id = packet_id + 1;
        memcpy(dummy_data_full, &next_packet_id, sizeof(next_packet_id));
        pCharacteristic->setValue(dummy_data_full, sizeof(dummy_data_full));

        packet_id = next_packet_id;
        packet_start_time = esp_timer_get_time();
        waiting_for_confirm = true;

        bool success = pCharacteristic->indicate();


        if(!success)
        {
            packet_id--;
            waiting_for_confirm = false;
            delay(1);

        }
    } else {
        delay(1); 
    }
}