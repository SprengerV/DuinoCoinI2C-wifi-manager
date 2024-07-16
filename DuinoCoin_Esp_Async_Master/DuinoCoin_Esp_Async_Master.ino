/*
  DoinoCoin_Esp_Master.ino
  created 10 05 2021
  by Luiz H. Cassettari
  
  Modified by JK Rolling
*/

void wire_setup();
void wire_readAll();
boolean wire_exists(byte address);
void wire_sendJob(byte address, String lastblockhash, String newblockhash, int difficulty);
void Wire_sendln(byte address, String message);
void Wire_send(byte address, String message);
String wire_readLine(int address);
boolean wire_runEvery(unsigned long interval);

// uncomment for ESP01
//#define ESP01 true
#define REPORT_INTERVAL 60000
#define CHECK_MINING_KEY false
#define REPEATED_WIRE_SEND_COUNT 1      // 1 for AVR, 8 for RP2040

#if ESP8266
#include <ESP8266WiFi.h> // Include WiFi library
#include <ESP8266mDNS.h> // OTA libraries
#include <WiFiUdp.h>
#endif
#if ESP32
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

#include <ArduinoOTA.h>
#include <StreamString.h>
#include <WiFiManager.h>

#include <DuinoCoin_Wire.h>
#include <DuinoCoin_Pool.h>
#include <DuinoCoin_Clients.h>
#include <DuinoCoin_AsyncWebServer.h>

#define BLINK_SHARE_FOUND    1
#define BLINK_SETUP_COMPLETE 2
#define BLINK_CLIENT_CONNECT 3
#define BLINK_RESET_DEVICE   5

#if ESP8266
#define MINER "AVR I2C v4.1"
#define JOB "AVR"
#endif

#if ESP32
#define MINER "AVR I2C v4.1"
#define JOB "AVR"
#endif

const char* rigIdentifier = "AVR-I2C";  // Change this if you want a custom miner name
const char* mDNSRigIdentifier = "esp";  // Change this if you want a custom local mDNS miner name
char DUCO_USER[40];
char MINING_KEY[40];
bool shouldSave = false;
WiFiManagerParameter* ducouser;
WiFiManagerParameter* miningkey;

void handleSystemEvents(void) {
  ArduinoOTA.handle();
  yield();
}

void saveConfigCallback () {
  Serilal.println("Should save config.");
  shouldSave = true;
}

void SetupWifi() {
  // Serial.println("Connecting to: " + String(ssid));
  // WiFi.mode(WIFI_STA); // Setup ESP in client mode

  // if (ssid == "")
  //   WiFi.begin();
  // else
  //   WiFi.begin(ssid, password);

  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(500);
  //   Serial.print(".");
  // }

  WiFi.mode(WIFI_STA);

  #if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
  #else
    WiFi.setSleep(false);
  #endif

  if (SPIFFS.begin()) {
    Serial.println("Mounting file system...");

    if (SPIFFS.exists("/config.json")) {
      Serial.println("Reading config file...");

      File cf = SPIFFS.open("/config.json", "r")
      if (cf) {
        Serial.println("Opened config file!");

        // Allocate a buffer to store contents of config.json
        size_t sz = cf.size();
        stf::unique_ptr<char[]> buf(new char[sz]);

        // AurduinoJson 6
        DynamicJsonDocument jBuff(sz);
        DeserializationError err = deserializeJson(jBuff, buff.get());
        JsonObject json = jBuff.as<JsonObject>();
        Serial.println(json);

        if (!err) {
          Serial.println("\nParsed JSON!");
          strcpy(DUCO_USER, json["DUCO_USER"]);
          strcpy(MINING_KEY, json["MINING_KEY"]);
        }
        else {
          Serial.println("Failed to load JSON!");
        }
      }
    }
  }
  else {
    Serial.println("Failed to mount file system!");
  }
  // Local initialization of WiFIManager
  WiFiManager wm;

  // Uncomment to wipe settings
  wm.resetSettings();

  // Uncomment for debugging
  wm.setDebugOutput(true);
  wm.debugPlatformInfo();

  // Add custom parameters
  ducouser = new WiFiManager("User", "Duco User", DUCO_USER, 40);
  miningkey = new WiFiManager("Key", "Mining Key", MINING_KEY, 40, "type=\"password\"");
  wm.addParameter(ducouser);
  wm.addParameter(miningkey);

  wm.setSaveConfigCallback(saveConfigCallback);
  // Set custom IP for AP
  wm.setAPStaticIPConfig(IPAddress(10,0,0,1), IPAddress(10,0,0,1), IPAddress(255,255,255,0));
  // Set a timeout for connection
  wm.setTimeout(129);

  bool res = wm.autoConnect(mDNSRigIdentifier);

  strcpy(DUCO_USER, ducouser->getValue());
  strcpy(MINING_KEY, miningkey->getValue());

  // Save configuration
  if (shouldSave) {
    Serial.println("Saving configuration...");

    // ArduinoJaon 6
    jsonObject json;
    json["DUCO_USER"] = DUCO_USER;
    json["MINING_KEY"] = MINING_KEY;
    serializeJson(json, Serial);

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open config file for writing!")
    }

    serializeJson(json, configFile);
    configFile.close();
    Serial.println("Config file saved!");

    if (!res) {
      Serial.println("Failed to connect!");

      ESP.restart();
    }
    else {
      Serial.println("\nSuccessfully connected to WiFi!");
      Serial.println("Local IP address: " + WiFi.localIP().toString());
      Serial.println("Rig name: " + String(mDNSRigIdentifier));
    }

    int wait_passes = 0
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      delay(1000);
      Serial.println(".");
      if (++wait_passes > 30) {
        wm.resetSettings();
        ESP.restart();
        wait_passes = 0;
      }
    }
  }

  Serial.println("\nConnected to WiFi!");
  Serial.println("Local IP address: " + WiFi.localIP().toString());
}

