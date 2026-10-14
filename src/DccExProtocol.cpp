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
// FONCTIONS LOCOMOTIVE : <F ADRESSE NUMERO ETAT>
// Alias historique de la souris : <f ADRESSE NUMERO ETAT>.
// F0..F28 : 5 groupes XpressNet, un masque par groupe et par loco.
// Les etats des autres fonctions du groupe sont preserves.
// ============================================================

namespace {
constexpr uint8_t FUNCTION_GROUP_COUNT = 5;

struct FunctionState {
  uint16_t address = 0;
  uint8_t groupMasks[FUNCTION_GROUP_COUNT] = {};
  uint32_t lastUse = 0;
};

// 64 adresses conservees en memoire, eviction de la moins recente.
constexpr size_t FUNCTION_STATE_CAPACITY = 64;
FunctionState functionStates[FUNCTION_STATE_CAPACITY];
uint32_t functionSequence = 0;
uint32_t functionCount = 0;

FunctionState &findFunctionState(uint16_t address) {
  FunctionState *oldest = &functionStates[0];
  for (FunctionState &state : functionStates) {
    if (state.address == address) return state;
    if (state.address == 0) return state;
    if (state.lastUse < oldest->lastUse) oldest = &state;
  }
  return *oldest;
}

// Les numeros de groupe sont des indices internes 0..4.
uint8_t groupForFunction(unsigned int number) {
  if (number <= 4) return 0;       // F0..F4  : 0x20
  if (number <= 8) return 1;       // F5..F8  : 0x21
  if (number <= 12) return 2;      // F9..F12 : 0x22
  if (number <= 20) return 3;      // F13..F20: 0x23
  return 4;                        // F21..F28: 0x28
}

uint8_t bitForFunction(unsigned int number) {
  if (number == 0) return 4;       // F0 = bit 4 du groupe 0
  if (number <= 4) return number - 1;
  if (number <= 8) return number - 5;
  if (number <= 12) return number - 9;
  if (number <= 20) return number - 13;
  return number - 21;
}
} // namespace

static bool processFunctionCommand() {
  unsigned int address = 0;
  unsigned int number = 0;
  unsigned int enabled = 0;
  char extra = '\0';

  // commandBuffer[0] a deja ete verifie : 'F' ou 'f'.
  // Le 'F' majuscule est la commande DCC-EX individuelle standard.
  if (sscanf(commandBuffer + 1, " %u %u %u %c",
             &address, &number, &enabled, &extra) != 3 ||
      address < 1 || address > 10239 ||
      number > 28 || enabled > 1) {
    return false;
  }

  FunctionState &state = findFunctionState(static_cast<uint16_t>(address));
  if (state.address != address) {
    state.address = static_cast<uint16_t>(address);
    memset(state.groupMasks, 0, sizeof(state.groupMasks));
  }

  const uint8_t group = groupForFunction(number);
  const uint8_t bit = bitForFunction(number);
  const uint8_t mask = static_cast<uint8_t>(1U << bit);
  const uint8_t current = state.groupMasks[group];
  const uint8_t next = enabled ? (current | mask) : (current & ~mask);

  uint8_t frame[MAX_XPRESSNET_BYTES];
  const size_t length = buildFunctionGroupFrame(
      static_cast<uint16_t>(address), group, next, frame);
  if (length == 0) {
    Serial.println("DCC-EX <F/f> -> trame XpressNet invalide");
    return false;
  }
  if (!queueXpressNetMessage(frame, length)) {
    Serial.println("DCC-EX <F/f> -> FIFO XpressNet pleine");
    return true;
  }

  state.groupMasks[group] = next;
  state.lastUse = ++functionSequence;
  ++functionCount;
  Serial.printf("DCC-EX <F/f> -> loco %u F%u %s groupe %u masque 0x%02X\n",
                address, number, enabled ? "ON" : "OFF", group, next);
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


  // FONCTIONS F0..F28
  if (commandBuffer[0] == 'f' || commandBuffer[0] == 'F') {
    if (processFunctionCommand()) return;
    ++invalidCommandCount;
    Serial.printf("DCC-EX commande <f> invalide : <%s>\n", commandBuffer);
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
    "Protocole DCC-EX : <1> / <0> / <t CAB SPEED DIRECTION> / <F CAB FNUM ETAT> (alias <f>)");
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