/**
 * @file reset.h
 * @brief Reset button
 */

#ifndef RESET_H
#define RESET_H

// Definicja pinu
const int BUTTON_FLASH = 0; // GPIO0

void initializeFlashButton();
void processFlashButton();

#endif