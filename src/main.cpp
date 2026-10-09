/*
   ============================================================

   TCP-XPRESSNET-GATEWAY
   BASE FREERTOS V2 + NETWORK

   ESP32 / ESP32-S3 + SP3485 + LENZ LZV100

   ============================================================

   Le moteur XpressNet est séparé dans :

       XpressNet.h
       XpressNet.cpp

   La partie réseau est séparée dans :

       Network.h
       Network.cpp

   main.cpp conserve :

       - l'interface de test Serial
       - l'état de la locomotive de test
       - CommandTask
       - setup()
       - loop()

   ============================================================
*/

#include <Arduino.h>

#include "Config.h"
#include "XpressNet.h"
#include "TcpNetwork.h"
#include "EspNow.h"
#include "DccExProtocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// ============================================================
// ETAT LOCOMOTIVE DE TEST
// ============================================================

static uint8_t functionState = 0;

static bool currentDirectionForward = true;

static uint8_t currentSpeed = 0;


// ============================================================
// TRAITEMENT COMMANDES SERIAL
// ============================================================

void processSerialCommand(
  String line) {

  line.trim();

  line.toLowerCase();


  // ----------------------------------------------------------
  // POWER ON
  // ----------------------------------------------------------

  if (line == "on") {

    uint8_t frame[MAX_XPRESSNET_BYTES];


    size_t length =
      buildPowerOnFrame(
        frame);


    if (queueXpressNetMessage(
          frame,
          length)) {

      Serial.println(
        "POWER ON prepare et ajoute a la FIFO");
    }

    return;
  }


  // ----------------------------------------------------------
  // POWER OFF
  // ----------------------------------------------------------

  if (line == "off") {

    uint8_t frame[MAX_XPRESSNET_BYTES];


    size_t length =
      buildPowerOffFrame(
        frame);


    if (queueXpressNetMessage(
          frame,
          length)) {

      Serial.println(
        "POWER OFF prepare et ajoute a la FIFO");
    }

    return;
  }


  // ----------------------------------------------------------
  // STOP
  // ----------------------------------------------------------

  if (line == "stop") {

    uint8_t frame[MAX_XPRESSNET_BYTES];


    size_t length =
      buildSpeedFrame(
        TEST_LOCO_ADDRESS,
        currentDirectionForward,
        0,
        frame);


    if (queueXpressNetMessage(
          frame,
          length)) {

      currentSpeed =
        0;


      Serial.println(
        "STOP prepare et ajoute a la FIFO");
    }

    return;
  }


  // ----------------------------------------------------------
  // FWD
  // ----------------------------------------------------------

  if (line.startsWith("fwd ")) {

    int speed =
      line.substring(4).toInt();


    if (speed < 0 ||
        speed > 127 ||
        speed == 1) {

      Serial.println(
        "Vitesse invalide : 0 ou 2..127");

      return;
    }


    uint8_t frame[MAX_XPRESSNET_BYTES];


    size_t length =
      buildSpeedFrame(
        TEST_LOCO_ADDRESS,
        true,
        (uint8_t)speed,
        frame);


    if (queueXpressNetMessage(
          frame,
          length)) {

      currentDirectionForward =
        true;

      currentSpeed =
        (uint8_t)speed;


      Serial.printf(
        "FWD %d prepare et ajoute a la FIFO\n",
        speed);
    }

    return;
  }


  // ----------------------------------------------------------
  // REV
  // ----------------------------------------------------------

  if (line.startsWith("rev ")) {

    int speed =
      line.substring(4).toInt();


    if (speed < 0 ||
        speed > 127 ||
        speed == 1) {

      Serial.println(
        "Vitesse invalide : 0 ou 2..127");

      return;
    }


    uint8_t frame[MAX_XPRESSNET_BYTES];


    size_t length =
      buildSpeedFrame(
        TEST_LOCO_ADDRESS,
        false,
        (uint8_t)speed,
        frame);


    if (queueXpressNetMessage(
          frame,
          length)) {

      currentDirectionForward =
        false;

      currentSpeed =
        (uint8_t)speed;


      Serial.printf(
        "REV %d prepare et ajoute a la FIFO\n",
        speed);
    }

    return;
  }


  // ----------------------------------------------------------
  // F0 ... F4
  // ----------------------------------------------------------

  if (line.length() >= 5 &&
      line.charAt(0) == 'f') {

    int functionNumber =
      line.substring(
        1,
        2)
        .toInt();


    if (functionNumber >= 0 &&
        functionNumber <= 4) {

      bool turnOn;


      if (line.endsWith(" on")) {

        turnOn =
          true;

      } else if (line.endsWith(" off")) {

        turnOn =
          false;

      } else {

        Serial.println(
          "Syntaxe : f0 on / f0 off");

        return;
      }


      uint8_t mask =
        0;


      switch (functionNumber) {

        case 0:
          mask = 0x10;
          break;

        case 1:
          mask = 0x01;
          break;

        case 2:
          mask = 0x02;
          break;

        case 3:
          mask = 0x04;
          break;

        case 4:
          mask = 0x08;
          break;
      }


      uint8_t newFunctionState =
        functionState;


      if (turnOn) {

        newFunctionState |=
          mask;

      } else {

        newFunctionState &=
          ~mask;
      }


      uint8_t frame[MAX_XPRESSNET_BYTES];


      size_t length =
        buildFunctionFrame(
          TEST_LOCO_ADDRESS,
          newFunctionState,
          frame);


      if (queueXpressNetMessage(
            frame,
            length)) {

        functionState =
          newFunctionState;


        Serial.printf(
          "F%d %s prepare et ajoute a la FIFO\n",
          functionNumber,
          turnOn ? "ON" : "OFF");
      }

      return;
    }
  }


  // ----------------------------------------------------------
  // STATUS
  // ----------------------------------------------------------

  if (line == "status") {

    Serial.println();

    Serial.println(
      "======================================");

    Serial.println(
      " STATUS TCP-XPRESSNET-GATEWAY");

    Serial.println(
      "======================================");


    printXpressNetStatus();


    Serial.printf(
      "Vitesse          : %u\n",
      currentSpeed);


    Serial.printf(
      "Direction        : %s\n",
      currentDirectionForward
        ? "FWD"
        : "REV");


    Serial.printf(
      "Fonctions        : 0x%02X\n",
      functionState);


    if (COMMUNICATION_MODE == CommunicationMode::WIFI_TCP)
      printNetworkStatus();
    else
      printEspNowStatus();


    Serial.println(
      "======================================");

    Serial.println();

    return;
  }


  Serial.println(
    "Commande inconnue");
}


