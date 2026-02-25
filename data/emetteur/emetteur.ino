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
//  Constantes et definitions materielles
// ─────────────────────────────────────────────────────────────────────────────

#define DHT_1_PIN 4
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
#define APN ""
String tel = "+237699974400";
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);

// Variables pour l'heure reelle GSM
bool gsmTimeInitialized = false;
unsigned long gsmTimeOffset = 0;
enum GSMState { IDLE,
                SENDING_SMS,
                MAKING_CALL,
                CALL_ACTIVE,
                HANGUP };
GSMState gsmState = IDLE;
unsigned long gsmTimer = 0;
String pendingAlertMessage = "";


// WiFi Access Point
const char *ap_ssid = "Module emetteur";
const char *ap_password = "12345678";
IPAddress ap_ip(192, 168, 4, 1);

// WiFi Station (pour NTP)
const char *sta_ssid = "VOTRE_SSID";
const char *sta_password = "VOTRE_PASSWORD";
bool connectToWiFi = false;

// Seuils par defaut
float TEMP_SEUIL_DEFAULT = 35.0;
float HUM_SEUIL_DEFAULT = 80.0;
int SMOKE_SEUIL_DEFAULT = 1500;
int FLAME_SEUIL_DEFAULT = 2000;

// Seuils actuels (charges depuis settings.json ou valeurs par defaut)
float tempSeuil = TEMP_SEUIL_DEFAULT;
float humSeuil = HUM_SEUIL_DEFAULT;
int smokeSeuil = SMOKE_SEUIL_DEFAULT;
int seuilFlame = FLAME_SEUIL_DEFAULT;

// controlle alimentation
bool powerOn = true;             // État actuel de l'alimentation (true = courant passe) 

unsigned long timeReference = 0;
bool timeReferenceSet = false;

// ─────────────────────────────────────────────────────────────────────────────
//  Objets globaux
// ─────────────────────────────────────────────────────────────────────────────

