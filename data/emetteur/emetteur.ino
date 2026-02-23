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
#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>

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
#define LCD_SDA_PIN 21
#define LCD_SCL_PIN 22

// Pins LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 27
#define LORA_RST 12
#define LORA_DIO0 26

// UART vers SIM800L
#define SerialAT Serial2
#define MODEM_RX 16
#define MODEM_TX 15
// Configuration – À CHANGER
#define SMS_TARGET "+237699974400"
#define CALL_TARGET "+237699974400"
#define APN ""
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);

// Variables pour l'heure réelle GSM
bool gsmTimeInitialized = false;
unsigned long gsmTimeOffset = 0;

// WiFi Access Point
const char *ap_ssid = "Module emetteur";
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

bool gsmCalling = false;
unsigned long gsmCallStart = 0;

// ─────────────────────────────────────────────────────────────────────────────
//  Objets globaux
// ─────────────────────────────────────────────────────────────────────────────

DHT dht1(DHT_1_PIN, DHT_TYPE);
// DHT dht2(DHT_2_PIN, DHT_TYPE);

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
bool isFlame  = false;

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

// -----------------------------------------------------------------------------
// Fonctions pour les alertes gsm
/*/ -----------------------------------------------------------------------------
void sendAlertGSM(const String &message) {

  if (!modem.isNetworkConnected()) {
    Serial.println("[GSM] Réseau non connecté → abandon alerte GSM");
    return;
  }

  // 1. Envoi SMS
  Serial.print("[GSM] Envoi SMS → ");
  Serial.println(message);

  if (modem.sendSMS(SMS_TARGET, message)) {
    Serial.println("→ SMS envoyé OK");
  } else {
    Serial.println("→ ÉCHEC envoi SMS");
  }

  // 2. Appel sortant (sonne ~20 secondes puis raccroche)
  Serial.print("[GSM] Appel sortant vers ");
  Serial.print(CALL_TARGET);
  Serial.println(" ...");

  SerialAT.print("ATD");
  SerialAT.print(CALL_TARGET);
  SerialAT.println(";");

  // On laisse sonner ~20 secondes
  unsigned long debutAppel = millis();
  while (millis() - debutAppel < 20000UL) {
    if (SerialAT.available()) {
      String ligne = SerialAT.readStringUntil('\n');
      Serial.println("[GSM] → " + ligne);
      if (ligne.indexOf("NO CARRIER") >= 0 || ligne.indexOf("BUSY") >= 0) {
        break;
      }
    }
    delay(100);
  }

  // Raccrocher dans tous les cas
  SerialAT.println("ATH");
  Serial.println("[GSM] Appel terminé (ATH envoyé)");
}*/

void sendAlertGSM(const String &message) {

  if (!modem.isNetworkConnected()) {
    Serial.println("[GSM] Réseau non connecté → abandon alerte GSM");
    return;
  }

  // SMS
  if (modem.sendSMS(SMS_TARGET, message)) {
    Serial.println("→ SMS envoyé OK");
  } else {
    Serial.println("→ ÉCHEC envoi SMS");
  }

  // Lancer appel
  SerialAT.print("ATD");
  SerialAT.print(CALL_TARGET);
  SerialAT.println(";");

  gsmCalling = true;
  gsmCallStart = millis();

  Serial.println("[GSM] Appel lancé (non bloquant)");
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
  debugHistoryFile();
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

    Serial.println("Envoi historique complet au client");
    serializeJson(historyDoc, Serial);
    Serial.println();
    
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
  if (gsmTimeInitialized) {
    unsigned long now_ms = millis() + gsmTimeOffset;
    time_t now = now_ms / 1000;

    struct tm *timeinfo = localtime(&now);

    char buf[20];
    // Format : 14:35:22 ou 02/21 14:35 selon préférence
    strftime(buf, sizeof(buf), "%H:%M:%S", timeinfo);
    // ou strftime(buf, sizeof(buf), "%m/%d %H:%M", timeinfo);  ← autre style
    return String(buf);
  }

  // Fallback : temps depuis démarrage
  if (!timeReferenceSet) return "NC";

  unsigned long elapsed = (millis() - timeReference) / 1000;
  unsigned long h = elapsed / 3600;
  unsigned long m = (elapsed % 3600) / 60;
  unsigned long s = elapsed % 60;

  char buf[12];
  if (h > 0) snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  else snprintf(buf, sizeof(buf), "%02lu:%02lu", m, s);

  return String(buf);
}

