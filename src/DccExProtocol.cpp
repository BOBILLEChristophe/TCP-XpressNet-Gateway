#include "DccExProtocol.h"

#include "XpressNet.h"

#include <stdlib.h>
#include <string.h>

// ============================================================
// CALLBACK POUR LES REPONSES TCP
// ============================================================

static DccExReplyCallback replyCallback = nullptr;

void setDccExReplyCallback(DccExReplyCallback callback)
{
  replyCallback = callback;
}

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

static void processPowerOn()
{

  uint8_t frame[MAX_XPRESSNET_BYTES];

  size_t length =
      buildPowerOnFrame(
          frame);

  if (queueXpressNetMessage(
          frame,
          length))
  {

    powerOnCount++;

    Serial.println(
        "DCC-EX <1> -> POWER ON");
  }
  else
  {

    Serial.println(
        "DCC-EX <1> -> FIFO XpressNet pleine");
  }
}

// ============================================================
// COMMANDE POWER OFF
// ============================================================

static void processPowerOff()
{

  uint8_t frame[MAX_XPRESSNET_BYTES];

  size_t length =
      buildPowerOffFrame(
          frame);

  if (queueXpressNetMessage(
          frame,
          length))
  {

    powerOffCount++;

    Serial.println(
        "DCC-EX <0> -> POWER OFF");
  }
  else
  {

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

static bool processThrottleCommand()
{

  unsigned int locoAddress = 0;

  int dccExSpeed = 0;

  unsigned int direction = 0;

  char extra = '\0';

  // ----------------------------------------------------------
  // ANALYSE DES COMMANDES DE VITESSE
  //
  // Deux syntaxes acceptees :
  //
  // <t CAB SPEED DIRECTION>
  // <t REGISTER CAB SPEED DIRECTION>
  //
  // Le registre est ignore : la passerelle utilise
  // directement l'adresse DCC de la locomotive.
  // ----------------------------------------------------------

  unsigned int registerId = 0;
  // char extra = '\0';

  // Essai avec registre (4 parametres)
  int parameterCount = sscanf(
      commandBuffer,
      "t %u %u %d %u %c",
      &registerId,
      &locoAddress,
      &dccExSpeed,
      &direction,
      &extra);

  if (parameterCount != 4)
  {

    // Ancienne syntaxe (3 parametres)
    extra = '\0';

    parameterCount = sscanf(
        commandBuffer,
        "t %u %d %u %c",
        &locoAddress,
        &dccExSpeed,
        &direction,
        &extra);

    if (parameterCount != 3)
    {
      return false;
    }
  }

  // ----------------------------------------------------------
  // VALIDATION ADRESSE
  // ----------------------------------------------------------

  if (locoAddress < 1 ||
      locoAddress > 10239)
  {

    return false;
  }

  // ----------------------------------------------------------
  // VALIDATION VITESSE DCC-EX
  // ----------------------------------------------------------

  if (dccExSpeed < -1 ||
      dccExSpeed > 126)
  {

    return false;
  }

  // ----------------------------------------------------------
  // VALIDATION DIRECTION
  // ----------------------------------------------------------

  if (direction > 1)
  {

    return false;
  }

  // ----------------------------------------------------------
  // CONVERSION VITESSE
  // ----------------------------------------------------------

  uint8_t xpressNetSpeed;

  if (dccExSpeed == -1)
  {

    // Emergency Stop

    xpressNetSpeed = 1;
  }
  else if (dccExSpeed == 0)
  {

    // Stop

    xpressNetSpeed = 0;
  }
  else
  {

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
          length))
  {

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
// FONCTIONS LOCOMOTIVE : F0..F28
// ============================================================
// DCC-EX moderne : <F CAB NUMERO ETAT>
// DCC++ classique : <f CAB BYTE1 [BYTE2]>
// Compatibilite ancienne passerelle : <f CAB NUMERO ETAT>
// Les 5 groupes sont memorises par adresse pour eviter
// de modifier les autres fonctions lors d'une bascule.
// ============================================================

namespace
{
  struct FunctionState
  {
    uint16_t address = 0;
    uint8_t groups[5] = {};
    uint32_t lastUse = 0;
  };

  constexpr size_t FUNCTION_STATE_CAPACITY = 32;
  FunctionState functionStates[FUNCTION_STATE_CAPACITY];
  uint32_t functionSequence = 0;
  uint32_t functionCount = 0;

  FunctionState &findFunctionState(uint16_t address)
  {
    FunctionState *oldest = &functionStates[0];
    for (FunctionState &state : functionStates)
    {
      if (state.address == address)
        return state;
      if (state.address == 0)
        return state;
      if (state.lastUse < oldest->lastUse)
        oldest = &state;
    }
    // Cache limite a 32 adresses : la moins recente est reutilisee.
    return *oldest;
  }

  // Seul ce chemin met a jour le cache, APRES mise en FIFO reussie.
  bool sendFunctionGroup(uint16_t address, uint8_t group, uint8_t value)
  {
    uint8_t frame[MAX_XPRESSNET_BYTES];
    const size_t length = buildFunctionGroupFrame(address, group, value, frame);
    if (length == 0)
      return false;

    if (!queueXpressNetMessage(frame, length))
    {
      Serial.println("DCC-EX fonction -> FIFO XpressNet pleine");
      return true; // commande valide mais non transmise
    }

    FunctionState &state = findFunctionState(address);
    if (state.address != address)
    {
      state.address = address;
      memset(state.groups, 0, sizeof(state.groups));
    }
    state.groups[group] = value;
    state.lastUse = ++functionSequence;
    ++functionCount;

    Serial.printf("DCC-EX fonction -> loco %u groupe %u masque 0x%02X\n",
                  address, group, value);
    return true;
  }

  bool sendSingleFunction(uint16_t address, unsigned int number, bool enabled)
  {
    if (number > 28)
      return false;

    uint8_t group;
    uint8_t bit;
    if (number == 0)          { group = 0; bit = 4; }
    else if (number <= 4)     { group = 0; bit = number - 1; }
    else if (number <= 8)     { group = 1; bit = number - 5; }
    else if (number <= 12)    { group = 2; bit = number - 9; }
    else if (number <= 20)    { group = 3; bit = number - 13; }
    else                      { group = 4; bit = number - 21; }

    FunctionState &state = findFunctionState(address);
    const uint8_t previous = state.address == address ? state.groups[group] : 0;
    const uint8_t mask = static_cast<uint8_t>(1U << bit);
    const uint8_t next = enabled ? (previous | mask) : (previous & ~mask);
    return sendFunctionGroup(address, group, next);
  }
} // namespace

static bool processFunctionCommand()
{
  unsigned int address = 0;
  unsigned int first = 0;
  unsigned int second = 0;
  char extra = '\0';
  const int count = sscanf(commandBuffer + 1, " %u %u %u %c",
                           &address, &first, &second, &extra);

  if (address < 1 || address > 10239)
    return false;

  // Syntaxe officielle DCC-EX : <F CAB FONCTION 0|1>.
  if (commandBuffer[0] == 'F')
  {
    if (count != 3 || first > 28 || second > 1)
      return false;
    return sendSingleFunction(static_cast<uint16_t>(address), first, second == 1);
  }

  // DCC++ classique, F13..F20 : <f CAB 222 OCTET>.
  // DCC++ classique, F21..F28 : <f CAB 223 OCTET>.
  if (count == 3 && (first == 222 || first == 223) && second <= 255)
  {
    const uint8_t group = first == 222 ? 3 : 4;
    return sendFunctionGroup(static_cast<uint16_t>(address), group,
                             static_cast<uint8_t>(second));
  }

  // DCC++ classique, F0..F12 : <f CAB OCTET>.
  if (count == 2)
  {
    if (first >= 128 && first <= 159)
      return sendFunctionGroup(static_cast<uint16_t>(address), 0,
                               static_cast<uint8_t>(first - 128));
    if (first >= 176 && first <= 191)
      return sendFunctionGroup(static_cast<uint16_t>(address), 1,
                               static_cast<uint8_t>(first - 176));
    if (first >= 160 && first <= 175)
      return sendFunctionGroup(static_cast<uint16_t>(address), 2,
                               static_cast<uint8_t>(first - 160));
    return false;
  }

  // Compatibilite avec l'ancienne syntaxe non standard de la passerelle.
  if (count == 3 && first <= 28 && second <= 1)
    return sendSingleFunction(static_cast<uint16_t>(address), first, second == 1);

  return false;
}

// ============================================================
// TRAITEMENT D'UNE COMMANDE COMPLETE
// ============================================================

static void processCommand()
{

  // ----------------------------------------------------------
  // TERMINAISON DE LA CHAINE
  // ----------------------------------------------------------

  commandBuffer[commandLength] = '\0';

  commandCount++;

  // Identification demandee par Rocrail : <s>
  if (strcmp(commandBuffer, "s") == 0)
  {

    if (replyCallback != nullptr)
    {
      replyCallback(
          "<iDCCEX 1.0 / ESP32-S3 / LENZ-LZV100 / 1>");
    }

    Serial.println("DCC-EX <s> -> identification");
    return;
  }

  /// ----------------------------------------------------------
  // POWER ON
  // Accepte <1> et <1 MAIN>
  // ----------------------------------------------------------

  if (strcmp(commandBuffer, "1") == 0 ||
      strcmp(commandBuffer, "1 MAIN") == 0)
  {

    processPowerOn();
    return;
  }

  // ----------------------------------------------------------
  // POWER OFF
  // Accepte <0> et <0 MAIN>
  // ----------------------------------------------------------

  if (strcmp(commandBuffer, "0") == 0 ||
      strcmp(commandBuffer, "0 MAIN") == 0)
  {

    processPowerOff();
    return;
  }

  // ----------------------------------------------------------
  // THROTTLE
  // ----------------------------------------------------------

  if (commandBuffer[0] == 't')
  {

    if (processThrottleCommand())
    {

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

  // FONCTIONS F0..F28 : DCC-EX <F> ou DCC++ classique <f>.
  if (commandBuffer[0] == 'F' || commandBuffer[0] == 'f')
  {
    if (processFunctionCommand())
      return;
    ++invalidCommandCount;
    Serial.printf("DCC-EX commande fonction invalide : <%s>\n", commandBuffer);
    return;
  }

  // ----------------------------------------------------------
  // COURANT DE VOIE : <c>
  //
  // La passerelle ne dispose pas actuellement
  // d'une mesure de courant.
  // Commande reconnue, mais aucune valeur inventee.
  // ----------------------------------------------------------

  if (strcmp(commandBuffer, "c") == 0)
  {

    static bool warningDisplayed = false;

    if (!warningDisplayed)
    {
      Serial.println(
          "DCC-EX <c> : mesure de courant indisponible");
      warningDisplayed = true;
    }

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

void initDccExProtocol()
{

  commandLength = 0;

  commandInProgress = false;

  Serial.println(
      "Protocole DCC-EX : <1> / <0> / <t CAB SPEED DIR> / <F CAB FNUM ETAT> / <f CAB BYTE1 [BYTE2]>");
}

// ============================================================
// RECEPTION D'UN OCTET
// ============================================================

void processDccExByte(
    uint8_t value)
{

  // ----------------------------------------------------------
  // DEBUT D'UNE COMMANDE
  // ----------------------------------------------------------

  if (value == '<')
  {

    commandLength = 0;

    commandInProgress = true;

    return;
  }

  // ----------------------------------------------------------
  // RIEN A TRAITER TANT QUE '<' N'A PAS ETE RECU
  // ----------------------------------------------------------

  if (!commandInProgress)
  {

    return;
  }

  // ----------------------------------------------------------
  // FIN DE COMMANDE
  // ----------------------------------------------------------

  if (value == '>')
  {

    commandInProgress = false;

    if (commandLength > 0)
    {

      processCommand();
    }

    commandLength = 0;

    return;
  }

  // ----------------------------------------------------------
  // STOCKAGE
  // ----------------------------------------------------------

  if (commandLength <
      DCCEX_COMMAND_BUFFER_SIZE - 1)
  {

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

void printDccExStatus()
{

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
      "Commandes fonctions: %lu\n",
      (unsigned long)functionCount);

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