void SetupOTA() {
  ArduinoOTA.onStart([]() { // Prepare OTA stuff
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

#if ESP8266
  char hostname[32];
  sprintf(hostname, "Miner-Async-%06x", ESP.getChipId());
  ArduinoOTA.setHostname(hostname);
#endif

#if ESP32
  char hostname[32];
  sprintf(hostname, "Miner32-Async-%06x", ESP.getEfuseMac());
  ArduinoOTA.setHostname(hostname);
#endif


  ArduinoOTA.begin();
}


void blink(uint8_t count, uint8_t pin = LED_BUILTIN) {
  uint8_t state = HIGH;
  for (int x = 0; x < (count << 1); ++x) {
    digitalWrite(pin, state ^= HIGH);
    delay(50);
  }
}

void RestartESP(String msg) {
  Serial.println(msg);
  Serial.println("Resetting ESP...");
  #ifndef ESP01
    blink(BLINK_RESET_DEVICE);
  #endif
  #if ESP8266
    ESP.reset();
  #endif
}

void setup() {
  #ifndef ESP01
    pinMode(LED_BUILTIN, OUTPUT);
  #endif
  Serial.begin(500000);
  Serial.print("\nDuino-Coin");
  Serial.println(MINER);

  wire_setup();
  SetupWifi();
  SetupOTA();

  server_setup();
  
  if (!MDNS.begin(mDNSRigIdentifier)) {
    Serial.println("mDNS unavailable");
  }
  MDNS.addService("http", "tcp", 80);
  Serial.println("Configured mDNS for dashboard on http://" 
                + String(mDNSRigIdentifier)
                + ".local (or http://"
                + WiFi.localIP().toString()
                + ")");
  Serial.println();
  
  UpdatePool();
  if (CHECK_MINING_KEY) UpdateMiningKey();
  else SetMiningKey(MINING_KEY);
  #ifndef ESP01
    blink(BLINK_SETUP_COMPLETE);
  #endif
}

void loop() {
  ArduinoOTA.handle();
  clients_loop();
  if (runEvery(REPORT_INTERVAL))
  {
    Serial.print("[ ]");
    Serial.println("FreeRam: " + String(ESP.getFreeHeap()) + " " + clients_string());
    ws_sendAll("FreeRam: " + String(ESP.getFreeHeap()) + " - " + clients_string());
    periodic_report(REPORT_INTERVAL);
  }
}

boolean runEvery(unsigned long interval)
{
  static unsigned long previousMillis = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    return true;
  }
  return false;
}
