#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// Max number of networks to store
#define MAX_NETWORKS 10
#define AP_SSID "Nightscout_Setup"
#define AP_PASS "12345678" // Optional password for the setup AP

struct WifiNetwork {
  char ssid[33];
  char password[65];
  unsigned long lastUsedTimestamp; // Counter to track LRU
};

class NightscoutWifiManager {
private:
  Preferences prefs;
  WebServer server;
  DNSServer dnsServer;
  WifiNetwork networks[MAX_NETWORKS];
  
  // Helper to load networks from NVS
  void loadNetworks() {
    prefs.begin("wifi_creds", true); // Read-only mode
    size_t size = prefs.getBytes("nets", networks, sizeof(networks));
    prefs.end();
    
    if (size != sizeof(networks)) {
      Serial.println("No saved networks found or size mismatch. Initializing empty.");
      memset(networks, 0, sizeof(networks));
    } else {
        Serial.println("Loaded saved networks:");
        for(int i=0; i<MAX_NETWORKS; i++) {
            if(strlen(networks[i].ssid) > 0) {
                Serial.printf("Slot %d: %s (Last used: %lu)\n", i, networks[i].ssid, networks[i].lastUsedTimestamp);
            }
        }
    }
  }

  // Helper to save networks to NVS
  void saveNetworks() {
    prefs.begin("wifi_creds", false); // Read-write mode
    prefs.putBytes("nets", networks, sizeof(networks));
    prefs.end();
  }

  // Find the index of the Least Recently Used network (smallest timestamp)
  int getLRUIndex() {
    int lruIndex = 0;
    unsigned long minTimestamp = -1; // Max ULONG

    // First, look for an empty slot
    for (int i = 0; i < MAX_NETWORKS; i++) {
      if (strlen(networks[i].ssid) == 0) {
        return i;
      }
    }

    // If full, find the oldest
    for (int i = 0; i < MAX_NETWORKS; i++) {
      if (networks[i].lastUsedTimestamp < minTimestamp) {
        minTimestamp = networks[i].lastUsedTimestamp;
        lruIndex = i;
      }
    }
    return lruIndex;
  }

  // Find index by SSID
  int getIndexBySSID(const char* ssid) {
    for (int i = 0; i < MAX_NETWORKS; i++) {
      if (strncmp(networks[i].ssid, ssid, 32) == 0) {
        return i;
      }
    }
    return -1;
  }

  // Web Server Handlers
  void handleRoot() {
    int n = WiFi.scanNetworks();
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
    html += "body{font-family:sans-serif;padding:20px;background:#222;color:#fff} h2{border-bottom:1px solid #555; margin-top: 20px;}";
    html += "a{display:block;background:#444;color:#fff;padding:10px;margin:5px 0;text-decoration:none;border-radius:5px}";
    html += "input{width:100%;padding:10px;margin:5px 0;box-sizing:border-box}";
    html += "button{color:white;border:none;padding:10px 20px;cursor:pointer;width:100%;border-radius:5px;margin-bottom:10px;font-size:16px;}";
    html += ".btn-connect{background:#007bff;} .btn-add{background:#28a745;}";
    html += "ul{list-style-type:none;padding:0;} li{background:#333;padding:5px 10px;margin:2px 0;border-radius:3px;}";
    html += "</style></head><body>";

    // --- Section: Saved Networks ---
    html += "<h2>Saved Networks (" + String(MAX_NETWORKS) + " max)</h2><ul>";
    bool anySaved = false;
    for (int i = 0; i < MAX_NETWORKS; i++) {
        if (strlen(networks[i].ssid) > 0) {
            html += "<li>" + String(networks[i].ssid) + "</li>";
            anySaved = true;
        }
    }
    if (!anySaved) html += "<li>No networks saved.</li>";
    html += "</ul>";

    // --- Section: Scan Results ---
    html += "<h2>Available Networks</h2>";
    if (n == 0) {
      html += "<p>No networks found.</p>";
    } else {
      for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "" : "*";
        html += "<a href='javascript:void(0)' onclick=\"document.getElementById('s').value='" + ssid + "'\">" + ssid + " " + encryption + " (" + rssi + ")</a>";
      }
    }

