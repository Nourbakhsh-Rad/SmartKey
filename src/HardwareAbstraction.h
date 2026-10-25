#ifndef SMARTKEY_HARDWARE_H
#define SMARTKEY_HARDWARE_H

/**
 * Hardware scanners. No dynamic allocation: all buffers are fixed-size.
 * scan()      -> first pressed key (single-key use with Manager)
 * scanMulti() -> ALL pressed keys   (use with MultiManager / combos)
 */

#include "SmartKeyCore.h"

#ifndef SMARTKEY_MAX_GPIO_PINS
    #define SMARTKEY_MAX_GPIO_PINS 16
#endif
#ifndef SMARTKEY_MAX_ANALOG_KEYS
    #define SMARTKEY_MAX_ANALOG_KEYS 8
#endif

namespace SmartKey {

class KeyScanner {
public:
    virtual ~KeyScanner() {}
    virtual bool begin() = 0;
    virtual uint8_t scan() = 0;                       // first pressed key or NO_KEY
    virtual const char* getError() const { return nullptr; }

    // Fill out[] with every pressed key, return how many. Default: at most one key.
    virtual uint8_t scanMulti(uint8_t* out, uint8_t maxKeys) {
        if (!out || maxKeys == 0) return 0;
        const uint8_t k = scan();
        if (k == NO_KEY) return 0;
        out[0] = k;
        return 1;
    }
};

// ===================================
// GPIO scanner (one pin per key)
// ===================================
class GPIOScanner : public KeyScanner {
    struct PinConfig { uint8_t pin; uint8_t keyCode; bool activeLow; };
    PinConfig pins_[SMARTKEY_MAX_GPIO_PINS];
    uint8_t pinCount_, limit_;
    bool initialized_;

    bool isPressed(uint8_t i) const {
#ifdef ARDUINO
        const bool level = digitalRead(pins_[i].pin) == HIGH;
        return pins_[i].activeLow ? !level : level;
#else
        (void)i;
        return false;
#endif
    }

public:
    // maxPins is kept for source compatibility; capacity is SMARTKEY_MAX_GPIO_PINS.
    explicit GPIOScanner(uint8_t maxPins = SMARTKEY_MAX_GPIO_PINS)
        : pinCount_(0),
          limit_(maxPins > SMARTKEY_MAX_GPIO_PINS ? (uint8_t)SMARTKEY_MAX_GPIO_PINS : maxPins),
          initialized_(false) {}

    bool addPin(uint8_t pin, uint8_t keyCode, bool activeLow = true) {
        if (pinCount_ >= limit_ || keyCode == NO_KEY) return false;
        pins_[pinCount_].pin = pin;
        pins_[pinCount_].keyCode = keyCode;
        pins_[pinCount_].activeLow = activeLow;
        pinCount_++;
        return true;
    }

    bool begin() override {
        if (pinCount_ == 0) return false;
#ifdef ARDUINO
        for (uint8_t i = 0; i < pinCount_; i++)
            pinMode(pins_[i].pin, pins_[i].activeLow ? INPUT_PULLUP : INPUT);
#endif
        initialized_ = true;
        return true;
    }

    uint8_t scan() override {
        if (!initialized_) return NO_KEY;
        for (uint8_t i = 0; i < pinCount_; i++)
            if (isPressed(i)) return pins_[i].keyCode;
        return NO_KEY;
    }

    uint8_t scanMulti(uint8_t* out, uint8_t maxKeys) override {
        if (!initialized_ || !out) return 0;
        uint8_t n = 0;
        for (uint8_t i = 0; i < pinCount_ && n < maxKeys; i++)
            if (isPressed(i)) out[n++] = pins_[i].keyCode;
        return n;
    }

    uint8_t getPinCount() const { return pinCount_; }
    const char* getError() const override { return initialized_ ? nullptr : "Not initialized"; }
};

// ===================================
// Matrix scanner (max 8x8, diodes required for reliable multi-key)
// Key code = row * colCount + col
// ===================================
class MatrixScanner : public KeyScanner {
    static const uint8_t MAXN = 8;
    uint8_t rows_[MAXN];
    uint8_t cols_[MAXN];
    uint8_t rowCount_, colCount_;
    bool valid_, initialized_;

    uint8_t scanInternal(uint8_t* out, uint8_t maxKeys) const {
#ifdef ARDUINO
        uint8_t n = 0;
        for (uint8_t r = 0; r < rowCount_ && n < maxKeys; r++) {
            digitalWrite(rows_[r], LOW);
            delayMicroseconds(10);
            for (uint8_t c = 0; c < colCount_ && n < maxKeys; c++)
                if (digitalRead(cols_[c]) == LOW) out[n++] = (uint8_t)(r * colCount_ + c);
            digitalWrite(rows_[r], HIGH);
        }
        return n;
#else
        (void)out; (void)maxKeys;
        return 0;
#endif
    }

public:
    MatrixScanner(const uint8_t* rows, uint8_t rowCount, const uint8_t* cols, uint8_t colCount)
        : rowCount_(rowCount), colCount_(colCount), valid_(false), initialized_(false) {
        if (rows && cols && rowCount > 0 && colCount > 0 && rowCount <= MAXN && colCount <= MAXN) {
            for (uint8_t i = 0; i < rowCount; i++) rows_[i] = rows[i];
            for (uint8_t i = 0; i < colCount; i++) cols_[i] = cols[i];
            valid_ = true;
        } else {
            rowCount_ = colCount_ = 0;
        }
    }

