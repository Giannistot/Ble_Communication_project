#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include "host/ble_gap.h"

#define SERVICE_UUID "2e75afd8-1981-4b3c-b5f5-256539a37d3f"
#define CHARACTERISTIC_UUID "54bbb94b-b2e8-44b4-82cf-94bf26d02f9d"
#define ACK_CHARACTERISTIC_UUID "11111111-2222-3333-4444-555555555555"

//Total size of data buffer
#define BUFFER_SIZE_BYTES 30

//Select value of phy level 1:1M (1Mbps), 2:2M(2Mbps), 3:Coded_2 (500kbps), 4:Coded_8 (125kbps)
#define PHY_LEVEL 1

#define CONN_INTERVAL 40

static const NimBLEAdvertisedDevice* advDevice;

static bool doConnect = false;
static bool isConnected = false;

uint8_t buffer_data[BUFFER_SIZE_BYTES];
/*atomic<uint32_t> ensures that the 3 steps (Load-Add-Store) (line 75) are performed 
as a single, indivisible operation in one clock cycle. 
While it is performing these three steps involving `volatile`, 
the operating system could interrupt the process midway, 
execute `loop()`, and read incomplete or corrupted data.*/
std::atomic<uint32_t> buffer_data_length(0);
float throughput_kps = 0;
int packets = 0;
uint32_t expected_packet_id;
uint32_t total_packet_loss;
uint32_t packet_loss_per_sec;

unsigned long last_time = 0;
unsigned long current_time = 0;
unsigned long connection_start_time = 0;

NimBLERemoteCharacteristic* pAckChr = nullptr;

//*****************Callbacks**********************
class MyclientCallbacks : public NimBLEClientCallbacks{//Callback for connction/disconnection of Client
  void onConnect(NimBLEClient* pClient){
    Serial.println(">>> Client is connected to Server");

    uint16_t conn_id = pClient -> getConnHandle();
    int rc = -1;
    switch (PHY_LEVEL)
    {
    case 1:
      rc = ble_gap_set_prefered_le_phy(conn_id, BLE_HCI_LE_PHY_1M_PREF_MASK, BLE_HCI_LE_PHY_1M_PREF_MASK, 0);
      Serial.println(">>> Requesting 1M PHY...");
      break;
    case 2:
      rc = ble_gap_set_prefered_le_phy(conn_id, BLE_HCI_LE_PHY_2M_PREF_MASK, BLE_HCI_LE_PHY_2M_PREF_MASK, 0);
      Serial.println(">>> Requesting 2M PHY...");
      break;
    case 3:
      rc = ble_gap_set_prefered_le_phy(conn_id, BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_S2);
      Serial.println(">>> Requesting Coded PHY (S=2)...");
      break;
    case 4:
      rc = ble_gap_set_prefered_le_phy(conn_id, BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_S8);
      Serial.println(">>> Requesting Coded PHY (S=8)...");
      break;
    }

    if (rc == 0) {
        Serial.println(">>> PHY change request sent successfully.");
    } else {
        Serial.printf(">>> Failed to send PHY request. Error code: %d\n", rc);
    }
  }

  void onDisconnect(NimBLEClient* pClient){
    Serial.println(">>>Disconnected");
    isConnected = false;
  }

  void onPhyUpdate(NimBLEClient* pClient, uint8_t txPhy, uint8_t rxPhy) {
      Serial.printf(">>> [CENTRAL CONFIRMATION] PHY Updated! TX PHY: %d | RX PHY: %d\n", txPhy, rxPhy);
  }

  void onPassKeyEntry(NimBLEConnInfo& connInfo)
  {
    Serial.print("Server passkey entry\n");
    NimBLEDevice::injectPassKey(connInfo, 123456);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo)
  {
    if(connInfo.isEncrypted())
    {
      Serial.println("Encryption succeeded! The connection is secure.");
    }
    else {
      Serial.println("Encryption failed!");
    }
  }

};

class MyScanCallbacks : public NimBLEScanCallbacks{
  //Discovering  peripheral based on service uuid , so we need the result of the discovery
  void onResult(const NimBLEAdvertisedDevice* AdvertisedDevice) override{
    if(AdvertisedDevice -> isAdvertisingService(NimBLEUUID (SERVICE_UUID)))
    {
      Serial.println("Service has found.");

      Serial.print("Server found with MAC: ");
      Serial.println(AdvertisedDevice->getAddress().toString().c_str());

      //stop scanning before connecting
      NimBLEDevice::getScan()->stop();

      // Save the device reference in a global for the client to use
      advDevice = AdvertisedDevice;

      //Ready to connect now
      doConnect = true;
    }

  }

}scanCallbacks;

//Callback for client to collect notify data from server
void notifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){

  if(length <= sizeof(buffer_data))
  {
    memcpy(buffer_data ,pData, length);
    buffer_data_length += (length - sizeof(uint32_t));    
  }

  // --- ID logic  ---
  // We ensure that we have found the ACK flag AND that the packet has at least 4 bytes.
  if (pAckChr != nullptr && length >= sizeof(uint32_t)) {
    uint32_t received_id;
    
    // 1. Extraction of the first 4 bytes and place them into the variable `received_id`.
    memcpy(&received_id, pData, sizeof(uint32_t));

    // --- Calculation of packet loss every second and total packet loss ---
    if (received_id > expected_packet_id) {
      packet_loss_per_sec += (received_id - expected_packet_id);
      total_packet_loss += (received_id - expected_packet_id);
    }
    // Updating what we expect for next time.
    expected_packet_id = received_id + 1;
    
    // 2. Send back to Peripheral the first 4 bytes (id) wihout response 
    pAckChr->writeValue((uint8_t*)&received_id, sizeof(uint32_t), false); 
  }
}

