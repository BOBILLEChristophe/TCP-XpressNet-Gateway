# Souris ESP-NOW — OLED et batterie 18650

Branche de test `feature/mouse-oled-battery`, creee a partir de `feature/mouse-estop`.
La passerelle et la branche `main` ne sont pas modifiees.

## Materiel ESP32 classique

- OLED I2C SSD1306 128x64 : SDA GPIO21, SCL GPIO22, VCC 3V3, GND.
- Encodeur EC11 : A GPIO25, B GPIO26, bouton GPIO27 (commun GND).
- Bouton E-STOP : GPIO32 vers GND.
- Mesure batterie : GPIO34 (ADC1), pont diviseur R1=100k et R2=100k.
  - P+ du BMS (avant step-up) -> R1 -> GPIO34 -> R2 -> GND.
  - Idealement alimenter le pont apres l'interrupteur general pour ne pas vider la cellule a l'arret.
  - Batterie, BMS, step-up et ESP32 doivent partager la meme masse.
- Step-up 5 V regule vers entree 5V/VIN de la carte ESP32, **jamais vers 3V3**.
- Pour l'OLED 1,3 pouce SH1106 : modifier uniquement le constructeur U8g2 dans `src/MouseDisplay.cpp`.

## Compilation

Ouvrir **ce dossier** (`examples/espnow-mouse`) dans PlatformIO.
Environnement par defaut `esp32`, bibliotheque `olikraus/U8g2`.

Avant les essais radio, renseigner la MAC STA de la passerelle dans
`src/mouse.cpp` (`GATEWAY_MAC`). Elle reste volontairement a zero dans GitHub.

## Fonctionnement

- Vitesse 0..100 et direction : encodeur, inchanges.
- Appui encodeur : STOP normal si vitesse > 0, sinon inversion du sens.
- Bouton GPIO32 : E-STOP via commande `<t ADRESSE -1 DIRECTION>`, inchange.
- OLED : locomotive, vitesse, sens, F0..F7, pourcentage et jauge batterie.
- Mesure batterie : 32 echantillons et lissage, environ toutes les 2 secondes,
  echantillonnage etalé pour ne pas bloquer les boutons.
- A 30 % et moins : pourcentage clignotant.
- `f0 on` a `f7 off` : **affichage local uniquement**, pas encore de commande DCC.
- L'etat affiche correspond aux commandes locales, **pas** a une confirmation de la centrale.

## Validation progressive

1. Demarrer sans locomotive : verifier OLED et tension batterie.
2. Tester encodeur, inversion et bouton E-STOP au moniteur serie.
3. Renseigner la MAC, tester ESP-NOW avec la passerelle.
4. Tester avec locomotive, en surveillant la stabilite du step-up et du BMS.

Ne pas raccorder l'ESP32 tant que la sortie du step-up n'a pas ete verifiee au voltmetre.
