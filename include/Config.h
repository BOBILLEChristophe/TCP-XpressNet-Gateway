#pragma once
#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/uart.h"

constexpr gpio_num_t XPRESSNET_RX_PIN  = GPIO_NUM_18;
constexpr gpio_num_t XPRESSNET_TX_PIN  = GPIO_NUM_17;
constexpr gpio_num_t XPRESSNET_RTS_PIN = GPIO_NUM_16;

constexpr uint32_t XPRESSNET_BAUDRATE = 62500;
constexpr uart_port_t UART_PORT = UART_NUM_1;
constexpr size_t UART_RX_BUFFER_SIZE = 2048;

// Réglages validés sur Lenz LZV100
constexpr uint32_t RESPONSE_DELAY_US = 0;
constexpr uint32_t ACK_DELAY_US = 48;

constexpr uint16_t TEST_LOCO_ADDRESS = 6400;

// Mode exclusif : le TCP Wi-Fi v1.0 reste le choix par défaut.
enum class CommunicationMode : uint8_t {
  WIFI_TCP,
  ESP_NOW
};

constexpr CommunicationMode COMMUNICATION_MODE =
    CommunicationMode::WIFI_TCP; // passer à ESP_NOW pour les essais

// Canal 2,4 GHz identique sur la passerelle et la souris.
constexpr uint8_t ESPNOW_CHANNEL = 6;

// MAC Wi-Fi STA de la souris autorisée. 00:00:00:00:00:00 = désactivé.
// La souris affiche sa MAC dans le moniteur série au démarrage.
constexpr uint8_t ESPNOW_ALLOWED_MOUSE_MAC[6] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
