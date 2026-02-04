/**
 * @file blink.h
 * @brief UART-based LED control (Hardware Hack).
 * @author Kamil Baranski
 * @date 2026-01-23
 */

#ifndef BLINK_H
#define BLINK_H

#include <Arduino.h>

/** @name LED Control Functions */
///@{

/**
 * @brief Triggers a specific number of blinks.
 * @param howManyTimes Number of blink cycles to execute.
 */
void blink(uint8_t howManyTimes);

/**
 * @brief Triggers a single blink.
 */
void blink();

/**
 * @brief Non-blocking handler for the blink state machine.
 * Must be called frequently in the main loop.
 */
void processBlinks();

///@}

#endif // BLINK_H