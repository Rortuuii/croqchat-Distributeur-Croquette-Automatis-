# Architecture et avancement

## Dépendances et responsabilité

`app_main()` initialise les sorties moteur à zéro, la NVS et les interfaces des blocs, puis appelle `ble_manager_start()`. Les files, commandes et réponses sont encapsulées dans `fb06_ble/ble_commands.c`, traité par `ble_manager_tick()` depuis la boucle principale. ESP-IDF gère ses tâches FreeRTOS et la pile NimBLE. Tous les fichiers de l'application active sont en C ; les bibliothèques fournies par ESP-IDF conservent naturellement leurs propres implémentations.

| Module | Responsabilité prévue | État actuel |
| --- | --- | --- |
| FB01 WeightManager | HX711 GPIO34/25, filtrage, stabilité, tare, calibration NVS | Lecture brute, moyenne, conversion calibrée, filtrage, stabilité et stockage NVS implémentés ; validation matérielle requise |
| FB02 MotorController | ULN2003 GPIO26/27/14/13, demi-pas, sens, vitesse, timeout | Demi-pas implémentés ; le moteur est maintenu à l'arrêt dans `main.c` pendant les essais de la balance |
| FB03 DispenserController | Distribution pondérale et machine à états | États et interface définis ; demandes refusées, état IDLE |
| FB04 SafetyManager | Défauts, STOP, arrêt et mémorisation | STOP réel vers sorties à zéro, code défaut en RAM ; aucune surveillance capteur/timeout |
| FB05 MealScheduler | Six repas, NVS, horloge, autonomie et anti-répétition | Interface uniquement ; liste vide, clock=false, aucune exécution |
| FB06 BLEManager | GATT, fragments, notifications, connexions et diagnostics | Implémenté ; validation sur tablette encore à faire |
| CatIdentification | CR95HF UART RX16/TX17, tags et chats/NVS | Interface uniquement ; UART non initialisé, rfid=none |
| Storage | Initialisation NVS partagée | Réelle ; aucune calibration/repas/tag encore sauvegardé |
| Protocol | Reconstruction LF/CRLF et validation C | Implémenté, indépendant des tâches et pilotes |

Les initialisations des interfaces non réalisées renvoient `ESP_OK` pour indiquer que le module de diagnostic est prêt, tout en journalisant son état **non implémenté**. Leurs fonctions d'action renvoient `ESP_ERR_NOT_SUPPORTED`. Les états retournés sont des valeurs indisponibles explicites, pas des mesures simulées.

## Tâches et priorités

- La tâche hôte NimBLE traite GATT et les événements de connexion. Le callback RX copie chaque fragment dans une file bornée sans attendre son traitement.
- La tâche `croqchat_rx` affiche ASCII/HEX, reconstruit les lignes puis les valide. Les commandes valides sont copiées vers les files de `app_main()`.
- STOP dispose d'une boîte de priorité dédiée, indépendante de la file de commandes courantes. La boucle principale traite STOP et les sécurités avant les autres commandes. STOP annule les commandes encore en attente et maintient les sorties moteur à zéro.
- Les commandes manuelles BLE précèdent l'emplacement réservé aux repas automatiques, puis les futurs ticks de pesée et de régulation. Ces derniers sont actuellement inactifs.
- Les notifications sont envoyées dans la tâche NimBLE par un timer de 25 ms, au maximum 20 octets par notification. Les tâches applicatives ne bloquent pas la réception BLE en attendant la fin d'une réponse.

Chaque connexion possède un numéro de session. Les fragments incomplets et réponses en attente de l'ancienne session sont abandonnés à la déconnexion ; les commandes applicatives périmées ne sont pas exécutées. STOP reste une demande de mise en sécurité même si le téléphone s'est déconnecté après son envoi. Les notifications doivent être réactivées à chaque connexion.

Les files RX, TX et applicatives sont bornées. En cas de perte RX, le flux est abandonné jusqu'à un nouveau LF, pour éviter de traiter une fin de ligne tronquée comme une commande autonome. Une erreur ATT signale le refus d'une écriture avec réponse ; le terminal et TX signalent également la désynchronisation. Une saturation TX est journalisée et ne produit pas de faux ACK de transmission.

