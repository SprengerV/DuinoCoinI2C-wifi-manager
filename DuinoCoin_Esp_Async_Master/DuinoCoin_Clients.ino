/*
  DuinoCoin_Clients.ino
  created 10 05 2021
  by Luiz H. Cassettari
  
  Modified by JK Rolling
*/

#if ESP8266
#include <ESP8266WiFi.h> // Include WiFi library
#include <ESP8266mDNS.h> // OTA libraries
#include <WiFiUdp.h>
#endif
#if ESP32
#include <WiFi.h>
#include <WiFiClient.h>
#endif

#define CLIENTS 10

#define CLIENT_CONNECT_EVERY 30000
#define CLIENT_TIMEOUT_CONNECTION 30000
#define CLIENT_TIMEOUT_REQUEST 100

#define END_TOKEN  '\n'
#define SEP_TOKEN  ','
#define BAD "Rejected"
#define GOOD "Accepted"
#define BLOCK "Block"

#define HASHRATE_FORCE true
#define HASHRATE_SPEED 195.0

String host = "51.158.113.59";
int port = 6042;

void SetHostPort(String h, int p)
{
  host = h;
  port = p;
}

String getHostPort()
{
    return host + String(":") + String(port);
}

String SetHost(String h)
{
  host = h;
  return host;
}

int SetPort(int p)
{
  port = p;
  return port;
}

// State Machine
enum Duino_State
{
  DUINO_STATE_NONE,

  DUINO_STATE_VERSION_WAIT,

  DUINO_STATE_MOTD_REQUEST,
  DUINO_STATE_MOTD_WAIT,

  DUINO_STATE_JOB_REQUEST,
  DUINO_STATE_JOB_WAIT,

  DUINO_STATE_JOB_DONE_SEND,
  DUINO_STATE_JOB_DONE_WAIT,
};

WiFiClient clients[CLIENTS];
byte clientsWaitJob[CLIENTS];
int clientsShares[CLIENTS];
String clientsBuffer[CLIENTS];
unsigned long clientsTimes[CLIENTS];
unsigned long clientsTimeOut[CLIENTS];
unsigned long clientsPingTime[CLIENTS];
unsigned long clientsGetTime[CLIENTS];
unsigned long clientsElapsedTime[CLIENTS];
float clientsHashRate[CLIENTS];
unsigned int clientsDiff[CLIENTS];
byte clientsBadJob[CLIENTS];
byte clientsForceReconnect[CLIENTS];
String poolMOTD;
unsigned int share_count = 0;
unsigned int accepted_count = 0;
unsigned int block_count = 0;
unsigned int last_share_count = 0;
unsigned long startTime = millis();

unsigned long clientsConnectTime = 0;
bool clientsMOTD = true;

bool clients_connected(byte i)
{
  return clients[i].connected();
}

bool clients_connect(byte i)
{
  if (clients[i].connected())
  {
    return true;
  }

  wire_readLine(i);

  Serial.print("[" + String(i) + "]");
  Serial.println("Connecting to Duino-Coin server... " + String(host) + " " + String(port));

  ws_sendAll("[" + String(i) + "]" + "Connecting to Duino-Coin server... " + String(host) + " " + String(port));

  clients[i].setTimeout(30000);
  clients[i].flush();
  if (!clients[i].connect(host.c_str(), port))
  {
    Serial.print("[" + String(i) + "]");
    Serial.println("Connection failed. Wait 5s before retry..");
    delay(5000);
    UpdatePool();
    return false;
  }
  clients[i].setTimeout(1);

  clientsShares[i] = 0;
  clientsBadJob[i] = 0;
  clientsTimes[i] = millis();
  clientsBuffer[i] = "";
  clients_state(i, DUINO_STATE_VERSION_WAIT);
  clientsForceReconnect[i] = false;
  return true;
}

void clients_state(byte i, byte state)
{
  clientsWaitJob[i] = state;
  clientsTimeOut[i] = millis();
}

bool clients_stop(byte i)
{
  clients_state(i, DUINO_STATE_NONE);
  clients[i].stop();
  return true;
}

void force_clients_reconnect()
{
    for (byte j = 0; j < CLIENTS; j++)
    {
        clientsForceReconnect[j] = true;
    }
    clientsMOTD = true;
}

