/*
 * SmartKey Library - Double Click Example
 * 
 * This example demonstrates double-click detection.
 * Single-click toggles LED, double-click changes blink pattern.
 * 
 * Hardware:
 * - Connect a button to pin 2 (with internal pullup)
 * 
 * Author: SmartKey Project
 * License: MIT
 */

#include <SmartKey.h>

using namespace SmartKey;

// Configuration
const uint8_t BUTTON_PIN = 2;
const uint8_t BUTTON_KEY_CODE = 0x01;

// Hardware scanner
GPIOScanner scanner(1);

// Key manager
Manager keyManager;

// Application state
bool ledState = false;
uint8_t blinkMode = 0;  // 0=solid, 1=slow, 2=fast
unsigned long lastBlinkTime = 0;
uint16_t blinkInterval = 0;

void onKeyEvent(uint8_t /*keyCode*/, Event event) {
    Serial.print("[");
    Serial.print(eventToString(event));
    Serial.print("] ");
    
    if (event == Event::Click) {
        Serial.println("Single click - Toggle LED");
        ledState = !ledState;
        if (blinkMode == 0) {
            digitalWrite(LED_BUILTIN, ledState);
        }
    }
    else if (event == Event::DoubleClick) {
        Serial.println("Double click - Change blink mode");
        blinkMode = (blinkMode + 1) % 3;
        
        switch(blinkMode) {
            case 0:
                Serial.println("  Mode: Solid");
                digitalWrite(LED_BUILTIN, ledState);
                break;
            case 1:
                Serial.println("  Mode: Slow blink (500ms)");
                blinkInterval = 500;
                break;
            case 2:
                Serial.println("  Mode: Fast blink (100ms)");
                blinkInterval = 100;
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);
    
    Serial.println("\n=== SmartKey - Double Click Example ===\n");
    Serial.println("Single click: Toggle LED");
    Serial.println("Double click: Change blink mode\n");
    
    pinMode(LED_BUILTIN, OUTPUT);
    
    // Setup scanner
    scanner.addPin(BUTTON_PIN, BUTTON_KEY_CODE, true);
    scanner.begin();
    
    // Setup manager with double-click enabled.
    // Because a double click is possible, a single Click is reported only after
    // doubleClickTime has passed without a second press (so it never arrives
    // together with a DoubleClick).
    Config cfg;
    cfg.enableDoubleClick = true;
    cfg.doubleClickTime = 300;  // 300ms window for double-click
    keyManager.setConfig(cfg);
    keyManager.setEventCallback(onKeyEvent);
    
    Serial.println("Ready!");
}

void loop() {
    // Update key manager
    uint8_t keyCode = scanner.scan();
    keyManager.update(keyCode, millis());
    
    // Handle LED blinking
    if (blinkMode > 0) {
        unsigned long now = millis();
        if (now - lastBlinkTime >= blinkInterval) {
            lastBlinkTime = now;
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        }
    }
    
    delay(10);
}

