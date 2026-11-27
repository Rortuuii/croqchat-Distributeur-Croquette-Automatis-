# CROQCHAT — Cahier des charges du firmware ESP32

Nous développons un distributeur automatique de croquettes pour chat, dans le cadre du projet N7AE07A – Microprocesseurs.

**Le programme doit être entièrement écrit en langage C avec ESP-IDF**, sur ESP32-WROOM-32 DevKit, dans Visual Studio Code avec PlatformIO.

## 1. Architecture de communication

Le distributeur communique **uniquement avec l'application web Android CroqChat via Bluetooth Low Energy (BLE)**.

Ne pas intégrer :
- Wi-Fi.
- Home Assistant.
- MQTT.
- Serveur Web sur l'ESP32.
- Services cloud.

L'application Android existe déjà. Son protocole BLE doit être respecté.

Le distributeur reste autonome lorsqu'aucun téléphone n'est connecté : il continue à surveiller l'heure et peut exécuter les repas programmés.

## 2. Matériel et broches

| Module | Connexion ESP32 |
|---|---|
| HX711 DOUT | GPIO34 |
| HX711 SCK | GPIO25 |
| ULN2003 IN1 | GPIO26 |
| ULN2003 IN2 | GPIO27 |
| ULN2003 IN3 | GPIO14 |
| ULN2003 IN4 | GPIO13 |
| RFID CR95HF TX | GPIO16 / RX2 |
| RFID CR95HF RX | GPIO17 / TX2 |

Le HX711 et le lecteur RFID doivent utiliser les tensions compatibles avec leurs modules respectifs.

Le moteur est un **28BYJ-48 5 V unipolaire**, piloté par ULN2003, et non un NEMA 17.

## 3. Organisation du programme

Conserver les six blocs fonctionnels.

### FB01 — WeightManager

Gestion de la cellule de charge et du HX711 :
- Lecture du poids.
- Tarage.
- Calibration avec masse de référence.
- Filtrage des mesures.
- Détection de stabilité.
- Détection de défaut capteur.
- Sauvegarde de la calibration dans la mémoire NVS.

### FB02 — MotorController

Commande du moteur 28BYJ-48 :
- Rotation avant et arrière.
- Vitesse configurable.
- Séquence de demi-pas.
- Arrêt immédiat.
- Timeout.
- Désactivation des bobines à l'arrêt.

Les commandes doivent être non bloquantes.

### FB03 — DispenserController

Gestion de la distribution :
- Réception d'une portion cible en grammes.
- Mesure du poids initial.
- Démarrage de la vis sans fin.
- Surveillance de l'augmentation du poids.
- Arrêt anticipé paramétrable.
- Stabilisation de la pesée finale.
- Vérification de la quantité distribuée.
- Gestion des cycles de distribution avec une machine à états.

### FB04 — SafetyManager

Surveillance des défauts :
- Défaillance du HX711.
- Balance non calibrée.
- Portion invalide.
- Temps de fonctionnement excessif.
- Absence d'augmentation de masse.
- Demande STOP prioritaire.
- Erreur pendant la distribution.

Arrêter le moteur et mémoriser le code défaut si nécessaire.

Ne pas considérer une absence d'écoulement comme la preuve certaine d'un bourrage : le réservoir peut être vide.

### FB05 — MealScheduler

Gestion des repas programmés :
- Jusqu'à six repas.
- Heure individuelle.
- Portion en grammes.
- Activation/désactivation.
- Suppression et modification.
- Sauvegarde dans la NVS.
- Exécution autonome.
- Protection contre les distributions répétées.

L'heure est synchronisée depuis le téléphone grâce au BLE.

Tant que l'ESP32 reste alimenté, il peut continuer à compter le temps sans téléphone connecté.

Après une coupure d'alimentation, si aucune source d'heure fiable n'est disponible, attendre une nouvelle synchronisation BLE avant d'exécuter les repas automatiques.

Attention au passage heure d'été/heure d'hiver : un décalage UTC fixe ne suffit pas à le gérer indéfiniment.

### FB06 — BLEManager

Gérer exclusivement la communication Bluetooth avec l'application CroqChat.

Nom du périphérique :

`CroqChat-ESP32`

Service UUID :

