# Vérification avant essais — 12 novembre 2026

## Verdict

Compilation ESP32 réussie. Essais élémentaires supervisés possibles, mais distribution pesée à ne pas considérer comme validée : plusieurs défauts de surveillance et de calibration restent à corriger. Aucun téléversement ni essai physique effectué. Le firmware et ses modes n'ont pas été modifiés pendant cette revue.

## Vérifications effectuées

Lecture de tous les modules C actifs, configuration PlatformIO/ESP-IDF, transport de l'application HTML et tests existants.
Commande exécutée : pio run -e esp32dev (via l'exécutable du dossier .platformio).
Résultat : SUCCESS, 69,79 s ; aucune alerte de compilation observée sur les sources recompilées.
RAM statique : 28 536 / 327 680 octets (8,7 %).
Flash applicative : 490 785 / 1 048 576 octets (46,8 %).
Les dépendances Python ESP-IDF manquantes ont été installées automatiquement par PlatformIO.
Les tests C natifs et JavaScript n'ont pas été exécutés : clang, gcc et node ne sont pas accessibles dans le PATH ; aucun exécutable Zig trouvé dans son cache local. La compilation ESP32 ne remplace pas ces tests.

## Anomalies prioritaires

### 1. Perte du HX711 non détectée après une première lecture — élevé

src/fb01_weight/weight_manager.c:156-157 : sensor_ready passe à true à la première lecture et ne repasse jamais à false. Aucun âge de mesure n'est conservé. Si les conversions cessent pendant la distribution, le distributeur continue avec l'ancien poids. Le timeout global de 30 s finit par arrêter, mais la portion peut être dépassée avant cela. Une valeur NaN pendant une distribution n'est pas non plus rejetée dans dispenser_controller_tick.
À corriger : horodater les lectures, invalider les mesures périmées et arrêter/verrouiller un défaut capteur pendant une distribution.

### 2. Calibration nulle ou aberrante acceptée — élevé

src/fb01_weight/weight_manager.c:169-170 : le coefficient calculé est enregistré et calibrated=true sans contrôle. Une calibration sans ajouter la masse connue peut produire un coefficient nul, puis une division par zéro ; un écart trop faible produit un poids très amplifié. Le coefficient rechargé depuis NVS est seulement comparé à zéro, sans vérification de finitude ni de taille du bloc chargé.
À corriger : valider la référence et l'écart mesuré, refuser un coefficient non fini/nul/trop faible selon le capteur ; conserver la dernière calibration valide en cas d'échec. Un coefficient négatif peut être normal selon le câblage et ne doit pas être rejeté sur son seul signe.

### 3. Erreurs de mémoire ignorées pour la balance — moyen

src/fb01_weight/weight_manager.c:46-52 et 76-81 : les retours nvs_open/get_blob/set_blob/commit ne sont pas contrôlés. Une calibration peut sembler sauvegardée puis disparaître au redémarrage ; une lecture invalide n'est pas distinguée d'une absence de réglages. Une tare avant chaque distribution entraîne également une sauvegarde lorsque la balance est calibrée.
À corriger : contrôler les retours, journaliser les échecs et distinguer réglage actif et sauvegarde réussie ; évaluer si le zéro de chaque portion doit réellement être permanent.

### 4. Réarmement de défaut absent — moyen

RESET_FAULT est reconnu par le parseur et envoyé par le bouton de l'application HTML, mais ble_commands.c:121-124 retourne ERR;code=TEST_ONLY. Après un timeout, aucun chemin ne remet le défaut à zéro hormis safety_manager_init au redémarrage.
À corriger : implémenter un réarmement avec conditions sûres ou rendre ce bouton indisponible. Pour les essais actuels : retirer le badge, contrôler le montage puis redémarrer ; resynchroniser ensuite l'horloge.

## Comportements à connaître avant le test

- main.c : mode_test=false, automatique=true et badge manuel=true. Les options tester_balance/tester_rfid sont donc sans effet sur ce démarrage normal. Toute carte détectée peut commander le moteur après tare ; aucun filtrage UID.
- Le mode badge distribue tant que la carte est présente, sans consigne de masse et sans obligation de calibration valide ; la tare et le timeout restent ses protections. Ce comportement est distinct d'une portion pesée.
- Le timeout est provisoirement de 30 s, pauses de dosage comprises, hors tare. La tare expire après 10 s. La phase fine fonctionne par impulsions de 100 ms séparées de pauses de 500 ms : vérifier qu'une petite portion peut finir dans les 30 s.
- Le moteur commande au maximum 500 demi-pas/s. La cadence réelle dépend de la fréquence de la boucle et des traitements, notamment des écritures NVS et journaux ; la compilation ne garantit ni débit ni couple.
- Le RFID rejette une présence vieille de 600 ms. Le retrait n'est pas un arrêt instantané : vérifier la latence réelle, les pertes de lecture et l'absence de redémarrages intempestifs pendant la présence d'une carte.
- Le BLE dispose d'une file STOP dédiée au niveau commandes. Cela ne garantit pas un STOP reçu si le lien radio ou la file de fragments RX est saturé. La déconnexion du téléphone n'arrête pas automatiquement une portion.
- Le planning est sauvegardé mais l'heure est perdue au redémarrage. Synchronisation requise ; validité de 7 jours, décalage horaire fixe. Repas refusés pour occupation/sécurité non rattrapés ; deux repas simultanés ne sont pas distribués à la suite. Un repas pris en charge est marqué avant la tare, donc un échec ne le relance pas le même jour.
- Les défauts SENSOR et NO_FLOW existent dans l'énumération, mais leur détection n'est pas implémentée. Une vis bloquée ou un réservoir vide s'arrête actuellement par timeout.
- platformio.ini utilise COM4, tandis que docs/test-balance.md mentionne COM5. Vérifier le port réel. Cette fiche indique aussi à tort que l'application est absente du dépôt.

## Points cohérents à la lecture

- Sorties moteur mises à zéro dès app_main, avant l'initialisation des périphériques.
- Tare automatique identifiée par un compteur de réussite ; une ancienne tare ne valide pas la nouvelle demande.
- Tare/calibration interdites pendant une rotation ; STOP annule une tare de distribution.
- Priorité des arrêts et défaut verrouillé après timeout ; pauses de dosage incluses dans la surveillance.
- Protection du snapshot RFID partagé entre tâches et péremption de présence.
- Parseur borné, validation des valeurs finies et limites, rejet des doublons et resynchronisation sur saut de ligne.
- Réponses BLE associées aux sessions, notifications fragmentées ; l'application active TX avant ses commandes.
- Planning avec sauvegarde contrôlée et protection contre un double déclenchement quotidien.
Ces constats sont issus du code, sans validation physique ni exécution des tests unitaires.

## Ordre conseillé des essais sur le montage

1. Pour isoler les premiers essais, choisir mode_test=true, tester_moteur=false, tester_balance=true, tester_rfid=true ; activer tester_ble si la calibration doit se faire depuis l'application. Recompiler et téléverser explicitement après modification. Dans ce mode le badge ne déclenche pas le moteur.
2. Vérifier le câblage : HX711 DOUT=34/SCK=25 ; pilote moteur IN1=26/IN2=27/IN3=14/IN4=13 ; RFID RX ESP32=16/TX ESP32=17. Valider alimentation, masses communes et pilote avec le montage réel.
3. Balance immobile et vide : TARE, attendre la fin des 20 mesures ; poser une masse connue, CALIBRATE;mass_ref=100 uniquement si 100 g sont réellement posés. Contrôler zéro, masse de référence, une seconde masse, répétabilité et conservation après redémarrage. ACK signifie seulement demande acceptée.
4. RFID seul : vérifier UID stable pour les cartes utilisées, retrait, reconnexion du lecteur et badge absent au démarrage. Ne pas lancer de test moteur pendant ces vérifications.
5. Moteur : essai bref sous surveillance ; vérifier sens de rotation et alimentation, arrêt au retrait du badge, STOP pendant la tare et pendant la rotation. Le mode tester_moteur continu n'initialise pas le BLE : STOP depuis le téléphone n'y est pas disponible.
6. Après correction des défauts de balance, essayer de petites portions puis des portions usuelles ; peser indépendamment la quantité réellement sortie et relever erreur/dispersion/durée. Tester aussi avec un bol contenant déjà des croquettes : la consigne est la quantité ajoutée grâce à la tare préalable.
7. Vérifier le timeout sur un essai contrôlé sans provoquer de blocage mécanique dommageable ; constater défaut verrouillé et absence de redémarrage tant qu'il est actif. Ne pas interpréter le bouton RESET_FAULT comme fonctionnel actuellement.
8. Programmer un repas quelques minutes à l'avance après synchronisation ; vérifier déclenchement unique, STOP, repas pendant occupation, restauration du planning et absence de distribution avant resynchronisation après coupure.
9. BLE : connexion, notifications, statut, reconnexion, commandes invalides et STOP ; confirmer les résultats sur les journaux série et les sorties réelles.

## Tests à compléter après correction

Ajouter des cas ciblés aux tests existants : perte de conversions en cours de portion, poids non fini, calibration sans masse ajoutée, coefficient NVS invalide et erreurs NVS, puis réarmement réel du défaut. Exécuter tests/run_native.ps1 avec un compilateur natif et tests/android_protocol_test.js avec Node ; conserver les résultats avant validation finale.
