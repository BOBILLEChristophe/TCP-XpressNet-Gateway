#pragma once

#include <Arduino.h>


// ============================================================
// INITIALISATION
// ============================================================

void initDccExProtocol();


// ============================================================
// RECEPTION
// ============================================================

/*
   Transmet un octet recu par TCP au parseur DCC-EX.

   Les commandes sont delimitees par :

       < ... >

   Premiere version :

       <1>  alimentation voie ON
       <0>  alimentation voie OFF
*/

void processDccExByte(
  uint8_t value);


// ============================================================
// STATUS
// ============================================================

void printDccExStatus();