//TESTS
void debugHistoryFile() {
  Serial.println("---- DEBUG HISTORY ----");

  if (!LittleFS.exists("/db/history.json")) {
    Serial.println("❌ history.json n'existe PAS");
    return;
  }

  File file = LittleFS.open("/db/history.json", "r");
  if (!file) {
    Serial.println("❌ Impossible d'ouvrir history.json");
    return;
  }

  Serial.println("✅ history.json existe. Contenu :");

  while (file.available()) {
    Serial.write(file.read());
  }

  file.close();
  Serial.println("\n---- FIN DEBUG ----");
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
  // dht2.begin();

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
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
  WiFi.setSleep(false);

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
  WiFi.softAP(ap_ssid, ap_password, 6, 0);
Serial.println("AP démarré sur canal 6 fixe");
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
  LoRa.setSpreadingFactor(9);
  // LoRa.setSpreadingFactor(9); a 10 la distance est divisee par 4 mais plus rapide
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setTxPower(20);
  Serial.println("LoRa OK");

  // ─── Initialisation SIM800L ───────────────────────────────────────────────
  Serial.println("Initialisation SIM800L...");
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1800);

  if (!modem.restart()) {
    Serial.println("→ modem.restart() échoué !");
  } else {
    Serial.println("→ modem redémarré");
  }

  delay(2000);

  // Attente réseau (max 60s)
  if (!modem.waitForNetwork(60000L)) {
    Serial.println("→ Pas de réseau GSM après 60s");
  } else {
    Serial.println("→ Réseau GSM OK");
    Serial.println("Opérateur : " + modem.getOperator());

    // Récupération heure réseau (format "yy/MM/dd,hh:mm:ss±zz")
    // Récupération heure réseau via pointeurs
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    float timezone = 0.0;

    if (modem.getNetworkTime(&year, &month, &day, &hour, &minute, &second, &timezone)) {
      Serial.printf("→ Heure réseau : %02d/%02d/%02d  %02d:%02d:%02d  (fuseau %.1f h)\n",
                    year, month, day, hour, minute, second, timezone);

      struct tm timeinfo = { 0 };
      timeinfo.tm_year = year + 2000 - 1900;  // années depuis 1900
      timeinfo.tm_mon = month - 1;            // 0 = janvier
      timeinfo.tm_mday = day;
      timeinfo.tm_hour = hour;
      timeinfo.tm_min = minute;
      timeinfo.tm_sec = second;

      time_t rawtime = mktime(&timeinfo);
      if (rawtime != -1) {
        // Ajustement fuseau horaire (timezone est en heures)
        rawtime += (long)(timezone * 3600);
        gsmTimeOffset = (unsigned long)rawtime * 1000UL - millis();
        gsmTimeInitialized = true;
        Serial.println("→ Heure GSM synchronisée avec succès");
      } else {
        Serial.println("→ Échec de mktime()");
      }
    } else {
      Serial.println("→ Échec de getNetworkTime()");
    }
  }

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

  lcd.print(" H:");
  lcd.print(hum_moy, 1);
  if (flameAlert) {
    lcd.print(" FLAMME!   ");
  } else {
    lcd.print(" G:");
    lcd.print(smoke);
    lcd.print(" FL:");
    lcd.print(flame);
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
      isFlame = true;
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

    // ─── ENVOI ALERTE GSM ────────────────────────────────────────
    String alertMsg = "URGENT - Alerte sur module emetteur !\n";
    if (tempAlert) alertMsg += "Température élevée: " + String(temp_moy, 1) + "°C\n";
    if (humAlert) alertMsg += "Humidité élevée: " + String(hum_moy, 1) + "%\n";
    if (smokeAlert) alertMsg += "Détection fumée: " + String(smoke) + "\n";
    if (flameAlert) alertMsg += "Détection flamme !\n";

    if(isFlame){
      sendAlertGSM(alertMsg);
      
    }

  } else if (!alerte && prevAlert) {
    // Plus d'alerte → on réinitialise newAlerte
    hasNewAlerte = false;
    // newAlerte reste valide mais on sait qu'il n'y en a plus de nouvelle
    isFlame = false;  // Réinitialiser le flag après envoi
  }

  // Gestion appel GSM non bloquant
  if (gsmCalling) {
    if (millis() - gsmCallStart >= 20000UL) {
        SerialAT.println("ATH");
        Serial.println("[GSM] Appel terminé (20s écoulées)");
        gsmCalling = false;
    }
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