# TCP-XpressNet-Gateway

Passerelle **ESP32-S3 / SP3485 / Lenz LZV100** : commandes XpressNet via moniteur série USB et serveur TCP Wi-Fi (sous-ensemble DCC-EX).

## Version 1.0 — sources validées sur matériel

Les sept fichiers `src/` proviennent de la version transmise après essais concluants : alimentation, vitesse, direction et fonctions répondent dès la première demande, via USB et TCP. La compilation de cette branche et un nouveau test sur la carte n'ont **pas** été réalisés ici.

## Matériel et protocole

- ESP32-S3, transceiver RS-485 SP3485 et Lenz LZV100.
- UART1 RX : GPIO18 ; RMT TX : GPIO17 ; commande RTS/DE : GPIO16.
- XpressNet : 62 500 bauds, UART 8O1, adresse 21, interrogation normale `0x55`, demande ACK `0x95`.
- Configuration : `src/Config.h`. Le projet PlatformIO fourni cible `esp32-s3-devkitc-1` par défaut.

## Fonctionnement

- **Réception immédiate :** `uart_set_rx_full_threshold(UART_PORT, 1)` dans `src/XpressNet.cpp`. Ce seuil a supprimé la latence et les commandes ignorées observées avant correction.
- **Transmission :** RMT à 1 MHz, bits XpressNet de 16 µs, émission via RTS/DE et file FreeRTOS de pointeurs vers 16 tampons statiques préencodés.
- **Temporisations validées :** `RESPONSE_DELAY_US = 0` et `ACK_DELAY_US = 48`.
- **Démarrage :** `src/main.cpp` programme deux trames POWER OFF après `initXpressNet()` et avant `initNetwork()`. La LZV100 coupe les rails à la mise en service de la passerelle.

**Sécurité :** ces deux POWER OFF constituent une précaution *logicielle*. Ils ne garantissent pas l'absence de tension sur les rails entre l'alimentation de la centrale et la réception des commandes. Prévoir une coupure matérielle indépendante si une absence de tension immédiate est impérative.

## Commandes

### Moniteur série USB (115 200 bauds)

- `on`, `off`, `stop`.
- `fwd 10`, `rev 10` (locomotive de test définie par `TEST_LOCO_ADDRESS`, actuellement 6400).
- `f0 on` / `f0 off` jusqu'à `f4 on` / `f4 off`.
- `status` : compteurs UART, émissions, erreurs, FIFO et réseau.

### TCP Wi-Fi (port 2560)

Sous-ensemble DCC-EX :

- `<1>` : alimentation ON.
- `<0>` : alimentation OFF.
- `<t 6400 10 1>` : vitesse 10, direction avant pour la locomotive 6400 ; `<t 6400 0 1>` : arrêt.

Le protocole DCC-EX n'est **pas** entièrement implémenté. Le code TCP actuel prend en charge un client à la fois.

## Installation PlatformIO

1. Ouvrir le dépôt dans VS Code / PlatformIO.
2. Copier `src/Secrets.example.h` vers `src/Secrets.h`, puis y saisir les identifiants Wi-Fi.
3. Vérifier que `src/Secrets.h` reste ignoré par Git et ne jamais publier de mots de passe.
4. Compiler l'environnement `esp32-s3` et téléverser sur la carte.
5. Vérifier le POWER OFF automatique, puis les commandes ON/OFF, vitesse et fonctions par USB et TCP.

`platformio.ini` reprend la configuration fournie. L'environnement `esp32` classique y figure également, mais n'est pas validé dans cette version.

## Organisation

- `src/main.cpp` : interface série, état de la locomotive de test, initialisation.
- `src/XpressNet.cpp/.h` : réception UART, RMT, FIFO, trames.
- `src/TcpNetwork.cpp/.h` : Wi-Fi et serveur TCP.
- `src/DccExProtocol.cpp/.h` : décodage des commandes DCC-EX.
- `src/Config.h` : broches et constantes.
- `src/Secrets.example.h` : modèle sans identifiants réels.

## Limites

- Le compteur TX indique l'émission locale, pas l'acquittement applicatif par la LZV100.
- La validation explicite du neuvième bit des Call Bytes n'est pas implémentée.
- Le POWER OFF de démarrage dépend des interrogations XpressNet et de la disponibilité du bus.

Ne pas modifier les mécanismes temporels de cette version sans nouvel essai sur matériel.
