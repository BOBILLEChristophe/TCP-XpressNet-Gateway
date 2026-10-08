# Historique des versions

## v1.0 — 9 octobre 2026

- Publication des sept fichiers sources fournis après validation matérielle des commandes USB et TCP.
- Réception UART : interruption dès le premier octet avec `uart_set_rx_full_threshold(UART_PORT, 1)`.
- RMT et file de pointeurs vers 16 tampons statiques préparés en avance.
- Deux commandes POWER OFF automatiquement programmées au démarrage.
- Temporisations : `RESPONSE_DELAY_US = 0`, `ACK_DELAY_US = 48`.
- Documentation, modèle de secrets Wi-Fi et configuration PlatformIO.

**Validation :** essais concluants rapportés sur ESP32-S3 et LZV100 ; pas de compilation ni de nouveau test matériel effectués dans cette préparation GitHub.