int client_i = 0;

void clients_loop()
{
  if (clients_runEvery(clientsConnectTime))
  {
    clientsConnectTime = CLIENT_CONNECT_EVERY;
    for (client_i = 0; client_i < CLIENTS; client_i++)
    {
      int i = client_i;
      if (wire_exists(i + 1) && !clients_connect(i))
      {
        break;
      }
    }
  }

  for (client_i = 0; client_i < CLIENTS; client_i++)
  {
    int i = client_i;
    if (wire_exists(i + 1) && clients_connected(i))
    {
      for(int j = 0; j < 3; j++ )
      switch (clientsWaitJob[i])
      {
        case DUINO_STATE_VERSION_WAIT:
          clients_waitRequestVersion(i);
          break;
        case DUINO_STATE_JOB_REQUEST:
          clients_requestJob(i);
          break;
        case DUINO_STATE_JOB_WAIT:
          clients_waitRequestJob(i);
          break;
        case DUINO_STATE_JOB_DONE_SEND:
          clients_sendJobDone(i);
          break;
        case DUINO_STATE_JOB_DONE_WAIT:
          clients_waitFeedbackJobDone(i);
          break;
        case DUINO_STATE_MOTD_REQUEST:
          clients_requestMOTD(i);
          break;
        case DUINO_STATE_MOTD_WAIT:
          clients_waitMOTD(i);
          break;
      }

      if (millis() - clientsTimeOut[i] > CLIENT_TIMEOUT_CONNECTION)
      {
        Serial.println("[" + String(i) + "]" + " --------------- TIMEOUT ------------- ");
        ws_sendAll("[" + String(i) + "]" + " --------------- TIMEOUT ------------- ");
        clients_stop(i);
      }

    }
  }
}

void clients_waitMOTD(byte i)
{
  if (clients[i].available()) {
    String buffer = clients[i].readString();
    poolMOTD = buffer;
    Serial.println("[" + String(i) + "]" + buffer);
    clientsWaitJob[i] = DUINO_STATE_JOB_REQUEST;
    clientsTimeOut[i] = millis();
  }
}

void clients_requestMOTD(byte i)
{
  Serial.print("[" + String(i) + "]");
  Serial.println("MOTD Request: " + String(ducouser));
  clients[i].print("MOTD");
  clientsWaitJob[i] = DUINO_STATE_MOTD_WAIT;
  clientsTimeOut[i] = millis();
}

String printMOTD()
{
    return poolMOTD;
}

void clients_waitRequestVersion(byte i)
{
  if (clients[i].available()) {
    String buffer = clients[i].readStringUntil(END_TOKEN);
    Serial.println("[" + String(i) + "]" + buffer);
    clients_state(i, DUINO_STATE_JOB_REQUEST);
    if (clientsMOTD) clients_state(i, DUINO_STATE_MOTD_REQUEST);
    clientsMOTD = false;
  }
}

void clients_requestJob(byte i)
{
  Serial.print("[" + String(i) + "]");
  Serial.println("Job Request: " + String(ducouser));
  clients[i].print("JOB," + String(ducouser) + "," + JOB);
  clientsGetTime[i] = millis();
  clients_state(i, DUINO_STATE_JOB_WAIT);
}

void clients_waitRequestJob(byte i)
{
  String clientBuffer = clients_readData(i);
  if (clientBuffer.length() > 0)
  {
    unsigned long getJobTime = millis() - clientsGetTime[i];
    Serial.print("[" + String(i) + "] ");
    Serial.print("Job ");
    Serial.print(clientBuffer);
    Serial.print("  ping " + String(getJobTime) + " ms");
    //ws_sendAll("[" + String(i) + "] Job "+ clientBuffer + "  ping " + String(getJobTime) + " ms");

    // Not a Valid Job -> Request Again
    if (clientBuffer.indexOf(',') == -1)
    {
      clients_stop(i);
      return;
    }

    String hash = getValue(clientBuffer, SEP_TOKEN, 0);
    String job = getValue(clientBuffer, SEP_TOKEN, 1);
    unsigned int diff = getValue(clientBuffer, SEP_TOKEN, 2).toInt();
    clientsDiff[i] = diff;

    clientsElapsedTime[i] = millis();
    wire_sendJob(i + 1, hash, job, diff);
    clients_state(i, DUINO_STATE_JOB_DONE_SEND);
  }
}

