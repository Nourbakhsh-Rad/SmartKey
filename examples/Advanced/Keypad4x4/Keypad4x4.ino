/*
 * SmartKey - 4x4 matrix keypad: PIN door lock
 *
 *        col0 col1 col2 col3
 *  row0    1    2    3    A
 *  row1    4    5    6    B
 *  row2    7    8    9    C
 *  row3    *    0    #    D
 *
 * Wiring: the 4 row pins are outputs, the 4 column pins inputs with pull-up
 * (MatrixScanner configures them). Key code = row * 4 + col (0..15).
 * For reliable multi-key detection (the A+D combo below) put a diode in
 * series with every key (anode to the row, cathode to the key); without
 * diodes three pressed keys can create a "ghost" fourth one.
 *
 * Keys:
 *   0-9      : type the PIN (shown as '*')
 *   *        : click = backspace, hold 0.5 s = clear
 *   #        : click = submit the PIN
 *   A        : keypad backlight on/off (Toggle helper)
 *   B        : show status
 *   C        : lock again immediately
 *   A + D    : (hold both) programming mode on/off (ComboDetector).
 *              In programming mode type a new PIN (4-8 digits) and press #.
 *
 * Brute-force protection: RateLimiter allows 3 PIN attempts per 30 s.
 * Default PIN: 1234
 */

#include <SmartKey.h>

using namespace SmartKey;

// ═══════════════════════════════════════════════════════════════════
// Settings
// ═══════════════════════════════════════════════════════════════════

#if defined(ESP32)
static const uint8_t ROW_PINS[4] = {13, 12, 14, 27};
static const uint8_t COL_PINS[4] = {26, 25, 33, 32};
#else
static const uint8_t ROW_PINS[4] = {9, 8, 7, 6};
static const uint8_t COL_PINS[4] = {5, 4, 3, 2};
#endif

static const uint8_t LOCK_PIN = LED_BUILTIN;            // HIGH = unlocked (relay / LED)
static const unsigned long UNLOCK_MS = 5000;            // how long the lock stays open
static const unsigned long ENTRY_TIMEOUT_MS = 10000;    // typed digits are forgotten after this idle time
static const uint8_t PIN_MIN = 4;
static const uint8_t PIN_MAX = 8;

// Key code (row * 4 + col) -> label on the keypad
static const char KEY_LABELS[16] = {
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'
};

static const uint8_t KEY_A = 3, KEY_B = 7, KEY_C = 11, KEY_D = 15;
static const uint8_t KEY_STAR = 12, KEY_HASH = 14;

static unsigned long g_now = 0;

// ═══════════════════════════════════════════════════════════════════
// Application state
// ═══════════════════════════════════════════════════════════════════

static char secretPin[PIN_MAX + 1] = "1234";
static char entry[PIN_MAX + 1];
static uint8_t entryLen = 0;
static unsigned long lastKeyTime = 0;

static bool unlocked = false;
static unsigned long unlockUntil = 0;
static bool programMode = false;
static bool backlight = true;
static bool comboUsed = false;      // A+D was used: ignore the clicks that follow

static RateLimiter attempts(3, 30000);      // 3 PIN attempts per 30 s

// Helpers
static ComboDetector combos;
static Toggle toggles;

static bool isDigitKey(uint8_t code) {
    const char c = code < 16 ? KEY_LABELS[code] : '?';
    return c >= '0' && c <= '9';
}

static void printMasked() {
    Serial.print("PIN: ");
    for (uint8_t i = 0; i < entryLen; i++) Serial.print('*');
    Serial.println();
}

static void clearEntry() {
    entryLen = 0;
    entry[0] = '\0';
}

static void setLock(bool open) {
    unlocked = open;
    digitalWrite(LOCK_PIN, open ? HIGH : LOW);
    if (open) unlockUntil = g_now + UNLOCK_MS;
    Serial.println(open ? "🔓 UNLOCKED" : "🔒 LOCKED");
}

// ═══════════════════════════════════════════════════════════════════
// Helper callbacks
// ═══════════════════════════════════════════════════════════════════

static void onBacklightToggle(uint8_t /*key*/, bool state) {
    backlight = state;
    Serial.println(backlight ? "💡 Backlight ON" : "💡 Backlight OFF");
}

static void onProgramCombo(uint8_t* /*keys*/, uint8_t /*count*/) {
    comboUsed = true;
    programMode = !programMode;
    clearEntry();
    Serial.println(programMode ? "🛠  Programming mode: type the new PIN and press #"
                               : "🛠  Programming mode off");
}

// ═══════════════════════════════════════════════════════════════════
// PIN handling
// ═══════════════════════════════════════════════════════════════════

