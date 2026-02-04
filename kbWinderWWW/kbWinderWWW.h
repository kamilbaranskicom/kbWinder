/**
 * @file kbWinder.h
 * @brief Global constants, enumerations, and structures for kbWinder controller
 * @author Kamil Baranski
 * @date 2026-01-23
 */

#ifndef KBWINDER_H
#define KBWINDER_H

#include <Arduino.h>

/** * @name Visual Assets
 * ASCII art logo used during system startup.
 */
///@{
const char logo1[] PROGMEM = "░██       ░██        ░██       ░██ ░██                  ░██                     ";
const char logo2[] PROGMEM = "░██       ░██        ░██       ░██                      ░██                     ";
const char logo3[] PROGMEM = "░██    ░██░████████  ░██  ░██  ░██ ░██░████████   ░████████  ░███████  ░██░████ ";
const char logo4[] PROGMEM = "░██   ░██ ░██    ░██ ░██ ░████ ░██ ░██░██    ░██ ░██    ░██ ░██    ░██ ░███     ";
const char logo5[] PROGMEM = "░███████  ░██    ░██ ░██░██ ░██░██ ░██░██    ░██ ░██    ░██ ░█████████ ░██      ";
const char logo6[] PROGMEM = "░██   ░██ ░███   ░██ ░████   ░████ ░██░██    ░██ ░██   ░███ ░██        ░██      ";
const char logo7[] PROGMEM = "░██    ░██░██░█████  ░███     ░███ ░██░██    ░██  ░█████░██  ░███████  ░██      ";
const char logo8[] PROGMEM = "";
const char logo9[] PROGMEM = "";

/**
 * @brief Array of pointers to the ASCII logo lines in Flash memory.
 */
const __FlashStringHelper *logo[] = {
    FPSTR(logo1), FPSTR(logo2), FPSTR(logo3), FPSTR(logo4), FPSTR(logo5), FPSTR(logo6), FPSTR(logo7), FPSTR(logo8), FPSTR(logo9)};
///@}

#endif // KBWINDER_H