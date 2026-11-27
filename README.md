# CroqChat — firmware ESP32

Firmware C / ESP-IDF / NimBLE pour le distributeur de croquettes ESP32, compile avec PlatformIO.
La communication avec l'application utilise le BLE.

## Fonctionnement actuel

Les modes sont regles en haut de src/main.c : le mode test est desactive,
le mode automatique et le mode manuel par carte sont actifs.

- Distribution d'une portion pesee et repas programmes avec sauvegarde NVS.
- Mode manuel avec toute carte RFID detectee, sans filtrage UID.
- Tare automatique avant les distributions pesees. Le mode badge tourne sans tare et s'arrete 2 s apres la derniere lecture valide.
- Commandes BLE de tare, calibration, planning, distribution et STOP.
- Protection par timeout moteur et verrouillage des defauts.

La calibration et la validation sur le montage reel restent necessaires.
Voir [le mode manuel](docs/manual-badge.md), [le planning](docs/meal-scheduler.md)
et [les essais balance](docs/test-balance.md).
## Compilation, téléversement et test

Dans le terminal PlatformIO, à la racine du projet :

```powershell
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor --port COM4 --baud 115200
```

Dans VS Code, les tâches PlatformIO **Build**, **Upload**, **Monitor** donnent le même résultat. Fermer le moniteur avant Upload. Si nécessaire, maintenir BOOT pendant `Connecting...`, puis relâcher dès le début de l'écriture. Le programme n'est pas téléversé automatiquement par la réorganisation du projet.

Les fichiers générés sont placés dans `%LOCALAPPDATA%/CroqChat/build`, car PlatformIO/ESP-IDF refuse les espaces dans le chemin de compilation. Les sources restent dans Nextcloud.

Le premier build installe ESP-IDF et ses outils ; il est plus long que les suivants. La plateforme est fixée à `espressif32@7.1.3` pour conserver la cohérence des dépendances. Le débit série est 115200 et le port configuré est COM4 ; l'adapter si Windows le change.

Ouvrir CroqChat BLE en **HTTPS** dans Chrome Android, activer le Bluetooth, toucher le bouton de connexion et choisir **CroqChat-ESP32**. L'application doit activer les notifications TX avant d'envoyer une commande RX. Voir la [procédure de test BLE](docs/tests-ble.md).

## Organisation

```text
platformio.ini                 Cible et outils ESP-IDF
CMakeLists.txt                 Projet ESP-IDF
sdkconfig.defaults             NimBLE, série, FreeRTOS
include/croqchat_config.h      Broches, UUID, tailles et limites
src/main.c                    app_main, files d'événements et priorités
src/common/                   Initialisation NVS partagée
src/protocol/                 Parseur et reconstruction de lignes, C indépendant de l'IDF
src/fb01_weight/              FB01 — WeightManager
src/fb02_motor/               FB02 — MotorController
src/fb03_dispenser/           FB03 — DispenserController
src/fb04_safety/              FB04 — SafetyManager
src/fb05_scheduler/           FB05 — MealScheduler
src/fb06_ble/                 FB06 — BLEManager / NimBLE
src/cat_identification/       Interface RFID CR95HF indépendante
```

Les scripts tools/ servent aux essais BLE et a la visualisation des mesures.

Lire [l'architecture et les étapes suivantes](docs/architecture.md) avant de compléter les modules.

Résultats et limites des vérifications : [compte rendu de validation](docs/validation.md).

[Brochage et alimentation des modules](docs/brochage.md)
