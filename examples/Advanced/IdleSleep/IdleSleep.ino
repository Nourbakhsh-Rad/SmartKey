/*
 * SmartKey - two-stage idle (inactivity) handling with IdleMonitor
 *
 * Behaviour:
 *   - IDLE1_MS without any key  -> idle stage 1: the display / backlight is switched off
 *   - IDLE2_MS without any key  -> idle stage 2: the system goes to deep sleep
 *   - a key pressed while idle  -> only wakes the system up (backlight on, timers restart).
 *     The key has no other action: its release, click and hold events are ignored too.
 *
 * Both times are measured from the LAST key activity (a key that is still held counts as
 * activity), so IDLE2_MS must be larger than IDLE1_MS.
 *
 * What the single key does while the system is active (key code 1):
 *   click : counter + 1        hold 0.5 s : counter = 0
 *
 * Hardware: a button on KEY_PIN (to GND, internal pull-up) and the backlight output on
 * BACKLIGHT_PIN (LED_BUILTIN here). Set USE_SERIAL_INPUT to 1 to use characters typed in the
 * Serial Monitor instead of a button.
 *
 * Deep sleep (ESP32): the key pin wakes the chip (EXT0). A deep-sleep wake-up restarts the
 * program, so the key press that woke it is still down in setup(): the sketch waits until it
 * is released, which is the same "wake key does nothing" rule across a reset.
 * KEY_PIN must be an RTC-capable GPIO (for example 25, 26, 27, 32 or 33).
 * Other boards: put your own low-power code in goToSleep().
 */

#include <SmartKey.h>

using namespace SmartKey;

#if defined(ESP32)
  #include <esp_sleep.h>
  #include <driver/rtc_io.h>
#endif

// ═══════════════════════════════════════════════════════════════════
// Settings
// ═══════════════════════════════════════════════════════════════════

#ifndef IDLE1_MS
#define IDLE1_MS 10000UL            // backlight off after 10 s (typical product: 15-60 s)
#endif
#ifndef IDLE2_MS
#define IDLE2_MS 30000UL            // deep sleep after 30 s (typical product: 2-10 min)
#endif
#ifndef USE_SERIAL_INPUT
#define USE_SERIAL_INPUT 0          // 1 = type characters in the Serial Monitor instead of a button
#endif

#if defined(ESP32)
static const uint8_t KEY_PIN = 33;  // RTC GPIO (needed to wake from deep sleep)
#else
static const uint8_t KEY_PIN = 2;
#endif
static const uint8_t BACKLIGHT_PIN = LED_BUILTIN;
static const uint8_t KEY_MAIN = 1;

// ═══════════════════════════════════════════════════════════════════
// Application
// ═══════════════════════════════════════════════════════════════════

static unsigned long g_now = 0;
static int counter = 0;

static void showCounter() {
    Serial.print("Counter: ");
    Serial.println(counter);
}

static void goToSleep() {
    Serial.println("Idle 2: deep sleep");
#if defined(ESP32)
    rtc_gpio_pullup_en((gpio_num_t)KEY_PIN);        // keep the button pulled up while sleeping
    rtc_gpio_pulldown_dis((gpio_num_t)KEY_PIN);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)KEY_PIN, 0);   // wake when the key pulls the pin LOW
    digitalWrite(BACKLIGHT_PIN, LOW);
    Serial.flush();
    esp_deep_sleep_start();                         // does not return
#else
    Serial.println("(deep sleep would start here - add your board's low-power code)");
#endif
}

// ═══════════════════════════════════════════════════════════════════
// IdleMonitor: one callback per idle flag, one for the wake-up
// ═══════════════════════════════════════════════════════════════════

static void onIdle1() {
    Serial.println("Idle 1: backlight off");
    digitalWrite(BACKLIGHT_PIN, LOW);
}

static void onIdle2() {
    goToSleep();
}

static void onWake(IdleState from) {
    Serial.print("Wake-up from ");
    Serial.println(from == IdleState::Idle2 ? "idle 2" : "idle 1");
    digitalWrite(BACKLIGHT_PIN, HIGH);
}

static IdleMonitor idle(IDLE1_MS, IDLE2_MS);

// ═══════════════════════════════════════════════════════════════════
// SmartKey
// ═══════════════════════════════════════════════════════════════════

#if USE_SERIAL_INPUT
static SerialScanner scanner;
#else
static GPIOScanner scanner;
#endif
static Manager keyManager;

static void onKeyEvent(uint8_t keyCode, Event event) {
    // EVERY key event goes through the idle monitor first. It returns false for the
    // events of the key press that woke the system up: ignore them.
    if (!idle.handleKeyEvent(keyCode, event, g_now)) return;

    // Normal key handling
    if (event == Event::Click) {
        counter++;
        showCounter();
    } else if (event == Event::HoldMid) {
        counter = 0;
        Serial.println("Counter reset");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);     // wait for the Serial Monitor (up to 3 s)

    Serial.println("\n=== SmartKey - two-stage idle ===");

    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);

#if defined(ESP32)
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Woke from deep sleep");
        // The key that woke the chip is still down: wait for its release so it does nothing
        while (digitalRead(KEY_PIN) == LOW) delay(10);
        delay(50);
    }
#endif

#if !USE_SERIAL_INPUT
    scanner.addPin(KEY_PIN, KEY_MAIN);      // active low, internal pull-up
#endif
    scanner.begin();

    Config cfg;
    cfg.enableDoubleClick = false;          // immediate clicks
    keyManager.setConfig(cfg);
    keyManager.setEventCallback(onKeyEvent);

    idle.setIdle1Callback(onIdle1);
    idle.setIdle2Callback(onIdle2);
    idle.setWakeCallback(onWake);
    if (idle.getLastError() != ErrorCode::OK) {
        Serial.println("Invalid idle timeouts (IDLE2_MS must be larger than IDLE1_MS)");
    }
    idle.reset(millis());                   // start counting now

    Serial.print("Idle 1 after ");
    Serial.print(IDLE1_MS / 1000);
    Serial.print(" s, idle 2 after ");
    Serial.print(IDLE2_MS / 1000);
    Serial.println(" s without a key");
    showCounter();
}

void loop() {
    g_now = millis();

    keyManager.update(scanner.scan(), g_now);
    idle.update(g_now);                     // raises the idle flags and calls their callbacks
}
