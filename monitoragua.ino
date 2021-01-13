#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>

#define CS_PIN D8
#define trig D1
#define echo D2
#define interval 100
#define arraySize 600

const char* APssid = "NodeMCU";
const char* APpass = "";
const char* network_settings_file = "NETWORK.TXT";
const char* history_file = "HISTORY.TXT";
const char* water_tank_file = "WATER_TANK.TXT";
const int led_pin = 13;
const int AVAIL_NET_LIST_SIZE = 5;
const int quarterSample = arraySize / 4;
const int updateChartTime = 60;
const int historySize = 120;
const long connection_timeout = 15000;
int i = 0;
int j = 0;
float dists[arraySize] = {0};
float mediumDists[arraySize / 2] = {0};
float history[historySize] = {};
float media = 0;
unsigned long previousTime = 0;
char ssid[20];
char pass[20];
String content;
String available_networks[AVAIL_NET_LIST_SIZE][2] = {"0"};
String ssidSD = "";
String passSD = "";
String max_distance = "";

AsyncWebServer server(80);

//---------------------------------------------------------
// Server/Access point functions
//---------------------------------------------------------

void setupAccessPoint() {

  int n = WiFi.scanNetworks();
  if (n > 0) {
    if (n > AVAIL_NET_LIST_SIZE) {
      n = AVAIL_NET_LIST_SIZE;
    }
    for (int i = 0; i < n; i++) {
      available_networks[i][0] = WiFi.SSID(i);
      long signal_strength = WiFi.RSSI(i);
      if (signal_strength >= -50) {
        available_networks[i][1] = "Sinal excelente";
      }
      else if (signal_strength < -50 && signal_strength >= -60) {
        available_networks[i][1] = "Sinal bom";
      }
      else if (signal_strength < -60 && signal_strength >= -70) {
        available_networks[i][1] = "Sinal médio";
      }
      else if (signal_strength < -70) {
        available_networks[i][1] = "Sinal ruim";
      }
    }
  }

  readCredentialsFromSD();

  WiFi.disconnect();
  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);

  if (WiFi.SSID() != ssid) {
    Serial.println(F("Initialising Wifi..."));
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, pass);
    WiFi.persistent(true);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  }
  else {
    WiFi.begin(ssid, pass);
  }

  WiFi.softAP(APssid, APpass);
  IPAddress APIP = WiFi.softAPIP();
  Serial.print("IP do ponto de acesso: ");
  Serial.println(APIP);

  long startConnection = millis();
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    if (startConnection + connection_timeout > millis()) {
      Serial.println("Não conectado ao Wifi");
      break;
    }
  }

  if(WiFi.localIP()){
    Serial.print("Conectado ao WiFi: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP na rede WiFi: ");
    Serial.println(WiFi.localIP());
    digitalWrite(D4, HIGH);  
  }

}

void setupServer() {

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    content = readFile("INDEX.HTM");
    request->send_P(200, "text/html", content.c_str(), processor_index);
  });

  server.on("/goTo", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String filename;
    if (request->hasParam("file")) {
      filename = request->getParam("file")->value();
      if (filename == "SETTINGS.HTM") {
        content = readFile(filename);
        // readHistoryFromSD();
        request->send_P(200, "text/html", content.c_str(), processor_settings);
      }
      else {
        Serial.println("File not mapped in goTo request");
        Serial.print("Filename: ");
        Serial.println(filename);
        request->send(200, "text/html", content);
      }
    }

  });

  server.on("/saveSettings", HTTP_GET, [](AsyncWebServerRequest * request) {
    String netSSID = "" + request->getParam("ssid")->value() + "";
    String netPASS = "" + request->getParam("pass")->value() + "";
    String distance = "" + request->getParam("max_dist")->value() + "";
    if (SD.exists(network_settings_file)) {
      SD.remove(network_settings_file);
    }
    if (netPASS.length() != 0){
      File file = SD.open(network_settings_file, FILE_WRITE);
      if (file) {
        file.println(netSSID);
        file.print(netPASS);
        file.close();
        Serial.println("Credenciais atualizadas");
  //      request->send(200, "text/plain", "");
      }
      else {
        request->send(500, "text/plain", "Erro ao atualizar credenciais");
      }
    }
    if (SD.exists(water_tank_file)) {
      SD.remove(water_tank_file);
      Serial.println("Arquivo do tanque deletado");
    }
    File tank_file = SD.open(water_tank_file, FILE_WRITE);
    Serial.println("Arquivo do tanque criado");
    if (tank_file) {
      tank_file.print(distance);
      tank_file.close();
      Serial.println("Arquivo do tanque atualizado");
      max_distance = distance;
      Serial.println("Distância atualizada");
      request->send(200, "text/plain", "");
    }
    else {
      request->send(500, "text/plain", "Erro ao atualizar distância máxima");
    }

  });

  server.on("/updateChart", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Update chart request");
    if(media > 0.01 && media < max_distance.toFloat()){
      request->send(200, "text/plain", String((max_distance.toFloat()) - media));
    }
    else{
      request->send(500, "text/plain", "Media zero ou maior que a caixa d'água");
    }
      
  });

  server.begin();
}

