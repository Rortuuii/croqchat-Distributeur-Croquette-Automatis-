# Etude de la derive de la balance

Le programme mesure la masse toutes les 60 secondes, a partir du demarrage.
Il conserve les 120 derniers points (temps en millisecondes et masse en grammes)
dans un tableau circulaire en RAM. Une lecture en echec est ignoree et signalee.
La tare est faite uniquement au demarrage. Garder une charge constante pour
etudier la derive ; un changement de charge apparait aussi sur la courbe.

## Afficher le graphique sur le PC

1. Compiler et televerser le programme avec PlatformIO.
2. Fermer le moniteur serie pour liberer le port.
3. Installer les dependances : `python -m pip install -r tools/requirements.txt`.
4. Lancer : `python tools/graphique_masse.py COM3` (adapter le port).

Le graphique se rafraichit chaque seconde ; un nouveau point arrive chaque minute.
Il affiche la masse en fonction du temps et la variation entre le premier et le
dernier point conserves. L'axe vertical s'ajuste automatiquement aux mesures.

L'ouverture du port serie peut redemarrer l'Uno et vider l'historique : lancer le
script au debut de l'experience et le laisser ouvert. Les donnees ne sont pas
conservees apres une coupure ou un redemarrage. La commande serie `H` renvoie
l'historique dans l'ordre chronologique entre `BEGIN` et `END` ; chaque point
est transmis au format `temps_ms,masse_g`.

## Lecture immediate sans refaire la tare

Dans le graphique deja ouvert, cliquer sur **Lire la masse**. La valeur
s'affiche en bas de la fenetre sans rouvrir le port serie. Cette lecture
ne modifie ni l'historique ni le rythme des mesures chaque minute.
Dans un moniteur serie deja ouvert a 9600 bauds, envoyer `M` produit le meme
resultat. Ne pas fermer puis rouvrir la connexion : cela peut redemarrer l'Uno.
