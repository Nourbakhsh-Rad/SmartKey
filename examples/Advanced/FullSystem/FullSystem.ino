/*
 * SmartKey - Full Manager demo
 *
 * Shows the advanced features of Manager:
 *   1. Simple usage with one event callback
 *   2. Separate callbacks per event type (press, release, click, hold, state)
 *   3. Custom configuration (timings, triple click, hold repeat)
 *   4. Query functions
 *   5. Per-key behaviour (different keys react differently to the same events)
 *
 * No hardware needed: the demos feed the managers with simulated key presses
 * in real time. In a real project you would call
 *     manager.update(scanner.scan(), millis());
 * from loop().
 */

#include <SmartKey.h>

using namespace SmartKey;

// ===================================
// Way 1: simple usage with one callback
// ===================================

void simpleCallback(uint8_t keyCode, Event event) {
    Serial.print("Simple: Key 0x");
    Serial.print(keyCode, HEX);
    Serial.print(" → ");
    Serial.println(eventToString(event));
}

Manager simpleManager(simpleCallback);

// ===================================
// Way 2: separate callbacks per event type
// ===================================

void onPress(uint8_t keyCode) {
    Serial.print("🔽 Key 0x");
    Serial.print(keyCode, HEX);
    Serial.println(" PRESSED");
}

void onRelease(uint8_t keyCode, unsigned long duration) {
    Serial.print("🔼 Key 0x");
    Serial.print(keyCode, HEX);
    Serial.print(" RELEASED after ");
    Serial.print(duration);
    Serial.println("ms");
}

void onClick(uint8_t keyCode, uint8_t clickCount) {
    Serial.print("👆 Key 0x");
    Serial.print(keyCode, HEX);
    Serial.print(" CLICKED ");
    Serial.print(clickCount);
    Serial.println(clickCount == 1 ? " time" : " times");
}

void onHold(uint8_t keyCode, Event holdType) {
    Serial.print("⏱️  Key 0x");
    Serial.print(keyCode, HEX);
    Serial.print(" HOLD: ");
    Serial.println(eventToString(holdType));
}

void onStateChange(uint8_t /*keyCode*/, State state, unsigned long duration) {
    Serial.print("🔄 State changed to: ");
    Serial.print(stateToString(state));
    Serial.print(" (duration: ");
    Serial.print(duration);
    Serial.println("ms)");
}

// Build the callback set in a function so the sketch compiles with any
// C++ standard (designated initializers need C++20).
static Callbacks makeAdvancedCallbacks() {
    Callbacks cb;
    cb.onStateChange = onStateChange;
    cb.onPress = onPress;
    cb.onRelease = onRelease;
    cb.onClick = onClick;
    cb.onHold = onHold;
    return cb;
}

Manager advancedManager(makeAdvancedCallbacks());

// ===================================
// Way 5: per-key behaviour
// ===================================

void handleKeyA(Event event) {
    // Key A: typing
    switch (event) {
        case Event::Click:
            Serial.println("Type: A");
            break;
        case Event::DoubleClick:
            Serial.println("Type: AA");
            break;
        case Event::HoldRepeat:
            Serial.println("Type: A (repeat)");
            break;
        default:
            break;
    }
}

void handleKeyB(Event event) {
    // Key B: media player control
    switch (event) {
        case Event::Click:
            Serial.println("Music: Play/Pause");
            break;
        case Event::DoubleClick:
            Serial.println("Music: Next Track");
            break;
        case Event::HoldLong:
            Serial.println("Music: Fast Forward");
            break;
        default:
            break;
    }
}

void handleKeyC(Event event) {
    // Key C: settings
    switch (event) {
        case Event::Click:
            Serial.println("Settings: Toggle");
            break;
        case Event::HoldVeryLong:
            Serial.println("Settings: Factory Reset");
            break;
        default:
            break;
    }
}

void handleKeyByCode(uint8_t keyCode, Event event) {
    switch (keyCode) {
        case 0x41: handleKeyA(event); break;    // 'A'
        case 0x42: handleKeyB(event); break;    // 'B'
        case 0x43: handleKeyC(event); break;    // 'C'
        default: break;
    }
}

