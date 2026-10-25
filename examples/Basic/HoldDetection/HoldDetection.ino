/*
 * SmartKey Library - Hold Detection Example
 * 
 * This example demonstrates hold detection with multiple time thresholds:
 * - Click: Toggle LED
 * - Hold Mid (500ms): Start brightness fade
 * - Hold Long (1500ms): Faster fade
 * - Hold Very Long (3000ms): Reset to default
 * 
 * Hardware:
 * - Connect a button to pin 2
 * - LED on pin 13 (built-in)
 * 
 * Author: SmartKey Project
 * License: MIT
 */

#include <SmartKey.h>

using namespace SmartKey;

const uint8_t BUTTON_PIN = 2;
const uint8_t BUTTON_KEY_CODE = 0x01;

GPIOScanner scanner(1);
Manager keyManager;

// LED state
bool ledOn = false;
uint8_t brightness = 128;
uint8_t fadeSpeed = 0;

void onKeyEvent(uint8_t /*keyCode*/, Event event) {
    Serial.print("[");
    Serial.print(eventToString(event));
    Serial.print("] ");
    
    switch(event) {
        case Event::Click:
            Serial.println("Click - Toggle LED");
            ledOn = !ledOn;
            fadeSpeed = 0;
            analogWrite(LED_BUILTIN, ledOn ? brightness : 0);
            break;
            
        case Event::HoldMid:
            Serial.println("Hold Mid - Start slow fade");
            fadeSpeed = 1;
            break;
            
        case Event::HoldLong:
            Serial.println("Hold Long - Fast fade");
            fadeSpeed = 3;
            break;
            
        case Event::HoldVeryLong:
            Serial.println("Hold Very Long - Reset");
            brightness = 128;
            fadeSpeed = 0;
            ledOn = true;
            analogWrite(LED_BUILTIN, brightness);
            break;
            
        case Event::Released:
            fadeSpeed = 0;
            Serial.println("Released");
            break;
            
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);
    
    Serial.println("\n=== SmartKey - Hold Detection Example ===\n");
    Serial.println("Click:           Toggle LED");
    Serial.println("Hold Mid:        Start fade");
    Serial.println("Hold Long:       Faster fade");
    Serial.println("Hold Very Long:  Reset\n");
    
    pinMode(LED_BUILTIN, OUTPUT);
    
    scanner.addPin(BUTTON_PIN, BUTTON_KEY_CODE, true);
    scanner.begin();
    
    Config cfg;
    cfg.enableDoubleClick = false;   // report Click immediately (no double-click window)
    cfg.midPressTime = 500;
    cfg.longPressTime = 1500;
    cfg.veryLongPressTime = 3000;
    keyManager.setConfig(cfg);
    keyManager.setEventCallback(onKeyEvent);
    
    Serial.println("Ready!");
}

void loop() {
    uint8_t keyCode = scanner.scan();
    keyManager.update(keyCode, millis());
    
    // Handle brightness fade
    if (fadeSpeed > 0 && ledOn) {
        static unsigned long lastFadeTime = 0;
        if (millis() - lastFadeTime >= 50) {
            lastFadeTime = millis();
            brightness = (brightness + fadeSpeed) % 256;
            analogWrite(LED_BUILTIN, brightness);
            
            if (brightness % 32 == 0) {
                Serial.print("Brightness: ");
                Serial.println(brightness);
            }
        }
    }
    
    delay(10);
}

