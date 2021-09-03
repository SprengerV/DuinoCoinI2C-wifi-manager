/*
  DuinoCoin_Pool.ino
  created 31 07 2021
  by Luiz H. Cassettari
  
  Modified by JK-Rolling
*/

#define GETPOOL_EN true

#if ESP8266
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#endif
#if ESP32
#include <HTTPClient.h>
#endif

#include <ArduinoJson.h>

//const char * urlPool = "http://51.15.127.80:4242/getPool";
const char * urlPool = "https://server.duinocoin.com/getPool";

void UpdateHostPort(String input)
{
  // {"ip":"server.duinocoin.com","port":2812,"name":"Main server"}
  DynamicJsonDocument doc(256);
  deserializeJson(doc, input);

  const char* name = doc["name"];
  const char* ip = doc["ip"];
  int port = doc["port"];

  Serial.println("[ ]Update " + String(name) + " " + String(ip) + " " + String(port));
  SetHostPort(String(ip), port);
}

void UpdatePool()
{
  String input;
  if (GETPOOL_EN)
  {
    input = httpGetString(urlPool);
    if (input == "") return;
  }
  else
  {
    // AVR_Miner 2.64 uses this address
    const char* hardcode_ip = "51.158.182.90";
    int hardcode_port = 6000;
    Serial.println("[ ]Update " + String(hardcode_ip) + " " + String(hardcode_port));
    SetHostPort(String(hardcode_ip), hardcode_port);
    return;
  }
  
  UpdateHostPort(input);
}

String httpGetString(String URL)
{
  String payload = "";
  WiFiClientSecure client;
  client.setInsecure()
  HTTPClient http;
  if (http.begin(client, URL))
  {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
      payload = http.getString();
    }
    else
    {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
  return payload;
}