    // --- Section: Form ---
    html += "<h2>Add Network</h2><form action='/save' method='POST'>";
    html += "<input type='text' id='s' name='ssid' placeholder='SSID' required>";
    html += "<input type='password' name='pass' placeholder='Password'>";
    
    html += "<button type='submit' name='action' value='connect' class='btn-connect'>Save & Connect (Reboot)</button>";
    html += "<button type='submit' name='action' value='add' class='btn-add'>Save Only (Stay Here)</button>";
    
    html += "</form>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  }

  void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String action = server.arg("action");

    if (ssid.length() > 0) {
      // Find where to save
      int idx = getIndexBySSID(ssid.c_str());
      if (idx == -1) {
        idx = getLRUIndex(); // New network, overwrite LRU
      }
      
      // Update the slot
      strncpy(networks[idx].ssid, ssid.c_str(), 32);
      networks[idx].ssid[32] = 0; // Ensure null termination
      strncpy(networks[idx].password, pass.c_str(), 64);
      networks[idx].password[64] = 0;
      
      // We set a high timestamp so it's considered "newest"
      networks[idx].lastUsedTimestamp = millis(); 

      saveNetworks();

      if (action == "add") {
          // If "Save Only", redirect back to root
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
      } else {
          // If "Save & Connect", reboot
          String html = "<!DOCTYPE html><html><body><h2>Saved!</h2><p>Connecting to " + ssid + "...</p><p>Device will reboot.</p></body></html>";
          server.send(200, "text/html", html);
          delay(2000);
          ESP.restart();
      }
    } else {
      server.send(400, "text/plain", "SSID missing");
    }
  }

public:
  NightscoutWifiManager() : server(80) {}

  // Function to supply to the main loop to draw status
  typedef void (*DisplayUpdateCallback)(const char* msg);

  bool connect(DisplayUpdateCallback updateDisplay) {
    loadNetworks();
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.println("Scanning networks...");
    updateDisplay("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d networks\n", n);

    int bestSlot = -1;
    int bestRSSI = -1000;

    // Find the best known network available
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        for(int j=0; j<MAX_NETWORKS; j++) {
            if(ssid.equals(networks[j].ssid)) {
                 Serial.printf("Match found: %s (%d dBm)\n", networks[j].ssid, WiFi.RSSI(i));
                 if(WiFi.RSSI(i) > bestRSSI) {
                     bestRSSI = WiFi.RSSI(i);
                     bestSlot = j;
                 }
            }
        }
    }

    if (bestSlot != -1) {
        Serial.printf("Connecting to best match: %s\n", networks[bestSlot].ssid);
        updateDisplay(networks[bestSlot].ssid);
        
        WiFi.begin(networks[bestSlot].ssid, networks[bestSlot].password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nConnected!");
            // Update Usage Counter (using a global monotonic counter stored in NVS would be better, 
            // but for simplicity we can just increment the max existing timestamp + 1)
            
            unsigned long maxTs = 0;
             for(int k=0; k<MAX_NETWORKS; k++) {
                 if(networks[k].lastUsedTimestamp > maxTs) maxTs = networks[k].lastUsedTimestamp;
             }
             networks[bestSlot].lastUsedTimestamp = maxTs + 1;
             saveNetworks(); // Save the updated timestamp
             
            return true;
        } else {
            Serial.println("\nFailed to connect.");
        }
    }

    return false; // Failed to find or connect
  }

  void startAPMode(DisplayUpdateCallback updateDisplay) {
    Serial.println("Starting AP Mode...");
    WiFi.mode(WIFI_AP);
    
    // Configure static IP for AP
    IPAddress local_IP(10, 10, 10, 1);
    IPAddress gateway(10, 10, 10, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    
    WiFi.softAP(AP_SSID, AP_PASS);
    
    dnsServer.start(53, "*", WiFi.softAPIP()); // Captive portal DNS

    updateDisplay("SETUP_MODE"); // Signal to main.cpp to show AP info
    
    server.on("/", [this](){ handleRoot(); });
    server.on("/save", [this](){ handleSave(); });
    server.onNotFound([this](){ handleRoot(); }); // Redirect all to root (Captive Portal)
    server.begin();
    
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    
    while(true) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(10); // Yield
    }
  }

  const char* getApSsid() { return AP_SSID; }
  const char* getApPass() { return AP_PASS; }
  String getApIp() { return WiFi.softAPIP().toString(); }
};