//Function(Fail-Fast logic) with 4 steps of checking the connection between client and server
bool connectToServer(){
  Serial.println(">>>Starting connection between client and server.");

  //1)------Create a client and achieve a connection with the server---------
  NimBLEClient* pClient = NimBLEDevice::createClient();

  //Serial.println("Client created.");
  pClient -> setClientCallbacks(new MyclientCallbacks());

  //Set connection parameters (min connection interval,  max connection interval, latency, timeout, scan interval, scan window).
  //min & max interval in 1.25ms units, scan interval & window in 0.625ms units, timeout in 10ms units before disconnecting.
  pClient->setConnectionParams(CONN_INTERVAL, CONN_INTERVAL , 0 , 500, 160, 158);

  if(!pClient->connect(advDevice)){
    //If client failed to connect, don't need to keep it as it has no data
    NimBLEDevice::deleteClient(pClient);
    Serial.println("Connection failed and client deleted");

    return false;
  }
  Serial.println("Connection established successfully");

  Serial.println("Starting Secure Connection process...");
  pClient->secureConnection();

  pClient->exchangeMTU();

  //2)------Service Discovery---------------
  NimBLERemoteService* pSvc = pClient->getService(SERVICE_UUID);
  if(pSvc == nullptr)
  {
    Serial.println("Service not found and Client disconnected.");
    pClient-> disconnect();
    return false;
  }

  //3)-------Characteristic Discovery--------------
  NimBLERemoteCharacteristic* pChr = pSvc->getCharacteristic(CHARACTERISTIC_UUID);
  if(pChr == nullptr)
  {
    Serial.println("Characteristic not found and Client disconnected.");
    pClient -> disconnect();
    return false;
  }
  // Searching for the ACK characteristic
  pAckChr = pSvc->getCharacteristic(ACK_CHARACTERISTIC_UUID);
  if(pAckChr == nullptr)
  {
    Serial.println("ACK Characteristic not found! (Check Peripheral code)");
  }

  //4)-----------Notify------------
  if(pChr->canNotify())
  {
    expected_packet_id = 1;
    total_packet_loss = 0;
    packet_loss_per_sec = 0;
    buffer_data_length = 0;
    last_time = millis();
    connection_start_time = last_time;
    pChr->subscribe(true, notifyCallback);
    Serial.println("Subscribe! Waiting for data...");
  }
  else
  {
    Serial.println("Characteristic does not support Notify. Disconnection...");
    pClient->disconnect();
    return false;
  }

  return true;

}

void setup() {
  Serial.begin(921600);

  Serial.println("Starting NimBLE Client...");

  NimBLEDevice::init(" ");

  NimBLEDevice::setSecurityAuth(false, true , true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);

  NimBLEDevice::setMTU(517);

  NimBLEScan* pScan = NimBLEDevice::getScan(); //create a new scan

  pScan->setScanCallbacks(&scanCallbacks, true);

  //set the scanning parameters (scan interval, scan window)
  //pScan -> setInterval(100);
  //pScan -> setWindow(99);

  pScan -> setActiveScan(true); //active scanning

  pScan -> start(0, false);

}

void loop() {
  // put your main code here, to run repeatedly:

  // Serial.print("Data (HEX): ");
  // for(int i=0; i < buffer_data_length; i++)
  // {
  //   if(buffer_data[i] < 16) Serial.print("0");
  //   Serial.print(buffer_data[i], HEX);
  //   Serial.print("-");

  // }
  // Serial.println();

  if(doConnect){
    doConnect = false;

    if(connectToServer())
    {
      Serial.println("Client is ready and receives data.");
      isConnected = true;

      //last_time = millis();

      //Serial.printf(">>> Connected at t=%lu ms (since boot)\n", last_time); 

      // expected_packet_id = 1;
      // total_packet_loss = 0;
      // packet_loss_per_sec = 0;
    }
    else
    {
      Serial.println("Failed to connect, starting scan");
      NimBLEDevice::getScan()->start(0,true);
    }
  }
  current_time = millis();

  if(isConnected)
  {

    if(current_time - last_time >= 1000)
    {

      throughput_kps = (buffer_data_length * 8.0)/1000.0;
      packets = buffer_data_length / BUFFER_SIZE_BYTES;
      unsigned long experiment_time = current_time - connection_start_time;

      Serial.printf("Total time: %lu | Packets: %d | Throughput: %.2f kps | Packet Loss per Second: %lu | Total Packet Loss: %lu\n", 
                                      experiment_time, packets, throughput_kps, packet_loss_per_sec, total_packet_loss);

      last_time += 1000;
      buffer_data_length = 0;
      packet_loss_per_sec = 0;
    }
  }

}