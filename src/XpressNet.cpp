#include "XpressNet.h"
#include "driver/uart.h"  // types UART requis par Config.h
#include "Config.h"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


// ============================================================
// CALL BYTES
// ============================================================

/*
   Adresse XpressNet 21.

   Valeurs déjà validées :

       Normal Inquiry = 0x55
       Request ACK    = 0x95

   On les garde volontairement explicites.
*/

static constexpr uint8_t CALL_NORMAL = 0x55;
static constexpr uint8_t CALL_ACK = 0x95;


// ============================================================
// RMT
// ============================================================

static constexpr uint32_t RMT_RESOLUTION_HZ = 1000000;
static constexpr uint16_t XPRESSNET_BIT_US = 16;

static rmt_channel_handle_t txChannel = nullptr;
static rmt_encoder_handle_t txEncoder = nullptr;


// ============================================================
// MESSAGES PREPARES : POOL STATIQUE + FILE DE POINTEURS
// ============================================================

/*
   Les symboles RMT sont prepares dans un tampon reserve AVANT
   la reception du Call Byte.

   L'ancienne FIFO copiait la structure entiere (jusqu'a 368 octets)
   dans XpressNetTask, apres 0x55.

   Desormais, la FIFO ne transporte qu'un pointeur (4 octets sur ESP32).
   Le pool est statique : pas de malloc/free, pas de fragmentation.
   Chaque tampon est rendu au pool APRES la fin du RMT.
*/

static constexpr size_t MAX_RMT_SYMBOLS = 90;

struct PreparedXpressNetMessage {
  size_t symbolCount;
  rmt_symbol_word_t symbols[MAX_RMT_SYMBOLS];
};

static constexpr UBaseType_t TX_QUEUE_LENGTH = 16;

static PreparedXpressNetMessage txMessagePool[TX_QUEUE_LENGTH];

// Pointeurs vers messages en attente d'une interrogation 0x55.
static QueueHandle_t xpressNetTxQueue = nullptr;

// Pointeurs vers tampons disponibles pour preparer une commande.
static QueueHandle_t freeTxSlotsQueue = nullptr;


// ============================================================
// ACK PRECONSTRUIT
// ============================================================

static rmt_symbol_word_t ackSymbols[16];

static size_t ackSymbolCount = 0;


// ============================================================
// STATISTIQUES
// ============================================================

static volatile uint32_t uartByteCount = 0;
static volatile uint32_t normalInquiryCount = 0;
static volatile uint32_t ackRequestCount = 0;
static volatile uint32_t commandTxCount = 0;
static volatile uint32_t ackTxCount = 0;
static volatile uint32_t txErrorCount = 0;


// ============================================================
// XOR
// ============================================================

static uint8_t calculateXor(
  const uint8_t *data,
  size_t length) {

  uint8_t result = 0;

  for (size_t i = 0; i < length; i++)
    result ^= data[i];

  return result;
}


// ============================================================
// ADRESSE LOCOMOTIVE
// ============================================================

static void encodeLocoAddress(
  uint16_t address,
  uint8_t &AH,
  uint8_t &AL) {

  if (address <= 99) {
    AH = 0x00;
    AL = (uint8_t)address;
  } else {
    AH = 0xC0 | ((address >> 8) & 0x3F);
    AL = address & 0xFF;
  }
}


// ============================================================
// AJOUT D'UN CARACTERE XPRESSNET
// ============================================================

static void appendXpressNetChar(
  bool *bits,
  size_t &index,
  uint8_t value) {

  // START
  bits[index++] = false;

  // D0 ... D7
  for (uint8_t bit = 0; bit < 8; bit++)
    bits[index++] = ((value >> bit) & 0x01) != 0;

  // BIT9 = 0 : DATA BYTE
  bits[index++] = false;

  // STOP
  bits[index++] = true;
}


// ============================================================
// CONVERSION TRAME -> SYMBOLES RMT
// ============================================================

static bool buildSymbols(
  const uint8_t *frame,
  size_t frameLength,
  rmt_symbol_word_t *symbols,
  size_t &symbolCount) {

  if (frame == nullptr ||
      frameLength == 0 ||
      frameLength > MAX_XPRESSNET_BYTES)
    return false;

  static constexpr size_t MAX_BITS =
    MAX_XPRESSNET_BYTES * 11 + 1;

  bool bits[MAX_BITS];

  size_t bitIndex = 0;


  // ----------------------------------------------------------
  // Construction des caractères physiques
  // ----------------------------------------------------------

  for (size_t i = 0; i < frameLength; i++)
    appendXpressNetChar(
      bits,
      bitIndex,
      frame[i]);


  // ----------------------------------------------------------
  // Nombre pair de bits pour le RMT
  // ----------------------------------------------------------

  if (bitIndex & 1)
    bits[bitIndex++] = true;

  symbolCount =
    bitIndex / 2;

  if (symbolCount > MAX_RMT_SYMBOLS)
    return false;


  // ----------------------------------------------------------
  // Deux bits XpressNet par symbole RMT
  // ----------------------------------------------------------

  for (size_t i = 0; i < symbolCount; i++) {

    symbols[i].level0 =
      bits[i * 2];

    symbols[i].duration0 =
      XPRESSNET_BIT_US;

    symbols[i].level1 =
      bits[(i * 2) + 1];

    symbols[i].duration1 =
      XPRESSNET_BIT_US;
  }

  return true;
}


