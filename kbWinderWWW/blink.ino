/**
 * @file blink.ino
 * @brief Asynchronous LED control using Serial1 TX (GPIO2) signal levels.
 * * @details This implementation uses a hardware trick: GPIO2 (D4) is the TX pin
 * for Serial1. In UART, the IDLE state is HIGH. By default, an LED connected
 * to this pin will be ON. To turn it OFF, we flood the UART buffer with 0x00
 * bytes, which pulls the line LOW for the duration of the transmission.
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
 * * @note When blinksRemaining > 0 and ledState is LOW, the function floods
 * Serial1 with 0x00 bytes to keep the line LOW (LED OFF).
 * When blinksRemaining is 0, the line returns to IDLE (HIGH), turning the LED ON.
 */
void processBlinks() {
  // If no blinks are scheduled, UART remains IDLE (High), so the LED is ON.
  if (blinksRemaining == 0)
    return;

  unsigned long now = millis();

  /** * @section DarkPhase
   * If the current state is LOW, we need to actively turn the LED OFF.
   * We flood the buffer with zeros to force the line LOW.
   */
  if (ledState == LOW) {
    if (debugSerial.availableForWrite() >= 32) {
      for (int i = 0; i < 32; i++) {
        debugSerial.write((uint8_t)0x00);
      }
    }
  }

  // Handle state transitions based on time interval
  if (now - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = now;
    ledState = !ledState;
    blinksRemaining--;

    // Finalize: Return to IDLE state (High / LED ON)
    if (blinksRemaining == 0) {
      ledState = HIGH;
      debugSerial.print(F("\r")); // Clear terminal line after the flood of zeros
    }
  }
}