#pragma once

#include <Arduino.h>

// ============================================================
// CONSTANTES PUBLIQUES
// ============================================================

constexpr size_t MAX_XPRESSNET_BYTES = 16;


// ============================================================
// INITIALISATION
// ============================================================

void initXpressNet();


// ============================================================
// CONSTRUCTION DES TRAMES XPRESSNET
// ============================================================

size_t buildPowerOnFrame(
  uint8_t *frame);

size_t buildPowerOffFrame(
  uint8_t *frame);

size_t buildSpeedFrame(
  uint16_t locoAddress,
  bool directionForward,
  uint8_t speed,
  uint8_t *frame);

size_t buildFunctionFrame(
  uint16_t locoAddress,
  uint8_t functions,
  uint8_t *frame);


// ============================================================
// AJOUT D'UNE TRAME A LA FIFO XPRESSNET
// ============================================================

bool queueXpressNetMessage(
  const uint8_t *data,
  size_t length);


// ============================================================
// STATUS
// ============================================================

void printXpressNetStatus();