// ============================================================
// EMISSION DE SYMBOLES DEJA PREPARES
// ============================================================

static bool transmitPreparedSymbols(
  const rmt_symbol_word_t *symbols,
  size_t symbolCount) {

  if (symbols == nullptr ||
      symbolCount == 0) {

    return false;
  }

  rmt_transmit_config_t txConfig = {};

  txConfig.loop_count =
    0;

  txConfig.flags.eot_level =
    1;


  // ----------------------------------------------------------
  // SP3485 -> émission
  // ----------------------------------------------------------

  gpio_set_level(
    XPRESSNET_RTS_PIN,
    1);


  // ----------------------------------------------------------
  // RMT
  // ----------------------------------------------------------

  esp_err_t err =
    rmt_transmit(
      txChannel,
      txEncoder,
      symbols,
      symbolCount * sizeof(rmt_symbol_word_t),
      &txConfig);


  if (err == ESP_OK) {

    err =
      rmt_tx_wait_all_done(
        txChannel,
        pdMS_TO_TICKS(20));
  }


  // ----------------------------------------------------------
  // SP3485 -> réception
  // ----------------------------------------------------------

  gpio_set_level(
    XPRESSNET_RTS_PIN,
    0);


  if (err != ESP_OK) {

    txErrorCount++;

    return false;
  }

  return true;
}


// ============================================================
// PREPARATION ACK 20 20
// ============================================================

static void prepareAckFrame() {

  const uint8_t frame[] = {
    0x20,
    0x20
  };

  bool result =
    buildSymbols(
      frame,
      sizeof(frame),
      ackSymbols,
      ackSymbolCount);


  if (!result) {

    Serial.println(
      "ERREUR preparation ACK");

    while (true) {
      delay(1000);
    }
  }
}


// ============================================================
// POWER ON
// ============================================================

size_t buildPowerOnFrame(
  uint8_t *frame) {

  frame[0] =
    0x21;

  frame[1] =
    0x81;

  frame[2] =
    frame[0] ^ frame[1];

  return 3;
}


// ============================================================
// POWER OFF
// ============================================================

size_t buildPowerOffFrame(
  uint8_t *frame) {

  frame[0] =
    0x21;

  frame[1] =
    0x80;

  frame[2] =
    frame[0] ^ frame[1];

  return 3;
}


// ============================================================
// VITESSE / DIRECTION
// ============================================================

size_t buildSpeedFrame(
  uint16_t locoAddress,
  bool directionForward,
  uint8_t speed,
  uint8_t *frame) {

  uint8_t AH;
  uint8_t AL;

  encodeLocoAddress(
    locoAddress,
    AH,
    AL);


  uint8_t RV =
    speed & 0x7F;


  if (directionForward) {

    RV |=
      0x80;
  }


  frame[0] =
    0xE4;

  frame[1] =
    0x13;

  frame[2] =
    AH;

  frame[3] =
    AL;

  frame[4] =
    RV;


  frame[5] =
    calculateXor(
      frame,
      5);


  return 6;
}


// ============================================================
// F0 ... F4
// ============================================================

size_t buildFunctionFrame(
  uint16_t locoAddress,
  uint8_t functions,
  uint8_t *frame) {

  uint8_t AH;
  uint8_t AL;


  encodeLocoAddress(
    locoAddress,
    AH,
    AL);


  frame[0] =
    0xE4;

  frame[1] =
    0x20;

  frame[2] =
    AH;

  frame[3] =
    AL;

  frame[4] =
    functions;


  frame[5] =
    calculateXor(
      frame,
      5);


  return 6;
}


// ============================================================
// AJOUT D'UNE TRAME PREPAREE A LA FILE (POINTEUR SEULEMENT)
// ============================================================

