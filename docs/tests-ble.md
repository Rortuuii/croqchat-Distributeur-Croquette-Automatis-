**Mode moteur actuel :** le moteur reste désactivé au démarrage pour les essais de la balance. STOP reste prioritaire et les sorties moteur restent à zéro.

# Essais BLE — étapes 1 et 2

## Préparer le test réel

1. Compiler puis téléverser avec PlatformIO, cible `esp32dev`.
2. Ouvrir le moniteur COM5 à **115200 bauds**. Appuyer sur EN/RESET pour revoir la bannière.
3. Vérifier les avertissements « non initialisé/non implémenté » des modules et les GPIO ULN2003 maintenus à zéro.
4. Sur la tablette, ouvrir CroqChat BLE dans Chrome Android depuis HTTPS, activer le Bluetooth et toucher le bouton de connexion. Choisir `CroqChat-ESP32`.
5. Activer les notifications TX avant d'envoyer la première commande RX. Aucune association Bluetooth classique préalable n'est nécessaire.

| Élément | Valeur |
| --- | --- |
| Service | `32a44c10-fdd0-4b30-bb4b-5a8900000001` |
| RX, WRITE avec réponse ou sans réponse | `32a44c10-fdd0-4b30-bb4b-5a8900000002` |
| TX, NOTIFY | `32a44c10-fdd0-4b30-bb4b-5a8900000003` |

Le firmware accepte les fragments de 18 octets de l'application. Chaque ligne doit se terminer par LF (`0A`) ou CRLF (`0D 0A`). Le terminal affiche chaque fragment en ASCII échappé, ses octets HEX, la ligne complète et les paramètres, même si leur validation échoue ensuite. Le nom complet du périphérique est placé dans la réponse au scan et l'UUID du service dans l'annonce principale.

## Réponses attendues depuis les boutons CroqChat

| Commande | Réponse attendue |
| --- | --- |
| `GET_STATUS` | `ACK;cmd=GET_STATUS`, puis une ligne `STATUS` avec les dernières valeurs réelles ; avant toute mesure, `sensor=0;cal=0;tare=0` |
| `GET_MEALS` | `ACK;cmd=GET_MEALS`, puis `MEALS_BEGIN`, puis `MEALS_END` |
| `SET_MEAL;id=1;time=08:00;mass=60;enable=1` | Paramètres affichés puis `ERR;code=TEST_ONLY` |
| `DELETE_MEAL;id=1` | `ERR;code=TEST_ONLY` |
| `MANUAL_FEED;mass=20` | Masse affichée puis `ERR;code=TEST_ONLY` |
| `STOP` | Arrêt prioritaire, sorties à zéro, puis `ACK;cmd=STOP` |
| `TARE` | `ACK;cmd=TARE` si accepté ; attendre le journal série `Tare terminee` |
| `CALIBRATE;mass_ref=100` | Après tare et pose de la masse de 100 g : `ACK;cmd=CALIBRATE`, puis journal série `Calibration terminee` |
| `RESET_FAULT` | `ERR;code=TEST_ONLY` |
| `SET_TIME;epoch=1792150000;offset=120` | Valeurs exactes affichées puis `ERR;code=TEST_ONLY`, clock reste 0 |

L'application doit assembler les notifications jusqu'à LF. Elles contiennent au plus **20 octets** chacune, espacées de 25 ms. Attendre les réponses avant de lancer un autre essai pour distinguer les résultats. `ACK` pour TARE/CALIBRATE signifie que l'opération a été acceptée, pas qu'elle est déjà terminée. Aucun `DONE` n'est émis, car aucune distribution n'a lieu.

## Essai de la balance

1. Vérifier le câblage et la tension logique du HX711 : GPIO34 reçoit DOUT, GPIO25 reçoit SCK, masses communes. Le niveau de DOUT ne doit jamais dépasser la tension admissible de l'ESP32 ; vérifier la documentation du module HX711 utilisé.
2. Vérifier que la balance est vide, immobile et que le moteur ne tourne pas. Connecter CroqChat et envoyer `TARE`. Attendre `Tare terminee` dans le moniteur série.
3. Poser une masse connue, sans toucher la balance, puis envoyer par exemple `CALIBRATE;mass_ref=100` pour une masse de 100 g. Attendre le journal `Calibration terminee`.
4. Envoyer `GET_STATUS` : attendre `sensor=1;cal=1;tare=1`. `w` doit refléter le poids posé. Retirer puis reposer la masse pour vérifier le retour proche de zéro et la répétabilité.
5. Redémarrer sans effacer la NVS et vérifier que `cal=1;tare=1` est conservé.

