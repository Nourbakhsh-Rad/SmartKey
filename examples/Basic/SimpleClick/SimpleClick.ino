/*
 * SmartKey Library - Simple Click Example
 * 
 * This example demonstrates basic single-click detection using the SmartKey library.
 * It detects when a key is pressed, released, and clicked.
 * 
 * Hardware:
 * - Connect a button to pin 2 (with internal pullup)
 * - Or use serial input for testing
 * 
 * Author: SmartKey Project
 * License: MIT
 */

#include <SmartKey.h>

using namespace SmartKey;

// Pin configuration
const uint8_t BUTTON_PIN = 2;
const uint8_t BUTTON_KEY_CODE = 0x01;

// Key scanner (choose one method)
GPIOScanner scanner(1);  // For a hardware button
// SerialScanner scanner;   // For testing: type a character in the Serial Monitor

// Key manager
Manager keyManager;

// Event callback
void onKeyEvent(uint8_t keyCode, Event event) {
    Serial.print("Key: 0x");
    Serial.print(keyCode, HEX);
    Serial.print(" Event: ");
    Serial.println(eventToString(event));
    
    // Handle specific events
    switch(event) {
        case Event::Pressed:
            Serial.println("  --> Button is pressed");
            break;
            
        case Event::Released:
            Serial.println("  --> Button is released");
            break;
            
        case Event::Click:
            Serial.println("  --> Click detected!");
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            break;
            
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Wait for serial (up to 3s)
    
    Serial.println("\n=== SmartKey - Simple Click Example ===\n");
    
    // Configure LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    // Setup hardware scanner
    scanner.addPin(BUTTON_PIN, BUTTON_KEY_CODE, true);
    scanner.begin();
    
    // Setup key manager
    keyManager.setEventCallback(onKeyEvent);

    // Only plain clicks are used here, so disable double click:
    // with double click enabled a Click is reported ~300 ms after release.
    Config cfg;
    cfg.enableDoubleClick = false;
    keyManager.setConfig(cfg);
    
    Serial.println("Ready! Press the button or send keys via serial.");
    Serial.println("Each click will toggle the LED.\n");
}

void loop() {
    // Scan the hardware: returns the pressed key code or NO_KEY
    uint8_t keyCode = scanner.scan();
    
    // Update the key manager (the library never reads the clock itself)
    keyManager.update(keyCode, millis());
    
    // Small delay
    delay(10);
}

