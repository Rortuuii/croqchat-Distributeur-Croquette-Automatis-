# Validation du 17 octobre 2026

- Plateforme : espressif32 7.1.3 ; framework : ESP-IDF 6.1.0 ; cible : esp32dev.
- Étape 1 (interfaces et app_main en C) : compilation complète **SUCCESS** (journal historique supprimé lors du nettoyage).
- Étape 2 (serveur NimBLE et diagnostic de commandes) : compilation complète **SUCCESS** (journal historique supprimé lors du nettoyage).
- Firmware final : 452813 octets de flash (43,2 % de la partition applicative) et 28016 octets de RAM statique (8,5 %). Les allocations dynamiques des tâches et de la pile BLE s'ajoutent à cette valeur de RAM statique.
- Parseur et tests C : vérification de syntaxe avec `-std=c11 -Wall -Wextra -Werror` réussie. Les tests portables ne sont pas exécutés sur ce PC, aucun compilateur C natif n'y ayant été détecté.
- UUID service/RX/TX : décodage des constantes little-endian NimBLE vérifié, valeurs conformes au cahier des charges.
- Aucun fichier C++ dans les sources actives `src/`. Les anciennes versions Arduino ont été supprimées lors du nettoyage.
- Le binaire définit `app_main` et `ble_manager_init` ; il ne définit ni `esp_wifi_init`, ni `httpd_start`, ni `esp_mqtt_client_start`.
- Aucun téléversement, mouvement moteur ou essai de connexion Android effectué pendant cette réorganisation.

Le fonctionnement radio, la réception depuis CroqChat Android, les notifications et les reconnexions restent à vérifier avec le montage réel suivant `docs/tests-ble.md`. Les étapes matérielles 3 à 8 ne sont pas réalisées ni simulées.

## Compatibilité Windows / Nextcloud

Le dossier de sources reste inchangé. Les artefacts de compilation sont placés dans `%LOCALAPPDATA%/CroqChat/build` pour éviter les espaces dans le chemin de build ESP-IDF. Le masquage des chemins des macros de diagnostic est désactivé afin d'éviter un argument `-fmacro-prefix-map` mal échappé par PlatformIO. Aucun changement des permissions Windows n'est nécessaire.
