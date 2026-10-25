/*
 * SmartKey - key code received on 4 parallel lines
 *
 * Scenario: an external device produces the CODE of the pressed key and puts
 * it on 4 lines connected to 4 pins of the microcontroller (for example a
 * keypad encoder such as 74C922, another MCU, a decoder chip ...).
 *
 *   - The 4 pins use the internal pull-up. The device pulls a line LOW for a
 *     1 bit (open-collector style), so with no key all lines read HIGH
 *     (0b1111). BinaryCodeScanner inverts the levels: idle = code 0 = "no key",
 *     keys are codes 1..15.
 *   - CODE_PINS[0] is the least significant bit.
 *   - If your device uses the opposite convention (HIGH = 1, idle = 0) set
 *     CODE_ACTIVE_LOW = false. If it has a different "no key" code, change
 *     NO_KEY_CODE. If it has a "data available" output (DA on the 74C922),
 *     set DATA_VALID_PIN.
 *
 * What the example does with the keys (change as you like):
 *   code 1 / 2 : level up / down (0-100, step 5, auto-repeat when held)
 *   code 3     : output on/off (Toggle helper)
 *   code 4     : mode - click: next mode, double click: reset, hold: lock panel
 *   code 15    : hold 1.5 s: service menu
 *   others     : just logged
 *
 * Tip: set PRINT_RAW_CODE to 1 first to check the wiring and the bit order:
 * every change of the decoded code is printed before SmartKey filters it.
 */

#include <SmartKey.h>

using namespace SmartKey;

// ═══════════════════════════════════════════════════════════════════
// Settings
// ═══════════════════════════════════════════════════════════════════

#ifndef PRINT_RAW_CODE
#define PRINT_RAW_CODE 0            // 1 = print every change of the raw code
#endif

#if defined(ESP32)
static const uint8_t CODE_PINS[4] = {16, 17, 18, 19};   // bit 0 (LSB) ... bit 3 (MSB)
#else
static const uint8_t CODE_PINS[4] = {2, 3, 4, 5};
#endif

static const bool    CODE_ACTIVE_LOW = true;    // LOW line = 1 bit (pull-ups, open-collector device)
static const uint8_t NO_KEY_CODE = 0;           // decoded code that means "no key"
static const int16_t DATA_VALID_PIN = -1;       // optional "data available" line, -1 = not used
static const bool    DATA_VALID_ACTIVE_LOW = true;

static const uint8_t OUTPUT_PIN = LED_BUILTIN;  // controlled by code 3

// Key codes used by the application
static const uint8_t KEY_LEVEL_UP   = 1;
static const uint8_t KEY_LEVEL_DOWN = 2;
static const uint8_t KEY_OUTPUT     = 3;
static const uint8_t KEY_MODE       = 4;
static const uint8_t KEY_SERVICE    = 15;

static unsigned long g_now = 0;

// ═══════════════════════════════════════════════════════════════════
// Application
// ═══════════════════════════════════════════════════════════════════

static const char* const MODE_NAMES[] = {"Eco", "Normal", "Boost"};
static uint8_t modeIndex = 1;
static bool panelLocked = false;

static void onLevelChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.print("Level: ");
    Serial.print(newValue);
    Serial.print("% [");
    for (int i = 0; i < 20; i++) Serial.print(i < newValue / 5 ? "#" : "-");
    Serial.println("]");
}

static ValueController levelController(KEY_LEVEL_UP, KEY_LEVEL_DOWN, 50, 0, 100, 5, false, onLevelChange);
static ValueControllerManager<1> controllers;

static void onOutputToggle(uint8_t /*key*/, bool state) {
    digitalWrite(OUTPUT_PIN, state ? HIGH : LOW);
    Serial.println(state ? "Output: ON" : "Output: OFF");
}
static Toggle toggles;

static void printMode() {
    Serial.print("Mode: ");
    Serial.println(MODE_NAMES[modeIndex]);
}

// ═══════════════════════════════════════════════════════════════════
// SmartKey
// ═══════════════════════════════════════════════════════════════════

static BinaryCodeScanner scanner(CODE_PINS, 4, CODE_ACTIVE_LOW, NO_KEY_CODE);
static Manager keyManager;

static void onKeyEvent(uint8_t keyCode, Event event) {
    // Log everything except the repeat events
    if (event != Event::HoldRepeat) {
        Serial.print("[code ");
        Serial.print(keyCode);
        Serial.print(" ");
        Serial.print(eventToString(event));
        Serial.println("]");
    }

    // A locked panel only reacts to a long hold of the MODE key (unlock)
    if (panelLocked) {
        if (keyCode == KEY_MODE && event == Event::HoldLong) {
            panelLocked = false;
            Serial.println("Panel UNLOCKED");
        }
        return;
    }

    controllers.handleKeyEvent(keyCode, event, g_now);   // level up / down
    toggles.handleKeyEvent(keyCode, event);              // output on / off

    if (keyCode == KEY_MODE) {
        if (event == Event::Click) {
            modeIndex = (modeIndex + 1) % 3;
            printMode();
        } else if (event == Event::DoubleClick) {
            modeIndex = 1;
            Serial.print("Reset - ");
            printMode();
        } else if (event == Event::HoldLong) {
            panelLocked = true;
            Serial.println("Panel LOCKED (hold MODE again to unlock)");
        }
    } else if (keyCode == KEY_SERVICE && event == Event::HoldLong) {
        Serial.println("Service menu");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);     // wait for the Serial Monitor (up to 3 s)

    Serial.println("\n=== SmartKey - 4-line parallel code input ===");

    pinMode(OUTPUT_PIN, OUTPUT);
    digitalWrite(OUTPUT_PIN, LOW);

    if (DATA_VALID_PIN >= 0) scanner.setValidPin((uint8_t)DATA_VALID_PIN, DATA_VALID_ACTIVE_LOW);
    if (!scanner.begin()) {
        Serial.print("Scanner error: ");
        Serial.println(scanner.getError());
        for (;;) delay(1000);
    }

    // The 4 lines never change at exactly the same moment, so for a short
    // time the decoder can show a wrong code (e.g. 0 -> 1 -> 5 for key 5).
    // A debounce time longer than that skew hides these intermediate codes.
    Config cfg;
    cfg.debounceTime = 30;
    cfg.enableDoubleClick = true;       // MODE key: double click = reset
    cfg.enableHoldRepeat = true;        // level auto-repeat
    cfg.holdRepeatInterval = 100;
    cfg.longPressTime = 1500;
    if (keyManager.setConfig(cfg) != ErrorCode::OK) {
        Serial.println("Invalid SmartKey configuration!");
    }
    keyManager.setEventCallback(onKeyEvent);

    levelController.setHoldSettings(100, 1);
    controllers.addController(&levelController);
    toggles.registerToggle(KEY_OUTPUT, false, onOutputToggle);

    Serial.println("Ready. Press keys on the external device.");
    printMode();
}

void loop() {
    g_now = millis();

    const uint8_t code = scanner.scan();    // decoded key code or NO_KEY

#if PRINT_RAW_CODE
    static uint8_t lastCode = 0xFE;
    if (code != lastCode) {
        lastCode = code;
        Serial.print("raw code: ");
        if (code == NO_KEY) Serial.println("none");
        else Serial.println(code);
    }
#endif

    keyManager.update(code, g_now);
}
