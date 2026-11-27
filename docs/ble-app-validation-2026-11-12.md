# BLE et application : corrections du 12 novembre 2026

Le firmware et le HTML doivent etre utilises ensemble. Aucun site distant n'a ete publie.

- RESET_FAULT fonctionne si le moteur est arrete, aucune operation en cours et le badge retire. Un defaut capteur exige une mesure valide avant acquittement.
- STATUS ajoute scale, schedule et op. La perte de conversions pendant une portion arrete le moteur et verrouille SENSOR.
- TARE/CALIBRATE : ACK signifie acceptation ; DONE;cmd=TARE ou DONE;cmd=CALIBRATE signifie operation terminee. ERR;cmd=...;code=SENSOR/INVALID_CALIBRATION/STORAGE/CANCELLED signale un echec.
- Calibration limitee a 1000 g et ecart brut minimal de 20 comptes ; la valeur doit etre validee sur la cellule reelle. Les tares par portion ne reecrivent plus la NVS ; la calibration conserve le zero et le coefficient.
- Application : commandes serialisees et confirmees, timeout avec deconnexion si resultat inconnu, STOP prioritaire apres fin de la ligne deja commencee, controles inactifs hors connexion, planning relu apres sauvegarde, suppression du dernier repas possible et uniquement apres confirmation.
- Un planning incomplet recu est conserve a part jusqu'a MEALS_END. Les messages et commandes d'une ancienne connexion ne sont pas reutilises.

## Verification

Build ESP32 reussi : flash 492029 octets, RAM statique 28552 octets.
Tests JavaScript reussis : tests/android_protocol_test.js et tests/app_ble_test.js. Le second execute tout le script HTML avec DOM/GATT simules : connexion, statut, tare/calibration, erreur stockage, sauvegarde et suppression du dernier repas, deconnexion.
Sept suites C executees avec succes en WebAssembly/WASI : protocole, scheduler/commandes/securite/NVS, badge, coordinateur, distributeur, poids, timeout/rearmement/perte capteur. Windows bloque les executables de test natifs non signes ; le test RFID utilise setjmp/longjmp et n'a pas ete execute avec cette cible WASI. Le module RFID compile dans le firmware ESP32.

## Utilisation

Ouvrir le HTML mis a jour depuis une adresse HTTPS dans Chrome Android. Connecter CroqChat-ESP32. Le statut est lu avant les fonctions et l'heure est synchronisee si le planning est actif. Faire TARE a vide, puis CALIBRATE avec la masse effectivement posee ; attendre le message de fin. Verifier le poids avant toute distribution.
La coupure BLE ne stoppe pas une portion autonome. Le timeout moteur reste fixe a 30 secondes. Les essais radio, les mesures de precision et le comportement mecanique doivent encore etre controles sur le montage reel.
