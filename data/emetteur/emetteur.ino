#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <LittleFS.h>
#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Constantes et définitions matérielles
// ─────────────────────────────────────────────────────────────────────────────

#define DHT_1_PIN 4
#define DHT_2_PIN 15
#define DHT_TYPE DHT22
#define MQ2_PIN 34
#define RELAY_PIN 17
#define LED_V_PIN 2
#define LED_R_PIN 33
#define BUZZER_PIN 13
#define FLAME_PIN 32

// Pins LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 27
#define LORA_RST 12
#define LORA_DIO0 26

// WiFi Access Point
const char *ap_ssid = "AlarmeGaz-ESP32";
const char *ap_password = "12345678";
IPAddress ap_ip(192, 168, 4, 1);

// WiFi Station (pour NTP)
const char *sta_ssid = "VOTRE_SSID";
const char *sta_password = "VOTRE_PASSWORD";

// Seuils par défaut
float TEMP_SEUIL_DEFAULT = 35.0;
float HUM_SEUIL_DEFAULT = 80.0;
int SMOKE_SEUIL_DEFAULT = 1500;
int FLAME_SEUIL_DEFAULT = 2000;

// Seuils actuels (chargés depuis settings.json ou valeurs par défaut)
float tempSeuil = TEMP_SEUIL_DEFAULT;
float humSeuil = HUM_SEUIL_DEFAULT;
int smokeSeuil = SMOKE_SEUIL_DEFAULT;
int seuilFlame = FLAME_SEUIL_DEFAULT;

// controlle automatique de la ventillation
bool manualVentil = false;
bool overrideActive = false;

unsigned long timeReference = 0;
bool timeReferenceSet = false;

// ─────────────────────────────────────────────────────────────────────────────
//  Objets globaux
// ─────────────────────────────────────────────────────────────────────────────

DHT dht1(DHT_1_PIN, DHT_TYPE);
DHT dht2(DHT_2_PIN, DHT_TYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Dernières valeurs mesurées
float lastTemp = 0.0;
float lastHum = 0.0;
int lastSmoke = 0;
int lastFlame = 0;
bool lastAlert = false;

// Historique des alertes
DynamicJsonDocument historyDoc(2048);
JsonArray history;

JsonObject newAlerte;
bool hasNewAlerte = false;

// Détection nouvelle alerte
bool prevAlert = false;

bool receiveCommand = false;

// ─────────────────────────────────────────────
// Alerte
// ─────────────────────────────────────────────
void updateActuators(bool alert) {
  digitalWrite(LED_R_PIN, alert ? HIGH : LOW);
  digitalWrite(LED_V_PIN, alert ? LOW : HIGH);
  digitalWrite(BUZZER_PIN, alert ? HIGH : LOW);
}


// ─────────────────────────────────────────────────────────────────────────────
//  Fonctions de gestion des seuils persistants
// ─────────────────────────────────────────────────────────────────────────────

void loadSettings() {
  if (!LittleFS.exists("/db/settings.json"))
    return;

  File file = LittleFS.open("/db/settings.json", "r");
  if (!file)
    return;

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Erreur lecture settings.json : " + String(error.c_str()));
    return;
  }

  tempSeuil = doc["sT"] | TEMP_SEUIL_DEFAULT;
  humSeuil = doc["sH"] | HUM_SEUIL_DEFAULT;
  smokeSeuil = doc["sSm"] | SMOKE_SEUIL_DEFAULT;
  seuilFlame = doc["sF"] | FLAME_SEUIL_DEFAULT;
}