Manager perKeyManager(handleKeyByCode);

// ===================================
// Demo helpers (simulated key presses, real time)
// ===================================

// Keep `key` pressed (NO_KEY = nothing pressed) on manager `m` for `ms`
// milliseconds. A single update() call is not enough: debouncing needs a
// stable level for debounceTime.
static void feed(Manager& m, uint8_t key, unsigned long ms) {
    const unsigned long end = millis() + ms;
    while ((long)(end - millis()) > 0) {
        m.update(key, millis());
        delay(10);
    }
}

// ===================================
// Demos
// ===================================

void demoSimpleUsage() {
    Serial.println("\n--- Demo 1: Simple Usage ---");
    feed(simpleManager, 0x41, 100);     // press
    feed(simpleManager, NO_KEY, 400);   // release (Click arrives after the double-click window)
}

void demoAdvancedCallbacks() {
    Serial.println("\n--- Demo 2: Advanced Callbacks ---");
    Serial.println("Notice: separate callbacks for each event type\n");

    feed(advancedManager, 0x42, 1200);  // hold until HoldLong
    feed(advancedManager, NO_KEY, 300);
}

void demoTripleClick() {
    Serial.println("\n--- Demo 3: Triple Click ---");

    for (int i = 0; i < 3; i++) {
        feed(advancedManager, 0x43, 80);
        feed(advancedManager, NO_KEY, 100);
    }
    feed(advancedManager, NO_KEY, 500);
}

void demoHoldRepeat() {
    Serial.println("\n--- Demo 4: Hold Repeat (auto-repeat) ---");
    Serial.println("Simulating a continuous key press...\n");

    feed(advancedManager, 0x44, 700);   // HoldMid at 300 ms, then a repeat every 50 ms
    feed(advancedManager, NO_KEY, 300);
}

void demoQueryFunctions() {
    Serial.println("\n--- Demo 5: Query Functions ---");

    feed(advancedManager, 0x45, 100);

    Serial.print("Is any key active? ");
    Serial.println(advancedManager.isAnyKeyActive() ? "Yes" : "No");

    Serial.print("Current key: 0x");
    Serial.println(advancedManager.getCurrentKey(), HEX);

    Serial.print("Current state: ");
    Serial.println(stateToString(advancedManager.getCurrentState()));

    Serial.print("Press duration: ");
    Serial.print(advancedManager.getCurrentPressDuration(millis()));
    Serial.println("ms");

    feed(advancedManager, NO_KEY, 300);
}

void demoPerKeyBehaviour() {
    Serial.println("\n--- Demo 6: Per-Key Behaviour ---");

    Serial.println("Key A: click, then double click");
    feed(perKeyManager, 0x41, 80);
    feed(perKeyManager, NO_KEY, 500);
    feed(perKeyManager, 0x41, 80);
    feed(perKeyManager, NO_KEY, 100);
    feed(perKeyManager, 0x41, 80);
    feed(perKeyManager, NO_KEY, 500);

    Serial.println("Key B: click, then long hold");
    feed(perKeyManager, 0x42, 80);
    feed(perKeyManager, NO_KEY, 500);
    feed(perKeyManager, 0x42, 1000);
    feed(perKeyManager, NO_KEY, 300);

    Serial.println("Key C: very long hold");
    feed(perKeyManager, 0x43, 1700);
    feed(perKeyManager, NO_KEY, 300);
}

// ===================================
// Setup and loop
// ===================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== Manager Advanced Demo ===\n");

    // --- Custom configuration (Way 3) ---
    Config customConfig;
    customConfig.debounceTime = 30;             // faster debounce
    customConfig.midPressTime = 300;            // faster hold detection
    customConfig.longPressTime = 1000;
    customConfig.veryLongPressTime = 2000;
    customConfig.doubleClickTime = 250;
    customConfig.tripleClickTime = 400;
    customConfig.holdRepeatInterval = 50;       // fast repeat
    customConfig.enableDoubleClick = true;
    customConfig.enableTripleClick = true;      // enable triple click
    customConfig.enableHoldRepeat = true;       // enable repeat while held
    customConfig.maxClickCount = 3;

    // setConfig() returns an error code and keeps the old settings
    // when the new ones are inconsistent.
    if (advancedManager.setConfig(customConfig) != ErrorCode::OK) {
        Serial.println("Invalid configuration!");
    }

    // The per-key demo uses its own, shorter hold times
    Config perKeyConfig;
    perKeyConfig.midPressTime = 300;
    perKeyConfig.longPressTime = 800;
    perKeyConfig.veryLongPressTime = 1500;
    perKeyManager.setConfig(perKeyConfig);

    demoSimpleUsage();
    delay(1000);

    demoAdvancedCallbacks();
    delay(1000);

    demoTripleClick();
    delay(1000);

    demoHoldRepeat();
    delay(1000);

    demoQueryFunctions();
    delay(1000);

    demoPerKeyBehaviour();
}