Si `CALIBRATE` répond `NOT_TARED`, refaire la tare à vide. Une tare ou calibration sans mesures HX711 est abandonnée après 10 secondes ; vérifier alors le câblage et le signal DOUT. Si la console signale une erreur de sauvegarde NVS, ne pas considérer la calibration comme persistante. La tolérance de stabilité est initialement fixée à 1 g sur cinq lectures ; elle reste à confirmer et peut nécessiter un réglage selon la cellule et le montage mécanique.

## Robustesse, si un envoi brut est disponible

Les `\n` et `\r` du tableau désignent de vrais octets LF/CR, pas des caractères littéraux antislash + lettre. Les longues lignes sont à envoyer par écritures de 18 octets maximum.

| Entrée | Résultat attendu |
| --- | --- |
| `MANUAL_`, puis `FEED;mass=20`, puis LF | Rien avant LF, puis une seule commande valide |
| `STOP\nTARE\n` en une écriture | Deux lignes affichées ; STOP est prioritaire et peut annuler TARE déjà en attente (`CANCELLED`) |
| `GET_STATUS\r`, puis LF | GET_STATUS valide |
| `\n\r\n` | Lignes vides ignorées |
| `SET_MEAL;id=6;time=23:59;mass=0.1;enable=0\n` | Commande valide, TEST_ONLY |
| `SET_MEAL;id=7;time=08:00;mass=60;enable=1\n` | INVALID_ID |
| `SET_MEAL;id=1;time=24:00;mass=60;enable=1\n` | INVALID_TIME |
| `SET_MEAL;id=1;time=08:00;mass=60\n` | MISSING_PARAM |
| `MANUAL_FEED;mass=-1\n` ou `mass=nan` | INVALID_MASS |
| `MANUAL_FEED;mass=0\n` ou `mass=1001` | INVALID_MASS (limite configurable : 1000 g) |
| `MANUAL_FEED;mass=20;mass=30\n` | DUPLICATE_PARAM |
| `MANUAL_FEED;mass=\n` | BAD_FORMAT |
| `GET_STATUS;x=1\n` | UNKNOWN_PARAM, x tout de même affiché |
| `INCONNUE;x=abc\n` | UNKNOWN_COMMAND, x affiché |
| `SET_TIME;epoch=2200000000;offset=-60\n` | Epoch conservé en 64 bits et affiché, TEST_ONLY |
| 256 caractères X puis LF | Ligne acceptée, UNKNOWN_COMMAND |
| 257 caractères X puis `\nGET_STATUS\n` | RX_OVERFLOW, puis GET_STATUS correctement traité |
| Octet `00`, puis `\nGET_STATUS\n` | BAD_ASCII, puis GET_STATUS correctement traité |
| Ligne partielle, déconnexion, reconnexion, `GET_STATUS\n` | Pas de mélange entre les sessions |

Le tampon de 256 octets compte un éventuel CR avant LF. Un CR interne, suivi d'un caractere autre que LF, provoque BAD_ASCII et abandonne la ligne jusqu'au prochain LF.

## Connexions et saturation

Déconnecter/reconnecter cinq fois et vérifier la relance de la publicité et le fonctionnement de GET_STATUS à chaque fois. Réactiver systématiquement les notifications côté application. Vérifier que la réponse STATUS, plus longue que 20 octets, est bien reconstruite dans l'application.

Les limites de files sont centralisées dans `include/croqchat_config.h`. Si RX sature, une écriture avec réponse peut recevoir une erreur ATT et le terminal/TX signale `RX_QUEUE_FULL`. Envoyer LF pour resynchroniser, attendre la réponse puis renvoyer la commande complète. Une commande tronquée ne doit jamais être traitée comme une nouvelle demande. Si la file applicative sature, elle répond `COMMAND_QUEUE_FULL` ; la boîte STOP reste séparée. Une saturation TX est journalisée et peut perdre une réponse : réduire la cadence et renvoyer une commande de lecture pour vérifier l'état.

La compilation ne prouve pas le fonctionnement radio ou électrique. Les connexions Android, pertes de paquets, reconnexions et niveaux physiques des sorties restent à valider sur le montage réel. Le moteur ne doit effectuer aucun mouvement lors de tous ces essais.

Référence : [Chrome — Web Bluetooth, HTTPS et geste utilisateur](https://developer.chrome.com/docs/capabilities/bluetooth).

## Tests C portables du protocole

`tests/protocol_test.c` teste la reconstruction, les limites, la resynchronisation, la validation et les entiers epoch 64 bits, sans matériel. Avec un compilateur C natif disponible (GCC ou Clang), depuis la racine :

```sh
cc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/protocol src/protocol/ble_protocol.c tests/protocol_test.c -o protocol_test
./protocol_test
```

Résultat attendu : `Protocol tests: PASS`. Le compilateur Xtensa de PlatformIO permet aussi de vérifier la syntaxe de ces tests, mais ne permet pas de les exécuter sur le PC. Les essais radio et électriques du présent document restent indispensables.