bool queueXpressNetMessage(
  const uint8_t *data,
  size_t length) {

  if (data == nullptr ||
      length == 0 ||
      length > MAX_XPRESSNET_BYTES) {
    Serial.println("ERREUR : message XpressNet invalide");
    return false;
  }

  if (freeTxSlotsQueue == nullptr || xpressNetTxQueue == nullptr) {
    Serial.println("ERREUR : FIFO XpressNet non initialisee");
    return false;
  }

  // Recuperation d'un tampon LIBRE avant de preparer les symboles.
  // Plusieurs producteurs (Serial, TCP) peuvent appeler cette fonction.
  PreparedXpressNetMessage *message = nullptr;

  if (xQueueReceive(freeTxSlotsQueue, &message,
                    pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("ERREUR : FIFO XpressNet pleine (aucun tampon libre)");
    return false;
  }

  // Construction en dehors de la tache XpressNet et du chemin critique.
  if (!buildSymbols(data, length,
                    message->symbols, message->symbolCount)) {
    xQueueSend(freeTxSlotsQueue, &message, 0);
    Serial.println("ERREUR preparation symboles RMT");
    return false;
  }

  // La file ne copie que le POINTEUR, jamais les symboles RMT.
  if (xQueueSend(xpressNetTxQueue, &message,
                 pdMS_TO_TICKS(100)) != pdTRUE) {
    xQueueSend(freeTxSlotsQueue, &message, 0);
    Serial.println("ERREUR : FIFO XpressNet pleine");
    return false;
  }

  return true;
}


// ============================================================
// XPRESSNET TASK
// ============================================================

static void XpressNetTask(
  void *parameter) {

  uint8_t byte;


  for (;;) {

    /*
       Cette tâche reste bloquée ici
       tant qu'aucun caractère n'arrive.

       Elle est la seule tâche propriétaire
       de l'émission XpressNet.
    */

    int length =
      uart_read_bytes(
        UART_PORT,
        &byte,
        1,
        portMAX_DELAY);


    if (length != 1) {
      continue;
    }


    uartByteCount++;


    // --------------------------------------------------------
    // REQUEST ACK : 0x95
    // --------------------------------------------------------

    if (byte == CALL_ACK) {

      ackRequestCount++;


      /*
         Même principe que notre programme
         de récupération validé :

             0x95
               |
              48 us
               |
             20 20
      */

      delayMicroseconds(
        ACK_DELAY_US);


      if (transmitPreparedSymbols(
            ackSymbols,
            ackSymbolCount)) {

        ackTxCount++;
      }

      continue;
    }


    // --------------------------------------------------------
    // NORMAL INQUIRY : 0x55
    // --------------------------------------------------------

    if (byte == CALL_NORMAL) {

      normalInquiryCount++;


      // Une seule copie de 4 octets (pointeur) apres le Call Byte.
      // Aucun encodage RMT, aucune copie de tableau dans cette section.
      PreparedXpressNetMessage *message = nullptr;

      if (xQueueReceive(xpressNetTxQueue, &message, 0) == pdTRUE) {

        if (RESPONSE_DELAY_US > 0) {
          delayMicroseconds(RESPONSE_DELAY_US);
        }

        if (transmitPreparedSymbols(
              message->symbols,
              message->symbolCount)) {
          commandTxCount++;
        }

        // Le RMT a termine : le tampon peut etre reutilise.
        // Cette operation a lieu APRES la section temporelle critique.
        if (xQueueSend(freeTxSlotsQueue, &message, 0) != pdTRUE) {
          txErrorCount++;
        }
      }

      continue;
    }
  }
}


// ============================================================
// INITIALISATION UART
// ============================================================

static void initXpressNetUart() {

  uart_config_t uartConfig = {};


  uartConfig.baud_rate =
    XPRESSNET_BAUDRATE;


  uartConfig.data_bits =
    UART_DATA_8_BITS;


  uartConfig.parity =
    UART_PARITY_ODD;


  uartConfig.stop_bits =
    UART_STOP_BITS_1;


  uartConfig.flow_ctrl =
    UART_HW_FLOWCTRL_DISABLE;


  uartConfig.source_clk =
    UART_SCLK_DEFAULT;


  ESP_ERROR_CHECK(
    uart_driver_install(
      UART_PORT,
      UART_RX_BUFFER_SIZE,
      0,
      0,
      nullptr,
      0));


  ESP_ERROR_CHECK(
    uart_param_config(
      UART_PORT,
      &uartConfig));


  ESP_ERROR_CHECK(
    uart_set_pin(
      UART_PORT,
      UART_PIN_NO_CHANGE,
      XPRESSNET_RX_PIN,
      UART_PIN_NO_CHANGE,
      UART_PIN_NO_CHANGE));


  // CRITIQUE : reveiller la tache des le premier octet XpressNet.
  // Sans ce seuil, le pilote UART peut retarder le Call Byte et
  // les commandes sont ignorees ou executees avec retard.
  ESP_ERROR_CHECK(
    uart_set_rx_full_threshold(UART_PORT, 1));

  ESP_ERROR_CHECK(
    uart_flush_input(UART_PORT));
}


// ============================================================
// INITIALISATION RMT
// ============================================================

static void initXpressNetRmt() {

  rmt_tx_channel_config_t txConfig = {};


  txConfig.gpio_num =
    XPRESSNET_TX_PIN;


  txConfig.clk_src =
    RMT_CLK_SRC_DEFAULT;


  txConfig.resolution_hz =
    RMT_RESOLUTION_HZ;


  txConfig.mem_block_symbols =
    64;


  txConfig.trans_queue_depth =
    4;


  txConfig.intr_priority =
    0;


  txConfig.flags.invert_out =
    false;


  txConfig.flags.with_dma =
    false;


  ESP_ERROR_CHECK(
    rmt_new_tx_channel(
      &txConfig,
      &txChannel));


  rmt_copy_encoder_config_t
    encoderConfig = {};


  ESP_ERROR_CHECK(
    rmt_new_copy_encoder(
      &encoderConfig,
      &txEncoder));


  ESP_ERROR_CHECK(
    rmt_enable(
      txChannel));
}


// ============================================================
// INITIALISATION XPRESSNET
// ============================================================

void initXpressNet() {

  // ----------------------------------------------------------
  // RTS
  // ----------------------------------------------------------

  gpio_config_t ioConfig = {};

  ioConfig.pin_bit_mask =
    (1ULL << XPRESSNET_RTS_PIN);

  ioConfig.mode =
    GPIO_MODE_OUTPUT;

  ioConfig.pull_up_en =
    GPIO_PULLUP_DISABLE;

  ioConfig.pull_down_en =
    GPIO_PULLDOWN_DISABLE;

  ioConfig.intr_type =
    GPIO_INTR_DISABLE;

  ESP_ERROR_CHECK(
    gpio_config(&ioConfig));

  gpio_set_level(
    XPRESSNET_RTS_PIN,
    0);


  // ----------------------------------------------------------
  // UART
  // ----------------------------------------------------------

  initXpressNetUart();


  // ----------------------------------------------------------
  // RMT
  // ----------------------------------------------------------

  initXpressNetRmt();


  // ----------------------------------------------------------
  // ACK
  // ----------------------------------------------------------

  prepareAckFrame();


  // ----------------------------------------------------------
  // DEUX FILES DE POINTEURS + POOL STATIQUE
  // ----------------------------------------------------------

  xpressNetTxQueue = xQueueCreate(
    TX_QUEUE_LENGTH, sizeof(PreparedXpressNetMessage *));

  freeTxSlotsQueue = xQueueCreate(
    TX_QUEUE_LENGTH, sizeof(PreparedXpressNetMessage *));

  if (xpressNetTxQueue == nullptr || freeTxSlotsQueue == nullptr) {
    Serial.println("ERREUR creation FIFO XpressNet");
    while (true) { delay(1000); }
  }

  // Tous les tampons sont libres au demarrage.
  for (size_t i = 0; i < TX_QUEUE_LENGTH; ++i) {
    PreparedXpressNetMessage *message = &txMessagePool[i];
    if (xQueueSend(freeTxSlotsQueue, &message, 0) != pdTRUE) {
      Serial.println("ERREUR initialisation pool XpressNet");
      while (true) { delay(1000); }
    }
  }

  // ----------------------------------------------------------
  // XPRESSNET TASK
  // ----------------------------------------------------------

  BaseType_t result;

  result =
    xTaskCreate(
      XpressNetTask,
      "XpressNetTask",
      4096,
      nullptr,
      4,
      nullptr);


  if (result != pdPASS) {

    Serial.println(
      "ERREUR creation XpressNetTask");

    while (true) {
      delay(1000);
    }
  }
}


// ============================================================
// STATUS XPRESSNET
// ============================================================

void printXpressNetStatus() {

  Serial.printf(
    "UART bytes       : %lu\n",
    (unsigned long)uartByteCount);

  Serial.printf(
    "0x55             : %lu\n",
    (unsigned long)normalInquiryCount);

  Serial.printf(
    "0x95             : %lu\n",
    (unsigned long)ackRequestCount);

  Serial.printf(
    "ACK 20 20        : %lu\n",
    (unsigned long)ackTxCount);

  Serial.printf(
    "Messages TX      : %lu\n",
    (unsigned long)commandTxCount);

  Serial.printf(
    "Erreurs TX       : %lu\n",
    (unsigned long)txErrorCount);


  UBaseType_t waiting =
    uxQueueMessagesWaiting(
      xpressNetTxQueue);


  Serial.printf(
    "FIFO attente     : %u / %u\n",
    (unsigned int)waiting,
    (unsigned int)TX_QUEUE_LENGTH);
}