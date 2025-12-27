#include <LittleFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>

// ─────────────────────────────────────────────
// Pins matériels
// ─────────────────────────────────────────────
#define LED_V_PIN 2
#define LED_R_PIN 33
#define BUZZER_PIN 13

// Pins LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 27
#define LORA_RST 12
#define LORA_DIO0 26

// WiFi AP
const char *ap_ssid = "AlarmeGaz-RECEPTEUR";
const char *ap_password = "12345678";
IPAddress ap_ip(192, 168, 4, 1);

// ─────────────────────────────────────────────
// Variables globales
// ─────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

DynamicJsonDocument historyDoc(4096);
JsonArray history;

String lastJson = "{}";
bool currentAlert = false;

// *** NOUVELLE VARIABLE : commande en attente d'envoi ***
String pendingCommand = "";

// ─────────────────────────────────────────────
// Envoi d'une commande LoRa
// ─────────────────────────────────────────────
void sendPendingCommand(bool receiveCommand)
{
    if (receiveCommand)
    {
        pendingCommand = "";
        return;
    }
    if (pendingCommand.isEmpty())
    {
        return;
    }

    LoRa.beginPacket();
    LoRa.print(pendingCommand);
    LoRa.endPacket();

    Serial.println("Commande envoyée via LoRa (piggyback) → " + pendingCommand);
}

// ─────────────────────────────────────────────
// WebSocket : réception commande depuis page web
// ─────────────────────────────────────────────
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        StaticJsonDocument<1024> baseDoc;
        deserializeJson(baseDoc, lastJson);

        DynamicJsonDocument fullDoc(2048);
        fullDoc.set(baseDoc.as<JsonObject>());
        fullDoc["his"] = history;
        fullDoc["newAlerte"] = nullptr;

        String fullJson;
        serializeJson(fullDoc, fullJson);
        client->text(fullJson);
    }
    else if (type == WS_EVT_DATA)
    {
        String message;
        for (size_t i = 0; i < len; i++)
        {
            message += (char)data[i];
        }

        Serial.println("Commande reçue via WebSocket ← " + message);

        // On STOCKE la commande, on ne l'envoie PAS tout de suite
        pendingCommand = message;
    }
}

// ─────────────────────────────────────────────
// Alerte
// ─────────────────────────────────────────────
void updateActuators(bool alert)
{
    digitalWrite(LED_R_PIN, alert ? HIGH : LOW);
    digitalWrite(LED_V_PIN, alert ? LOW : HIGH);
    digitalWrite(BUZZER_PIN, alert ? HIGH : LOW);
}

// ─────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_V_PIN, OUTPUT);
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(LED_V_PIN, HIGH);

    // LittleFS pour servir la page web
    if (!LittleFS.begin(true))
    {
        Serial.println("Erreur montage LittleFS !");
        while (true)
            delay(1000);
    }

    // Chargement de l'historique existant (persistant)
    if (LittleFS.exists("/db/history.json"))
    {
        File file = LittleFS.open("/db/history.json", "r");
        if (file)
        {
            DeserializationError err = deserializeJson(historyDoc, file);
            if (!err)
            {
                history = historyDoc.as<JsonArray>();
                Serial.printf("Historique chargé : %d alertes\n", history.size());
            }
            file.close();
        }
    }
    else
    {
        history = historyDoc.to<JsonArray>();
    }

    // WiFi en mode Access Point uniquement (pas besoin de STA/NTP ici)
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
    if (WiFi.softAP(ap_ssid, ap_password))
    {
        Serial.println("Access Point démarré");
        Serial.printf("SSID : %s\n", ap_ssid);
        Serial.printf("Mot de passe : %s\n", ap_password);
        Serial.printf("IP : %s\n", WiFi.softAPIP().toString().c_str());
    }
    else
    {
        Serial.println("Échec démarrage Access Point");
    }

    // Configuration LoRa
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(433E6))
    {
        Serial.println("ERREUR : Impossible de démarrer LoRa !");
        while (true)
        {
            digitalWrite(LED_R_PIN, HIGH);
            delay(200);
            digitalWrite(LED_R_PIN, LOW);
            delay(200);
        }
    }

    LoRa.setSyncWord(0xF3);
    LoRa.setSpreadingFactor(9);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(8);
    LoRa.setTxPower(20);
    Serial.println("LoRa initialisé avec succès");

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();

    Serial.println("Récepteur prêt");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
}

// ─────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────
void loop()
{
    // 1. Réception paquet de l'émetteur
    int packetSize = LoRa.parsePacket();
    if (packetSize)
    {
        String received = "";
        while (LoRa.available())
        {
            received += (char)LoRa.read();
        }
        Serial.println("Données reçues via LoRa ← " + received);

        // Traitement normal du statut
        StaticJsonDocument<1024> doc;
        if (!deserializeJson(doc, received))
        {
            // Extraction de l'état d'alerte
            bool alert = doc["isA"] | false;
            bool receiveCommand = doc["isRC"] | false;

            // 2. *** APRÈS avoir reçu un paquet, on envoie la commande en attente ***
            sendPendingCommand(receiveCommand);

            // Gestion nouvelle alerte (compatible ArduinoJson 7.x - copie champ par champ)
            JsonVariantConst newAlerteVar = doc["nAlrt"];

            if (!newAlerteVar.isNull() && newAlerteVar.is<JsonObject>())
            {
                if (history.size() >= 50)
                {
                    history.remove(0);
                }

                JsonObject entry = history.add<JsonObject>();

                // Copie des champs connus (t, var, val) - safe et compatible v7
                JsonVariantConst timeVal = newAlerteVar["t"];
                if (!timeVal.isNull())
                    entry["t"] = timeVal;

                JsonVariantConst varVal = newAlerteVar["var"];
                if (!varVal.isNull())
                    entry["var"] = varVal;

                JsonVariantConst valVal = newAlerteVar["val"];
                if (!valVal.isNull())
                    entry["val"] = valVal;

                // Sauvegarde immédiate de l'historique
                File file = LittleFS.open("/db/history.json", "w");
                if (file)
                {
                    serializeJson(historyDoc, file);
                    file.close();
                    Serial.println("Nouvelle alerte ajoutée à l'historique local");
                }
            }

            // Mise à jour des actionneurs locaux
            if (alert != currentAlert)
            {
                currentAlert = alert;
                updateActuators(alert);
            }

            lastJson = received;
            ws.textAll(received);
        }
    }

    ws.cleanupClients();
    delay(10);
}