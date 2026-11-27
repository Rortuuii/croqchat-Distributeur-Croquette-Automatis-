# FB05 MealScheduler

## Resultats sur ESP32 du 4 novembre 2026

- Firmware compile et televerse sur COM3, modes moteur continu et RFID seul desactives.
- Tests C natifs (scheduler, parseur, commandes, NVS simulee, autorisations) : PASS.
- Vraies fonctions JavaScript du HTML, fragmentation et reception des repas/STATUS : PASS.
- Radio BLE reelle depuis Windows : creation d'un repas desactive, lecture exacte,
  modification heure/portion, deconnexion et redemarrage, relecture NVS : PASS.
- GET_STATUS apres redemarrage : clock=0 ; SET_TIME acquitte puis clock=1 : PASS.
- DELETE_MEAL acquitte et absence du slot dans GET_MEALS : PASS.
- Le test radio conserve un repas desactive : aucun moteur actionne.
- La premiere reconnexion Windows a echoue sur l'abonnement GATT ; le client de test
  recharge maintenant les services sans cache et ferme explicitement les notifications.
  L'essai complet suivant a reussi.
- Restent a verifier : gestes dans Chrome Android et distribution physique programmee,
  notamment masse, fin, STOP et concurrence avec une distribution manuelle sur montage reel.

Firmware C / ESP-IDF. `main.c` appelle le coordinateur `common/app_controller.c`,
qui initialise la NVS, le planning, le lecteur RFID puis le BLE.
Les modes de test moteur et RFID sont desactives par defaut. Le RFID est aussi lu
en mode normal pour la [commande manuelle par badge](manual-badge.md), independante du planning.

## Protocole de l'application existante

Application analysee : `Applicationde contrôle du distributeur.html` (nom reel du fichier fourni).
Son JavaScript synchronise l'heure a la connexion, puis demande STATUS et GET_MEALS.
Les ecritures sont fragmentees en 18 octets ; les notifications sont reconstituees jusqu'au LF.
Le transport existant reste inchange, UUID identiques.

Commandes implementees :

```text
SET_TIME;epoch=1792150000;offset=120
SET_MEAL;id=1;time=08:00;mass=60;enable=1
DELETE_MEAL;id=1
GET_MEALS
GET_STATUS
```

ACK n'est envoye pour une modification qu'apres sauvegarde NVS reussie.
Erreurs : INVALID_MEAL (portion hors 1..200 g), STORAGE (NVS indisponible),
et erreurs de format deja produites par le parseur. GET_MEALS envoie ACK,
MEALS_BEGIN, les seuls slots existants (y compris desactives), puis MEALS_END.
STATUS.clock indique la validite de l'heure ; STATUS.last reste une masse en grammes,
conformement au JavaScript, et represente la derniere distribution terminee depuis le demarrage.
L'application actualise STATUS toutes les 3,5 secondes : busy et last suivent la distribution.
Le HTML n'est pas modifie. Son interface ne permet pas de supprimer le dernier repas
(bouton masque quand il n'en reste qu'un), mais DELETE_MEAL fonctionne aussi pour ce cas.
Une modification d'heure/portion/activation dans l'interface doit etre transmise avec son bouton
d'enregistrement ; les bascules ne sont pas envoyees immediatement par le JavaScript existant.

## Temps et echeances

UTC du telephone + offset local en minutes + temps ecoule via esp_timer_get_time().
L'horloge n'est jamais restauree de NVS et reste inconnue apres redemarrage.
Chaque SET_TIME remplace la reference et annule les echeances en attente : aucun
repas anterieur ou egal a l'instant de synchronisation n'est rattrape.
L'execution accepte moins de 60 secondes de retard de boucle, jamais plusieurs heures.
Un repas cree/modifie apres son heure attend le lendemain.

Le decalage fixe ne predit pas les changements ete/hiver : resynchroniser a ces changements,
apres redemarrage et au plus tard tous les sept jours. Apres sept jours sans synchronisation,
clock=0 et les repas automatiques sont suspendus. La deconnexion seule ne change rien.

## Distribution et concurrence

Le scheduler expose une echeance (`update`, `take_due`). Main consulte le SafetyManager
(absence de defaut, capteur pret, tare/calibration, poids fini, pas de tare ni distribution en cours).
Si autorise, `claim` sauvegarde d'abord la date en NVS, puis main appelle FB03 une fois.
FB05 ne commande ni GPIO, ni moteur, ni pile BLE. Le resultat FB03 DONE/STOP/FAULT
est suivi par `report`, et journalise avec l'identifiant du repas.

