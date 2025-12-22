//io_ui.h

#pragma once
#include <Arduino.h>

// Initializes LED and BOOT button GPIO.
void ioSetup(int ledPin, bool ledActiveLow, int bootPin);

// Returns true on BOOT button press edge (debounced).
bool ioBootPressedOnce();

// Call from the main loop to update LED state machine.
void ioLoop();

// Globally enable/disable LED output (forces LED off when disabled).
void ioSetLedEnabled(bool en);

// Enable/disable heartbeat breathing animation.
// When enabled, heartbeat runs only if neither AP nor STA blink is active.
void ioSetHeartbeatEnabled(bool en);

// Fast blink while AP/captive portal is active.
void ioSetApBlinkEnabled(bool en);

// Slow blink while STA is connecting / reconnecting.
void ioSetStaBlinkEnabled(bool en);

// Short LED pulse for network activity (TX/RX).
void ioPulseActivity(uint16_t ms = 60);
