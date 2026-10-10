# Souris ESP-NOW - multilo (prototype)

Branche `feature/mouse-multiloco` derivee de `feature/mouse-keypad-3x4`.
La passerelle ESP32-S3 n'est pas modifiee.

## Configuration des locomotives

Modifier exclusivement la table `DCC_ADDRESSES` dans `src/LocoManager.cpp`.
Les emplacements 1 a 8 correspondent aux touches 1 a 8. Une adresse `0`
signifie emplacement inutilise et ne peut pas etre selectionnee.
Par defaut, seule la locomotive 1 (adresse DCC 6400) est configuree.
Ne pas saisir d'adresses en double.

Aucun stockage NVS/Flash. Au demarrage, le slot 1 est selectionne ;
les vitesses locales sont initialisees a 0, sens avant, fonctions OFF.
Cela n'envoie aucune commande d'arret aux locomotives : ne pas redemarrer
la souris avec des trains en marche sans prendre les precautions adaptees.

## Clavier 3 x 4 (GPIO testes)

Lignes : 13, 14, 4, 33 ; colonnes : 18, 19, 23.
Les fils ne relient que des GPIO ligne et colonne ; ne pas les relier
au GND ou a une alimentation.

| Touche | Connexion | Mode normal | Mode selection |
| --- | --- | --- | --- |
| 1 | 13-18 | F0 | slot 1 |
| 2 | 13-19 | F1 | slot 2 |
| 3 | 13-23 | F2 | slot 3 |
| 4 | 14-18 | F3 | slot 4 |
| 5 | 14-19 | F4 | slot 5 |
| 6 | 14-23 | F5 | slot 6 |
| 7 | 4-18 | F6 | slot 7 |
| 8 | 4-19 | F7 | slot 8 |
| 9 | 4-23 | reserve | non pris en charge |
| * | 33-18 | reserve | annule |
| 0 | 33-19 | reserve | non pris en charge |
| # | 33-23 | ouvre selection | confirme |

Procedure : #, numero (1 a 8), #. Exemple : 33-23, 13-19,
33-23 pour selectionner le slot 2. Pour annuler : 33-18.
Le moniteur serie a 115200 affiche les selections et les erreurs.

Pendant la saisie, l'encodeur est lu mais ses commandes sont ignorees.
Une selection ne transmet aucune commande de vitesse. Chaque locomotive
conserve en RAM vitesse, sens et F0..F7. Les autres locomotives
continuent avec leur derniere consigne envoyee.

Le bouton d'arret d'urgence GPIO32 conserve son fonctionnement actuel :
E-STOP pour la locomotive selectionnee, pas arret general de toutes
les locomotives. Il reste prioritaire sur la selection et l'encodeur.

## Premier essai prudent

1. Modifier la table pour renseigner une seconde adresse DCC reelle.
2. Configurer localement la MAC de la passerelle dans `src/mouse.cpp`
   (le depot contient volontairement `00:00:00:00:00:00`).
3. Televerser la souris (PlatformIO `examples/espnow-mouse`, env `esp32`).
4. Tester avec les locomotives a l'arret : # 2 # puis # 1 #.
5. Verifier l'OLED et le moniteur serie, puis F0/F1 et l'encodeur
   sur chacune. Tester les changements avec une locomotive en mouvement
   uniquement apres ces verifications.

Le code n'a pas encore ete compile ni teste physiquement dans cette branche.
