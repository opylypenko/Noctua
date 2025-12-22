//io_ui.cp

#include "io_ui.h"

#include <Ticker.h>

// ============================================================
// GPIO / state
// ============================================================

static int  gLedPin = -1;
static bool gLedActiveLow = true;
static int  gBootPin = 0;

static bool gApBlink = false;
static bool gStaBlink = false;
static bool gLedEnabled = true;

// Debounce
static const uint32_t BOOT_DEBOUNCE_MS = 50;

// Blink rates
static const uint32_t AP_BLINK_MS  = 120;  // fast blink for AP mode
static const uint32_t STA_BLINK_MS = 700;  // slow blink for STA connecting

// Activity pulse (highest priority)
static uint32_t gPulseUntilMs = 0;

// Heartbeat animation
static bool gHeartbeatActive = false;
static uint32_t gHeartbeatStartMs = 0;
static const uint32_t HEARTBEAT_DURATION_MS = 800;
static bool gHeartbeatEnabled = false;

// GPIO16 has no hardware PWM (analogWrite). Provide a lightweight software PWM.
static Ticker gSoftPwmTicker;
static const uint8_t SOFT_PWM_STEPS = 16;          // duty resolution
static const uint32_t SOFT_PWM_TICK_MS = 1;        // 1 kHz tick => ~62.5 Hz PWM
static volatile uint8_t gSoftPwmPhase = 0;
static volatile uint8_t gSoftPwmDutySteps = 0;     // 0..SOFT_PWM_STEPS
static bool gSoftPwmRunning = false;

// Blink engine
static uint32_t gLastBlinkMs = 0;
static bool gLedOn = false;

// ============================================================
// Internal helpers
// ============================================================

static void ledWrite(bool on) {
  if (gLedPin < 0) return;
  gLedOn = on;
  digitalWrite(gLedPin, gLedActiveLow ? !on : on);
}

static void ledWritePWM(uint8_t brightness) {
  if (gLedPin < 0) return;
  analogWrite(gLedPin, gLedActiveLow ? (255 - brightness) : brightness);
}

static void softPwmStop() {
  if (!gSoftPwmRunning) return;
  gSoftPwmTicker.detach();
  gSoftPwmRunning = false;
  gSoftPwmPhase = 0;
  gSoftPwmDutySteps = 0;
  // Force off
  digitalWrite(gLedPin, gLedActiveLow ? HIGH : LOW);
}

static void softPwmTick() {
  // This callback runs from the SDK timer context; keep it tiny.
  uint8_t phase = gSoftPwmPhase;
  phase++;
  if (phase >= SOFT_PWM_STEPS) phase = 0;
  gSoftPwmPhase = phase;

  const bool on = (phase < gSoftPwmDutySteps);
  digitalWrite(gLedPin, gLedActiveLow ? !on : on);
}

static void softPwmSetBrightness(uint8_t brightness) {
  if (gLedPin < 0) return;

  // Map 0..255 => 0..SOFT_PWM_STEPS
  const uint16_t steps = (uint16_t)((brightness * (uint16_t)SOFT_PWM_STEPS + 254) / 255);
  gSoftPwmDutySteps = (steps > SOFT_PWM_STEPS) ? SOFT_PWM_STEPS : (uint8_t)steps;

  if (!gSoftPwmRunning) {
    gSoftPwmPhase = 0;
    gSoftPwmTicker.attach_ms(SOFT_PWM_TICK_MS, softPwmTick);
    gSoftPwmRunning = true;
  }
}

static void resetBlinkPhase() {
  gLastBlinkMs = millis();
  gLedOn = false;
}

// ============================================================
// Public API
// ============================================================

void ioSetup(int ledPin, bool ledActiveLow, int bootPin) {
  gLedPin = ledPin;
  gLedActiveLow = ledActiveLow;
  gBootPin = bootPin;

  if (gLedPin >= 0) pinMode(gLedPin, OUTPUT);
  pinMode(gBootPin, INPUT_PULLUP);

  gApBlink = false;
  gStaBlink = false;
  gPulseUntilMs = 0;
  gLedEnabled = true;

  resetBlinkPhase();
  ledWrite(false);

  // Ensure software PWM is off on boot.
  if (gLedPin == 16) {
    softPwmStop();
  }
}

void ioSetLedEnabled(bool en) {
  if (gLedEnabled == en) return;
  gLedEnabled = en;

  if (!gLedEnabled) {
    gApBlink = false;
    gStaBlink = false;
    gHeartbeatActive = false;
    gHeartbeatEnabled = false;
    gPulseUntilMs = 0;
    resetBlinkPhase();
    ledWrite(false);

    if (gLedPin == 16) {
      softPwmStop();
    }
  } else {
    resetBlinkPhase();
  }
}

