#include <DHT.h>
#include <DHT_U.h>

#include <MPS.h>
#include "config.h"


// ---- Broches et délais ----
#define BUTTON_PIN 3 // GPIO3 (G3) connecté au bouton
#define DHT_PIN 0
#define DHT_TYPE DHT11
#define LED_PIN 2
#define SERIAL_BAUD_RATE 115200
#define COUNTER_PERIOD_MS 2000

static DHT dht(DHT_PIN, DHT_TYPE);

// ---- Structures de payload ----
// Valeur du compteur envoyée à l’IHM.
struct __attribute__((packed)) CounterPayload {
  uint32_t value;
};
static_assert(sizeof(CounterPayload) == 4, "CounterPayload = 4 octets");

// État du bouton physique : relâché ou appuyé.
struct __attribute__((packed)) ButtonPayload {
  uint8_t value; // 0 : relâché, 1 : appuyé
};
static_assert(sizeof(ButtonPayload) == 1, "ButtonPayload = 1 octets"); 

// Température et humidité mesurées par le DHT11.
struct __attribute__((packed)) DHTPayload {
  float temperatureDegrees; // Température en degrés Celsius
  float humidityPercent; // Humidité relative en pourcentage
};
static_assert(sizeof(DHTPayload) == 8, "DHTPayload = 8 octets");

// Couleur de la LED, avec une intensité de 0 à 255 par composante.
struct __attribute__((packed)) LedColorPayload {
  uint8_t red = 0;  // Valeur par défaut puisque la led est eteinte dans la setup
  uint8_t green = 0;  // Valeur par défaut puisque la led est eteinte dans la setup
  uint8_t blue = 0;  // Valeur par défaut puisque la led est eteinte dans la setup
};
static_assert(sizeof(LedColorPayload) == 3, "LedColorPayload = 3 octet");

// État de la LED : 0 pour éteinte, 1 pour allumée.
struct __attribute__((packed)) LedStatePayload {
  uint8_t state = 0;  // Valeur par défaut puisque la led est eteinte dans la setup
};
static_assert(sizeof(LedStatePayload) == 1, "LedStatePayload = 1 octet");


// ---- Variables globales ----
static CounterPayload counter = {0};
static ButtonPayload buttonStatus = {false};
static DHTPayload sensorStatus = {0};
static LedColorPayload ledColor = {0};
static LedStatePayload ledState = {0};

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
constexpr mps::Subject kSubGetState = mps::makeSubject("get_" DEVICE_NAME "_state");

constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdReset = mps::makeSubject("cmd_" DEVICE_NAME "_reset");

// Bouton
constexpr mps::Subject kSubStatusButton = mps::makeSubject("status_" DEVICE_NAME "_button");

// Capteur temperature et humidite
constexpr mps::Subject kSubStatusSensor = mps::makeSubject("status_" DEVICE_NAME "_sensor");

