// Final Compatible Dual-Core ESP32 Dashboard
// Core 0: WiFi and ESP-NOW operations
// Core 1: Display operations
#include <TFT_eSPI.h>
#include <esp_now.h>
#include <WiFi.h>

// Define backlight pin
#define LCD_BL_PIN      (40)

// Create an instance of the TFT_eSPI class
TFT_eSPI tft = TFT_eSPI();

// ESP-NOW initialization variables
bool espNowInitialized = false;
bool wifiInitialized = false;
uint32_t receivedCounter = 0;
unsigned long lastDataReceived = 0;

// Data structure matching sender
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

// Buffered dashboard data and mutex
DashboardData_t dashData = {0};
volatile bool dataUpdated = false;
SemaphoreHandle_t dataMutex = NULL;

// Task handles
TaskHandle_t wifiTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

// Display update flags
volatile bool systemRunning = true;
bool displayInitialized = false;

// Function prototypes for tasks
void wifiTask(void *parameter);
void displayTask(void *parameter);

// Function prototypes for helpers
void initWiFi();
void initESPNow();
void drawDashboardTemplate();
void updateDashboardValues(const DashboardData_t *data);
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len);

void setup() {
  // Initialize serial
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=========================================");
  Serial.println("Final Compatible Dual-Core ESP32 Dashboard");
  Serial.println("=========================================");
  
  // Create data mutex
  dataMutex = xSemaphoreCreateMutex();
  if (dataMutex == NULL) {
    Serial.println("Failed to create mutex!");
    while(1); // Critical failure
  }
  
  // Set initial state
  systemRunning = true;
  
  // Create a task that will run on core 0 (WiFi/ESP-NOW)
  xTaskCreatePinnedToCore(
    wifiTask,           // Task function
    "WiFiTask",         // Name of task
    10000,              // Stack size (bytes)
    NULL,               // Parameter to pass
    1,                  // Task priority (1 is low)
    &wifiTaskHandle,    // Task handle
    0                   // Core to run task on (0)
  );
  
  // Create a task that will run on core 1 (Display)
  xTaskCreatePinnedToCore(
    displayTask,        // Task function
    "DisplayTask",      // Name of task
    10000,              // Stack size (bytes)
    NULL,               // Parameter to pass
    1,                  // Task priority (1 is low)
    &displayTaskHandle, // Task handle
    1                   // Core to run task on (1)
  );
  
  // The setup() and loop() functions run on core 1
  Serial.println("Tasks created. Setup complete.");
}

void loop() {
  // Main loop is kept empty, as the real work is done in the tasks
  // Just a brief delay to avoid using all CPU time
  delay(500);
}

// WiFi/ESP-NOW task that runs on core 0
void wifiTask(void *parameter) {
  Serial.println("WiFi task started on core 0");
  
  // Initialize WiFi
  Serial.println("Initializing WiFi...");
  initWiFi();
  
  if (wifiInitialized) {
    // Initialize ESP-NOW
    Serial.println("Initializing ESP-NOW...");
    initESPNow();
  }
  
  // Main task loop
  uint32_t previousCount = 0;
  while (systemRunning) {
    // Log reception rate every 5 seconds
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 5000) {
      uint32_t packetsPerSecond = (receivedCounter - previousCount) / 5;
      previousCount = receivedCounter;
      
      Serial.print("WiFi task running, packets/sec: ");
      Serial.println(packetsPerSecond);
      lastLog = millis();
    }
    
    // Give other tasks time to run
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  // We should never get here unless system is shutting down
  vTaskDelete(NULL);
}

