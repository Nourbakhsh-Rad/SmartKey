#ifndef SMARTKEY_VALUE_CONTROLLER_H
#define SMARTKEY_VALUE_CONTROLLER_H

/**
 * Value controllers driven by key events (volume, brightness, temperature ...).
 *
 * Behaviour:
 *  - Pressed        : one step (immediately)
 *  - HoldMid        : starts hold-repeat      (needs Config::enableHoldRepeat = true)
 *  - HoldRepeat     : step * acceleration     (rate limited by setHoldSettings interval)
 *  - HoldLong/VeryLong raise the acceleration x3 / x5 (relative to the base acceleration)
 *  - Released       : stops repeat and resets the acceleration (v2.0 kept it forever)
 *  - Double/Triple click: no extra jump by default (each press already moved one step);
 *    opt in with setMultiClickJump().
 */

#include "SmartKeyCore.h"

namespace SmartKey {

typedef void (*ValueChangeCallback)(int32_t oldValue, int32_t newValue);

class ValueController {
public:
    ValueController(uint8_t incKey, uint8_t decKey, int32_t initialValue = 0,
                    int32_t minVal = 0, int32_t maxVal = 100, int32_t step = 1,
                    bool rollover = false, ValueChangeCallback callback = nullptr)
        : incKey_(incKey), decKey_(decKey),
          min_(minVal <= maxVal ? minVal : maxVal), max_(minVal <= maxVal ? maxVal : minVal),
          step_(step), rollover_(rollover), value_(0), onChange_(callback),
          holdActive_(false), holdInterval_(0), baseAccel_(1), accel_(1), lastHoldUpdate_(0),
          doubleJump_(0), tripleJump_(0) {
        value_ = clamp(initialValue);
    }

    // --- Event handling ---
    void handleKeyEvent(uint8_t keyCode, Event event, unsigned long now) {
        if (keyCode != incKey_ && keyCode != decKey_) return;
        const int32_t dir = (keyCode == incKey_) ? 1 : -1;
        switch (event) {
            case Event::Pressed:
                holdActive_ = false;
                accel_ = baseAccel_;
                changeValue(dir * step_);
                break;
            case Event::Released:
                holdActive_ = false;
                accel_ = baseAccel_;
                break;
            case Event::DoubleClick:
                if (doubleJump_) changeValue(dir * step_ * (int32_t)doubleJump_);
                break;
            case Event::TripleClick:
                if (tripleJump_) changeValue(dir * step_ * (int32_t)tripleJump_);
                break;
            case Event::HoldMid:
                holdActive_ = true;
                accel_ = baseAccel_;
                lastHoldUpdate_ = now;
                break;
            case Event::HoldLong:
                accel_ = (uint16_t)(baseAccel_ * 3);
                break;
            case Event::HoldVeryLong:
                accel_ = (uint16_t)(baseAccel_ * 5);
                break;
            case Event::HoldRepeat:
                if (holdActive_ && (now - lastHoldUpdate_) >= holdInterval_) {
                    lastHoldUpdate_ = now;
                    changeValue(dir * step_ * (int32_t)accel_);
                }
                break;
            default:
                break;
        }
    }
    void handleKeyEvent(uint8_t keyCode, uint8_t event, unsigned long now) {
        handleKeyEvent(keyCode, static_cast<Event>(event), now);
    }
#ifdef ARDUINO
    void handleKeyEvent(uint8_t keyCode, Event event) { handleKeyEvent(keyCode, event, millis()); }
    void handleKeyEvent(uint8_t keyCode, uint8_t event) { handleKeyEvent(keyCode, static_cast<Event>(event), millis()); }
#endif

    // --- Value ---
    int32_t getValue() const { return value_; }

    void setValue(int32_t v) {
        const int32_t nv = clamp(v);
        if (nv != value_) {
            const int32_t old = value_;
            value_ = nv;
            if (onChange_) onChange_(old, value_);
        }
    }

    void changeValue(int32_t delta) { setValue((int32_t)clamp64((int64_t)value_ + delta)); }

