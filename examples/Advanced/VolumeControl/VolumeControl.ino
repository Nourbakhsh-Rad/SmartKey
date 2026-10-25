/*
 * SmartKey - ValueController demo
 *
 * Controls several values (volume, brightness, temperature, channel) with
 * "increase" / "decrease" keys:
 *   - click       : one step (applied immediately on press)
 *   - hold        : auto-repeat with acceleration (needs enableHoldRepeat)
 *   - double click: optional extra jump (setMultiClickJump)
 *
 * No hardware needed. An automated demo runs first (it feeds the manager
 * with simulated key presses in real time), then the sketch reads keys from
 * the Serial Monitor: + - U D R L C V (letters are case-insensitive).
 */

#include <SmartKey.h>

using namespace SmartKey;

// Time used by all callbacks (updated in loop() and in the demo helpers)
static unsigned long g_now = 0;

// ===================================
// Scenario 1: Volume (0-100)
// ===================================

void onVolumeChange(int32_t oldValue, int32_t newValue) {
    Serial.print("🔊 Volume: ");
    Serial.print(oldValue);
    Serial.print(" → ");
    Serial.print(newValue);
    Serial.print(" (");
    Serial.print(newValue);
    Serial.println("%)");
}

ValueController volumeController(
    0x2B,    // '+' key = increase
    0x2D,    // '-' key = decrease
    50,      // initial: 50%
    0,       // min: 0
    100,     // max: 100
    5,       // step: 5
    false,   // no rollover
    onVolumeChange
);

// ===================================
// Scenario 2: Brightness with rollover
// ===================================

void onBrightnessChange(int32_t oldValue, int32_t newValue) {
    Serial.print("💡 Brightness: ");
    Serial.print(oldValue);
    Serial.print(" → ");
    Serial.print(newValue);

    // Draw a bar
    Serial.print(" [");
    for (int i = 0; i < newValue / 10; i++) {
        Serial.print("█");
    }
    for (int i = newValue / 10; i < 10; i++) {
        Serial.print("░");
    }
    Serial.println("]");
}

ValueController brightnessController(
    0x55,    // 'U' key (up)
    0x44,    // 'D' key (down)
    50,      // initial: 50%
    0,       // min: 0
    100,     // max: 100
    10,      // step: 10
    true,    // rollover enabled (100 → 0, 0 → 100)
    onBrightnessChange
);

// ===================================
// Scenario 3: Temperature (-50 to +50)
// ===================================

void onTempChange(int32_t oldValue, int32_t newValue) {
    Serial.print("🌡️  Temperature: ");
    Serial.print(oldValue);
    Serial.print("°C → ");
    Serial.print(newValue);
    Serial.println("°C");
}

ValueController temperatureController(
    0x52,    // 'R' key (right / increase)
    0x4C,    // 'L' key (left / decrease)
    20,      // initial: 20°C
    -50,     // min: -50°C
    50,      // max: +50°C
    1,       // step: 1°C
    false,   // no rollover
    onTempChange
);

// ===================================
// Scenario 4: Channel (1-999 with rollover)
// ===================================

void onChannelChange(int32_t oldValue, int32_t newValue) {
    Serial.print("📺 Channel: ");
    Serial.print(oldValue);
    Serial.print(" → ");
    Serial.println(newValue);
}

ValueController channelController(
    0x43,    // 'C' key (channel up)
    0x56,    // 'V' key (channel down)
    1,       // initial: channel 1
    1,       // min: 1
    999,     // max: 999
    1,       // step: 1
    true,    // rollover: 999 → 1, 1 → 999
    onChannelChange
);

// ===================================
// Key manager and controller manager
// ===================================

ValueControllerManager<4> controllerManager;
Manager keyManager;

void onKeyEvent(uint8_t keyCode, Event event) {
    // Forward every event to all controllers (each one ignores foreign keys).
    // The time is passed explicitly so hold-repeat pacing uses the same clock.
    controllerManager.handleKeyEvent(keyCode, event, g_now);

    // Log (optional)
    Serial.print("[Key: 0x");
    Serial.print(keyCode, HEX);
    Serial.print(" Event: ");
    Serial.print(eventToString(event));
    Serial.println("]");
}

