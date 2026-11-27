# Test rapide de la balance dans CroqChat

1. Compiler et televerser la cible esp32dev sur COM5 avec PlatformIO.
2. Connecter CroqChat a CroqChat-ESP32 et activer les notifications TX.
3. Actualiser avec GET_STATUS : sensor doit valoir 1.
4. Balance vide et immobile : envoyer TARE. Actualiser jusqu a busy=0 et tare=1.
5. Poser une masse connue de 100 g. Envoyer CALIBRATE;mass_ref=100.
6. Actualiser jusqu a busy=0 et cal=1. Le poids w doit etre proche de 100 g.
7. Retirer puis reposer la masse : verifier le retour proche de zero et la repetabilite.
8. Redemarrer : la calibration est conservee en NVS.

Utiliser la masse reellement posee dans mass_ref (grammes). Chaque commande se termine par LF.
GET_STATUS donne une lecture a la demande : utiliser le bouton pour actualiser dans CroqChat.
ACK signifie demande acceptee. Une operation prend 20 lectures HX711 (environ 2 s a 10 Hz) et expire apres 10 s.
Si busy revient a 0 sans le resultat attendu, consulter le journal serie : absence de mesure, calibration invalide ou erreur NVS. Les anciens indicateurs tare/cal restent presents pendant une nouvelle operation ; ils ne prouvent pas a eux seuls sa reussite.
Le moteur reste au repos pendant ces tests. DOUT est sur GPIO34, SCK sur GPIO25.

Le code de CroqChat cote application est absent de ce depot ; le protocole BLE existant est conserve.