void loop() {
    // In a real application the key code comes from hardware:
    //   advancedManager.update(scanner.scan(), millis());
}

/*
 * ===================================
 * Sample output
 * ===================================
 *
 * === Manager Advanced Demo ===
 *
 *
 * --- Demo 1: Simple Usage ---
 * Simple: Key 0x41 → Pressed
 * Simple: Key 0x41 → Released
 * Simple: Key 0x41 → Click
 *
 * --- Demo 2: Advanced Callbacks ---
 * Notice: separate callbacks for each event type
 *
 * 🔄 State changed to: Pressed (duration: 0ms)
 * 🔽 Key 0x42 PRESSED
 * 🔄 State changed to: HoldMid (duration: 300ms)
 * ⏱️  Key 0x42 HOLD: HoldMid
 * ⏱️  Key 0x42 HOLD: HoldRepeat   [... x13]
 * 🔄 State changed to: HoldLong (duration: 1000ms)
 * ⏱️  Key 0x42 HOLD: HoldLong
 * ⏱️  Key 0x42 HOLD: HoldRepeat   [... x5]
 * 🔼 Key 0x42 RELEASED after 1200ms
 * 🔄 State changed to: Idle (duration: 1200ms)
 *
 * --- Demo 3: Triple Click ---
 * 🔄 State changed to: Pressed (duration: 0ms)
 * 🔽 Key 0x43 PRESSED
 * 🔼 Key 0x43 RELEASED after 80ms
 * 🔄 State changed to: Idle (duration: 80ms)
 * 🔄 State changed to: Pressed (duration: 0ms)
 * 🔽 Key 0x43 PRESSED
 * 🔼 Key 0x43 RELEASED after 80ms
 * 🔄 State changed to: Idle (duration: 80ms)
 * 🔄 State changed to: Pressed (duration: 0ms)
 * 🔽 Key 0x43 PRESSED
 * 🔼 Key 0x43 RELEASED after 80ms
 * 👆 Key 0x43 CLICKED 3 times
 * 🔄 State changed to: Idle (duration: 80ms)
 *
 * --- Demo 4: Hold Repeat (auto-repeat) ---
 * Simulating a continuous key press...
 *
 * 🔄 State changed to: Pressed (duration: 0ms)
 * 🔽 Key 0x44 PRESSED
 * 🔄 State changed to: HoldMid (duration: 300ms)
 * ⏱️  Key 0x44 HOLD: HoldMid
 * ⏱️  Key 0x44 HOLD: HoldRepeat   [... x8]
 * 🔼 Key 0x44 RELEASED after 700ms
 * 🔄 State changed to: Idle (duration: 700ms)
 *
 * --- Demo 5: Query Functions ---
 * 🔄 State changed to: Pressed (duration: 0ms)
 * 🔽 Key 0x45 PRESSED
 * Is any key active? Yes
 * Current key: 0x45
 * Current state: Pressed
 * Press duration: 70ms
 * 🔼 Key 0x45 RELEASED after 100ms
 * 🔄 State changed to: Idle (duration: 100ms)
 * 👆 Key 0x45 CLICKED 1 time
 *
 * --- Demo 6: Per-Key Behaviour ---
 * Key A: click, then double click
 * Type: A
 * Type: AA
 * Key B: click, then long hold
 * Music: Play/Pause
 * Music: Fast Forward
 * Key C: very long hold
 * Settings: Factory Reset
 */
