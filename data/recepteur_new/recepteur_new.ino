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
#define LED_V_PIN 2    // LED verte (état normal)
#define LED_R_PIN 33   // LED rouge (alerte)
#define BUZZER_PIN 13  // Buzzer

// Pins LoRa
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 27
#define LORA_RST 12
#define LORA_DIO0 26

// ─────────────────────────────────────────────
// WiFi Access Point (récepteur)
// ─────────────────────────────────────────────
const char *ap_ssid = "AlarmeGaz-RECEPTEUR";
const char *ap_password = "12345678";
IPAddress ap_ip(192, 168, 4, 1);

// ─────────────────────────────────────────────
// Objets globaux
// ─────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

DynamicJsonDocument historyDoc(4096);
JsonArray history;

// Dernier état JSON reçu de l'émetteur (pour les nouveaux clients WS)
String lastJson = "{}";

// Dernier état d'alerte (pour actionneurs)
bool currentAlert = false;

// ─────────────────────────────────────────────
// Envoi d'une commande via LoRa vers l'émetteur
// ─────────────────────────────────────────────
void sendLoRaCommand(const String &jsonCmd) {
  LoRa.beginPacket();
  LoRa.print(jsonCmd);
  LoRa.endPacket();
  Serial.println("Commande envoyée via LoRa → " + jsonCmd);
}

// ─────────────────────────────────────────────
// Gestion des événements WebSocket
// ─────────────────────────────────────────────
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    // Construire un JSON avec l'historique complet uniquement pour le nouveau client
    StaticJsonDocument<1024> baseDoc;
    deserializeJson(baseDoc, lastJson);

    DynamicJsonDocument fullDoc(2048);
    fullDoc.set(baseDoc.as<JsonObject>());
    fullDoc["his"] = history;
    fullDoc["newAlerte"] = nullptr;

    String fullJson;
    serializeJson(fullDoc, fullJson);
    client->text(fullJson);
  } else if (type == WS_EVT_DATA) {
    // Réception d'une commande depuis la page web (ex: mise à jour seuils ou ventilation manuelle)
    String message;
    for (size_t i = 0; i < len; i++) {
      message += (char)data[i];
    }

    Serial.println("Commande reçue via WebSocket ← " + message);

    // Relayer directement la commande via LoRa vers l'émetteur
    sendLoRaCommand(message);
  }
}

// ─────────────────────────────────────────────
// Mise à jour des actionneurs (LEDs + Buzzer)
// ─────────────────────────────────────────────
void updateActuators(bool alert) {
  digitalWrite(LED_R_PIN, alert ? HIGH : LOW);
  digitalWrite(LED_V_PIN, alert ? LOW : HIGH);
  digitalWrite(BUZZER_PIN, alert ? HIGH : LOW);
}

// ─────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Démarrage Récepteur AlarmeGaz ===");

  // Initialisation des sorties
  pinMode(LED_V_PIN, OUTPUT);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // État initial : tout éteint sauf LED verte (normal)
  digitalWrite(LED_V_PIN, HIGH);
  digitalWrite(LED_R_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // LittleFS pour servir la page web
  if (!LittleFS.begin(true)) {
    Serial.println("Erreur montage LittleFS !");
    while (true)
      delay(1000);
  }

  // Chargement de l'historique existant (persistant)
  if (LittleFS.exists("/db/history.json")) {
    File file = LittleFS.open("/db/history.json", "r");
    if (file) {
      DeserializationError err = deserializeJson(historyDoc, file);
      if (!err) {
        history = historyDoc.as<JsonArray>();
        Serial.printf("Historique chargé : %d alertes\n", history.size());
      }
      file.close();
    }
  } else {
    history = historyDoc.to<JsonArray>();
  }

  // WiFi en mode Access Point uniquement (pas besoin de STA/NTP ici)
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(ap_ssid, ap_password)) {
    Serial.println("Access Point démarré");
    Serial.printf("SSID : %s\n", ap_ssid);
    Serial.printf("Mot de passe : %s\n", ap_password);
    Serial.printf("IP : %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("Échec démarrage Access Point");
  }

  // Configuration LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println("ERREUR : Impossible de démarrer LoRa !");
    while (true) {
      digitalWrite(LED_R_PIN, HIGH);
      delay(200);
      digitalWrite(LED_R_PIN, LOW);
      delay(200);
    }
  }

  LoRa.setSyncWord(0xF3);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setTxPower(20);
  Serial.println("LoRa initialisé avec succès");

  // Serveur web statique (page HTML/JS/CSS dans LittleFS)
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // WebSocket pour communication bidirectionnelle
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();
  Serial.println("Serveur web et WebSocket démarrés");
  Serial.println("Récepteur prêt à recevoir les données LoRa");
}

// ─────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────
void loop() {
  // Réception d'un paquet LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }

    Serial.println("Données reçues via LoRa ← " + received);

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, received);

    if (error) {
      Serial.println("Erreur parsing JSON : " + String(error.c_str()));
    } else {
      // Extraction de l'état d'alerte
      bool alert = doc["isA"] | false;
      // Gestion nouvelle alerte (compatible ArduinoJson 7.x - copie champ par champ)
      JsonVariantConst newAlerteVar = doc["nAlrt"];

      if (!newAlerteVar.isNull() && newAlerteVar.is<JsonObject>()) {
        if (history.size() >= 50) {
          history.remove(0);
        }

        JsonObject entry = history.add<JsonObject>();

        // Copie des champs connus (t, var, val) - safe et compatible v7
        JsonVariantConst timeVal = newAlerteVar["t"];
        if (!timeVal.isNull()) entry["t"] = timeVal;

        JsonVariantConst varVal = newAlerteVar["var"];
        if (!varVal.isNull()) entry["var"] = varVal;

        JsonVariantConst valVal = newAlerteVar["val"];
        if (!valVal.isNull()) entry["val"] = valVal;

        // Sauvegarde immédiate de l'historique
        File file = LittleFS.open("/db/history.json", "w");
        if (file) {
          serializeJson(historyDoc, file);
          file.close();
          Serial.println("Nouvelle alerte ajoutée à l'historique local");
        }
      }

      // Mise à jour des actionneurs locaux
      if (alert != currentAlert) {
        currentAlert = alert;
        updateActuators(currentAlert);
        Serial.println(alert ? "ALERTE ACTIVÉE !" : "Retour à l'état normal");
      }

      // Sauvegarde du dernier JSON valide pour les nouveaux clients
      lastJson = received;

      // Diffusion à tous les clients web connectés
      ws.textAll(received);
    }
  }

  // Nettoyage des clients WebSocket déconnectés
  ws.cleanupClients();

  delay(500);
}