`32a44c10-fdd0-4b30-bb4b-5a8900000001`

RX (application vers ESP32) :

`32a44c10-fdd0-4b30-bb4b-5a8900000002`

TX (ESP32 vers application) :

`32a44c10-fdd0-4b30-bb4b-5a8900000003`

Les commandes ASCII sont terminées par un saut de ligne `\n` et peuvent être découpées en fragments de 18 octets.

Implémenter :
- Réception et reconstruction des trames.
- Validation des commandes.
- Traitement des paramètres.
- Envoi de notifications BLE.
- Gestion des connexions et déconnexions.
- Affichage des échanges dans le terminal série à 115200 bauds.

Commandes à reconnaître :

```text
GET_STATUS
GET_MEALS
SET_MEAL;id=1;time=08:00;mass=60;enable=1
DELETE_MEAL;id=1
MANUAL_FEED;mass=20
STOP
TARE
CALIBRATE;mass_ref=100
RESET_FAULT
SET_TIME;epoch=1792150000;offset=120
```

Réponses compatibles avec l'application :

```text
ACK;cmd=SET_MEAL
ERR;code=INVALID_MASS
DONE;grams=19.8
MEALS_BEGIN
MEAL;id=1;time=08:00;mass=60;enable=1
MEALS_END
STATUS;w=0;busy=0;fault=0;sensor=0;cal=0;tare=0;clock=0;rfid=none;last=0
```

Ne transmettre que des états réels et éviter de signaler une action réussie si elle n'a pas eu lieu.

## 4. RFID — CatIdentification

Créer un module indépendant pour le CR95HF.

Il doit :
- Initialiser l'UART.
- Communiquer avec le lecteur.
- Détecter un badge RFID compatible à 13,56 MHz.
- Lire son identifiant.
- Reconnaître les tags enregistrés.
- Associer un identifiant à un chat.
- Transmettre les informations à BLEManager.
- Sauvegarder les identifiants autorisés dans la NVS.

Le RFID sert à identifier le chat. Il ne doit pas interdire automatiquement les repas tant qu'un mode de contrôle d'accès n'a pas été explicitement configuré.

Vérifier le protocole UART et les commandes du module CR95HF à partir de sa documentation technique avant toute implémentation matérielle.

## 5. Programme principal

Créer `main.c` avec `app_main()`.

Centraliser les événements avec les priorités suivantes :

1. STOP et sécurités.
2. Distribution manuelle BLE.
3. Repas automatique.
4. Mesure et régulation de la distribution.
5. Attente.

Ne jamais bloquer la réception BLE pendant la rotation du moteur ou l'acquisition HX711.

L'ESP32 doit rester joignable en BLE lorsqu'il est en attente. Ne pas utiliser le Deep Sleep.

## 6. Contraintes de programmation

- Uniquement C, pas C++.
- Utiliser ESP-IDF, NimBLE pour BLE si compatible.
- Code modulaire en fichiers `.c` et `.h`.
- Utiliser `typedef struct`, `enum`, `stdint.h`, `stdbool.h`.
- Configuration centralisée.
- Gestion des erreurs avec `esp_err_t` lorsque pertinent.
- Affichage de diagnostic avec `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`.
- Pas de boucles bloquantes longues.
- Utiliser la NVS pour les réglages persistants.
- Initialisation des sorties moteur dans un état sûr.

## 7. Développement progressif

Commencer par :

**Étape 1 :** créer le projet ESP-IDF en C, les modules et leurs interfaces.

**Étape 2 :** implémenter le serveur BLE et afficher toutes les commandes reçues depuis l'application dans le terminal série.

**Étape 3 :** intégrer le HX711, puis valider le tarage et la calibration.

**Étape 4 :** intégrer le moteur 28BYJ-48.

**Étape 5 :** créer la distribution automatique pondérale et la gestion des défauts.

**Étape 6 :** intégrer la programmation des repas et la sauvegarde NVS.

**Étape 7 :** intégrer le lecteur RFID.

**Étape 8 :** valider le système complet.

Compiler après chaque étape. Signaler explicitement les fonctions encore simulées ou non testées.

**Commence uniquement par les étapes 1 et 2, sans actionner le moteur, et fournis un programme qui affiche les trames BLE reçues dans le terminal.**