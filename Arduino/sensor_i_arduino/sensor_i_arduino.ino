#include <DHT.h>
#include <DHT_U.h>

#include <MPS.h>
#include "config.h"


#define BUTTON_PIN 3 // GPIO3 (G3) connecté au bouton
#define DHT_PIN 0
#define DHT_TYPE DHT11
#define LED_PIN 2

DHT dht(DHT_PIN, DHT_TYPE);

// ---- Structures de payload ----
struct __attribute__((packed)) CounterPayload {
  uint32_t value;
};
static_assert(sizeof(CounterPayload) == 4, "CounterPayload = 4 octets");

struct __attribute__((packed)) ButtonPayload {
  uint8_t value; // 1 octet
};
static_assert(sizeof(ButtonPayload) == 1, "ButtonPayload = 1 octets"); 

struct __attribute__((packed)) DHTPayload {
  float temperature;
  float humidity;
};
static_assert(sizeof(DHTPayload) == 8, "DHTPayload = 8 octets"); 

struct __attribute__((packed)) LedPayloadColor {
  uint8_t R = 0;  // Valeur par défaut puisque la led est eteinte dans la setup
  uint8_t G = 0;  // Valeur par défaut puisque la led est eteinte dans la setup
  uint8_t B = 0;  // Valeur par défaut puisque la led est eteinte dans la setup 
};
static_assert(sizeof(LedPayloadColor) == 3, "LedPayloadColor = 3 octet");

struct __attribute__((packed)) LedPayloadState {
  uint8_t state = 0;  // Valeur par défaut puisque la led est eteinte dans la setup
};
static_assert(sizeof(LedPayloadState) == 1, "LedPayloadState = 1 octet");


// ---- Variables globales ----
static CounterPayload counter = {0};
static ButtonPayload status_button = {false};
static DHTPayload status_sensor = {0};
static LedPayloadColor led_payload_color = {0};
static LedPayloadState led_payload_state = {0};

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);

constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdReset = mps::makeSubject("cmd_" DEVICE_NAME "_reset");

// Bouton
constexpr mps::Subject kSubStatusButton = mps::makeSubject("status_" DEVICE_NAME "_button");

// Capteur temperature et humidite
constexpr mps::Subject kSubStatusSensor = mps::makeSubject("status_" DEVICE_NAME "_sensor");

