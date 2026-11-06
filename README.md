# Capteur connecté — groupe I

Appareil : `sensor_i`. Carte ESP32-C3, DHT11 sur GPIO0, bouton entre GPIO3 et GND
(`INPUT_PULLUP`), LED RGB sur GPIO2.

## Utilisation

- Ouvrir le croquis dans `Arduino/sensor_i_arduino` avec les bibliothèques MPS du TP et DHT d'Adafruit.
- Renseigner la passerelle dans `config.h`, puis ouvrir `HMI/project.godot` dans Godot 4.7.
- Compteur et mesures : toutes les 2 s ; heartbeat : toutes les secondes ; bouton : à chaque changement.

Convention conservée : macros en `UPPER_SNAKE_CASE`, constantes C++ en `kPascalCase`,
sigles usuels conservés dans les noms (`DHT`, `LED`, `ID`).

## Messages

Entiers en little-endian ; flottants IEEE 754 sur 4 octets. Les offsets et tailles sont en octets.

| Subject | Sens | Champs (offset : type, unité/sens) | Taille |
| --- | --- | --- | --- |
| `hb_sensor_i` | Capteur → IHM | 0 : `state` uint8_t (0 OK, 1 dégradé, 2 défaut) ; 1 : `counter` uint16_t ; 3 : `uptime_ms` uint32_t (ms) | 7 |
| `status_sensor_i_counter` | Capteur → IHM | 0 : `value` uint32_t, nombre d'incréments | 4 |
| `cmd_sensor_i_reset` | IHM → capteur | Vide ; remet le compteur à zéro | 0 |
| `status_sensor_i_button` | Capteur → IHM | 0 : `value` uint8_t, 0 relâché / 1 appuyé | 1 |
| `status_sensor_i_sensor` | Capteur → IHM | 0 : `temperatureDegrees` float (°C) ; 4 : `humidityPercent` float (%) | 8 |
| `set_sensor_i_led_color` | IHM → capteur | 0 : `red` uint8_t ; 1 : `green` uint8_t ; 2 : `blue` uint8_t ; intensités de 0 à 255 | 3 |
| `status_sensor_i_led_color` | Capteur → IHM | 0 : `red` uint8_t ; 1 : `green` uint8_t ; 2 : `blue` uint8_t ; intensités de 0 à 255 | 3 |
| `cmd_sensor_i_led_state` | IHM → capteur | 0 : `state` uint8_t, 0 éteindre / non nul allumer | 1 |
| `status_sensor_i_led_state` | Capteur → IHM | 0 : `state` uint8_t, 0 éteinte / 1 allumée | 1 |

Les `cmd_` et `set_` demandent un ACK ; le capteur confirme par un `status_`.
Le noir éteint la LED ; la commande d'état conserve la couleur mémorisée.
L'état est republié à chaque ouverture de session. Les tailles incorrectes sont ignorées.
Sans heartbeat pendant 3,5 s, les valeurs sont grisées et les commandes désactivées.