DHT dht1(DHT_1_PIN, DHT_TYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Dernières valeurs mesurees
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

// Detection nouvelle alerte
bool prevAlert = false;

bool receiveCommand = false;

bool prevCriticalAlert = false;
String pendingCriticalMessage = "";

// controle sonnore
bool buzzerSilenced = false;    // true = silence demandé par l'utilisateur
uint8_t silencedAlertMask = 0;  // masque des types d'alerte présents au moment du silence
                                // bit 0 = temp, bit 1 = hum, bit 2 = smoke, bit 3 = flame
bool wasNormalBefore = true;    // permet de détecter retour normal → nouvelle alerte

// ─────────────────────────────────────────────
// Alerte
// ─────────────────────────────────────────────
void updateActuators(bool alert) {
  // LEDs toujours réactives
  digitalWrite(LED_R_PIN, alert ? HIGH : LOW);
  digitalWrite(LED_V_PIN, alert ? LOW : HIGH);

  // Calcul des types d'alerte actuels (masque)
  uint8_t currentMask = 0;
  if (lastTemp >= tempSeuil) currentMask |= (1 << 0);
  if (lastHum >= humSeuil) currentMask |= (1 << 1);
  if (lastSmoke >= smokeSeuil) currentMask |= (1 << 2);
  if (lastFlame <= seuilFlame) currentMask |= (1 << 3);

  bool isCriticalNow = (lastTemp >= tempSeuil + 20.0) || (lastSmoke >= smokeSeuil * 2) || (lastFlame <= seuilFlame);

  // ─── Logique intelligente du buzzer ───────────────────────────────
  bool buzzNow = false;

  if (!alert) {
    // Plus aucune alerte → on réinitialise tout
    buzzerSilenced = false;
    silencedAlertMask = 0;
    wasNormalBefore = true;
  } else {
    // Il y a au moins une alerte
    if (buzzerSilenced) {
      // On compare la situation actuelle vs situation au silence
      bool sameAlerts = (currentMask == silencedAlertMask);
      bool noNewSeverity = !isCriticalNow;  // on pourrait affiner avec un masque critique séparé

      if (sameAlerts && noNewSeverity) {
        // Même alertes, même gravité → on reste silencieux
        buzzNow = false;
      } else {
        // Nouvelle alerte OU aggravation → on réactive
        buzzerSilenced = false;
        silencedAlertMask = 0;
        buzzNow = true;
        Serial.println("[BUZZER] Réactivation : nouvelle alerte ou aggravation");
      }
    } else {
      // Pas en mode silence → buzzer normal si alerte
      buzzNow = true;
    }
  }

  digitalWrite(BUZZER_PIN, buzzNow ? HIGH : LOW);
}

// -----------------------------------------------------------------------------
// Fonctions pour les alertes gsm
// -----------------------------------------------------------------------------
void handleGSMNonBlocking() {
  switch (gsmState) {
    case IDLE:
      if (pendingAlertMessage != "") {
        if (!modem.isNetworkConnected()) {
          Serial.println("[GSM] Pas de reseau → alerte abandonnee");
          pendingAlertMessage = "";
          break;
        }
        Serial.println("[GSM] Envoi SMS...");
        if (modem.sendSMS(tel, pendingAlertMessage)) {
          Serial.println("→ SMS OK");
        } else {
          Serial.println("→ Échec SMS");
        }
        gsmState = MAKING_CALL;
        gsmTimer = millis();
      }
      break;

    case MAKING_CALL:
      Serial.print("[GSM] Appel vers ");
      Serial.println(tel);
      SerialAT.print("ATD");
      SerialAT.print(tel);
      SerialAT.println(";");
      gsmState = CALL_ACTIVE;
      gsmTimer = millis();
      break;

    case CALL_ACTIVE:
      if (millis() - gsmTimer >= 15000UL) {  // 15 secondes max de sonnerie
        SerialAT.println("ATH");             // raccrocher
        Serial.println("[GSM] Appel termine (ATH)");
        gsmState = IDLE;
        pendingAlertMessage = "";
      }
      // Optionnel : lire les reponses pour detecter NO CARRIER / BUSY plus tôt
      while (SerialAT.available()) {
        String line = SerialAT.readStringUntil('\n');
        if (line.indexOf("NO CARRIER") >= 0 || line.indexOf("BUSY") >= 0) {
          SerialAT.println("ATH");
          gsmState = IDLE;
          pendingAlertMessage = "";
          break;
        }
      }
      break;

    default:
      gsmState = IDLE;
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fonctions de gestion des seuils persistants
// ─────────────────────────────────────────────────────────────────────────────

void loadSettings() {
  if (!LittleFS.exists("/db/settings.json")) return;

  File file = LittleFS.open("/db/settings.json", "r");
  if (!file) return;

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Erreur JSON settings : " + String(error.c_str()));
    return;
  }

  tempSeuil = doc["sT"] | TEMP_SEUIL_DEFAULT;
  humSeuil = doc["sH"] | HUM_SEUIL_DEFAULT;
  smokeSeuil = doc["sSm"] | SMOKE_SEUIL_DEFAULT;
  seuilFlame = doc["sF"] | FLAME_SEUIL_DEFAULT;

  // ← ICI : conversion correcte pour String
  tel = doc["tel"] | String("+237699974400");  // ou String(TEL_DEFAULT) si tu remets la constante
}

void saveSettings() {
  StaticJsonDocument<512> doc;
  doc["sT"] = tempSeuil;
  doc["sH"] = humSeuil;
  doc["sSm"] = smokeSeuil;
  doc["sF"] = seuilFlame;
  doc["tel"] = tel;

  File file = LittleFS.open("/db/settings.json", "w");
  if (!file) {
    Serial.println("Erreur ecriture settings.json");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Serial.println("Paramètres sauvegardes");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fonction partagee pour gerer les commandes (depuis WS ou LoRa)
// ─────────────────────────────────────────────────────────────────────────────
void handleCommand(const JsonDocument &doc) {
  if (!doc.containsKey("com")) {
    Serial.println("[CMD] Commande reçue sans clé 'com'");
    return;
  }

  String command = doc["com"].as<String>();

  receiveCommand = true;  // on le met une seule fois en début → signe qu’on a traité quelque chose

  if (command == "force_cut") {
    powerOn = false;
    Serial.println("[POWER] Coupure manuelle forcée");
    ws.textAll("{\"event\":\"power_updated\",\"success\":true,\"pwrOn\":false}");

  } else if (command == "restore_pwr") {
    powerOn = true;
    Serial.println("[POWER] Rétablissement manuel");
    ws.textAll("{\"event\":\"power_updated\",\"success\":true,\"pwrOn\":true}");

  } else if (command == "upd_conf") {
    JsonVariantConst v = doc["seuils"];
    if (!v.isNull() && v.is<JsonObjectConst>()) {
      JsonObjectConst seuils = v.as<JsonObjectConst>();

      tempSeuil = seuils["sT"] | tempSeuil;
      humSeuil = seuils["sH"] | humSeuil;
      smokeSeuil = seuils["sSm"] | smokeSeuil;
      seuilFlame = seuils["sF"] | seuilFlame;  // une seule fois
      tel = seuils["tel"] | tel;

      Serial.print("[CONF] Nouveau numéro : '");
      Serial.print(tel);
      Serial.println("'");

      saveSettings();
      Serial.println("[CONF] Paramètres mis à jour et sauvegardés");

      // Confirmation ciblée
      String resp = "{\"event\":\"config_updated\",\"success\":true,\"tel\":\"" + tel + "\"}";
      ws.textAll(resp);
    } else {
      Serial.println("[CONF] Format seuils invalide");
    }
  } else if (command == "off_buzzer") {
    if (lastAlert && !buzzerSilenced) {
      buzzerSilenced = true;

      // Capture l'état des alertes au moment du silence
      silencedAlertMask = 0;
      if (lastTemp >= tempSeuil) silencedAlertMask |= (1 << 0);
      if (lastHum >= humSeuil) silencedAlertMask |= (1 << 1);
      if (lastSmoke >= smokeSeuil) silencedAlertMask |= (1 << 2);
      if (lastFlame <= seuilFlame) silencedAlertMask |= (1 << 3);

      Serial.println("[BUZZER] Silence activé (intelligent) - masque = 0x" + String(silencedAlertMask, HEX));

      // Confirmation ciblée
      ws.textAll("{\"event\":\"off_buzzer\",\"state\":true}");
    } else {
      Serial.println("[BUZZER] Commande ignorée : pas d'alerte active ou déjà silencé");
      // Option : envoyer un ack négatif
      // ws.textAll("{\"event\":\"off_buzzer\",\"state\":false,\"reason\":\"no_alert_or_already_silenced\"}");
    }
  } else {
    Serial.println("[CMD] Commande inconnue : " + command);
    // Option : ws.textAll("{\"event\":\"error\",\"message\":\"commande inconnue\"}");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Gestion WebSocket
// ─────────────────────────────────────────────────────────────────────────────

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    Serial.printf("Client connecte #%u\n", client->id());
    uint32_t nombreClients = ws.count();
    Serial.printf("Nombre de clients connectes : %u\n", nombreClients);
    if (!timeReferenceSet) {
      timeReference = millis();
      timeReferenceSet = true;
      Serial.println("Reference temporelle definie (premier client connecte)");
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
    // Gestion des commandes (upd_conf, mnl_vent)
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
//  Fonction centralisee pour construire le JSON d'etat
// ─────────────────────────────────────────────────────────────────────────────

String buildStatusJson(bool withAllHistory, float temp, float hum, int smoke, int flame, bool isAlert,
                       bool tempAlert, bool humAlert, bool smokeAlert, bool flameAlert) {

  size_t capacity = JSON_OBJECT_SIZE(14) + JSON_ARRAY_SIZE(4) + JSON_OBJECT_SIZE(5);
  if (withAllHistory) capacity += historyDoc.memoryUsage() + 256;

  DynamicJsonDocument doc(capacity);

  doc["isA"] = isAlert;
  doc["isRC"] = receiveCommand;

  JsonArray types = doc.createNestedArray("alertType");
  if (tempAlert) types.add("T");
  if (humAlert) types.add("H");
  if (smokeAlert) types.add("S");
  if (flameAlert) types.add("F");

  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["sm"] = smoke;
  doc["fl"] = flame;

  doc["pwrOn"] = powerOn;
  doc["buzzOff"] = buzzerSilenced;

  JsonObject seuils = doc.createNestedObject("seuils");
  seuils["sT"] = tempSeuil;
  seuils["sH"] = humSeuil;
  seuils["sSm"] = smokeSeuil;
  seuils["sF"] = seuilFlame;
  seuils["tel"] = tel;

  if (withAllHistory) {
    doc["his"] = history;
  }

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

    char buf[16];  // suffisant pour "02/26 14:35\0"
    strftime(buf, sizeof(buf), "%m/%y %H:%M", timeinfo);
    return String(buf);
  }

  // Fallback si GSM pas synchronisé
  if (!timeReferenceSet) return "NC";

  unsigned long elapsed = (millis() - timeReference) / 1000;
  unsigned long h = elapsed / 3600;
  unsigned long m = (elapsed % 3600) / 60;

  char buf[12];
  snprintf(buf, sizeof(buf), "+%02lu:%02lu", h, m);  // ou autre format fallback
  return String(buf);
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
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_V_PIN, HIGH);

  dht1.begin();

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
    File f = LittleFS.open("/db/history.json", "r");
    if (f) {
      deserializeJson(historyDoc, f);
      history = historyDoc.as<JsonArray>();
      f.close();
    }
  } else {
    history = historyDoc.to<JsonArray>();
  }

  // WiFi AP + Station (pour NTP)
  if (connectToWiFi) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
    Serial.println("Mode AP PUR active (meilleure stabilite hotspot)");
  }
  WiFi.setSleep(false);

  // Connexion WiFi Station (NTP)
  if (connectToWiFi) {
    Serial.println("Tentative de connexion WiFi STA...");
    WiFi.begin(sta_ssid, sta_password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnecte au WiFi !");
      Serial.print("IP STA : ");
      Serial.println(WiFi.localIP());

      // Optionnel : NTP seulement si connecte
      timeClient.begin();
    } else {
      Serial.println("\nÉchec connexion WiFi STA → on continue en AP seul");
    }
  } else {
    Serial.println("Mode AP seul (pas de connexion routeur)");
  }

  // Demarrage AP
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ap_ssid, ap_password, 1, 0);
  Serial.printf("AP demarre → %s / %s  IP:%s\n", ap_ssid, ap_password, WiFi.softAPIP().toString().c_str());
  delay(1500);

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("Erreur demarrage LoRa !");
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
    Serial.println("→ modem.restart() echoue !");
  } else {
    Serial.println("→ modem redemarre");
  }

  delay(2000);

  // Attente reseau (max 60s)
  if (!modem.waitForNetwork(60000L)) {
    Serial.println("→ Pas de reseau GSM après 60s");
  } else {
    Serial.println("→ Reseau GSM OK");
    Serial.println("Operateur : " + modem.getOperator());

    // Recuperation heure reseau (format "yy/MM/dd,hh:mm:ss±zz")
    // Recuperation heure reseau via pointeurs
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    float timezone = 0.0;

    if (modem.getNetworkTime(&year, &month, &day, &hour, &minute, &second, &timezone)) {
      Serial.printf("→ Heure reseau : %02d/%02d/%02d  %02d:%02d:%02d  (fuseau %.1f h)\n",
                    year, month, day, hour, minute, second, timezone);

      struct tm timeinfo = { 0 };
      timeinfo.tm_year = year + 2000 - 1900;  // annees depuis 1900
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
        Serial.println("→ Heure GSM synchronisee avec succès");
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
  Serial.println("Serveur web demarre");

  lcd.clear();
  lcd.print("Prêt - Envoi...");

  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.print("Tel charge au demarrage : '");
  Serial.print(tel);
  Serial.println("'");
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
  // Lecture capteurs
  float t1 = dht1.readTemperature();
  float h1 = dht1.readHumidity();
  float temp_moy = t1;
  float hum_moy = h1;
  int smoke = analogRead(MQ2_PIN);
  int flame = analogRead(FLAME_PIN);

  // Détection alertes de base
  bool tempAlert = (temp_moy >= tempSeuil);
  bool humAlert = (hum_moy >= humSeuil);
  bool smokeAlert = (smoke >= smokeSeuil);
  bool flameAlert = (flame <= seuilFlame);
  bool alerte = tempAlert || humAlert || smokeAlert || flameAlert;

  // ────────────────────────────────────────────────
  // CAS CRITIQUES → coupure alimentation + SMS + APPEL
  // ────────────────────────────────────────────────
  bool criticalTemp = (temp_moy >= tempSeuil + 10.0);
  bool criticalSmoke = (smoke >= smokeSeuil * 2 || smoke >= 4000);  // ← ajuste le multiplicateur ou la valeur absolue
  bool criticalFlame = flameAlert;

  bool criticalAlert = criticalTemp || criticalSmoke || criticalFlame;

  // Logique relais : COUPURE sur cas critique (ou manuel)
  if (criticalAlert) {
    powerOn = false;  // Toute alerte critique = coupure immédiate + verrouillée
  }
  digitalWrite(RELAY_PIN, powerOn ? HIGH : LOW);

  // Actionneurs visuels/sonores (sur toute alerte, pas seulement critique)
  updateActuators(alerte);

  //   Affichage LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  if (criticalAlert) {
    lcd.print("!!! DANGER !!!");
  } else if (alerte) {
    lcd.print("!! ALERTE !!");
  } else {
    lcd.print("Etat: NORMAL");
  }
  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(temp_moy, 1);
  lcd.print(" H:");
  lcd.print(hum_moy, 1);
  if (flameAlert) {
    lcd.print(" FLAMME!");
  } else {
    lcd.print(" G:");
    lcd.print(smoke);
    lcd.print(" FL:");
    lcd.print(flame);
  }

  handleGSMNonBlocking();

  // Mémorisation pour la boucle suivante
  lastTemp = temp_moy;
  lastHum = hum_moy;
  lastSmoke = smoke;
  lastAlert = alerte;
  lastFlame = flame;

  // Enregistrement nouvelle alerte (uniquement au declenchement)
  if (alerte && !prevAlert) {
    // ─── Alerte normale ──→ SMS uniquement ───────────────────────
    if (pendingAlertMessage == "" && !criticalAlert) {
      String msg = "ALERTE salle technique\n";
      if (tempAlert) msg += "Temp: " + String(temp_moy, 1) + "°C\n";
      if (humAlert) msg += "Hum: " + String(hum_moy, 1) + "%\n";
      if (smokeAlert) msg += "Fumee: " + String(smoke) + "\n";
      if (flameAlert) msg += "Flamme detectee !\n";

      pendingAlertMessage = msg;
      gsmState = SENDING_SMS;  // → SMS puis fin (pas d'appel)
    }

    // ─── CAS CRITIQUE ──→ SMS + APPEL ────────────────────────────
    if (pendingCriticalMessage == "" && criticalAlert) {
      String msg = "!!! URGENCE CRITIQUE !!!\nSalle technique\n";
      if (criticalTemp) msg += "TEMP EXTREME: " + String(temp_moy, 1) + "°C\n";
      if (criticalSmoke) msg += "FUMEE TRES FORTE: " + String(smoke) + "\n";
      if (criticalFlame) msg += "INCENDIE - FLAMME DETECTEE !\n";
      msg += "Courant coupe automatiquement.";

      pendingCriticalMessage = msg;
      gsmState = SENDING_SMS;  // → le handler fera SMS puis APPEL
    }

    // Historique (on garde pour toutes les alertes)
    if (history.size() >= 30) history.remove(0);

    JsonObject obj = history.createNestedObject();
    obj["t"] = getFormatedTime();
    obj["crit"] = criticalAlert;

    if (criticalTemp) {
      obj["var"] = "T+";
      obj["val"] = temp_moy;
    } else if (tempAlert) {
      obj["var"] = "T";
      obj["val"] = temp_moy;
    } else if (humAlert) {
      obj["var"] = "H";
      obj["val"] = hum_moy;
    } else if (criticalSmoke || smokeAlert) {
      obj["var"] = criticalSmoke ? "S++" : "S";
      obj["val"] = smoke;
    } else if (flameAlert) {
      obj["var"] = "F";
      obj["val"] = flame;
    }

    File file = LittleFS.open("/db/history.json", "w");
    if (file) {
      serializeJson(historyDoc, file);
      file.close();
    }

    hasNewAlerte = true;
    newAlerte = obj;
  }

  if (!alerte) {
    hasNewAlerte = false;
  }

  prevAlert = alerte;
  prevCriticalAlert = criticalAlert;

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

  Serial.printf("T=%.1f H=%.1f S=%4d F=%4d | Alert=%d | Crit=%d | PowerOn=%d\n",
                temp_moy, hum_moy, smoke, flame, alerte, criticalAlert, powerOn);

  // Boucle d'attente active : on verifie regulièrement les paquets entrants pendant 1s
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
    delay(1);
  }
}