void ioSetHeartbeatEnabled(bool en) {
  if (!gLedEnabled) {
    gHeartbeatEnabled = false;
    gHeartbeatActive = false;
    if (gLedPin == 16) softPwmStop();
    return;
  }

  if (gHeartbeatEnabled == en) return;
  gHeartbeatEnabled = en;

  if (!gHeartbeatEnabled) {
    gHeartbeatActive = false;
    if (gLedPin == 16) softPwmStop();
    ledWrite(false);
  } else {
    gHeartbeatActive = true;
    gHeartbeatStartMs = millis();
  }
}

bool ioBootPressedOnce() {
  static bool last = HIGH;
  static uint32_t lastEdgeMs = 0;

  const bool cur = digitalRead(gBootPin);
  const uint32_t now = millis();

  if (cur != last && (now - lastEdgeMs) > BOOT_DEBOUNCE_MS) {
    lastEdgeMs = now;
    last = cur;
    if (cur == LOW) return true;
  }
  return false;
}

void ioSetApBlinkEnabled(bool en) {
  if (!gLedEnabled) {
    gApBlink = false;
    gStaBlink = false;
    return;
  }
  if (gApBlink == en) return;

  gApBlink = en;

  // AP blink has priority; disable STA blink when AP is enabled
  if (gApBlink) gStaBlink = false;

  resetBlinkPhase();
  if (!gApBlink && !gStaBlink && gPulseUntilMs == 0) ledWrite(false);
}

void ioSetStaBlinkEnabled(bool en) {
  if (!gLedEnabled) {
    gStaBlink = false;
    return;
  }
  // AP mode always wins
  if (gApBlink) {
    gStaBlink = false;
    return;
  }

  if (gStaBlink == en) return;

  gStaBlink = en;
  resetBlinkPhase();

  if (!gStaBlink && !gApBlink && gPulseUntilMs == 0) ledWrite(false);
}

void ioPulseActivity(uint16_t ms) {
  (void)ms;
  if (!gLedEnabled) return;
  // Trigger heartbeat animation
  gHeartbeatActive = true;
  gHeartbeatStartMs = millis();
}

void ioLoop() {
  if (gLedPin < 0) return;

  if (!gLedEnabled) {
    ledWrite(false);
    if (gLedPin == 16) softPwmStop();
    return;
  }

  const uint32_t now = millis();

  // Blink modes override heartbeat.
  if (gApBlink || gStaBlink) {
    if (gHeartbeatActive) {
      gHeartbeatActive = false;
      if (gLedPin == 16) softPwmStop();
    }
  } else {
    // No blink active: heartbeat follows the enabled flag.
    if (gHeartbeatEnabled && !gHeartbeatActive) {
      gHeartbeatActive = true;
      gHeartbeatStartMs = now;
    } else if (!gHeartbeatEnabled && gHeartbeatActive) {
      gHeartbeatActive = false;
      if (gLedPin == 16) softPwmStop();
    }
  }

  // 1) Heartbeat animation (highest priority)
  if (gHeartbeatActive) {
    const uint32_t elapsed = now - gHeartbeatStartMs;
    // Continuous breathing: loop with no pause.
    const uint32_t duration = 1600;
    const uint32_t phase = (duration == 0) ? 0 : (elapsed % duration);
    
    // Symmetric fade in/out.
    const uint32_t fadeInDuration = 800;
    const uint32_t fadeOutDuration = 800;
    if (phase < duration) {
      uint8_t brightness;
      
      if (phase < fadeInDuration) {
        // Fade in: 0 → 255
        brightness = (phase * 255) / fadeInDuration;
      } else {
        // Fade out: 255 → 0
        const uint32_t fadeOut = phase - fadeInDuration;
        brightness = 255 - ((fadeOut * 255) / fadeOutDuration);
      }

      // GPIO16 on ESP8266 doesn't support hardware PWM -> use software PWM.
      if (gLedPin == 16) softPwmSetBrightness(brightness);
      else ledWritePWM(brightness);
      return;
    }
  }

  // If we're not in heartbeat mode, ensure software PWM can't interfere with blink.
  if (gLedPin == 16 && gSoftPwmRunning) {
    softPwmStop();
  }

  // 2) Choose blink mode
  uint32_t period = 0;
  if (gApBlink) period = AP_BLINK_MS;
  else if (gStaBlink) period = STA_BLINK_MS;
  else {
    ledWrite(false);
    return;
  }

  // 3) Blink
  if (now - gLastBlinkMs >= period) {
    gLastBlinkMs = now;
    ledWrite(!gLedOn);
  }
}