// ===================================
// Input: typed characters act as keys (ASCII code = key code)
// ===================================

SerialScanner scanner;

// ===================================
// Demo helpers (simulated key presses, real time)
// ===================================

// Keep `key` pressed (NO_KEY = nothing pressed) for `ms` milliseconds.
// A single update() call is not enough: debouncing needs a stable level.
static void feed(uint8_t key, unsigned long ms) {
    const unsigned long end = millis() + ms;
    while ((long)(end - millis()) > 0) {
        g_now = millis();
        keyManager.update(key, g_now);
        delay(10);
    }
}

// Press for holdMs, then release and wait gapMs.
static void tap(uint8_t key, unsigned long holdMs, unsigned long gapMs) {
    feed(key, holdMs);
    feed(NO_KEY, gapMs);
}

// ===================================
// Setup
// ===================================

void runDemo();

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== ValueController Demo ===\n");

    keyManager.setEventCallback(onKeyEvent);

    // ValueController auto-repeat is driven by HoldRepeat events,
    // so they must be enabled in the manager configuration.
    Config config = keyManager.getConfig();
    config.enableHoldRepeat = true;
    config.holdRepeatInterval = 50;     // repeat event every 50 ms
    keyManager.setConfig(config);

    // Hold settings per controller:
    //   1st argument: minimum time between two steps (cannot be faster than
    //                 Config::holdRepeatInterval)
    //   2nd argument: base acceleration (x3 after HoldLong, x5 after HoldVeryLong)
    volumeController.setHoldSettings(50, 1);        // normal
    brightnessController.setHoldSettings(50, 2);    // faster
    temperatureController.setHoldSettings(100, 1);  // slow
    channelController.setHoldSettings(30, 1);       // very fast

    // Optional: a double click adds an extra jump of N steps
    brightnessController.setMultiClickJump(2);

    controllerManager.addController(&volumeController);
    controllerManager.addController(&brightnessController);
    controllerManager.addController(&temperatureController);
    controllerManager.addController(&channelController);

    Serial.println("Controllers initialized:");
    Serial.println("  Volume:      '+' / '-'  (0-100, step 5)");
    Serial.println("  Brightness:  'U' / 'D'  (0-100, step 10, rollover)");
    Serial.println("  Temperature: 'R' / 'L'  (-50 to +50, step 1)");
    Serial.println("  Channel:     'C' / 'V'  (1-999, step 1, rollover)");
    Serial.println("\nTry: click, double click, hold!");
    Serial.println("===========================================\n");

    runDemo();
}

void loop() {
    g_now = millis();
    uint8_t key = scanner.scan();
    if (key >= 'a' && key <= 'z') key -= 32;     // case-insensitive
    keyManager.update(key, g_now);
}

// ===================================
// Automated demo
// ===================================

void runDemo() {
    // --- Demo 1: Volume clicks ---
    Serial.println("\n--- Demo 1: Volume Control (single clicks) ---");
    for (int i = 0; i < 3; i++) {
        tap(0x2B, 100, 400);        // gap > doubleClickTime: three separate clicks
    }

    // --- Demo 2: Brightness double click ---
    Serial.println("\n--- Demo 2: Brightness (double click) ---");
    tap(0x55, 80, 100);
    tap(0x55, 80, 500);

    // --- Demo 3: Temperature hold ---
    Serial.println("\n--- Demo 3: Temperature (hold for auto-repeat) ---");
    feed(0x52, 1000);
    feed(NO_KEY, 500);

    // --- Demo 4: Channel rollover ---
    Serial.println("\n--- Demo 4: Channel Rollover (at max) ---");
    Serial.println("Set channel to 998...");
    channelController.setValue(998);
    tap(0x43, 100, 400);
    tap(0x43, 100, 400);            // 999 → 1 (rollover)

    // --- Demo 5: Query functions ---
    Serial.println("\n--- Demo 5: Query Functions ---");

    Serial.print("Volume: ");
    Serial.print(volumeController.getValue());
    Serial.print(" (");
    Serial.print(volumeController.getPercentage(), 1);
    Serial.print("%, ");
    Serial.print(volumeController.isAtMax() ? "AT MAX" : "NOT MAX");
    Serial.println(")");

    Serial.print("Brightness: ");
    Serial.println(brightnessController.getValue());

    Serial.print("Temperature: ");
    Serial.print(temperatureController.getValue());
    Serial.print("°C (range: ");
    Serial.print(temperatureController.getMin());
    Serial.print(" to ");
    Serial.print(temperatureController.getMax());
    Serial.println(")");

    Serial.print("Channel: ");
    Serial.println(channelController.getValue());

    Serial.println("\n=== Demo complete - type + - U D R L C V in the Serial Monitor ===");
}

