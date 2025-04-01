#include <esp_now.h>
#include <WiFi.h>

// Define data structure for gauge readings
typedef struct __attribute__((packed)) {
  uint32_t timestamp;
  uint16_t rpm;
  uint16_t speed;
  uint16_t coolantTemp;
  uint16_t oilPressure;
  uint16_t fuelLevel;
  uint16_t batteryVoltage;
  uint16_t boostPressure;
  uint8_t checkEngine;
  uint8_t turnSignals;
} DashboardData_t;

// MAC address of your child gauge (actual MAC you found)
uint8_t childGaugeAddresses[][6] = {
  {0x18, 0x8B, 0x0E, 0xCD, 0x06, 0x48}  // Your first child gauge
  // Add more child gauges here as you get their MAC addresses
};

// Number of child gauges
const int NUM_GAUGES = sizeof(childGaugeAddresses) / sizeof(childGaugeAddresses[0]);

// Variables to track transmission success
int successCount = 0;
int failCount = 0;
unsigned long lastStatsTime = 0;

// Dashboard data structure
DashboardData_t dashboardData;

// Timing variables
unsigned long lastBroadcastTime = 0;
const int BROADCAST_INTERVAL = 500;  // Broadcast every 50ms (20Hz update rate)

// Callback function called when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    successCount++;
  } else {
    failCount++;
  }
  
  // Print stats every 5 seconds
  unsigned long currentTime = millis();
  if (currentTime - lastStatsTime >= 5000) {
    lastStatsTime = currentTime;
    
    int totalAttempts = successCount + failCount;
    float successRate = (totalAttempts > 0) ? (successCount * 100.0 / totalAttempts) : 0;
    
    Serial.print("ESP-NOW Stats - Success: ");
    Serial.print(successCount);
    Serial.print(", Fails: ");
    Serial.print(failCount);
    Serial.print(", Success Rate: ");
    Serial.print(successRate);
    Serial.println("%");
    
    // Reset counters
    successCount = 0;
    failCount = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\nCentral Dashboard Hub");
  Serial.println("--------------------");
  
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  // Print MAC address
  Serial.print("Hub MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register the send callback
  esp_now_register_send_cb(OnDataSent);
  
  // Register all child gauges as peers
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  peerInfo.channel = 0;  // Use WiFi channel 0 (auto)
  peerInfo.encrypt = false;  // No encryption
  
  // Register each gauge as a peer
  for (int i = 0; i < NUM_GAUGES; i++) {
    memcpy(peerInfo.peer_addr, childGaugeAddresses[i], 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.print("Failed to add peer: ");
      Serial.println(i);
    } else {
      Serial.print("Added peer gauge: ");
      for (int j = 0; j < 6; j++) {
        Serial.printf("%02X", childGaugeAddresses[i][j]);
        if (j < 5) Serial.print(":");
      }
      Serial.println();
    }
  }
  
  // Initialize CAN bus communication
  // setupCAN(); // Uncomment and implement when ready for CAN integration
  
  Serial.println("Central hub initialized and ready!");
}

void loop() {
  // Read data from CAN bus (actual implementation would read from CAN)
  // readCANData(); // Uncomment and implement when ready
  
  // For testing, generate some sample data
  updateDashboardData();
  
  // Broadcast data at specified interval
  unsigned long currentTime = millis();
  if (currentTime - lastBroadcastTime >= BROADCAST_INTERVAL) {
    lastBroadcastTime = currentTime;
    broadcastDashboardData();
  }
}

void updateDashboardData() {
  // In a real implementation, this would read from CAN bus
  // For testing, we'll generate sample data
  dashboardData.timestamp = 0;
  dashboardData.rpm = 0;
  dashboardData.speed = 0;
  dashboardData.coolantTemp = random(55, 87);
  dashboardData.oilPressure = 0;
  dashboardData.fuelLevel = 0;
  dashboardData.batteryVoltage = 0; // In tenths of a volt (12.0 - 14.5V)
  dashboardData.boostPressure = 0;  // In PSI, negative for vacuum
  dashboardData.checkEngine = 0; // Occasionally turn on check engine light
  dashboardData.turnSignals = 0;  // Random turn signal state
}

void broadcastDashboardData() {
  // Send data to each gauge
  for (int i = 0; i < NUM_GAUGES; i++) {
    esp_err_t result = esp_now_send(childGaugeAddresses[i], (uint8_t *)&dashboardData, sizeof(dashboardData));
    
    if (result != ESP_OK) {
      Serial.print("Failed to send to gauge: ");
      Serial.println(i);
    }
  }
}
