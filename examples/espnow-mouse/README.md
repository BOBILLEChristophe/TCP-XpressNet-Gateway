# Souris ESP-NOW expérimentale

Ce dossier contient un **projet PlatformIO distinct** de la passerelle. Ouvrir directement ce dossier dans VS Code / PlatformIO, sans déplacer `mouse.cpp` dans le `src/` de la passerelle.

## Étapes

1. Dans la passerelle, choisir `CommunicationMode::ESP_NOW` dans `include/Config.h`. Le canal par défaut est 6.
2. Téléverser la passerelle. Son moniteur série affiche sa **MAC Wi-Fi STA**.
3. Reporter cette MAC dans `GATEWAY_MAC` de `src/mouse.cpp` ; téléverser la souris.
4. Le moniteur série de la souris affiche sa **MAC Wi-Fi STA**.
5. Reporter la MAC de la souris dans `ESPNOW_ALLOWED_MOUSE_MAC` de la passerelle, puis téléverser à nouveau cette dernière.
6. Sur la souris, saisir `on`, `off`, `t 6400 10 1`, `t 6400 0 1`, ou les mêmes commandes encadrées de `< >`.

Les deux cartes doivent utiliser **le même canal radio**. Aucun routeur ni accès Internet n'est nécessaire. Le serveur TCP de la passerelle n'est pas démarré en mode ESP-NOW.

## Précautions

- La liaison du prototype est **non chiffrée** ; la MAC source autorisée est filtrée mais peut être usurpée. Réserver les essais à un environnement contrôlé. Un appairage chiffré sera nécessaire avant un usage public.
- La souris confirme uniquement que la demande d'émission a été acceptée par la pile ESP-NOW ; elle ne reçoit pas encore d'acquittement applicatif de la passerelle.
- Les deux commandes POWER OFF au démarrage de la passerelle sont conservées. Elles ne constituent pas un dispositif de coupure électrique matérielle.
- La version initiale autorise **une seule souris** ; le support de plusieurs souris viendra après validation.
- **Non compilé / non testé sur matériel** à ce stade.

## Afficheur LCD 16x2 I2C (branche feature/mouse-lcd)

LCD HD44780 avec interface PCF8574 (adresse 0x27 par defaut, modifiable dans `src/MouseDisplay.cpp`).

- ESP32 classique : SDA GPIO21, SCL GPIO22.
- ESP32-S3 : SDA GPIO8, SCL GPIO9 (proposition, verifier la carte).
- VCC et GND : respecter les niveaux logiques. Beaucoup de modules LCD sont alimentes en 5 V et possedent des pull-ups I2C vers 5 V ; ne **pas** les relier directement aux GPIO 3,3 V de l'ESP32. Utiliser un convertisseur de niveau I2C adapte, ou un module compatible 3,3 V.
- La bibliotheque `LiquidCrystal_I2C` est ajoutee au `platformio.ini`.
- L'afficheur presente l'adresse DCC, la vitesse demandee, le sens `>>` / `<<` et les fonctions locales actives F0 a F3.
- Les commandes serie `on`, `off`, `t 6400 10 1`, `t 6400 0 1` continuent d'etre envoyees par ESP-NOW. L'etat local est mis a jour si la demande d'emission est acceptee par la pile ESP-NOW ; il ne s'agit pas d'un retour de la centrale.
- Les commandes `f0 on`, `f0 off` ... `f3 on`, `f3 off` ne servent **qu'a simuler l'affichage** et ne sont pas transmises a la passerelle.
- Si le LCD ne repond pas a l'adresse configuree, le programme continue de fonctionner par le moniteur serie.

**Cette evolution n'a pas encore ete compilee ou validee sur le materiel.**
