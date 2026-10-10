# Clavier matriciel 3 x 4 - souris ESP-NOW

Branche de test `feature/mouse-keypad-3x4`, issue de `feature/mouse-functions`.
La passerelle ne change pas ; seule la souris ESP32 classique est a flasher.
Ouvrir `examples/espnow-mouse` comme racine du projet PlatformIO (env `esp32`).
Conserver la MAC de passerelle configuree localement dans `src/mouse.cpp`.

## GPIO souris

| Ligne | GPIO | Colonne | GPIO |
| --- | --- | --- | --- |
| L1 | 13 | C1 | 18 |
| L2 | 14 | C2 | 19 |
| L3 | 16 | C3 | 23 |
| L4 | 17 | | |

Les touches relient une ligne a une colonne. Ne pas connecter ces lignes ou colonnes
au 5 V, 3,3 V ou GND. La souris utilise un balayage actif a LOW avec INPUT_PULLUP
sur les colonnes et les autres lignes en haute impedance.

| Touche | Liaison de test | Fonction |
| --- | --- | --- |
| 1 | GPIO13 - GPIO18 | F0 |
| 2 | GPIO13 - GPIO19 | F1 |
| 3 | GPIO13 - GPIO23 | F2 |
| 4 | GPIO14 - GPIO18 | F3 |
| 5 | GPIO14 - GPIO19 | F4 |
| 6 | GPIO14 - GPIO23 | F5 |
| 7 | GPIO16 - GPIO18 | F6 |
| 8 | GPIO16 - GPIO19 | F7 |
| 9 | GPIO16 - GPIO23 | reservee |
| * | GPIO17 - GPIO18 | reservee |
| 0 | GPIO17 - GPIO19 | reservee |
| # | GPIO17 - GPIO23 | reservee |

Premier contact = ON, separation = aucun changement, second contact = OFF.
Maintenir un contact ne repete pas la commande. Anti-rebond : 30 ms.
Plusieurs contacts simultanes sont ignores pour eviter les bascules ambiguës.
L'affichage OLED reste optimiste : il represente la demande locale, pas un acquittement
de la passerelle ou du decodeur.

## Test

1. Arreter la locomotive (vitesse 0).
2. Ouvrir le moniteur serie de la souris a 115200.
3. Faire toucher momentanement GPIO13 et GPIO18 pour F0 ON.
4. Separer les fils ; les toucher une seconde fois pour F0 OFF.
5. Essayer GPIO13-GPIO19 pour F1.
6. Verifier le moniteur serie de la passerelle et les fonctions de la locomotive.

Les GPIO25/26/27 de l'encodeur, GPIO32 de l'arret d'urgence,
GPIO21/22 de l'OLED et GPIO34 de la batterie restent inchanges.