void clients_sendJobDone(byte i)
{
  String responseJob = wire_readLine(i + 1);
  if (responseJob.length() > 0)
  {
    clientsElapsedTime[i] = (millis() - clientsElapsedTime[i]);
    //ws_sendAll("[" + String(i) + "]" + responseJob);

    StreamString response;
    response.print(responseJob);

    int job = response.readStringUntil(',').toInt();
    int time = response.readStringUntil(',').toInt();
    String id = response.readStringUntil('\n');
    float HashRate = job / (time * .000001f);
    clientsHashRate[i] = HashRate;

    if (HASHRATE_FORCE) // Force HashRate to slow down
    {
      Serial.print("[" + String(i) + "]");
      Serial.println("Slow down HashRate: " + String(HashRate, 2));
      HashRate = HASHRATE_SPEED + random(-50, 50) / 100.0;
    }

    if (id.length() > 0) id = "," + id;

    String identifier = String(rigIdentifier) + "-" + String(i);

    clients[i].print(String(job) + "," + String(HashRate, 2) + "," + MINER + "," + String(identifier) + id);

    Serial.print("[" + String(i) + "] ");
    Serial.println(String(job) + "," + String(HashRate, 2) + "," + MINER + "," + String(identifier) + id);
    //Serial.println("Job Done: (" + String(job) + ")" + " Hashrate: " + String(HashRate));
    clientsPingTime[i] = millis();
    clients_state(i, DUINO_STATE_JOB_DONE_WAIT);
  }
}

void clients_waitFeedbackJobDone(byte i)
{
  String clientBuffer = clients_readData(i);
  if (clientBuffer.length() > 0)
  {
    unsigned long time = (millis() - clientsTimes[i]);
    unsigned long pingTime = (millis() - clientsPingTime[i]);
    clientsShares[i]++;
    int Shares = clientsShares[i];
    
    String verdict = "";
    share_count++;
    if (clientBuffer == "GOOD") {
      accepted_count++;
      verdict = GOOD;
    }
    else if (clientBuffer == "BLOCK") {
      block_count++;
      verdict = BLOCK;
    }
    else {
      verdict = BAD;
    }

    Serial.println("[" + String(i) + "] " 
                + verdict + "  ⛏ " + String(accepted_count) + "/" + String(share_count) 
                + " (" + String((float)accepted_count/(float)share_count*100, 2) + "%)  "
                + String(clientsElapsedTime[i]/1000.0, 2) + "s  "
                + String(clientsHashRate[i], 2) + " H/s  ⚙ "
                + "diff " + String(clientsDiff[i])
                + "  ping " + String(pingTime) + "ms");
    ws_sendAll("[" + String(i) + "] " 
                + verdict + "  ⛏ " + String(accepted_count) + "/" + String(share_count) 
                + " (" + String((float)accepted_count/(float)share_count*100, 2) + "%)  "
                + String(clientsElapsedTime[i]/1000.0, 2) + "s  "
                + String(clientsHashRate[i], 2) + " H/s  ⚙ "
                + "diff " + String(clientsDiff[i])
                + "  ping " + String(pingTime) + "ms");

    clients_state(i, DUINO_STATE_JOB_REQUEST);

    if (clientBuffer == "BAD")
    {
      if (clientsBadJob[i]++ > 3)
      {
        Serial.print("[" + String(i) + "]");
        Serial.println("BAD BAD BAD BAD");
        ws_sendAll("[" + String(i) + "]" + "BAD BAD BAD BAD");
        clients_stop(i);
      }
    }
    else
    {
      clientsBadJob[i] = 0;
    }

    if (clientsForceReconnect[i])
    {
        Serial.print("[" + String(i) + "]");
        Serial.println("Forced disconnect");
        ws_sendAll("[" + String(i) + "]" + "Forced disconnect");
        clients_stop(i);
    }
  }
}

String clients_string()
{
  int i = 0;
  String str;
  str += "I2C ";
  str += "[";
  str += " ";
  for (i = 0; i < CLIENTS; i++)
  {
    if (wire_exists(i + 1))
    {
      str += (i);
      str += clients_connected(i) ? "." : "";
      str += " ";
    }
  }
  str += "]";
  return str;
}