void saveSettings() {
  StaticJsonDocument<256> doc;
  doc["sT"] = tempSeuil;
  doc["sH"] = humSeuil;
  doc["sSm"] = smokeSeuil;
  doc["sF"] = seuilFlame;

  File file = LittleFS.open("/db/settings.json", "w");
  if (!file) {
    Serial.println("Erreur ouverture settings.json en écriture");
    return;
  }

  serializeJson(doc, file);
  file.close();
  Serial.println("Seuils sauvegardés dans settings.json");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fonction partagée pour gérer les commandes (depuis WS ou LoRa)
// ─────────────────────────────────────────────────────────────────────────────

void handleCommand(const JsonDocument &doc) {
  if (doc["com"] == "upd_seuils") {
    JsonVariantConst v = doc["seuils"];
    if (!v.isNull() && v.is<JsonObjectConst>()) {
      JsonObjectConst seuils = v.as<JsonObjectConst>();

      tempSeuil = seuils["sT"] | tempSeuil;
      humSeuil = seuils["sH"] | humSeuil;
      smokeSeuil = seuils["sSm"] | smokeSeuil;
      seuilFlame = seuils["sF"] | seuilFlame;

      saveSettings();
      Serial.println("Nouveaux seuils reçus et enregistrés");
    }
  }

  if (doc["com"] == "mnl_vent") {
    overrideActive = doc["override"] | false;
    manualVentil = doc["state"] | false;
  }
  receiveCommand = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Gestion WebSocket
// ─────────────────────────────────────────────────────────────────────────────

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    Serial.printf("Client connecté #%u\n", client->id());
    uint32_t nombreClients = ws.count();
    Serial.printf("Nombre de clients connectés : %u\n", nombreClients);
    if (!timeReferenceSet) {
      timeReference = millis();
      timeReferenceSet = true;
      Serial.println("Référence temporelle définie (premier client connecté)");
    }

    String jsonStr = buildStatusJson(
      true,
      lastTemp, lastHum, lastSmoke, lastFlame, lastAlert,
      (lastTemp >= tempSeuil),
      (lastHum >= humSeuil),
      (lastSmoke >= smokeSeuil),
      (lastFlame <= seuilFlame));

    client->text(jsonStr);
  } else if (type == WS_EVT_DATA) {
    // Gestion des commandes (upd_seuils, mnl_vent)
    String message;
    for (size_t i = 0; i < len; i++)
      message += (char)data[i];

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      Serial.println("Erreur JSON WebSocket : " + String(error.c_str()));
      return;
    }

    handleCommand(doc);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fonction centralisée pour construire le JSON d'état
// ─────────────────────────────────────────────────────────────────────────────

String buildStatusJson(bool withAllHistory, float temp, float hum, int smoke, int flame, bool isAlert,
                       bool tempAlert, bool humAlert, bool smokeAlert, bool flameAlert) {

  // Taille dynamique : plus grande si on inclut l'historique complet
  size_t capacity = JSON_OBJECT_SIZE(10) + JSON_ARRAY_SIZE(4) + JSON_OBJECT_SIZE(4);
  if (withAllHistory) {
    capacity += historyDoc.memoryUsage();  // Estimation sûre de l'historique
  }
  capacity += 128;

  DynamicJsonDocument doc(capacity);

  doc["isA"] = isAlert;
  doc["isRC"] = receiveCommand;

  JsonArray types = doc.createNestedArray("alertType");
  if (tempAlert)
    types.add("T");
  if (humAlert)
    types.add("H");
  if (smokeAlert)
    types.add("S");
  if (flameAlert)
    types.add("F");

  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["sm"] = smoke;
  doc["fl"] = flame;
  doc["mnlOvrr"] = overrideActive;
  doc["mnlVent"] = manualVentil;

  JsonObject seuils = doc.createNestedObject("seuils");
  seuils["sT"] = tempSeuil;
  seuils["sH"] = humSeuil;
  seuils["sSm"] = smokeSeuil;
  seuils["sF"] = seuilFlame;

  // Ajout conditionnel de l'historique complet
  if (withAllHistory) {
    doc["his"] = history;
  }

  // Ajout de la nouvelle alerte (toujours présent, peut être null)
  if (hasNewAlerte && !newAlerte.isNull()) {
    doc["nAlrt"] = newAlerte;
    hasNewAlerte = false;
    newAlerte = JsonObject();
  } else {
    doc["nAlrt"] = nullptr;
  }

  String jsonStr;
  serializeJson(doc, jsonStr);
  return jsonStr;
}

String getFormatedTime() {
  if (!timeReferenceSet) {
    return "NC";
  }

  unsigned long elapsedMillis = millis() - timeReference;
  unsigned long elapsedSeconds = elapsedMillis / 1000;
  unsigned long hours = elapsedSeconds / 3600;
  unsigned long minutes = (elapsedSeconds % 3600) / 60;
  unsigned long seconds = elapsedSeconds % 60;

  char timeStr[20];
  if (hours > 0) {
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  } else {
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu", minutes, seconds);
  }
  return String(timeStr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialisation des sorties
  pinMode(LED_V_PIN, OUTPUT);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(LED_V_PIN, HIGH);

  dht1.begin();
  dht2.begin();

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.print("Demarrage...");

  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("Erreur LittleFS → formatage");
    lcd.clear();
    lcd.print("Formatage FS...");
    LittleFS.format();
    ESP.restart();
  }

  loadSettings();  // Charger les seuils persistants

  // Chargement historique des alertes
  if (LittleFS.exists("/db/history.json")) {
    File file = LittleFS.open("/db/history.json", "r");
    if (file) {
      DeserializationError error = deserializeJson(historyDoc, file);
      if (!error) {
        history = historyDoc.as<JsonArray>();
      } else {
        Serial.println("Erreur lecture history.json");
      }
      file.close();
    }
  } else {
    history = historyDoc.to<JsonArray>();
  }

  // WiFi AP + Station (pour NTP)
  WiFi.mode(WIFI_AP_STA);

  // Connexion WiFi Station (NTP)
  WiFi.begin(sta_ssid, sta_password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // Démarrage AP
  WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(ap_ssid, ap_password)) {
    Serial.println("Point d'accès démarré");
    Serial.printf("SSID: %s  -  Mot de passe: %s  -  IP: %s\n",
                  ap_ssid, ap_password, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("Échec démarrage AP");
  }

  delay(1500);

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("Erreur démarrage LoRa !");
    lcd.clear();
    lcd.print("Erreur LoRa");
    while (true)
      delay(1000);
  }

  LoRa.setSyncWord(0xF3);
  // LoRa.setSpreadingFactor(12); a 10 la distance est divisee par 4 mais plus rapide
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setTxPower(20);
  Serial.println("LoRa OK");

  // Serveur web
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();
  Serial.println("Serveur web démarré");

  lcd.clear();
  lcd.print("Prêt - Envoi...");

  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
  // Lecture capteurs
  float t1 = dht1.readTemperature();
  float h1 = dht1.readHumidity();
  // float t2 = dht2.readTemperature();
  // float h2 = dht2.readHumidity();

  // float temp_moy = (isnan(t1) ? t2 : isnan(t2) ? t1 : (t1 + t2) / 2.0);
  // float hum_moy  = (isnan(h1) ? h2 : isnan(h2) ? h1 : (h1 + h2) / 2.0);
  float temp_moy = t1;
  float hum_moy = h1;
  int smoke = analogRead(MQ2_PIN);
  int flame = analogRead(FLAME_PIN);

  // Détection alertes
  bool tempAlert = (temp_moy >= tempSeuil);
  bool humAlert = (hum_moy >= humSeuil);
  bool smokeAlert = (smoke >= smokeSeuil);
  bool flameAlert = (flame <= seuilFlame);
  bool alerte = tempAlert || humAlert || smokeAlert || flameAlert;

  bool ventil = tempAlert && !humAlert && !smokeAlert && !flameAlert;
  bool relayState;

  // Actionneurs
  updateActuators(alerte);
  if (overrideActive) {
    relayState = manualVentil;
  } else {
    relayState = ventil;
  }
  digitalWrite(RELAY_PIN, relayState);

  //   Affichage LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(alerte ? "!! ALERTE !!" : "Etat: NORMAL");
  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(temp_moy, 1);
  if (flameAlert) {
    lcd.print("FLAMME!   ");
  } else {
    lcd.print("S:");
    lcd.print(smoke);
    lcd.print("   ");
  }

  lastTemp = temp_moy;
  lastHum = hum_moy;
  lastSmoke = smoke;
  lastAlert = alerte;
  lastFlame = flame;

  // Enregistrement nouvelle alerte (uniquement au déclenchement)
  if (alerte && !prevAlert) {
    if (history.size() >= 50) {
      history.remove(0);
    }

    JsonObject obj = history.createNestedObject();
    obj["t"] = getFormatedTime();

    hasNewAlerte = false;

    if (tempAlert) {
      obj["var"] = "T";
      obj["val"] = temp_moy;
      hasNewAlerte = true;
    }
    if (humAlert) {
      obj["var"] = "H";
      obj["val"] = hum_moy;
      hasNewAlerte = true;
    }
    if (smokeAlert) {
      obj["var"] = "S";
      obj["val"] = smoke;
      hasNewAlerte = true;
    }
    if (flameAlert) {
      obj["var"] = "F";
      obj["val"] = flame;
      hasNewAlerte = true;
    }

    // Sauvegarde sur disque seulement si une alerte a été ajoutée
    if (hasNewAlerte) {
      File file = LittleFS.open("/db/history.json", "w");
      if (file) {
        serializeJson(historyDoc, file);
        file.close();
        Serial.println("Nouvelle alerte ajoutée et historique sauvegardé");
      }
      newAlerte = obj;
    }
  } else if (!alerte && prevAlert) {
    // Plus d'alerte → on réinitialise newAlerte
    hasNewAlerte = false;
    // newAlerte reste valide mais on sait qu'il n'y en a plus de nouvelle
  }
  prevAlert = alerte;

  String jsonStr = buildStatusJson(
    false,
    temp_moy, hum_moy, smoke, flame, alerte,
    tempAlert, humAlert, smokeAlert, flameAlert);
  ws.textAll(jsonStr);

  LoRa.beginPacket();
  LoRa.print(jsonStr);
  LoRa.endPacket();

  receiveCommand = false;
  LoRa.receive();

  Serial.printf("Envoi LoRa → T=%.1f°C  H=%.1f%%  S=%d Alert=%d\n",
                temp_moy, hum_moy, smoke, alerte);

  // Boucle d'attente active : on vérifie régulièrement les paquets entrants pendant 1s
  unsigned long startWait = millis();
  while (millis() - startWait < 1000) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String received = "";
      while (LoRa.available()) {
        received += (char)LoRa.read();
      }
      Serial.printf("Reçu LoRa : %s\n", received.c_str());

      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, received);
      if (!error) {
        handleCommand(doc);
      } else {
        Serial.println("Erreur JSON LoRa : " + String(error.c_str()));
      }
    }
    delay(1);  // Très petit delay pour ne pas bouffer 100% CPU
  }
}