# Mode manuel par badge RFID

Le fonctionnement normal execute le BLE, la balance, le planning et la commande
manuelle par badge dans la meme boucle. `main.c` choisit uniquement le mode et
appelle `app_controller_init(&fonctions)` puis `app_controller_update()` toutes les 1 ms
(un tick FreeRTOS avec la configuration actuelle).

## Activer ou desactiver les fonctions

Les trois modes sont regles par des variables booleennes en haut de `main.c` :

```c
static const bool mode_test = false;
static const bool mode_automatique = true;
static const bool mode_manuel_badge = true;
```

`true` active, `false` desactive ; recompiler et televerser pour appliquer.
Hors mode test, automatique et manuel par badge peuvent etre actifs ensemble.
Le BLE et la balance restent disponibles dans ce fonctionnement normal.

Si `mode_test=true`, les repas et le moteur commande par badge sont desactives.
Les variables `tester_balance`, `tester_rfid`, `tester_ble` selectionnent les essais.
Par defaut, seuls la lecture balance et l'affichage UID sont testes : le moteur reste arrete.
`tester_moteur=true` lance une rotation continue et remplace les autres essais.
Pour lire uniquement les UID : `mode_test=true`, `tester_rfid=true`, et les autres
variables `tester_*` a false. Les options internes sont regroupees dans la structure fonctions.
Sans balance, le scheduler et le mode badge manuel sont automatiquement inactifs ; sans RFID, le mode badge
manuel est inactif. Les commandes BLE d'une fonction coupee retournent DISABLED.

## Cartes acceptees

Toute carte detectee active le mode manuel, sans configuration ni filtrage UID.
Les UID restent affiches dans le terminal et dans GET_STATUS pour le diagnostic.
La vitesse est reglee par CROQCHAT_VITESSE_BADGE dans include/croqchat_config.h.

## Fonctionnement

- Toute carte detectee lance le moteur sans tare, a 500 demi-pas/s.
- Le lecteur est interroge environ toutes les 200 ms. Le badge reste present
  jusqu'a 2 s apres la derniere lecture valide. Le moteur s'arrete ensuite,
  meme si la tache RFID ne progresse plus.
- La copie UID/presence est protegee entre la tache RFID et la boucle principale.
- Lecteur absent ou initialisation echouee : aucun demarrage par badge.
  Le BLE et le planning continuent ; sans lecteur, l'initialisation peut ajouter
  environ 11 secondes au demarrage avant d'abandonner la synchronisation UART.

## Coexistence des commandes

La duree maximale du moteur est fixee provisoirement a 30 secondes via
`CROQCHAT_TIMEOUT_MOTEUR_MS` dans `include/croqchat_config.h`.
**RAPPEL : REGLER cette valeur apres les essais de debit et de portion.**
Le delai commence quand le moteur demarre. En distribution pesee,
il reste actif pendant les pauses de distribution.
A l'expiration, SafetyManager coupe le moteur et verrouille SAFETY_FAULT_TIMEOUT
(STATUS.fault=4) jusqu'au redemarrage. La tare conserve son propre delai de 10 secondes.

Une distribution deja commencee garde le moteur jusqu'a sa fin. Le badge ne coupe
pas un moteur dont FB03 est proprietaire. A la fin, si une carte est toujours
present et le moteur disponible, le mode badge peut commencer.

Quand le moteur tourne deja par badge, MANUAL_FEED retourne BUSY et les repas echus
sont ignores selon la strategie existante, sans rattrapage. Si un nouveau badge et
un repas arrivent lors de la meme iteration au repos, le repas est examine en premier.

STOP BLE arrete le moteur et bloque le mode badge tant qu'aucun retrait n'a ete
observe par le lecteur. Apres retrait puis nouvelle presentation, le badge peut
relancer le moteur. Une tare/calibration ou un defaut bloque aussi ce mode.
Le mode badge ne depend pas d'une masse cible ni du coefficient de calibration.
La tare automatique reste utilisee pour les distributions pesees.

## Tests

`tests/run_native.ps1` execute les vrais modules avec peripheriques simules :

- mode desactive, differents UID acceptes, remplacement et retrait ;
- STOP et attente de retrait avant nouveau demarrage ;
- ne pas couper le moteur d'une distribution en cours ;
- vrai pilote RFID sur UART simule : lecture UID, retrait, expiration a 2 s,
  panne du lecteur ;
- vrai coordinateur : priorites badge/repas/FB03, STOP, tare et defaut ;
- non-regression du parseur et du scheduler.

La validation physique avec plusieurs cartes reste a faire.
Par defaut, le mode test est desactive, et automatique + manuel par badge sont actifs.