    float getPercentage() const {
        if (max_ == min_) return 0.0f;
        return ((float)(value_ - min_) / (float)(max_ - min_)) * 100.0f;
    }
    void setPercentage(float percent) {
        if (percent < 0.0f) percent = 0.0f;
        if (percent > 100.0f) percent = 100.0f;
        setValue(min_ + (int32_t)((float)(max_ - min_) * percent / 100.0f));
    }

    // --- Settings ---
    void setRange(int32_t minVal, int32_t maxVal) {
        if (minVal > maxVal) { const int32_t t = minVal; minVal = maxVal; maxVal = t; }
        min_ = minVal; max_ = maxVal;
        setValue(value_);
    }
    void setStep(int32_t step) { step_ = step; }
    void setRollover(bool enable) { rollover_ = enable; }
    void setKeys(uint8_t incKey, uint8_t decKey) { incKey_ = incKey; decKey_ = decKey; }
    void setCallback(ValueChangeCallback cb) { onChange_ = cb; }

    // intervalMs: minimum time between two hold steps (cannot be faster than Config::holdRepeatInterval)
    // acceleration: base multiplier (HoldLong x3, HoldVeryLong x5 relative to it)
    void setHoldSettings(uint16_t intervalMs, uint16_t acceleration = 1) {
        holdInterval_ = intervalMs;
        baseAccel_ = acceleration ? acceleration : 1;
        accel_ = baseAccel_;
    }
    // Optional extra jump on double / triple click (0 = disabled)
    void setMultiClickJump(uint8_t doubleClickMultiplier, uint8_t tripleClickMultiplier = 0) {
        doubleJump_ = doubleClickMultiplier;
        tripleJump_ = tripleClickMultiplier;
    }

    int32_t getMin() const { return min_; }
    int32_t getMax() const { return max_; }
    int32_t getStep() const { return step_; }
    bool isAtMin() const { return value_ == min_; }
    bool isAtMax() const { return value_ == max_; }
    uint8_t getIncKey() const { return incKey_; }
    uint8_t getDecKey() const { return decKey_; }

private:
    uint8_t incKey_, decKey_;
    int32_t min_, max_, step_;
    bool rollover_;
    int32_t value_;
    ValueChangeCallback onChange_;
    bool holdActive_;
    uint16_t holdInterval_, baseAccel_, accel_;
    unsigned long lastHoldUpdate_;
    uint8_t doubleJump_, tripleJump_;

    int64_t clamp64(int64_t v) const {
        if (!rollover_) {
            if (v > max_) return max_;
            if (v < min_) return min_;
            return v;
        }
        const int64_t range = (int64_t)max_ - (int64_t)min_ + 1;
        int64_t off = (v - min_) % range;
        if (off < 0) off += range;
        return min_ + off;
    }
    int32_t clamp(int32_t v) const { return (int32_t)clamp64((int64_t)v); }
};

// Manager for N value controllers
template <uint8_t MAX_CONTROLLERS = 8>
class ValueControllerManager {
    ValueController* controllers_[MAX_CONTROLLERS];
    uint8_t count_;

public:
    ValueControllerManager() : count_(0) {}

    bool addController(ValueController* c) {
        if (!c || count_ >= MAX_CONTROLLERS) return false;
        controllers_[count_++] = c;
        return true;
    }

    void handleKeyEvent(uint8_t keyCode, Event event, unsigned long now) {
        for (uint8_t i = 0; i < count_; i++) controllers_[i]->handleKeyEvent(keyCode, event, now);
    }
    void handleKeyEvent(uint8_t keyCode, uint8_t event, unsigned long now) {
        handleKeyEvent(keyCode, static_cast<Event>(event), now);
    }
#ifdef ARDUINO
    void handleKeyEvent(uint8_t keyCode, Event event) { handleKeyEvent(keyCode, event, millis()); }
    void handleKeyEvent(uint8_t keyCode, uint8_t event) { handleKeyEvent(keyCode, static_cast<Event>(event), millis()); }
#endif

    uint8_t getCount() const { return count_; }
    ValueController* getController(uint8_t index) { return index < count_ ? controllers_[index] : nullptr; }
};

using KeyValueController = ValueController;
template <uint8_t N = 8> using KeyValueControllerManager = ValueControllerManager<N>;

} // namespace SmartKey

#endif // SMARTKEY_VALUE_CONTROLLER_H
