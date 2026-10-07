#pragma once

#include <Arduino.h>
#include "driver/gpio.h"

// ============================================================
// HARDWARE
// ============================================================

constexpr gpio_num_t XPRESSNET_RX_PIN  = GPIO_NUM_18;
constexpr gpio_num_t XPRESSNET_TX_PIN  = GPIO_NUM_17;
constexpr gpio_num_t XPRESSNET_RTS_PIN = GPIO_NUM_16;


// ============================================================
// XPRESSNET
// ============================================================

constexpr uint32_t XPRESSNET_BAUDRATE = 62500;
 constexpr uart_port_t UART_PORT = UART_NUM_1;
 constexpr size_t UART_RX_BUFFER_SIZE = 2048;

// ============================================================
// TIMING
// ============================================================

/*
   10 us :

   valeur utilisée dans la version non-FreeRTOS
   que nous venons de valider.

   Les essais ont également fonctionné avec
   une plage beaucoup plus large.
*/

 constexpr uint32_t RESPONSE_DELAY_US = 10;


/*
   ACK 0x95 :

   48 us déjà validés lors des essais
   de récupération de la LZV100.
*/

static constexpr uint32_t ACK_DELAY_US = 48;


// ============================================================
// LOCOMOTIVE DE TEST
// ============================================================

constexpr uint16_t TEST_LOCO_ADDRESS = 6400;