// ============================================================
// COMMAND TASK
// ============================================================

void CommandTask(
  void *parameter) {

  String line;


  for (;;) {

    while (Serial.available()) {

      char c =
        Serial.read();


      if (c == '\r' ||
          c == '\n') {

        if (line.length() > 0) {

          processSerialCommand(
            line);

          line =
            "";
        }

      } else {

        line +=
          c;
      }
    }


    /*
       Cette tâche n'a aucune contrainte
       temporelle XpressNet.

       La partie TCP dispose maintenant
       de sa propre tâche.
    */

    vTaskDelay(
      pdMS_TO_TICKS(5));
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);


  Serial.println();

  Serial.println(
    "======================================");

  Serial.println(
    " TCP-XPRESSNET-GATEWAY");

  Serial.println(
    " BASE FREERTOS V2 + NETWORK");

  Serial.println(
    "======================================");

  Serial.println();


  // ----------------------------------------------------------
  // XPRESSNET
  // ----------------------------------------------------------

  initXpressNet();

  // SECURITE AU DEMARRAGE : deux POWER OFF transmis
  // lors de deux interrogations XpressNet successives.
  // Cette action est volontairement effectuee avant initNetwork().
  {
    uint8_t frame[MAX_XPRESSNET_BYTES];
    const size_t length = buildPowerOffFrame(frame);

    for (uint8_t i = 0; i < 2; ++i) {
      if (queueXpressNetMessage(frame, length)) {
        Serial.printf("Demarrage : POWER OFF %u/2 programme\n", i + 1);
      } else {
        Serial.println("ERREUR : POWER OFF initial non programme");
      }
    }
  }


  // ----------------------------------------------------------
  // NETWORK
  // ----------------------------------------------------------

  if (COMMUNICATION_MODE == CommunicationMode::WIFI_TCP) {
    initNetwork();
  } else {
    initDccExProtocol();
    initEspNow();
  }


  // ----------------------------------------------------------
  // COMMAND TASK
  // ----------------------------------------------------------

  BaseType_t result;

  result =
    xTaskCreate(
      CommandTask,
      "CommandTask",
      4096,
      nullptr,
      1,
      nullptr);


  if (result != pdPASS) {

    Serial.println(
      "ERREUR creation CommandTask");

    while (true) {
      delay(1000);
    }
  }


  // ----------------------------------------------------------
  // AFFICHAGE
  // ----------------------------------------------------------

  Serial.printf(
    "UART RX : GPIO%d\n",
    (int)XPRESSNET_RX_PIN);

  Serial.printf(
    "RMT TX  : GPIO%d\n",
    (int)XPRESSNET_TX_PIN);

  Serial.printf(
    "RTS     : GPIO%d\n",
    (int)XPRESSNET_RTS_PIN);


  Serial.println();


  Serial.println(
    "XpressNet adresse 21");

  Serial.println(
    "Normal Inquiry : 0x55");

  Serial.println(
    "ACK Request    : 0x95");


  Serial.println();


  Serial.print(
    "Delai 0x55 : ");

  Serial.print(
    RESPONSE_DELAY_US);

  Serial.println(
    " us");


  Serial.print(
    "Delai 0x95 : ");

  Serial.print(
    ACK_DELAY_US);

  Serial.println(
    " us");


  Serial.println();


  Serial.print(
    "Locomotive test : ");

  Serial.println(
    TEST_LOCO_ADDRESS);


  Serial.println();


  Serial.println(
    "Commandes :");

  Serial.println(
    "  on");

  Serial.println(
    "  off");

  Serial.println(
    "  fwd 10");

  Serial.println(
    "  fwd 30");

  Serial.println(
    "  rev 10");

  Serial.println(
    "  rev 30");

  Serial.println(
    "  stop");

  Serial.println(
    "  f0 on / f0 off");

  Serial.println(
    "  ...");

  Serial.println(
    "  f4 on / f4 off");

  Serial.println(
    "  status");


  Serial.println();


  Serial.println(
    "Pret.");

  Serial.println();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  /*
     Rien de temporel ici.

     XpressNetTask :
         priorité 4
         UART + bus XpressNet

     CommandTask :
         priorité 1
         interface utilisateur

     NetworkTask :
         priorité 1
         Wi-Fi + TCP
  */

  vTaskDelay(
    pdMS_TO_TICKS(1000));
}