## Validation et réponses actuelles

Les clés obligatoires, doublons, clés inconnues et formats sont vérifiés. Les identifiants vont de 1 à 6, les heures sont au format HH:MM, enable vaut 0 ou 1. Les masses doivent être finies, strictement positives et inférieures ou égales à `CROQCHAT_MAX_MASS_GRAMS` (1000 g par défaut, limite de protocole provisoire à adapter au matériel). Epoch est un entier 64 bits strictement positif jusqu'à l'année 9999 ; offset est exprimé en minutes, entre -840 et +840.

`GET_STATUS` et `GET_MEALS` renvoient les données de diagnostic. `TARE` lance une tare non bloquante et `CALIBRATE;mass_ref=...` calibre avec la masse connue déjà posée ; la commande répond ACK si l'opération est acceptée, puis son résultat est visible dans la console série et `GET_STATUS`. Les autres commandes d'action valides restent en `ERR;code=TEST_ONLY`. `DONE` n'est jamais émis, car aucune distribution n'a lieu. Avant calibration, `w=0` n'est pas une mesure de poids ; `fault=0` signifie qu'aucun défaut n'est mémorisé, pas qu'une surveillance matérielle complète a été validée.

## Suite du développement

3. Tester le HX711, la tare et la calibration NVS sur le montage réel. Ne pas réutiliser sans validation l'ancien facteur de calibration Arduino ; la stabilité utilise actuellement une plage de 1 g sur cinq mesures et peut nécessiter un ajustement après essais.
4. Implémenter les demi-pas non bloquants du 28BYJ-48, la vitesse et le timeout dans FB02. Conserver la désactivation des bobines à l'arrêt.
5. Implémenter la machine à états de FB03 et les contrôles de FB04 : pesée initiale, augmentation de masse, arrêt anticipé, stabilisation et résultat final. Employer `NO_FLOW` pour absence d'écoulement, sans conclure automatiquement à un bourrage.
6. Implémenter FB05/NVS, synchronisation BLE et anti-répétition. L'horloge doit rester invalide après redémarrage tant qu'une source fiable ne l'a pas resynchronisée. Un offset UTC fixe ne couvre pas les changements été/hiver : définir une politique de fuseau ou de resynchronisation compatible avec l'application avant la mise en service autonome.
7. Vérifier la documentation du **module CR95HF exact**, son mode UART, ses commandes, son alimentation et son protocole RF avant d'implémenter le pilote. Les tags autorisés identifient le chat sans bloquer les repas, sauf configuration explicite d'un contrôle d'accès.
8. Tester les distributions, erreurs, coupures et connexions successives sur le matériel complet.

Les namespaces NVS de calibration, repas et tags devront être définis et versionnés lors des étapes correspondantes. `storage_init()` n'efface pas automatiquement une NVS incompatible ou pleine : un échec est signalé et le moteur reste à l'arrêt.

## Références techniques

- [ESP-IDF — NimBLE](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/nimble/index.html)
- [Exemple officiel ESP-IDF v6.1 bleprph](https://github.com/espressif/esp-idf/tree/v6.1/examples/bluetooth/nimble/bleprph)

Le CR95HF n'est volontairement pas implémenté à ces étapes ; aucune commande UART supposée n'est envoyée.

Le build minimal conserve les dependances internes imposees par ESP-IDF pour la radio et NimBLE. Leur presence dans les bibliotheques ne demarre pas de connexion Wi-Fi : aucune fonction d'initialisation Wi-Fi, serveur HTTP ou client MQTT n'est appelee par l'application.

Mode actuel : le moteur est désactivé au démarrage pendant la mise au point de la balance. STOP reste prioritaire, coupe les bobines et repond `ACK;cmd=STOP`. GET_STATUS reflete la marche moteur dans busy. Les commandes de distribution restent TEST_ONLY.