String clients_show2()
{
  int i = 0;
  String str;
  for (i = 0; i < CLIENTS; i++)
  {
    str += String(i);
    str += " ";
  }
  str += "\n";
  for (i = 0; i < CLIENTS; i++)
  {
    str += clients[i].connected();
    str += " ";
  }
  str += "\n";

  for (i = 0; i < CLIENTS; i++)
  {
    str += wire_exists(i + 1);
    str += " ";
  }
  str += "\n";
  return str;
}

String clients_show()
{
  int i = 0;
  String str;
  str += client_i;
  str += "\n";
  for (i = 0; i < CLIENTS; i++)
  {
    str += "[" + String(i) + "]";
    str += " ";
    str += clients[i].connected();
    str += " ";

    if (wire_exists(i + 1))
    {
      str += "Connected ";
    }
    else
    {
      str += "Not ";
    }
    str += clientsWaitJob[i];
    str += " ";
    str += "B";
    str += clientsBadJob[i];

    str += "\n";
  }
  ws_sendAll(str);
  return str;
}

String timeString(unsigned long t) {
  String str = "";
  t /= 1000;
  int s = t % 60;
  int m = (t / 60) % 60;
  int h = (t / 3600);
  str += h;
  str += ":";
  if (m < 10) str += "0";
  str += m;
  str += ":";
  if (s < 10) str += "0";
  str += s;
  return str;
}


String clients_readData(byte i)
{
  String str = "";

  while (clients[i].available()) {
    char c = clients[i].read();
    if (c == END_TOKEN)
    {
      str = clientsBuffer[i];
      clientsBuffer[i] = "";
    }
    else
      clientsBuffer[i] += c;
  }

  if (clientsBuffer[i] != "")
  {
    if (millis() - clientsTimeOut[i] > CLIENT_TIMEOUT_REQUEST)
    {
      str = clientsBuffer[i];
      clientsBuffer[i] = "";
    }
  }

  return str;
}

// https://stackoverflow.com/questions/9072320/split-string-into-string-array
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

String waitForClientData(int i) {
  unsigned long previousMillis = millis();
  unsigned long interval = 10000;

  String buffer;
  while (clients[i].connected()) {
    if (clients[i].available()) {
      Serial.println(clients[i].available());
      buffer = clients[i].readStringUntil(END_TOKEN);
      if (buffer.length() == 1 && buffer[0] == END_TOKEN)
        buffer = "???\n"; // NOTE: Should never happen...
      if (buffer.length() > 0)
        break;
    }
    if (millis() - previousMillis >= interval) break;
    handleSystemEvents();
  }
  return buffer;
}

boolean clients_runEvery(unsigned long interval)
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

void periodic_report(unsigned long interval)
{
    unsigned long uptime = (millis() - startTime);
    unsigned int report_shares = share_count - last_share_count;
    Serial.println("Periodic mining report:");
    Serial.println(" ‖ During the last " + String(interval/1000.0, 1) + " seconds");
    Serial.println(" ‖ You've mined " + String(report_shares)+ " shares (" + String((float)report_shares/(interval/1000.0), 2) + " shares/s)");
    Serial.println(" ‖ Block(s) found: " + String(block_count));
    Serial.println(" ‖ With the hashrate of " + String(clientsHashRate[CLIENTS-1], 2) + " H/s  ");
    Serial.println(" ‖ In this time period, you've solved " + String(report_shares) + " hashes");
    Serial.println(" ‖ Total miner uptime: " + timeString(uptime));
    
    ws_sendAll("Periodic mining report:");
    ws_sendAll(" ‖ During the last " + String(interval/1000.0, 1) + " seconds");
    ws_sendAll(" ‖ You've mined " + String(report_shares)+ " shares (" + String((float)report_shares/(interval/1000.0), 2) + " shares/s)");
    ws_sendAll(" ‖ Block(s) found: " + String(block_count));
    ws_sendAll(" ‖ With the hashrate of " + String(clientsHashRate[CLIENTS-1], 2) + " H/s  ");
    ws_sendAll(" ‖ In this time period, you've solved " + String(report_shares) + " hashes");
    ws_sendAll(" ‖ Total miner uptime: " + timeString(uptime));
    last_share_count = share_count;
}
