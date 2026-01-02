#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include "config.h"
#include <time.h> // Required for NTP and time functions

// Initialize the U8g2 library for the 128x64 I2C OLED display
// The constructor specifies the controller, resolution, and hardware I2C interface.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ I2C_SCL_PIN, /* data=*/ I2C_SDA_PIN);

// Global variables to hold Nightscout data
String sgv = "---";
String direction = "-";
long lastReadingTimestamp = 0;

// Timer variables for non-blocking updates
unsigned long previousMillis = 0;

// Function prototypes
void setupWifi();
void fetchNightscoutData();
void updateDisplay(const char* message = nullptr);
int getTrendArrowGlyph(String directionStr);

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C and the display
  u8g2.begin();
  u8g2.enableUTF8Print(); // Enable UTF-8 support for special symbols like arrows

  // Display a startup message
  updateDisplay("Connecting...");

  // Connect to WiFi
  setupWifi();

  // Configure timezone for timestamp conversion after WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    setenv("TZ", TZ_INFO, 1); // Set timezone based on config.h
    tzset(); // Apply timezone settings
    Serial.println("Timezone configured for timestamp conversion.");
  }

  // Fetch initial data immediately
  if (WiFi.status() == WL_CONNECTED) {
    fetchNightscoutData();
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Check if it's time for the next update
  if (currentMillis - previousMillis >= UPDATE_INTERVAL_MS) {
    previousMillis = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      fetchNightscoutData();
    } else {
      // If WiFi is disconnected, try to reconnect
      Serial.println("WiFi disconnected. Reconnecting...");
      setupWifi();
    }
  }
}

void setupWifi() {
  WiFi.disconnect(true); // Disconnect from any previous network
  delay(100);

  for (int i = 0; i < NUM_WIFI_NETWORKS; i++) {
    // Skip empty SSID entries
    if (strlen(WIFI_SSIDS[i]) == 0) {
      continue;
    }

    Serial.print("Attempting to connect to SSID: ");
    Serial.println(WIFI_SSIDS[i]);

    // Manually draw a two-line connecting message for long SSIDs
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr); // Use a standard font
    const char* line1 = "Connecting to";
    u8g2.drawStr((128 - u8g2.getStrWidth(line1)) / 2, 28, line1);
    const char* ssidLine = WIFI_SSIDS[i];
    u8g2.drawStr((128 - u8g2.getStrWidth(ssidLine)) / 2, 44, ssidLine); // Y=44 for the second line
    u8g2.sendBuffer();

    WiFi.begin(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);

    int attempts = 0;
    // Try to connect for about 15 seconds
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    // If connected, break the loop
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      updateDisplay("Connected!");
      delay(1000); // Show "Connected!" for a moment
      return; // Exit the function successfully
    } else {
      Serial.println("\nFailed to connect.");
      WiFi.disconnect(true); // Important to disconnect before trying the next one
      delay(100);
    }
  }

  // If we've gone through all networks and none connected
  Serial.println("\nFailed to connect to any WiFi network.");
  updateDisplay("WiFi Error");
}

void fetchNightscoutData() {
  HTTPClient http;
  // Match the user's curl command: curl -H "token: apireader-e59393e46837d979" "http://localhost:1337/api/v1/entries.json?count=5"
  String apiUrl = String(NIGHTSCOUT_URL) + "/api/v1/entries.json?count=1";

  Serial.print("Fetching data from: ");
  Serial.println(apiUrl);

  http.begin(apiUrl);
  // Add the custom token header required for authentication
  http.addHeader("token", NIGHTSCOUT_API_TOKEN);
  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      
      // Use ArduinoJson to parse the response
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        updateDisplay("JSON Error");
        return;
      }

      // Extract data from the first entry in the JSON array
      if (doc.is<JsonArray>() && doc.as<JsonArray>().size() > 0) {
        JsonObject firstEntry = doc[0];
        if (!firstEntry["sgv"].isNull()) {
          float rawSgv = firstEntry["sgv"].as<float>(); // Get SGV as float
          
          // Apply user's logic: divide by 18 and round to one decimal place
          float processedSgv = round(rawSgv / 18.0 * 10.0) / 10.0;
          sgv = String(processedSgv, 1); // Convert back to String with 1 decimal place
          // sgv = "28.7";

          direction = firstEntry["direction"].as<String>();
          lastReadingTimestamp = firstEntry["date"].as<long long>() / 1000; // Convert ms to seconds, use long long for safety
          
          Serial.print("Raw SGV: ");
          Serial.println(rawSgv);
          Serial.print("Processed SGV: ");
          Serial.println(sgv); // Print the processed SGV
          Serial.print("Direction: ");
          // direction = "DoubleUp";
          Serial.println(direction);

          updateDisplay(); // Update with new data
        } else {
          updateDisplay("No SGV");
        }
      }

    } else {
      Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
      String errorMsg = "HTTP Err " + String(httpCode);
      updateDisplay(errorMsg.c_str());
    }
  } else {
    Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    updateDisplay("HTTP Fail");
  }

  http.end();
}