//---------------------------------------------------------
// Server processors
//---------------------------------------------------------

String processor_index(const String& var) {
  String pageContent = "";
  if (var == "CURRENT_WATER_LEVEL") {
    pageContent += "<script type=\"text/javascript\">\n";
    pageContent += "  var i = 0;\n";
    pageContent += "  function setupChart() {\n";
    pageContent += "    var data = new google.visualization.arrayToDataTable([\n";
    pageContent += "      [\"Nível da água em cm\", \"Altura\"],\n";
    pageContent += "      [\"Nível da água em cm\", " + String(max_distance.toFloat() - media) + "],\n";
    pageContent += "    ]);\n";
    pageContent += "\n";
    pageContent += "    var view = new google.visualization.DataView(data);\n";
    pageContent += "    view.setColumns([\n";
    pageContent += "      0,\n";
    pageContent += "      1,\n";
    pageContent += "      {\n";
    pageContent += "        calc: \"stringify\",\n";
    pageContent += "        sourceColumn: 1,\n";
    pageContent += "        type: \"string\",\n";
    pageContent += "        role: \"annotation\",\n";
    pageContent += "      },\n";
    pageContent += "    ]);\n";
    pageContent += "\n";
    pageContent += "    var options = {\n";
    pageContent += "      width: 600,\n";
    pageContent += "      animation: {\n";
    pageContent += "        duration: 1000,\n";
    pageContent += "        easing: \"out\",\n";
    pageContent += "        startup: false,\n";
    pageContent += "      },\n";
    pageContent += "      vAxis: { minValue: 0, maxValue: 10 },\n";
    pageContent += "      legend: { position: \"none\" },\n";
    pageContent += "    };\n";
    pageContent += "\n";
    pageContent += "    var chart = new google.visualization.ColumnChart(\n";
    pageContent += "      document.getElementById(\"water_tank_chart\")\n";
    pageContent += "    );\n";
    pageContent += "\n";
    pageContent += "    chart.draw(view, options);\n";
    pageContent += "\n";
//    pageContent += "    var historyChart = new google.visualization.LineChart(\n";
//    pageContent += "      document.getElementById(\"water_level_history\")\n";
//    pageContent += "    );\n";
//    pageContent += "\n";
//    pageContent += "    var historyData = new google.visualization.arrayToDataTable(historyArray)\n";
//    pageContent += "\n";
//    pageContent += "    var historyOptions = {\n";
//    pageContent += "      title: \"Nível da água nos últimos " + String(historySize) + " minutos\",\n";
//    pageContent += "      width: 600,\n";
//    pageContent += "      vAxis: { minValue: 0, maxValue: 10 },\n";
//    pageContent += "      legend: { position: \"none\" },\n";
//    pageContent += "    };\n";
//    pageContent += "\n";
//    pageContent += "    historyChart.draw(historyData, historyOptions);\n";
    pageContent += "\n";
    pageContent += "    window.setInterval(function () {\n";
    pageContent += "      $.get(\"\" + window.location.origin + \"/updateChart\", {}, function (\n";
    pageContent += "        requestData,\n";
    pageContent += "        status,\n";
    pageContent += "        xhr\n";
    pageContent += "      ) {\n";
    pageContent += "        data.setValue(0, 1, parseFloat(requestData));\n";
    pageContent += "        view.setColumns([\n";
    pageContent += "          0,\n";
    pageContent += "          1,\n";
    pageContent += "          {\n";
    pageContent += "            calc: \"stringify\",\n";
    pageContent += "            sourceColumn: 1,\n";
    pageContent += "            type: \"string\",\n";
    pageContent += "            role: \"annotation\",\n";
    pageContent += "          },\n";
    pageContent += "        ]);\n";
    pageContent += "        chart.draw(view, options);\n";
    pageContent += "\n";
    pageContent += "        i++;\n";
    pageContent += "        if (historyArray.length < " + String(historySize) + ") {\n";
    pageContent += "          historyArray.push([new Date().toLocaleTimeString(), parseFloat(requestData)]);\n";
    pageContent += "        } else {\n";
    pageContent += "          historyArray.splice(1, 1);\n";
    pageContent += "          historyArray.push([new Date().toLocaleTimeString(), parseFloat(requestData)]);\n";
    pageContent += "        }\n";
    pageContent += "\n";
//    pageContent += "        historyData = new google.visualization.arrayToDataTable(historyArray)\n";
//    pageContent += "        historyOptions = {\n";
//    pageContent += "          title: \"Nível da água nos últimos " + String(historySize) + " minutos\",\n";
//    pageContent += "          width: 600,\n";
//    pageContent += "          vAxis: { minValue: 0, maxValue: 10 },\n";
//    pageContent += "          legend: { position: \"none\" },\n";
//    pageContent += "        };\n";
//    pageContent += "        historyChart.draw(historyData, historyOptions);\n";
//    pageContent += "\n";
    pageContent += "      });\n";
    pageContent += "\n";
    pageContent += "\n";
    pageContent += "    }, " + String(updateChartTime) + " * 1000);\n";
    pageContent += "    \n";
    pageContent += "  }\n";
    pageContent += "</script>\n";
    pageContent += "\n";
  }
  return pageContent;
}

