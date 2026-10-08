#include "DccExProtocol.h"

#include "XpressNet.h"

#include <stdlib.h>
#include <string.h>


// ============================================================
// CONFIGURATION
// ============================================================

static constexpr size_t DCCEX_COMMAND_BUFFER_SIZE = 32;


// ============================================================
// BUFFER DE RECEPTION
// ============================================================

static char commandBuffer[DCCEX_COMMAND_BUFFER_SIZE];

static size_t commandLength = 0;

static bool commandInProgress = false;


// ============================================================
// STATISTIQUES
// ============================================================

static uint32_t commandCount = 0;

static uint32_t powerOnCount = 0;

static uint32_t powerOffCount = 0;

static uint32_t throttleCount = 0;

static uint32_t unknownCommandCount = 0;

static uint32_t invalidCommandCount = 0;

static uint32_t overflowCount = 0;


// ============================================================
// COMMANDE POWER ON
// ============================================================

static void processPowerOn() {

  uint8_t frame[MAX_XPRESSNET_BYTES];

  size_t length =
    buildPowerOnFrame(
      frame);


  if (queueXpressNetMessage(
        frame,
        length)) {

    powerOnCount++;

    Serial.println(
      "DCC-EX <1> -> POWER ON");
  }
  else {

    Serial.println(
      "DCC-EX <1> -> FIFO XpressNet pleine");
  }
}


// ============================================================
// COMMANDE POWER OFF
// ============================================================

static void processPowerOff() {

  uint8_t frame[MAX_XPRESSNET_BYTES];

  size_t length =
    buildPowerOffFrame(
      frame);


  if (queueXpressNetMessage(
        frame,
        length)) {

    powerOffCount++;

    Serial.println(
      "DCC-EX <0> -> POWER OFF");
  }
  else {

    Serial.println(
      "DCC-EX <0> -> FIFO XpressNet pleine");
  }
}


// ============================================================
// COMMANDE THROTTLE
//
// DCC-EX :
//
//   <t CAB SPEED DIRECTION>
//
// Exemple :
//
//   <t 6400 10 1>
//
// SPEED DCC-EX :
//
//   -1       Emergency Stop
//    0       Stop
//    1..126  Vitesse
//
// Conversion vers la valeur DCC/XpressNet :
//
//   -1       -> 1
//    0       -> 0
//    1..126  -> 2..127
//
// DIRECTION :
//
//    1 = FWD
//    0 = REV
// ============================================================

static bool processThrottleCommand() {

  unsigned int locoAddress = 0;

  int dccExSpeed = 0;

  unsigned int direction = 0;

  char extra = '\0';


  // ----------------------------------------------------------
  // ANALYSE
  //
  // Le caractere supplementaire permet de verifier qu'il
  // n'existe pas un quatrieme parametre inattendu.
  // ----------------------------------------------------------

  int parameterCount =
    sscanf(
      commandBuffer,
      "t %u %d %u %c",
      &locoAddress,
      &dccExSpeed,
      &direction,
      &extra);


  if (parameterCount != 3) {

    return false;
  }


  // ----------------------------------------------------------
  // VALIDATION ADRESSE
  // ----------------------------------------------------------

  if (locoAddress < 1 ||
      locoAddress > 10239) {

    return false;
  }


  // ----------------------------------------------------------
  // VALIDATION VITESSE DCC-EX
  // ----------------------------------------------------------

  if (dccExSpeed < -1 ||
      dccExSpeed > 126) {

    return false;
  }


  // ----------------------------------------------------------
  // VALIDATION DIRECTION
  // ----------------------------------------------------------

  if (direction > 1) {

    return false;
  }


  // ----------------------------------------------------------
  // CONVERSION VITESSE
  // ----------------------------------------------------------

  uint8_t xpressNetSpeed;


  if (dccExSpeed == -1) {

    // Emergency Stop

    xpressNetSpeed = 1;
  }
  else if (dccExSpeed == 0) {

    // Stop

    xpressNetSpeed = 0;
  }
  else {

    // DCC-EX 1..126
    // devient DCC 2..127

    xpressNetSpeed =
      static_cast<uint8_t>(
        dccExSpeed + 1);
  }


  // ----------------------------------------------------------
  // CONSTRUCTION XPRESSNET
  // ----------------------------------------------------------

  uint8_t frame[MAX_XPRESSNET_BYTES];

  size_t length =
    buildSpeedFrame(
      static_cast<uint16_t>(locoAddress),
      direction == 1,
      xpressNetSpeed,
      frame);


  // ----------------------------------------------------------
  // AJOUT FIFO XPRESSNET
  // ----------------------------------------------------------

  if (!queueXpressNetMessage(
        frame,
        length)) {

    Serial.println(
      "DCC-EX <t> -> FIFO XpressNet pleine");

    return true;
  }


  throttleCount++;


  // ----------------------------------------------------------
  // TRACE
  // ----------------------------------------------------------

  Serial.printf(
    "DCC-EX <t> -> loco %u speed %d -> %u direction %s\n",
    locoAddress,
    dccExSpeed,
    xpressNetSpeed,
    direction
      ? "FWD"
      : "REV");


  return true;
}


