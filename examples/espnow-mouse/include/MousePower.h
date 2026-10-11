#pragma once

// Commande POWER de la centrale :
// - bouton momentane GPIO16, actif a LOW ;
// - LED GPIO17 ;
// - etat affiche UNIQUEMENT sur retour ESP-NOW de la passerelle.
namespace MousePower {
  void begin();
  bool poll();                   // true = nouvel appui autorise
  void confirm(bool powerOn);   // etat annonce par la passerelle
  bool desiredPowerOn();        // commande a envoyer lors d'un appui
  void markRequested();        // appeler uniquement apres esp_now_send == ESP_OK
  bool takeDisplayChanged();   // acquitte la demande de rafraichissement OLED
  const char *label();         // PWR ON / PWR OFF / PWR ? / ...*
}