// LED
constexpr mps::Subject kSubCmdLedColor = mps::makeSubject("cmd_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubStatusLedColor = mps::makeSubject("status_" DEVICE_NAME "_led_color");

constexpr mps::Subject kSubCmdLedState = mps::makeSubject("cmd_" DEVICE_NAME "_led_state");
constexpr mps::Subject kSubStatusLedState = mps::makeSubject("status_" DEVICE_NAME "_led_state");


// ---- Callbacks ----
// Appelé lors de l'ouverture de la session (callback)
static void onSessionOpened() {
  Serial.println("Session opened!");
  MPS.publishValue(kSubStatusCounter, counter);
  MPS.publishValue(kSubStatusSensor, status_sensor);
  MPS.publishValue(kSubStatusButton, status_button);
  MPS.publishValue(kSubStatusLedColor, led_payload_color);
  MPS.publishValue(kSubStatusLedState, led_payload_state);
}

// Appelé en cas d'échec d'envoi (callback)
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) {
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

static void onCmdReset(const mps::FrameView& message, void*) {
  if (message.payloadLength != 0) return; // ce format n'a pas de payload

  counter.value = 0;
  MPS.publishValue(kSubStatusCounter, counter); // l'IHM voit le résultat réel
}

static void onCmdLedColor(const mps::FrameView& message, void*)
{
  if(!mps::readPayloadAs(message, &led_payload_color)) return; // Copie dans led_payload_color le message s'il est conforme

  // Mettre a jour l'etat de la led
  if (led_payload_color.R == 0 && led_payload_color.G == 0 && led_payload_color.B == 0)
  {
    led_payload_state.state = 0;
  }
  else
  {
    led_payload_state.state = 1;
  }
  
  rgbLedWrite(LED_PIN, led_payload_color.R, led_payload_color.G, led_payload_color.B); // Si la valeur est 0,0,0 la led est eteinte

  MPS.publishValue(kSubStatusLedColor, led_payload_color); // Publier le changement
  MPS.publishValue(kSubStatusLedState, led_payload_state); // Publier le changement
}

static void onCmdLedStatus(const mps::FrameView& message, void*) {
  if (!mps::readPayloadAs(message, &led_payload_state)) return;

  led_payload_state.state = led_payload_state.state != 0;

  if (led_payload_state.state) 
  {
    rgbLedWrite(LED_PIN, led_payload_color.R, led_payload_color.G, led_payload_color.B);
  } else 
  {
    rgbLedWrite(LED_PIN, 0, 0, 0);
  }

  MPS.publishValue(kSubStatusLedState, led_payload_state);
}

// ---- Fonctions appelées par setup/loop ----
// envoi de heartbeats réguliers sans bloquer (comparaison au temps écoulé sans delay())
void publishHeartbeat(){
  static uint32_t lastHeartbeatTimeMs = 0;
  static const uint16_t kHbPeriod = 1000;
  if(millis() - lastHeartbeatTimeMs >= kHbPeriod) 
  {
    lastHeartbeatTimeMs = millis();
    
    // L'envoi de heartbeats depuis loop() assure au récepteur (IHM) que le code "métier" n'est pas planté
    MPS.publishHeartbeat(kSubHb);
  }
}

// ---- Fonctions Arduino ----
// Exécutée une fois au démarrage
void setup() {
  Serial.begin(115200); // Démarrage de la communication série à 115200 bauds (sortie des Serial.print.. par exemple)

  dht.begin();

  MPS.setLogOutput(&Serial); // Définition de la sortie des messages de log de la lib MPS

  pinMode(BUTTON_PIN, INPUT_PULLUP); //Le bouton reliera G3 à GND lorsqu'il est pressé (LOW) et sera HIGH au repos.
  pinMode(LED_PIN, OUTPUT);

  // enregistrement des callbacks
  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);

  MPS.on(kSubCmdReset, onCmdReset);
  MPS.on(kSubCmdLedColor, onCmdLedColor); 
  MPS.on(kSubCmdLedState, onCmdLedStatus);

  // constante d'adresse vide pour vérifier que l'adresse a été définie dans config.h
  static const uint8_t kUnsetMacAddress[6] = {0, 0, 0, 0, 0, 0};
  if(memcmp(kGatewayMacAddress, kUnsetMacAddress, 6) == 0) {
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

// Exécutée en boucle (= while(true))
void loop() {
  // appel de la boucle MPS (/!\ du code bloquant dans votre boucle loop() pourrait occasionner des problèmes de connexion)
  MPS.loop();

  // Appel non bloquant de l'envoi d'HB (polling)
  publishHeartbeat();

  static uint32_t lastCounterTimeMs = 0;
  if (millis() - lastCounterTimeMs >= 2000) {

    // Compteur
    lastCounterTimeMs = millis();
    counter.value++;
    MPS.publishValue(kSubStatusCounter, counter);

    // Capteur
    status_sensor.temperature = dht.readTemperature();
    status_sensor.humidity = dht.readHumidity();
    MPS.publishValue(kSubStatusSensor, status_sensor);
  }
  

  // Envoyer seulement quand l'état du bouton change, spam moins la cible
  if(status_button.value != (digitalRead(BUTTON_PIN) == LOW)) // verifie l'etat du bouton
  {
    status_button.value = digitalRead(BUTTON_PIN) == LOW; // Recupérer l'etat du bouton
    MPS.publishValue(kSubStatusButton, status_button);
  }  
}