// Display task that runs on core 1
void displayTask(void *parameter) {
  Serial.println("Display task started on core 1");
  
  // Wait a moment before initializing display (let WiFi start first)
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  // Initialize backlight pin
  pinMode(LCD_BL_PIN, OUTPUT);
  analogWrite(LCD_BL_PIN, 0); // Start with backlight off
  
  // Initialize the display
  Serial.println("Initializing display...");
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  
  // Turn on backlight
  analogWrite(LCD_BL_PIN, 255);
  
  // Draw initial screen
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.println("Display initialized");
  tft.setCursor(20, 130);
  if (wifiInitialized) {
    tft.setTextColor(TFT_GREEN);
    tft.println("WiFi OK");
  } else {
    tft.setTextColor(TFT_RED);
    tft.println("WiFi FAILED");
  }
  
  // Wait for WiFi and ESP-NOW to initialize
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  // Draw the dashboard template
  drawDashboardTemplate();
  displayInitialized = true;
  
  // Local data copy for display
  DashboardData_t localData = {0};
  
  // Main task loop
  unsigned long displayUpdateInterval = 500; // 2 updates per second
  unsigned long lastDisplayUpdate = 0;
  
  while (systemRunning) {
    unsigned long currentMillis = millis();
    
    // Check if it's time to update the display
    if (currentMillis - lastDisplayUpdate >= displayUpdateInterval) {
      lastDisplayUpdate = currentMillis;
      
      // Copy data with mutex protection if new data is available
      bool haveNewData = false;
      if (dataUpdated) {
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          memcpy(&localData, &dashData, sizeof(DashboardData_t));
          haveNewData = true;
          dataUpdated = false;
          xSemaphoreGive(dataMutex);
        }
      }
      
      // Update the values on screen
      if (haveNewData) {
        updateDashboardValues(&localData);
      }
      
      // Update connection status
      bool isConnected = (millis() - lastDataReceived < 3000);
      
      // Update status area
      tft.fillRect(100, 220, 140, 10, TFT_BLACK);
      tft.setCursor(100, 220);
      tft.setTextSize(1);
      
      if (!isConnected) {
        tft.setTextColor(TFT_RED);
        tft.println("NO DATA");
      } else {
        tft.setTextColor(TFT_GREEN);
        tft.println("RECEIVING");
      }
      
      // Show packets received
      tft.fillRect(100, 230, 140, 10, TFT_BLACK);
      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(100, 230);
      tft.print("Packets: ");
      tft.println(receivedCounter);
    }
    
    // Give other tasks time to run
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  
  // We should never get here unless system is shutting down
  vTaskDelete(NULL);
}

void drawDashboardTemplate() {
  tft.fillScreen(TFT_BLACK);
  
  // Draw header
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(40, 10);
  tft.println("Dashboard");
  
  // Draw horizontal line
  tft.drawFastHLine(0, 35, 240, TFT_WHITE);
  
  // Draw coolant temperature label
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(20, 50);
  tft.println("Coolant:");
  
  // Draw data status section
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(10, 220);
  tft.println("Data Status:");
  
  // Draw a box for the temperature display
  tft.drawRect(20, 80, 200, 60, TFT_DARKGREY);
}

void updateDashboardValues(const DashboardData_t *data) {
  // Update only the temperature value, not the whole screen
  tft.fillRect(30, 90, 180, 40, TFT_BLACK); // Clear previous value
  
  // Choose color based on temperature
  uint16_t temp = data->coolantTemp;
  uint16_t textColor;
  
  if (temp < 170) {
    textColor = TFT_BLUE;      // Cold
  } else if (temp < 210) {
    textColor = TFT_GREEN;     // Normal
  } else if (temp < 240) {
    textColor = TFT_YELLOW;    // Warning
  } else {
    textColor = TFT_RED;       // Overheating
  }
  
  // Draw temperature with larger font
  tft.setTextSize(3);
  tft.setTextColor(textColor);
  tft.setCursor(50, 95);
  tft.print(temp);
  
  // Draw temperature unit
  tft.setTextSize(2);
  tft.setCursor(150, 100);
  tft.println("F");
}

void initWiFi() {
  // Set device as a WiFi Station
  WiFi.mode(WIFI_STA);
  
  // Disable WiFi sleep mode using the standard Arduino WiFi API
  // This is compatible with all ESP32 Arduino core versions
  WiFi.setSleep(false);
  
  // Disconnect from any connected networks
  WiFi.disconnect();
  
  // Mark WiFi as initialized
  wifiInitialized = true;
  Serial.println("WiFi initialized");
}

void initESPNow() {
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    espNowInitialized = false;
    return;
  }
  
  // Register callback function
  esp_now_register_recv_cb(OnDataRecv);
  
  espNowInitialized = true;
  Serial.println("ESP-NOW initialized");
}

// Callback when data is received
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
  // Process data if it matches our structure's size
  if (data_len == sizeof(DashboardData_t)) {
    // Copy the received data to our dashboard structure with mutex protection
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      memcpy(&dashData, data, sizeof(DashboardData_t));
      dataUpdated = true;
      xSemaphoreGive(dataMutex);
    }
    
    // Update statistics
    receivedCounter++;
    lastDataReceived = millis();
    
    // Minimal debugging to reduce Serial workload
    if (receivedCounter % 100 == 0) {
      Serial.print("Packets: ");
      Serial.println(receivedCounter);
    }
  }
}