Avant chaque demande en grammes (repas ou MANUAL_FEED), FB03 lance une tare de
20 lectures HX711 et reste STARTING/busy, moteur arrete. Il attend une nouvelle tare
reussie puis prend cette mesure comme zero de depart avant de commencer les demi-pas.
Une ancienne tare ne suffit pas : un compteur de tares distingue la nouvelle operation.
STOP annule la preparation. Apres 10 secondes sans tare terminee, la demande est
annulee (FAULT et journal serie), sans demarrer le moteur. Le badge manuel fait
egalement sa tare avant chaque nouvel episode de rotation ; le test moteur continu
reste un test direct du moteur, sans tare.

Si une distribution manuelle/programmee est en cours, ou une securite refuse, le repas
est ignore pour cette echeance, sans file ni nouvelle tentative automatique. Si plusieurs
repas ont exactement la meme heure, le plus petit ID autorise est lance ; les autres sont ignores.
Une commande manuelle est egalement refusee lorsqu'une distribution est deja active.
STOP prioritaire annule les demandes BLE en attente et empeche un lancement automatique dans
la meme iteration. Un repas deja lance reste marque pour le jour, meme s'il est interrompu.

La date est une borne haute par ID : avancer puis reculer l'horloge ne rejoue pas les dates
anterieures. Modifier, supprimer puis recreer le meme ID conserve cette protection.
La reservation est persistante AVANT lancement : une coupure entre sauvegarde et commande
peut faire manquer ce repas, mais ne peut pas le distribuer deux fois. La sauvegarde ne se
fait qu'aux modifications/suppressions et aux lancements, jamais toutes les secondes.
Le blob NVS est versionne et valide au chargement ; un schema inconnu ou une entree invalide
desactive le planning sans effacer les autres donnees du distributeur.

FB04 surveille maintenant la duree de chaque occupation moteur : maximum provisoire
de 30 secondes apres la tare, pauses de distribution comprises, meme si le poids ne change plus.
Le delai est regle par `CROQCHAT_TIMEOUT_MOTEUR_MS` dans `include/croqchat_config.h`.
**RAPPEL : ajuster ce delai apres essais de debit et des portions maximales.**
L'expiration arrete le moteur et verrouille SAFETY_FAULT_TIMEOUT (STATUS.fault=4)
jusqu'au redemarrage. Le test moteur continu et le mode badge sont aussi surveilles.
La detection immediate d'un capteur stale, d'un blocage ou d'une absence de debit
reste a implementer ; l'arret temporel constitue la limite maximale de fonctionnement.

## Verification reproductible, sans moteur

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_native.ps1 -Compiler clang
node tests/android_protocol_test.js
pio run -e esp32dev
```

`run_native.ps1` accepte aussi le chemin de zig.exe (utilise alors zig cc).
Les vrais fichiers C du scheduler, du parseur, des commandes BLE et du SafetyManager
sont executes avec timer, NVS, queues et distributeur simules. Scenarios : creation,
modification, desactivation, suppression, six slots, lecture exacte des reponses,
deconnexion, persistance, horloge inconnue, resynchronisation, echeance avec leger retard,
anti-doublon apres reboot/modification/recreation, recul du temps, minuit/offset,
expiration, echec de commit, blob invalide, refus manuel concurrent et STOP.
Le test JS execute les fonctions extraites du vrai HTML (FeederBLE, parseMessage,
onBleLine) et verifie repas, STATUS et fragmentation 18/20 octets.

Pour la radio reelle (firmware deja televerse), fermer le moniteur serie :

```powershell
python -m pip install bleak pyserial
python tools/test_meals_ble.py --reboot-port COM3
```

Ce test n'utilise qu'un slot libre DESACTIVE : creation, modification, lecture,
redemarrage NVS/clock=0, synchronisation si aucun repas existant n'est actif,
suppression et restauration de la liste initiale. Aucun MANUAL_FEED n'est envoye.
Il s'arrete si les six slots sont occupes. Il ne pretend pas valider les clics
de Chrome Android ni une distribution physique : presenter ces essais separement.

Validation sur Android : connecter l'ESP32 en HTTPS, verifier clock=1, enregistrer
deux repas, actualiser/reconnecter pour relire, modifier et enregistrer, desactiver
et enregistrer, supprimer. Apres reboot, verifier les donnees puis synchroniser.
Pour le vrai moteur, tarer/calibrer et programmer un repas une minute dans le futur :
observer detection, demande, busy, fin et masse last ; verifier sans telephone,
puis concurrence manuelle et absence de deuxieme declenchement. Ces essais materiels
doivent etre distingues des tests logiciels et du test radio avec repas desactive.