String processor_settings(const String& var) {
  if (var == "AVAILABLE_WIFI_NETWORKS") {
    String wifi_nets = "";
    for (int i = 0; i < AVAIL_NET_LIST_SIZE; i++) {
      if (available_networks[i][0].length() != 0) {
        wifi_nets += "<option value=\"" + available_networks[i][0] + "\">" + available_networks[i][0] + " - " + available_networks[i][1] + "</option>";
      }
    }
    return wifi_nets;
  }
  else if (var == "CONNECTED_WIFI") {
    return String(WiFi.SSID());
  }
  else if (var == "WIFI_IP") {
    return WiFi.localIP().toString();
  }
  else if (var == "MAX_DIST") {
    return "value=\"" + max_distance + "\"";
  }
}

//---------------------------------------------------------
// SD card functions
//---------------------------------------------------------

void readCredentialsFromSD() {
  File file = SD.open(network_settings_file);
  bool saveToPass = false;
  int i = 0;
  int j = 0;
  if (file) {
    while (file.available()) {
      char c = file.read();
      if (c == '\n') {
        saveToPass = true;
        ssid[j] = '\0';
      }
      if (saveToPass && c != '\r' && c != '\n') {
        pass[i] = c;
        i++;
      }
      else if (!saveToPass && c != '\r' && c != '\n') {
        ssid[j] = c;
        j++;
      }
      delay(100);
    }
    pass[i] = '\0';
    file.close();
  }
  Serial.println(ssid);
  Serial.println(pass);

}

void readHistoryFromSD() {
  if (SD.exists(history_file)) {
    File file = SD.open(history_file, FILE_WRITE);
    if (file) {
      int l = 0;
      String val = "";
      while (file.available()) {
        char c = file.read();
        if (c != '\n') {
          val += c;
        }
        else {
          history[l] = val.toFloat();
        }
      }
      file.close();
    }
  }
}

void setupSD() {
  Serial.print("Initializing SD card...");

  if (!SD.begin(D8)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("initialization done.");
}

void updateSDHistory() {
  if (SD.exists(history_file)) {
    SD.remove(history_file);
  }
  File file = SD.open(history_file, FILE_WRITE);
  if (file) {
    for (int k = 0; k < historySize; k++) {
      file.println(history[k]);
    }
    file.close();

  }
  else {
    // error creating file
  }
}

//---------------------------------------------------------
// Ultrassonic sensor functions
//---------------------------------------------------------

void trigPulse() {
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
}

//---------------------------------------------------------
// Helper functions
//---------------------------------------------------------

void calcMedia() {
  float soma = 0;
  for (int j = 0; j < (arraySize / 2); j++) {
    soma += mediumDists[j];
  }
  media = soma / (arraySize / 2);
  if (j == historySize) {
    memcpy(history, &history[1], sizeof(history) - sizeof(int));
    history[historySize - 1] = media;
  }
  else {
    history[j] = media;
    j++;
  }
  updateSDHistory();
  //  Serial.println(history);
}

String readFile(String filename) {
  File file = SD.open(filename);
  content = "";
  if (file) {
    while (file.available()) {
      char c = file.read();
      content += c;
    }
    file.close();
  }
  return content;
}

void removeQuartis() {
  int k = 0;
  for (int j = quarterSample; j < quarterSample * 3; j++) {
    mediumDists[k] = dists[j];
    k++;
  }
}

void sortArray() {
  int aux;
  for (int j = 0; j < arraySize; j++) {
    for (int k = j + 1; k < arraySize; k++) {
      if (dists[j] > dists [k]) {
        aux = dists[k];
        dists[k] = dists[j];
        dists[j] = aux;
      }
    }
  }
}

//---------------------------------------------------------
// Systemfunctions
//---------------------------------------------------------

//---------------------------------------------------------
// Arduino functions
//---------------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  digitalWrite(trig, LOW);
  pinMode(D4, OUTPUT);
  
  setupSD();

  setupAccessPoint();

  if (SD.exists(water_tank_file)) {
    max_distance = readFile(water_tank_file);
  }

  Serial.print("Fundo da caixa d'água: ");
  Serial.println(max_distance);

  setupServer();

}

void loop() {

  long int currentTime = millis();

  if ( (currentTime - previousTime) > interval) {
    trigPulse();

    float pulse = pulseIn(echo, HIGH, 100000);
    float dist_cm = pulse / 58.82;
    if (dist_cm != 0) {
      dists[i] = dist_cm;
      i++;
    }
    if (i == arraySize) {
      sortArray();
      removeQuartis();
      calcMedia();
      i = 0;
    }

    previousTime = millis();
  }
}
