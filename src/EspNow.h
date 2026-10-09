#pragma once

// Transport ESP-NOW pour les commandes DCC-EX.
// Le décodage des commandes reste dans DccExProtocol.cpp.
void initEspNow();
void printEspNowStatus();