void updateDisplay(const char* message) {
  u8g2.clearBuffer();

  if (message) {
    // Display a centered message (for errors or status)
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth(message)) / 2, 36, message);
  } else {
    // --- Display Normal Data ---

    // Display SGV (centered) and Trend Arrow (to the right)
    u8g2.setFont(u8g2_font_logisoso46_tn); // Set the font for SGV
        const int gap = 5;
        int sgvWidth = u8g2.getStrWidth(sgv.c_str());
    
        // --- New Layout Logic to center SGV + Arrows together ---
        int arrowWidth = 0;
        if (direction.equals("DoubleDown") || direction.equals("DoubleUp")) {
            arrowWidth = 20; // Approx width for two arrows (10px each)
        } else if (getTrendArrowGlyph(direction) != 0) {
            arrowWidth = 10; // Approx width for one arrow
        }
    
        int totalWidth = sgvWidth + (arrowWidth > 0 ? gap + arrowWidth : 0);
        int sgvStartX = (128 - totalWidth) / 2;
        // --- End New Layout Logic ---
    
        // Draw SGV
        u8g2.drawStr(sgvStartX, 62, sgv.c_str()); // Adjusted Y for new font size
    
        // Draw Trend Arrow(s) to the right of the SGV
        int trendGlyph = getTrendArrowGlyph(direction);
        if (trendGlyph != 0) {
          int arrowStartX = sgvStartX + sgvWidth + gap; // Position arrow to the right of SGV
          u8g2.setFont(u8g2_font_unifont_t_symbols); // Use the dedicated symbols font
                                        u8g2.drawGlyph(arrowStartX, 47, trendGlyph); // Adjusted Y for new font size
                                        // If it's a double arrow case, draw a second one
                                        if (direction.equals("DoubleDown") || direction.equals("DoubleUp")) {
                                          u8g2.drawGlyph(arrowStartX + 10, 47, trendGlyph); // Adjusted Y for new font size
                                        }    }

    // Display Date (DD.MM.YYYY) and Time (HH:mm) at the top
    if (lastReadingTimestamp > 0) {
      struct tm timeinfo;
      localtime_r(&lastReadingTimestamp, &timeinfo);

      char timeStr[6];
      strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
      char dateStr[11];
      strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

      u8g2.setFont(u8g2_font_ncenR08_tr); // Reverted to original font for time/date

      // Draw Date top-left
      // u8g2.drawStr(2, 9, dateStr);

      // Draw Time top-right
      int timeWidth = u8g2.getStrWidth(timeStr);
      u8g2.drawStr(128 - timeWidth - 2, 9, timeStr);
    }

    // Wi-Fi Status Indicator
    u8g2.setFont(u8g2_font_siji_t_6x10);
    u8g2.drawGlyph(118, 20, 0xe29a);

    // Display SSID (left) and RSSI (right) at the bottom
    String ssidStr = WiFi.SSID();
    String rssiStr = String(WiFi.RSSI()) + "dBm";
    u8g2.setFont(u8g2_font_6x13_tf);
    
    u8g2.drawStr(2, 9, ssidStr.c_str());

    int rssiWidth = u8g2.getStrWidth(rssiStr.c_str());
    u8g2.drawStr((128 - rssiWidth) / 2, 9, rssiStr.c_str());
  }
  
  u8g2.sendBuffer();
}

// Returns the hex code for a U8g2 glyph based on direction string
int getTrendArrowGlyph(String directionStr) {
  if (directionStr.equals("SingleUp")) return 0x2191; // ↑
  if (directionStr.equals("DoubleUp")) return 0x2191; // ↑
  if (directionStr.equals("FortyFiveUp")) return 0x2197; // ↗
  if (directionStr.equals("Flat")) return 0x2192; // →
  if (directionStr.equals("FortyFiveDown")) return 0x2198; // ↘
  if (directionStr.equals("SingleDown")) return 0x2193; // ↓
  if (directionStr.equals("DoubleDown")) return 0x2193; // ↓
  return 0; // Return 0 if no match
}
