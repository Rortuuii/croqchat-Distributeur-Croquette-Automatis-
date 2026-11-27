# Brochage CroqChat

Le firmware cible une carte ESP32 DevKit (`esp32dev` dans PlatformIO).
Les numéros ci-dessous sont les GPIO de l’ESP32. Les affectations sont
définies dans [croqchat_config.h](../include/croqchat_config.h).

| Module | Broche du module | Connexion |
| --- | --- | --- |
| HX711 | VCC | **3,3 V** de l’ESP32 |
| HX711 | GND | Masse commune |
| HX711 | DT / DOUT | GPIO34 |
| HX711 | SCK / CLK | GPIO25 |
| ULN2003 | IN1 | GPIO26 |
| ULN2003 | IN2 | GPIO27 |
| ULN2003 | IN3 | GPIO14 |
| ULN2003 | IN4 | GPIO13 |
| ULN2003 | VCC | Alimentation 5 V du moteur 28BYJ-48 |
| ULN2003 | GND | Masse commune |
| CR95HF en mode UART | TX | GPIO16 (RX de l’ESP32) |
| CR95HF en mode UART | RX | GPIO17 (TX de l’ESP32) |
| CR95HF en mode UART | GND | Masse commune |

Le moteur 28BYJ-48 se branche sur le connecteur du module ULN2003. Le
lecteur RFID communique à 57 600 bauds. Utiliser la tension d’alimentation
indiquée sur le module CR95HF utilisé.

Relier la cellule de charge aux bornes du HX711 selon le repérage de la
cellule (`E+`, `E−`, `A+`, `A−` pour une cellule en pont à quatre fils).
