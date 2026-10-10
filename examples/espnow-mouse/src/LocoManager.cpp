#include "LocoManager.h"

namespace {
// CONFIGURATION DIY : modifier ici les adresses DCC de chaque emplacement.
// 0 signifie "emplacement libre" et interdit toute selection de ce slot.
// Ne jamais saisir deux fois la meme adresse.
// Seule l'adresse 6400 est renseignee par defaut ; ajouter les autres
// adresses reelles avant les essais multilo.
constexpr uint16_t DCC_ADDRESSES[LocoManager::MAX_SLOTS] = {
    6400,  // Loco 1
       0,  // Loco 2
       0,  // Loco 3
       0,  // Loco 4
       0,  // Loco 5
       0,  // Loco 6
       0,  // Loco 7
       0   // Loco 8
};
} // namespace

void LocoManager::begin() {
  selectedIndex_ = 0;
  for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
    locos_[i] = MouseState{};
    locos_[i].slot = i + 1;
    locos_[i].address = DCC_ADDRESSES[i];
    locos_[i].speed = 0;
    locos_[i].forward = true;
  }

  for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
    if (locos_[i].address == 0) continue;
    for (uint8_t j = i + 1; j < MAX_SLOTS; ++j) {
      if (locos_[i].address == locos_[j].address) {
        Serial.printf("ATTENTION : adresse DCC %u dupliquee (slots %u et %u)\n",
                      locos_[i].address, i + 1, j + 1);
      }
    }
  }

  Serial.println("Locomotives configurees :");
  for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
    if (locos_[i].address != 0) {
      Serial.printf("  Loco %u -> DCC %u\n", i + 1, locos_[i].address);
    }
  }
}

bool LocoManager::configured(uint8_t slot) const {
  return slot >= 1 && slot <= MAX_SLOTS && locos_[slot - 1].address != 0;
}

bool LocoManager::select(uint8_t slot) {
  if (!configured(slot)) return false;
  selectedIndex_ = slot - 1;
  return true;
}

MouseState &LocoManager::current() {
  return locos_[selectedIndex_];
}

const MouseState &LocoManager::current() const {
  return locos_[selectedIndex_];
}

MouseState *LocoManager::findByAddress(uint16_t address) {
  if (address == 0) return nullptr;
  for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
    if (locos_[i].address == address) return &locos_[i];
  }
  return nullptr;
}
