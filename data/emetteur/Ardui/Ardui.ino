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

#define DHT_1_PIN     4 
#define DHT_2_PIN     15 
#define DHT_TYPE     DHT22
#define MQ2_PIN       34
#define RELAY_PIN     17
#define LED_V_PIN      2
#define LED_R_PIN     33
#define BUZZER_PIN    13
#define FLAME_PIN     32

// Pins LoRa
#define LORA_SCK      18
#define LORA_MISO     19
#define LORA_MOSI     23
#define LORA_CS       27
#define LORA_RST      12
#define LORA_DIO0     26

// WiFi Access Point
const char* ap_ssid     = "AlarmeGaz-ESP32";
const char* ap_password = "12345678";
IPAddress   ap_ip(192, 168, 4, 1);

// WiFi Station (pour NTP)
const char* sta_ssid     = "VOTRE_SSID";       // ← À modifier
const char* sta_password = "VOTRE_PASSWORD";   // ← À modifier
const long  timezone_offset = 3600;            // UTC+1 (France)

// Seuils par défaut
float TEMP_SEUIL_DEFAULT  = 35.0;
float HUM_SEUIL_DEFAULT   = 80.0;
int   SMOKE_SEUIL_DEFAULT = 1500;

// Seuils actuels (chargés depuis settings.json ou valeurs par défaut)
float tempSeuil  = TEMP_SEUIL_DEFAULT;
float humSeuil   = HUM_SEUIL_DEFAULT;
int   smokeSeuil = SMOKE_SEUIL_DEFAULT;

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
float lastTemp   = 0.0;
float lastHum    = 0.0;
int   lastSmoke  = 0;
bool  lastAlert  = false;

// Historique des alertes
DynamicJsonDocument historyDoc(4096);
JsonArray history;

// Détection nouvelle alerte
bool prevAlert = false;

// ─────────────────────────────────────────────────────────────────────────────
//  Fonctions de gestion des seuils persistants
// ─────────────────────────────────────────────────────────────────────────────

void loadSettings() {
    if (!LittleFS.exists("/db/settings.json")) return;

    File file = LittleFS.open("/db/settings.json", "r");
    if (!file) return;

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Erreur lecture settings.json : " + String(error.c_str()));
        return;
    }

    tempSeuil  = doc["seuilTemp"]     | TEMP_SEUIL_DEFAULT;
    humSeuil   = doc["seuilHumidity"] | HUM_SEUIL_DEFAULT;
    smokeSeuil = doc["seuilSmoke"]    | SMOKE_SEUIL_DEFAULT;

    Serial.println("Seuils chargés depuis settings.json");
}

void saveSettings() {
    StaticJsonDocument<256> doc;
    doc["seuilTemp"]     = tempSeuil;
    doc["seuilHumidity"] = humSeuil;
    doc["seuilSmoke"]    = smokeSeuil;

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
//  Gestion WebSocket
// ─────────────────────────────────────────────────────────────────────────────

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
                
    Serial.println("Nouveaux seuils reçus ");
    
    if (type != WS_EVT_DATA) return;

    String message;
    for (size_t i = 0; i < len; i++) message += (char)data[i];

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        Serial.println("Erreur JSON WebSocket : " + String(error.c_str()));
        return;
    }

    if (doc["command"] == "update_thresholds") {
        JsonObject seuils = doc["seuils"];
        if (seuils) {
            tempSeuil  = seuils["seuilTemp"]     | tempSeuil;
            humSeuil   = seuils["seuilHumidity"] | humSeuil;
            smokeSeuil = seuils["seuilSmoke"]    | smokeSeuil;

            saveSettings();
            Serial.println("Nouveaux seuils reçus et enregistrés");
        }
    }
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
        lcd.clear(); lcd.print("Formatage FS...");
        LittleFS.format();
        ESP.restart();
    }
    Serial.println("LittleFS OK");

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

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnecté au Wi-Fi");
        configTime(timezone_offset, 0, "pool.ntp.org");
        timeClient.begin();
    } else {
        Serial.println("\nÉchec connexion WiFi - pas de synchro horaire");
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
        lcd.clear(); lcd.print("Erreur LoRa");
        while (true) delay(1000);
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
    delay(1000);
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
    float hum_moy  = h1;
    int   smoke    = analogRead(MQ2_PIN);

    // Détection alertes
    bool tempAlert  = (temp_moy >= tempSeuil);
    bool humAlert   = (hum_moy  >= humSeuil);
    bool smokeAlert = (smoke    >= smokeSeuil);
    bool alerte     = tempAlert || humAlert || smokeAlert;

    bool ventil     = tempAlert && !humAlert && !smokeAlert;

    // Actionneurs
    digitalWrite(BUZZER_PIN, alerte);
    digitalWrite(LED_R_PIN,  alerte);
    digitalWrite(LED_V_PIN, !alerte);
    digitalWrite(RELAY_PIN, ventil); 

    // Affichage LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(alerte ? "!! ALERTE !!" : "Etat: NORMAL");
    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(temp_moy, 1);
    lcd.print(" S:");
    lcd.print(smoke);

    lastTemp   = temp_moy;
    lastHum    = hum_moy;
    lastSmoke  = smoke;
    lastAlert  = alerte;

    // Heure formatée
    char timeStr[20] = "Pas d'heure";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%d-%m-%Y:%H-%M", &timeinfo);
    }

    // Enregistrement nouvelle alerte (uniquement au déclenchement)
    bool added = false;
    if (alerte && !prevAlert) {
        if (tempAlert) {
            JsonObject obj = history.createNestedObject();
            obj["time"] = timeStr;
            obj["type"] = "TEMPERATURE";
            obj["val"]  = temp_moy;
            added = true;
        }
        if (humAlert) {
            JsonObject obj = history.createNestedObject();
            obj["time"] = timeStr;
            obj["type"] = "HUMIDITY";
            obj["val"]  = hum_moy;
            added = true;
        }
        if (smokeAlert) {
            JsonObject obj = history.createNestedObject();
            obj["time"] = timeStr;
            obj["type"] = "SMOKE";
            obj["val"]  = smoke;
            added = true;
        }

        if (added) {
            File file = LittleFS.open("/db/history.json", "w");
            if (file) {
                serializeJson(historyDoc, file);
                file.close();
                Serial.println("Historique alertes sauvegardé");
            }
        }
    }
    prevAlert = alerte;

    // Préparation des données JSON
    StaticJsonDocument<256> doc;
    doc["success"] = true;
    doc["isAlert"] = alerte;

    JsonArray types = doc.createNestedArray("alertType");
    if (tempAlert)  types.add("TEMPERATURE");
    if (humAlert)   types.add("HUMIDITY");
    if (smokeAlert) types.add("SMOKE");

    doc["temp"]     = temp_moy;
    doc["humidity"] = hum_moy;
    doc["smoke"]    = smoke;

    JsonObject seuils = doc.createNestedObject("seuils");
    seuils["seuilTemp"]     = tempSeuil;
    seuils["seuilHumidity"] = humSeuil;
    seuils["seuilSmoke"]    = smokeSeuil;

    doc["history"] = history;

    // Diffusion WebSocket
    String jsonStr;
    serializeJson(doc, jsonStr);
    ws.textAll(jsonStr);
 
    // Envoi LoRa
    LoRa.beginPacket();
    serializeJson(doc, LoRa);
    LoRa.endPacket();

    Serial.printf("Envoi LoRa → T=%.1f°C  H=%.1f%%  S=%d Alert=%d\n",
                  temp_moy, hum_moy, smoke, alerte);

    delay(500);
}