# Fonctions F0-F7 — protocole de test

Branche `feature/mouse-functions` creee depuis `feature/mouse-oled-battery`.
La branche `main` et la version OLED+batterie validee ne sont pas modifiees.

## Deux firmwares a flasher

1. Passerelle **ESP32-S3** : dossier racine `TCP-XpressNet-Gateway`, environnement `esp32-s3`, avec la configuration ESP-NOW deja validee. Si le projet demande `Secrets.h`, utiliser le fichier de configuration local existant : ne pas publier les secrets sur GitHub.
2. Souris **ESP32 classique** : ouvrir **`examples/espnow-mouse` comme projet PlatformIO**, environnement `esp32`. Remettre localement la MAC STA de la passerelle dans `GATEWAY_MAC` (la valeur du depot reste volontairement a zero).

## Commandes de test

Moniteur serie de la **souris**, 115200 bauds, terminaison de ligne NL ou CRLF :

```text
f0 on
f0 off
f1 on
f2 on
f1 off
f5 on
f7 on
f7 off
```

La souris convertit `f0 on` en `<f 6400 0 1>` (pour la loco 6400).
La passerelle conserve un masque par adresse et envoie :
- groupe F0-F4 : XpressNet `E4 20 AH AL MASK XOR`, F0=bit4, F1-F4=bits0-3 ;
- groupe F5-F8 : XpressNet `E4 21 AH AL MASK XOR`, F5-F8=bits0-3.

Les commandes fonctions sont transmises par ESP-NOW, comme la vitesse. Le moniteur de la passerelle doit afficher `ESP-NOW RX ... <f ...>` puis `DCC-EX <f> ...`.

**Important :** l'OLED indique l'etat **demande** par la souris, pas une confirmation du decodeur. `esp_now_send() == ESP_OK` indique seulement que l'emission a ete acceptee localement.

## Precautions

- Premier essai a vitesse 0, avec F0 (feux), puis F1/F2, ensuite F5/F7.
- Les etats F0-F7 sont conserves par la passerelle dans un cache de 32 adresses (non persistant) ; si la centrale, Rocrail ou une autre manette modifie les fonctions, l'affichage de la souris n'est pas synchronise.
- Pas encore de boutons physiques pour les fonctions.
- En cas d'anomalie, revenir a `feature/mouse-oled-battery` pour la souris et au firmware passerelle precedemment valide.
