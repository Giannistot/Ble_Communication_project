#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_timer.h"
#include "esp_log.h"

#define SERVICE_UUID "2e75afd8-1981-4b3c-b5f5-256539a37d3f"
#define CHARACTERISTIC_UUID "54bbb94b-b2e8-44b4-82cf-94bf26d02f9d"

#define ACK_CHARACTERISTIC_UUID "11111111-2222-3333-4444-555555555555"

bool deviceConnected = false;

//uint8_t dummy_data[3];
//uint8_t dummy_data_1 ;
uint8_t dummy_data_full[40];
int64_t timestamps_log[4096] = {0};

uint32_t packet_id=0;

unsigned long connection_time = 0; // <--- ΠΡΟΣΘΕΣΕ ΑΥΤΟ
unsigned long last_time_delay = 0;

static NimBLEServer* pServer;

NimBLECharacteristic* pCharacteristic=NULL;
NimBLECharacteristic* pAckCharacteristic = NULL;

class ackCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        // 1. Σταματάμε το χρονόμετρο
        int64_t end_time = esp_timer_get_time();
        
        // 2. Διαβάζουμε το ID που μας έστειλε πίσω το Central (4 bytes)
        NimBLEAttValue rx_data = pChar->getValue();
        if (rx_data.length() == sizeof(uint32_t)) {
            uint32_t received_id;
            memcpy(&received_id, rx_data.data(), sizeof(uint32_t));
            
            // 3. Βρίσκουμε πότε έφυγε αυτό το συγκεκριμένο ID
            int64_t start_time = timestamps_log[received_id % 4096];
            
            // 4. Υπολογίζουμε το Latency
            if (start_time > 0) {
                int64_t rtt_us = end_time - start_time;
                float latency_ms = rtt_us / 2000.0; // One-Way Latency (RTT/2) in milliseconds 
                
                Serial.printf("ID: %u | Latency: %.2f ms\n", received_id, latency_ms);
            }
        }
    }
};
//---------Callback of Server-------------
class serverCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
        deviceConnected = true;
        connection_time = millis(); // Καταγράφουμε ΠΟΤΕ έγινε η σύνδεση
        pServer->updateConnParams(connInfo.getConnHandle(), 80, 80, 0, 200);//setting connection_interval=100Hz and timeout=2sec
        Serial.println(">>>Device connected. Connection Parameters updated.");
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
        deviceConnected = false;

        Serial.printf(">>>Device %d disconnected. Reason code:%d.\n",connInfo.getConnHandle(), reason);

        NimBLEDevice::startAdvertising();
    }
};



void setup(){
    Serial.begin(115200);

    //delay(2000);
    Serial.print("Starting NimBle Server...\n");

    NimBLEDevice::init("First_NimBle_21");

    NimBLEDevice::setMTU(517);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new serverCallbacks);

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService -> createCharacteristic(CHARACTERISTIC_UUID, 
                                                        NIMBLE_PROPERTY::NOTIFY);
    
    // Χαρακτηριστικό Λήψης ACK (Write No Response)
    pAckCharacteristic = pService->createCharacteristic(ACK_CHARACTERISTIC_UUID, 
                                                        NIMBLE_PROPERTY::WRITE_NR);
    pAckCharacteristic->setCallbacks(new ackCallbacks());

    //pCharacteristic->setValue("Above all health");
    //dummy_data_1 = 0;
    //memset(dummy_data, 0, sizeof(dummy_data));
    memset(dummy_data_full, 0 , sizeof(dummy_data_full));

    pServer->start();
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

    pAdvertising ->addServiceUUID(SERVICE_UUID);

    pAdvertising -> enableScanResponse(true);

    //advertising interval 144*0.625 = 90ms
    pAdvertising -> setMinInterval(144); 
    pAdvertising -> setMaxInterval(144);
    
    pAdvertising ->start();

}

void loop()
{
    // Guard time: Wait 5 seconds (5000 ms) before sending the first packet.
    // The BLE Service Discovery and Notification subscription (CCCD handshake) 
    // typically takes 3.5 to 4.5 seconds to complete between ESP32 devices. 
    // This delay ensures the Central is fully ready, preventing initial packet drop. 
    if(deviceConnected && (millis() - connection_time > 5000))
    {
        //By adding delay of 50ms we acheve to minimize packet loss ,
        // but it reduces the number of packets and throughput at the central
        if(millis() - last_time_delay > 10)
        {
           //dummy_data[0]=175;
            //dummy_data[1]=217;
            //dummy_data[2]=33;
            
            //pCharacteristic->setValue(dummy_data, sizeof(dummy_data));
            //dummy_data_1 = 175;
            //pCharacteristic -> setValue(dummy_data_1);
            for(int i=4; i < sizeof(dummy_data_full); i++)
            {

                if(i%3 == 0)
                {
                    dummy_data_full[i]=175;
                }
                else if(i % 3 == 1)
                {
                    dummy_data_full[i]=217;
                }
                else
                {
                    dummy_data_full[i]=33;
                }
            }

            packet_id++;

            memcpy(dummy_data_full, &packet_id, sizeof(packet_id));

            timestamps_log[packet_id % 4096] = esp_timer_get_time();

            pCharacteristic->setValue(dummy_data_full, sizeof(dummy_data_full));


            pCharacteristic->notify();

            last_time_delay = millis();

            //delay(100);
        }  

    }
        

}