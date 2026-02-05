/**
 * @file blink.ino
 */

#include "blink.h"

/** @name Global State Variables */
///@{
unsigned long lastBlinkTime = 0;     ///< Last timestamp of a state transition
uint8_t blinksRemaining = 0;         ///< Remaining phases (2 per blink)
bool ledState = HIGH;                ///< Current logical state (HIGH = ON / UART Idle)
const uint16_t BLINK_INTERVAL = 150; ///< Duration of each blink phase in ms
///@}

/**
 * @brief Sets the number of blinks to perform.
 * @param howManyTimes Number of blinks.
 */
void blink(uint8_t howManyTimes) { blinksRemaining = howManyTimes * 2; }

/**
 * @brief Convenience function for a single blink.
 */
void blink() { blink(1); }

/**
 * @brief Processes the blink state machine.
 */
void processBlinks() {
  if (blinksRemaining == 0)
    return;

  unsigned long now = millis();

  if (now - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = now;

    // Zmieniamy stan logiczny
    ledState = !ledState;
    blinksRemaining--;

    // Fizycznie zmieniamy stan pinu przy każdej fazie mrugnięcia
    digitalWrite(LED_BUILTIN, ledState);

    // Finalizacja: Po zakończeniu wszystkich faz upewniamy się, że dioda jest zgaszona
    if (blinksRemaining == 0) {
      ledState = HIGH; // Stan spoczynkowy (OFF dla Active-Low)
      digitalWrite(LED_BUILTIN, ledState);
    }
  }
}