// ============================================================
// TRAITEMENT D'UNE COMMANDE COMPLETE
// ============================================================

static void processCommand() {

  // ----------------------------------------------------------
  // TERMINAISON DE LA CHAINE
  // ----------------------------------------------------------

  commandBuffer[commandLength] = '\0';


  commandCount++;


  // ----------------------------------------------------------
  // POWER ON
  // ----------------------------------------------------------

  if (commandLength == 1 &&
      commandBuffer[0] == '1') {

    processPowerOn();

    return;
  }


  // ----------------------------------------------------------
  // POWER OFF
  // ----------------------------------------------------------

  if (commandLength == 1 &&
      commandBuffer[0] == '0') {

    processPowerOff();

    return;
  }


  // ----------------------------------------------------------
  // THROTTLE
  // ----------------------------------------------------------

  if (commandBuffer[0] == 't') {

    if (processThrottleCommand()) {

      return;
    }


    invalidCommandCount++;


    Serial.print(
      "DCC-EX commande <t> invalide : <");

    Serial.print(
      commandBuffer);

    Serial.println(
      ">");


    return;
  }


  // ----------------------------------------------------------
  // COMMANDE NON IMPLEMENTEE
  // ----------------------------------------------------------

  unknownCommandCount++;


  Serial.print(
    "DCC-EX commande ignoree : <");

  Serial.print(
    commandBuffer);

  Serial.println(
    ">");
}


// ============================================================
// INITIALISATION
// ============================================================

void initDccExProtocol() {

  commandLength = 0;

  commandInProgress = false;


  Serial.println(
    "Protocole DCC-EX : <1> / <0> / <t CAB SPEED DIRECTION>");
}


// ============================================================
// RECEPTION D'UN OCTET
// ============================================================

void processDccExByte(
  uint8_t value) {

  // ----------------------------------------------------------
  // DEBUT D'UNE COMMANDE
  // ----------------------------------------------------------

  if (value == '<') {

    commandLength = 0;

    commandInProgress = true;

    return;
  }


  // ----------------------------------------------------------
  // RIEN A TRAITER TANT QUE '<' N'A PAS ETE RECU
  // ----------------------------------------------------------

  if (!commandInProgress) {

    return;
  }


  // ----------------------------------------------------------
  // FIN DE COMMANDE
  // ----------------------------------------------------------

  if (value == '>') {

    commandInProgress = false;


    if (commandLength > 0) {

      processCommand();
    }


    commandLength = 0;

    return;
  }


  // ----------------------------------------------------------
  // STOCKAGE
  // ----------------------------------------------------------

  if (commandLength <
      DCCEX_COMMAND_BUFFER_SIZE - 1) {

    commandBuffer[commandLength] =
      static_cast<char>(value);

    commandLength++;

    return;
  }


  // ----------------------------------------------------------
  // BUFFER PLEIN
  // ----------------------------------------------------------

  overflowCount++;

  commandLength = 0;

  commandInProgress = false;


  Serial.println(
    "DCC-EX : commande trop longue ignoree");
}


// ============================================================
// STATUS
// ============================================================

void printDccExStatus() {

  Serial.println(
    "--------------------------------------");

  Serial.println(
    " DCC-EX");

  Serial.println(
    "--------------------------------------");


  Serial.printf(
    "Commandes recues  : %lu\n",
    (unsigned long)commandCount);


  Serial.printf(
    "POWER ON <1>      : %lu\n",
    (unsigned long)powerOnCount);


  Serial.printf(
    "POWER OFF <0>     : %lu\n",
    (unsigned long)powerOffCount);


  Serial.printf(
    "Commandes <t>     : %lu\n",
    (unsigned long)throttleCount);


  Serial.printf(
    "Commandes invalides: %lu\n",
    (unsigned long)invalidCommandCount);


  Serial.printf(
    "Commandes ignorees: %lu\n",
    (unsigned long)unknownCommandCount);


  Serial.printf(
    "Depassements      : %lu\n",
    (unsigned long)overflowCount);
}