static void submit() {
    if (programMode) {
        if (entryLen < PIN_MIN) {
            Serial.println("❌ PIN too short");
            return;
        }
        strcpy(secretPin, entry);
        programMode = false;
        clearEntry();
        Serial.println("✅ New PIN saved");
        return;
    }

    if (!attempts.checkClick(g_now)) {
        Serial.println("⛔ Too many attempts - wait 30 s");
        clearEntry();
        return;
    }

    if (strcmp(entry, secretPin) == 0) {
        attempts.reset();
        clearEntry();
        setLock(true);
    } else {
        Serial.print("❌ Wrong PIN. Attempts left: ");
        Serial.println(3 - attempts.getCurrentCount());
        clearEntry();
    }
}

static void showStatus() {
    Serial.println("---- status ----");
    Serial.print("Lock: ");        Serial.println(unlocked ? "open" : "closed");
    Serial.print("Backlight: ");   Serial.println(backlight ? "on" : "off");
    Serial.print("Programming: "); Serial.println(programMode ? "yes" : "no");
    Serial.print("Attempts used: "); Serial.println(attempts.getCurrentCount());
    Serial.println("----------------");
}

// ═══════════════════════════════════════════════════════════════════
// SmartKey
// ═══════════════════════════════════════════════════════════════════

static MatrixScanner scanner(ROW_PINS, 4, COL_PINS, 4);

// MultiManager: several keys at the same time (needed for the A+D combo)
static MultiManager<4> keyManager;

static void onKeyEvent(uint8_t keyCode, Event event) {
    // The combo needs Pressed / Released of every key
    combos.handleKeyEvent(keyCode, event);

    if (keyCode >= 16) return;

    // Digits react immediately on Pressed
    if (event == Event::Pressed && isDigitKey(keyCode)) {
        if (entryLen < PIN_MAX) {
            entry[entryLen++] = KEY_LABELS[keyCode];
            entry[entryLen] = '\0';
        }
        lastKeyTime = g_now;
        printMasked();
        return;
    }

    // '*' : hold = clear everything
    if (keyCode == KEY_STAR && event == Event::HoldMid) {
        clearEntry();
        lastKeyTime = g_now;
        Serial.println("PIN cleared");
        return;
    }

    // The A+D combo must not also toggle the backlight
    if (event == Event::Click && comboUsed) return;

    toggles.handleKeyEvent(keyCode, event);     // A: backlight

    if (event != Event::Click) return;

    lastKeyTime = g_now;
    switch (keyCode) {
        case KEY_STAR:
            if (entryLen > 0) entry[--entryLen] = '\0';
            printMasked();
            break;
        case KEY_HASH:
            submit();
            break;
        case KEY_B:
            showStatus();
            break;
        case KEY_C:
            if (unlocked) setLock(false);
            else Serial.println("Already locked");
            break;
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);     // wait for the Serial Monitor (up to 3 s)

    Serial.println("\n=== SmartKey - 4x4 keypad PIN lock ===");

    pinMode(LOCK_PIN, OUTPUT);
    digitalWrite(LOCK_PIN, LOW);

    if (!scanner.begin()) {
        Serial.print("Scanner error: ");
        Serial.println(scanner.getError());
        for (;;) delay(1000);
    }

    Config cfg;
    cfg.debounceTime = 30;              // mechanical keys bounce
    cfg.enableDoubleClick = false;      // clicks are reported immediately
    cfg.midPressTime = 500;             // '*' hold = clear
    if (keyManager.setConfig(cfg) != ErrorCode::OK) {
        Serial.println("Invalid SmartKey configuration!");
    }
    keyManager.setEventCallback(onKeyEvent);

    toggles.registerToggle(KEY_A, true, onBacklightToggle);
    combos.registerCombo2(KEY_A, KEY_D, onProgramCombo);

    clearEntry();
    Serial.println("Type the PIN and press #");
}

void loop() {
    g_now = millis();

    // Every pressed key (up to 4) -> MultiManager
    uint8_t keys[4];
    const uint8_t n = scanner.scanMulti(keys, 4);
    keyManager.update(keys, n, g_now);

    // The A+D combo is over when all keys are released
    if (comboUsed && keyManager.getActiveKeyCount() == 0) comboUsed = false;

    // Close the lock after UNLOCK_MS
    if (unlocked && (long)(g_now - unlockUntil) >= 0) setLock(false);

    // Forget half-typed digits after a pause
    if (entryLen > 0 && (g_now - lastKeyTime) > ENTRY_TIMEOUT_MS) {
        clearEntry();
        Serial.println("PIN entry timed out");
    }
}