// LED
constexpr mps::Subject kSubSetLedColor = mps::makeSubject("set_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubStatusLedColor = mps::makeSubject("status_" DEVICE_NAME "_led_color");

constexpr mps::Subject kSubCmdLedState = mps::makeSubject("cmd_" DEVICE_NAME "_led_state");
constexpr mps::Subject kSubStatusLedState = mps::makeSubject("status_" DEVICE_NAME "_led_state");


// ---- Callbacks ----
// Appelé lors de l'ouverture de la session (callback)
static void onSessionOpened() {
  Serial.println("Session opened!");
  MPS.publishValue(kSubStatusCounter, counter);
  MPS.publishValue(kSubStatusSensor, sensorStatus);
  MPS.publishValue(kSubStatusButton, buttonStatus);
  MPS.publishValue(kSubStatusLedColor, ledColor);
  MPS.publishValue(kSubStatusLedState, ledState);
}

// Appelé en cas d'échec d'envoi (callback)
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) {
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

// Renvoie les états courants à l’IHM lorsqu’elle se reconnecte.
static void onGetState(const mps::FrameView& message, void*) {
  if (message.payloadLength != 0) return;

  MPS.publishValue(kSubStatusCounter, counter);
  MPS.publishValue(kSubStatusSensor, sensorStatus);
  MPS.publishValue(kSubStatusButton, buttonStatus);
  MPS.publishValue(kSubStatusLedColor, ledColor);
  MPS.publishValue(kSubStatusLedState, ledState);
}

// Remet le compteur à zéro et confirme sa nouvelle valeur.
static void onCmdReset(const mps::FrameView& message, void*) {
  if (message.payloadLength != 0) return; // ce format n'a pas de payload

  counter.value = 0;
  MPS.publishValue(kSubStatusCounter, counter); // l'IHM voit le résultat réel
}

// Applique la couleur demandée et confirme la couleur et l’état de la LED.
static void onSetLedColor(const mps::FrameView& message, void*)
{
  if(!mps::readPayloadAs(message, &ledColor)) return; // Copie dans ledColor le message s'il est conforme

  // Mettre a jour l'etat de la led
  if (ledColor.red == 0 && ledColor.green == 0 && ledColor.blue == 0)
  {
    ledState.state = 0;
  }
  else
  {
    ledState.state = 1;
  }
  
  rgbLedWrite(LED_PIN, ledColor.red, ledColor.green, ledColor.blue); // Si la valeur est 0,0,0 la led est eteinte

  MPS.publishValue(kSubStatusLedColor, ledColor); // Publier le changement
  MPS.publishValue(kSubStatusLedState, ledState); // Publier le changement
}

// Allume ou éteint la LED en conservant la couleur mémorisée.
static void onCmdLedState(const mps::FrameView& message, void*) {
  if (!mps::readPayloadAs(message, &ledState)) return;

  ledState.state = ledState.state != 0;

  if (ledState.state)
  {
    rgbLedWrite(LED_PIN, ledColor.red, ledColor.green, ledColor.blue);
  } else 
  {
    rgbLedWrite(LED_PIN, 0, 0, 0);
  }

  MPS.publishValue(kSubStatusLedState, ledState);
}

// ---- Fonctions appelées par setup/loop ----
// envoi de heartbeats réguliers sans bloquer (comparaison au temps écoulé sans delay())
static void publishHeartbeat(){
  static uint32_t lastHeartbeatTimeMs = 0;
  static const uint16_t kHeartbeatPeriodMs = 1000;
  if(millis() - lastHeartbeatTimeMs >= kHeartbeatPeriodMs)
  {
    lastHeartbeatTimeMs = millis();
    
    // L'envoi de heartbeats depuis loop() assure au récepteur (IHM) que le code "métier" n'est pas planté
    MPS.publishHeartbeat(kSubHb);
  }
}

// ---- Fonctions Arduino ----
// Initialise le matériel et la communication avec la passerelle.
void setup() {
  Serial.begin(SERIAL_BAUD_RATE); // Démarrage de la communication série à 115200 bauds (sortie des Serial.print.. par exemple)

  dht.begin();

  MPS.setLogOutput(&Serial); // Définition de la sortie des messages de log de la lib MPS

  pinMode(BUTTON_PIN, INPUT_PULLUP); //Le bouton reliera G3 à GND lorsqu'il est pressé (LOW) et sera HIGH au repos.
  pinMode(LED_PIN, OUTPUT);

  // enregistrement des callbacks
  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);

  MPS.on(kSubGetState, onGetState);
  MPS.on(kSubCmdReset, onCmdReset);
  MPS.on(kSubSetLedColor, onSetLedColor);
  MPS.on(kSubCmdLedState, onCmdLedState);

  // constante d'adresse vide pour vérifier que l'adresse a été définie dans config.h
  static const uint8_t kUnsetMacAddress[sizeof(kGatewayMacAddress)] = {0, 0, 0, 0, 0, 0};
  if(memcmp(kGatewayMacAddress, kUnsetMacAddress, sizeof(kGatewayMacAddress)) == 0) {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  // démarrage du client MPS
  if(!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel)) {
    Serial.println("MPS: demarrage impossible");
  }

  // Eteindre la led au démarage
  rgbLedWrite(LED_PIN, 0, 0, 0);
}

// Traite les messages et publie les valeurs du capteur en continu.
void loop() {
  // appel de la boucle MPS (/!\ du code bloquant dans votre boucle loop() pourrait occasionner des problèmes de connexion)
  MPS.loop();

  // Appel non bloquant de l'envoi d'HB (polling)
  publishHeartbeat();

  static uint32_t lastCounterTimeMs = 0;
  if (millis() - lastCounterTimeMs >= COUNTER_PERIOD_MS) {

    // Compteur
    lastCounterTimeMs = millis();
    counter.value++;
    MPS.publishValue(kSubStatusCounter, counter);

    // Capteur
    sensorStatus.temperatureDegrees = dht.readTemperature();
    sensorStatus.humidityPercent = dht.readHumidity();
    MPS.publishValue(kSubStatusSensor, sensorStatus);
  }
  

  // Publie le bouton uniquement lorsque son état change.
  if(buttonStatus.value != (digitalRead(BUTTON_PIN) == LOW)) // verifie l'etat du bouton
  {
    buttonStatus.value = digitalRead(BUTTON_PIN) == LOW; // Recupérer l'etat du bouton
    MPS.publishValue(kSubStatusButton, buttonStatus);
  }  
}