/*
 * ===================================
 * Sample output (automated part)
 * ===================================
 *
 * === ValueController Demo ===
 *
 * Controllers initialized:
 *   Volume:      '+' / '-'  (0-100, step 5)
 *   Brightness:  'U' / 'D'  (0-100, step 10, rollover)
 *   Temperature: 'R' / 'L'  (-50 to +50, step 1)
 *   Channel:     'C' / 'V'  (1-999, step 1, rollover)
 *
 * Try: click, double click, hold!
 * ===========================================
 *
 *
 * --- Demo 1: Volume Control (single clicks) ---
 * 🔊 Volume: 50 → 55 (55%)
 * [Key: 0x2B Event: Pressed]
 * [Key: 0x2B Event: Released]
 * [Key: 0x2B Event: Click]
 * 🔊 Volume: 55 → 60 (60%)
 * [Key: 0x2B Event: Pressed]
 * [Key: 0x2B Event: Released]
 * [Key: 0x2B Event: Click]
 * 🔊 Volume: 60 → 65 (65%)
 * [Key: 0x2B Event: Pressed]
 * [Key: 0x2B Event: Released]
 * [Key: 0x2B Event: Click]
 *
 * --- Demo 2: Brightness (double click) ---
 * 💡 Brightness: 50 → 60 [██████░░░░]
 * [Key: 0x55 Event: Pressed]
 * [Key: 0x55 Event: Released]
 * 💡 Brightness: 60 → 70 [███████░░░]
 * [Key: 0x55 Event: Pressed]
 * [Key: 0x55 Event: Released]
 * 💡 Brightness: 70 → 90 [█████████░]
 * [Key: 0x55 Event: DoubleClick]
 *
 * --- Demo 3: Temperature (hold for auto-repeat) ---
 * 🌡️  Temperature: 20°C → 21°C
 * [Key: 0x52 Event: Pressed]
 * [Key: 0x52 Event: HoldMid]
 * [Key: 0x52 Event: HoldRepeat]
 * 🌡️  Temperature: 21°C → 22°C
 * [Key: 0x52 Event: HoldRepeat]   [... x2]
 * 🌡️  Temperature: 22°C → 23°C
 * [Key: 0x52 Event: HoldRepeat]   [... x2]
 * 🌡️  Temperature: 23°C → 24°C
 * [Key: 0x52 Event: HoldRepeat]   [... x2]
 * 🌡️  Temperature: 24°C → 25°C
 * [Key: 0x52 Event: HoldRepeat]   [... x2]
 * 🌡️  Temperature: 25°C → 26°C
 * [Key: 0x52 Event: HoldRepeat]
 * [Key: 0x52 Event: Released]
 *
 * --- Demo 4: Channel Rollover (at max) ---
 * Set channel to 998...
 * 📺 Channel: 1 → 998
 * 📺 Channel: 998 → 999
 * [Key: 0x43 Event: Pressed]
 * [Key: 0x43 Event: Released]
 * [Key: 0x43 Event: Click]
 * 📺 Channel: 999 → 1
 * [Key: 0x43 Event: Pressed]
 * [Key: 0x43 Event: Released]
 * [Key: 0x43 Event: Click]
 *
 * --- Demo 5: Query Functions ---
 * Volume: 65 (65.0%, NOT MAX)
 * Brightness: 90
 * Temperature: 26°C (range: -50 to 50)
 * Channel: 1
 *
 * === Demo complete - type + - U D R L C V in the Serial Monitor ===
 */