    bool begin() override {
        if (!valid_) return false;
#ifdef ARDUINO
        for (uint8_t i = 0; i < rowCount_; i++) { pinMode(rows_[i], OUTPUT); digitalWrite(rows_[i], HIGH); }
        for (uint8_t i = 0; i < colCount_; i++) pinMode(cols_[i], INPUT_PULLUP);
#endif
        initialized_ = true;
        return true;
    }

    uint8_t scan() override {
        if (!initialized_) return NO_KEY;
        uint8_t k;
        return scanInternal(&k, 1) ? k : NO_KEY;
    }

    uint8_t scanMulti(uint8_t* out, uint8_t maxKeys) override {
        if (!initialized_ || !out) return 0;
        return scanInternal(out, maxKeys);
    }

    const char* getError() const override {
        return !valid_ ? "Invalid matrix size (max 8x8)" : (initialized_ ? nullptr : "Not initialized");
    }
};

// ===================================
// Analog scanner (resistor ladder on one ADC pin)
// ===================================
class AnalogScanner : public KeyScanner {
    struct KeyRange { uint16_t minVal; uint16_t maxVal; uint8_t keyCode; };
    KeyRange keys_[SMARTKEY_MAX_ANALOG_KEYS];
    uint8_t keyCount_, limit_;
    uint8_t pin_;
    uint16_t tolerance_;
    bool initialized_;

public:
    explicit AnalogScanner(uint8_t pin, uint8_t maxKeys = SMARTKEY_MAX_ANALOG_KEYS, uint16_t tolerance = 50)
        : keyCount_(0),
          limit_(maxKeys > SMARTKEY_MAX_ANALOG_KEYS ? (uint8_t)SMARTKEY_MAX_ANALOG_KEYS : maxKeys),
          pin_(pin), tolerance_(tolerance), initialized_(false) {}

    bool addKey(uint16_t minVal, uint16_t maxVal, uint8_t keyCode) {
        if (keyCount_ >= limit_ || minVal > maxVal || keyCode == NO_KEY) return false;
        keys_[keyCount_].minVal = minVal;
        keys_[keyCount_].maxVal = maxVal;
        keys_[keyCount_].keyCode = keyCode;
        keyCount_++;
        return true;
    }

    // center +/- tolerance, clamped to 0..65535 (no unsigned wrap-around)
    bool addKeyCentered(uint16_t centerValue, uint8_t keyCode) {
        int32_t lo = (int32_t)centerValue - (int32_t)tolerance_;
        int32_t hi = (int32_t)centerValue + (int32_t)tolerance_;
        if (lo < 0) lo = 0;
        if (hi > 65535) hi = 65535;
        return addKey((uint16_t)lo, (uint16_t)hi, keyCode);
    }

    // Map an ADC value to a key code (first matching range wins)
    uint8_t classify(uint16_t value) const {
        for (uint8_t i = 0; i < keyCount_; i++)
            if (value >= keys_[i].minVal && value <= keys_[i].maxVal) return keys_[i].keyCode;
        return NO_KEY;
    }

    bool begin() override {
        if (keyCount_ == 0) return false;
#ifdef ARDUINO
        pinMode(pin_, INPUT);
#endif
        initialized_ = true;
        return true;
    }

    uint8_t scan() override {
        if (!initialized_) return NO_KEY;
#ifdef ARDUINO
        return classify((uint16_t)analogRead(pin_));
#else
        (void)pin_;
        return NO_KEY;
#endif
    }

    const char* getError() const override { return initialized_ ? nullptr : "Not initialized"; }
};

// ===================================
// Binary code scanner: an external device (keypad encoder such as 74C922,
// another MCU, a decoder ...) puts the code of the pressed key on N lines
// (N = 1..8). pins[0] is the least significant bit.
//
//  - activeLow = true : lines idle HIGH (pull-up) and the device pulls them LOW,
//                       so a LOW line is a 1 bit (the code is the inverted level).
//  - noKeyCode        : the decoded code that means "no key pressed" (default 0,
//                       i.e. all lines HIGH when activeLow). Every other code is
//                       returned as the key code.
//  - setValidPin()    : optional "data available" line (e.g. DA of a 74C922).
//                       The code is only accepted while this line is active.
//
// The lines of most encoders do not change at exactly the same moment, so use
// Config::debounceTime >= the skew (20-30 ms is plenty) - the debouncer then
// ignores the short-lived intermediate codes.
// With 8 lines the code 0xFF cannot be used (it equals NO_KEY).
// ===================================
class BinaryCodeScanner : public KeyScanner {
    static const uint8_t MAX_BITS = 8;
    uint8_t pins_[MAX_BITS];
    uint8_t bitCount_;
    uint8_t noKeyCode_;
    uint8_t validPin_;
    bool hasValidPin_, validActiveLow_;
    bool activeLow_, usePullup_;
    bool valid_, initialized_;

    uint8_t readRaw() const {
#ifdef ARDUINO
        uint8_t raw = 0;
        for (uint8_t i = 0; i < bitCount_; i++)
            if (digitalRead(pins_[i]) == HIGH) raw |= (uint8_t)(1u << i);
        return raw;
#else
        return 0;
#endif
    }

    bool readDataValid() const {
        if (!hasValidPin_) return true;
#ifdef ARDUINO
        const bool high = digitalRead(validPin_) == HIGH;
        return validActiveLow_ ? !high : high;
#else
        return true;
#endif
    }

public:
    BinaryCodeScanner(const uint8_t* pins, uint8_t bitCount, bool activeLow = true,
                      uint8_t noKeyCode = 0, bool usePullup = true)
        : bitCount_(bitCount), noKeyCode_(noKeyCode), validPin_(0),
          hasValidPin_(false), validActiveLow_(true),
          activeLow_(activeLow), usePullup_(usePullup),
          valid_(false), initialized_(false) {
        if (pins && bitCount >= 1 && bitCount <= MAX_BITS) {
            for (uint8_t i = 0; i < bitCount; i++) pins_[i] = pins[i];
            valid_ = true;
        } else {
            bitCount_ = 0;
        }
    }

    // Optional "data valid / data available" line
    void setValidPin(uint8_t pin, bool activeLow = true) {
        validPin_ = pin;
        validActiveLow_ = activeLow;
        hasValidPin_ = true;
    }

    // Pure decoding step (no hardware access): rawLevels has bit i = 1 when
    // line i reads HIGH. Returns the key code or NO_KEY.
    uint8_t decode(uint8_t rawLevels, bool dataValid = true) const {
        if (!valid_ || !dataValid) return NO_KEY;
        const uint8_t mask = (bitCount_ >= 8) ? (uint8_t)0xFF : (uint8_t)((1u << bitCount_) - 1u);
        const uint8_t code = (uint8_t)((activeLow_ ? ~rawLevels : rawLevels) & mask);
        return code == noKeyCode_ ? NO_KEY : code;
    }

    bool begin() override {
        if (!valid_) return false;
#ifdef ARDUINO
        for (uint8_t i = 0; i < bitCount_; i++) pinMode(pins_[i], usePullup_ ? INPUT_PULLUP : INPUT);
        if (hasValidPin_) pinMode(validPin_, usePullup_ ? INPUT_PULLUP : INPUT);
#endif
        initialized_ = true;
        return true;
    }

    uint8_t scan() override {
        if (!initialized_) return NO_KEY;
        return decode(readRaw(), readDataValid());
    }

    const char* getError() const override {
        return !valid_ ? "Invalid line count (1..8)" : (initialized_ ? nullptr : "Not initialized");
    }
};

// ===================================
// Serial scanner (testing). A received character is "held" for holdMs so the
// debouncer can see it (a one-shot pulse would be filtered out).
// ===================================
class SerialScanner : public KeyScanner {
    uint8_t lastKey_;
    bool active_;
    unsigned long holdUntil_;
    uint16_t holdMs_;

public:
    explicit SerialScanner(uint16_t holdMs = 120) : lastKey_(NO_KEY), active_(false), holdUntil_(0), holdMs_(holdMs) {}

    bool begin() override { return true; }

    uint8_t scan() override {
#ifdef ARDUINO
        while (Serial.available() > 0) {
            const int c = Serial.read();
            if (c < 0 || c == '\r' || c == '\n' || c == NO_KEY) continue;
            lastKey_ = (uint8_t)c;
            active_ = true;
            holdUntil_ = millis() + holdMs_;
        }
        if (active_ && (long)(holdUntil_ - millis()) <= 0) active_ = false;
        return active_ ? lastKey_ : NO_KEY;
#else
        return NO_KEY;
#endif
    }
};

// ===================================
// Custom scanner via function pointer
// ===================================
class CustomScanner : public KeyScanner {
public:
    typedef uint8_t (*ScanFunction)();
    typedef bool (*InitFunction)();

    CustomScanner(ScanFunction scanFn, InitFunction initFn = nullptr)
        : scanFn_(scanFn), initFn_(initFn), initialized_(false) {}

    bool begin() override {
        if (initFn_ && !initFn_()) return false;
        initialized_ = (scanFn_ != nullptr);
        return initialized_;
    }
    uint8_t scan() override { return (initialized_ && scanFn_) ? scanFn_() : NO_KEY; }

private:
    ScanFunction scanFn_;
    InitFunction initFn_;
    bool initialized_;
};

} // namespace SmartKey

#endif // SMARTKEY_HARDWARE_H
