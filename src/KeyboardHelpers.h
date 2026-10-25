#ifndef SMARTKEY_KEYBOARD_HELPERS_H
#define SMARTKEY_KEYBOARD_HELPERS_H

/**
 * Helpers built on top of key events:
 *  ComboDetector, SequenceDetector, Toggle, Macro, RateLimiter, IdleMonitor.
 *
 * ComboDetector needs Pressed/Released events of several keys that are held
 * at the same time -> feed it from a MultiManager (not from Manager).
 * Every time-dependent method takes the current time as an argument.
 */

#include "SmartKeyCore.h"

namespace SmartKey {

// ===================================
// Helper 1: ComboDetector (keys held simultaneously)
// ===================================
typedef void (*ComboCallback)(uint8_t* keyCodes, uint8_t count);

class ComboDetector {
    struct Combo {
        uint8_t keys[4];
        uint8_t keyCount;
        ComboCallback callback;
        bool enabled;
    };
    static const uint8_t MAX_COMBOS = 16;
    static const uint8_t MAX_ACTIVE = 8;

    Combo combos_[MAX_COMBOS];
    uint8_t comboCount_;
    uint8_t activeKeys_[MAX_ACTIVE];
    uint8_t activeCount_;

    bool isKeyActive(uint8_t k) const {
        for (uint8_t i = 0; i < activeCount_; i++) if (activeKeys_[i] == k) return true;
        return false;
    }

    void checkCombos() {
        for (uint8_t i = 0; i < comboCount_; i++) {
            Combo& c = combos_[i];
            if (!c.enabled || activeCount_ != c.keyCount) continue;
            bool all = true;
            for (uint8_t j = 0; j < c.keyCount; j++) if (!isKeyActive(c.keys[j])) { all = false; break; }
            if (all) {
                if (c.callback) c.callback(c.keys, c.keyCount);
                return;
            }
        }
    }

    void addActiveKey(uint8_t k) {
        if (activeCount_ < MAX_ACTIVE && !isKeyActive(k)) {
            activeKeys_[activeCount_++] = k;
            checkCombos();
        }
    }

    void removeActiveKey(uint8_t k) {
        for (uint8_t i = 0; i < activeCount_; i++) {
            if (activeKeys_[i] == k) {
                for (uint8_t j = i; j + 1 < activeCount_; j++) activeKeys_[j] = activeKeys_[j + 1];
                activeCount_--;
                return;
            }
        }
    }

public:
    ComboDetector() : comboCount_(0), activeCount_(0) {}

    bool registerCombo(const uint8_t* keys, uint8_t count, ComboCallback callback) {
        if (!keys || comboCount_ >= MAX_COMBOS || count == 0 || count > 4) return false;
        Combo& c = combos_[comboCount_];
        for (uint8_t i = 0; i < count; i++) c.keys[i] = keys[i];
        c.keyCount = count;
        c.callback = callback;
        c.enabled = true;
        comboCount_++;
        return true;
    }
    bool registerCombo2(uint8_t k1, uint8_t k2, ComboCallback cb) { const uint8_t k[2] = {k1, k2}; return registerCombo(k, 2, cb); }
    bool registerCombo3(uint8_t k1, uint8_t k2, uint8_t k3, ComboCallback cb) { const uint8_t k[3] = {k1, k2, k3}; return registerCombo(k, 3, cb); }
    bool registerCombo4(uint8_t k1, uint8_t k2, uint8_t k3, uint8_t k4, ComboCallback cb) { const uint8_t k[4] = {k1, k2, k3, k4}; return registerCombo(k, 4, cb); }

    void handleKeyEvent(uint8_t keyCode, Event event) {
        if (event == Event::Pressed) addActiveKey(keyCode);
        else if (event == Event::Released) removeActiveKey(keyCode);
    }
    void handleKeyEvent(uint8_t keyCode, uint8_t event) { handleKeyEvent(keyCode, static_cast<Event>(event)); }

    void enableCombo(uint8_t index, bool enable) { if (index < comboCount_) combos_[index].enabled = enable; }
    void clear() { comboCount_ = 0; activeCount_ = 0; }
    void resetActiveKeys() { activeCount_ = 0; }
    uint8_t getActiveKeyCount() const { return activeCount_; }
    uint8_t getComboCount() const { return comboCount_; }
};

// ===================================
// Helper 2: SequenceDetector (keys pressed one after another)
// maxInterval = max time between two consecutive keys of THAT sequence
// (0 = no timeout). Each sequence keeps its own progress.
// ===================================
typedef void (*SequenceCallback)(uint8_t sequenceId);

class SequenceDetector {
    struct Sequence {
        uint8_t id;
        uint8_t keys[16];
        uint8_t length;
        uint8_t pos;
        uint16_t maxInterval;
        unsigned long lastTime;
        SequenceCallback callback;
        bool enabled;
    };
    static const uint8_t MAX_SEQUENCES = 8;

    Sequence seqs_[MAX_SEQUENCES];
    uint8_t count_;

public:
    SequenceDetector() : count_(0) {}

    bool registerSequence(uint8_t id, const uint8_t* keys, uint8_t length, uint16_t maxInterval, SequenceCallback cb) {
        if (!keys || count_ >= MAX_SEQUENCES || length == 0 || length > 16) return false;
        Sequence& s = seqs_[count_];
        s.id = id;
        for (uint8_t i = 0; i < length; i++) s.keys[i] = keys[i];
        s.length = length;
        s.pos = 0;
        s.maxInterval = maxInterval;
        s.lastTime = 0;
        s.callback = cb;
        s.enabled = true;
        count_++;
        return true;
    }

    void handleKeyEvent(uint8_t keyCode, Event event, unsigned long currentTime) {
        if (event != Event::Pressed) return;
        for (uint8_t i = 0; i < count_; i++) {
            Sequence& s = seqs_[i];
            if (!s.enabled) continue;
            if (s.pos > 0 && s.maxInterval > 0 && (currentTime - s.lastTime) > s.maxInterval) s.pos = 0;
            if (keyCode == s.keys[s.pos]) {
                s.pos++;
                s.lastTime = currentTime;
                if (s.pos == s.length) {
                    const SequenceCallback cb = s.callback;
                    const uint8_t id = s.id;
                    reset();
                    if (cb) cb(id);
                    return;
                }
            } else {
                s.pos = (keyCode == s.keys[0]) ? 1 : 0;      // a wrong key may start the sequence again
                if (s.pos == 1) s.lastTime = currentTime;
            }
        }
    }
    void handleKeyEvent(uint8_t keyCode, uint8_t event, unsigned long currentTime) {
        handleKeyEvent(keyCode, static_cast<Event>(event), currentTime);
    }

    void reset() { for (uint8_t i = 0; i < count_; i++) seqs_[i].pos = 0; }
    void enableSequence(uint8_t index, bool enable) { if (index < count_) seqs_[index].enabled = enable; }
    uint8_t getSequenceCount() const { return count_; }
};

// ===================================
// Helper 3: Toggle (ON/OFF on every Click)
// Note: if double click is enabled in the Config, two quick presses arrive as
// ONE DoubleClick, which Toggle ignores. Use a Manager without double click
// for toggle keys.
// ===================================
typedef void (*ToggleCallback)(uint8_t keyCode, bool state);

class Toggle {
    struct ToggleKey { uint8_t keyCode; bool state; ToggleCallback callback; };
    static const uint8_t MAX_TOGGLES = 16;
    ToggleKey toggles_[MAX_TOGGLES];
    uint8_t count_;

public:
    Toggle() : count_(0) {}

    bool registerToggle(uint8_t keyCode, bool initialState, ToggleCallback callback) {
        if (count_ >= MAX_TOGGLES) return false;
        toggles_[count_].keyCode = keyCode;
        toggles_[count_].state = initialState;
        toggles_[count_].callback = callback;
        count_++;
        return true;
    }

    void handleKeyEvent(uint8_t keyCode, Event event) {
        if (event != Event::Click) return;
        for (uint8_t i = 0; i < count_; i++) {
            if (toggles_[i].keyCode == keyCode) {
                toggles_[i].state = !toggles_[i].state;
                if (toggles_[i].callback) toggles_[i].callback(keyCode, toggles_[i].state);
                return;
            }
        }
    }
    void handleKeyEvent(uint8_t keyCode, uint8_t event) { handleKeyEvent(keyCode, static_cast<Event>(event)); }

    bool getState(uint8_t keyCode) const {
        for (uint8_t i = 0; i < count_; i++) if (toggles_[i].keyCode == keyCode) return toggles_[i].state;
        return false;
    }
    void setState(uint8_t keyCode, bool state) {
        for (uint8_t i = 0; i < count_; i++) {
            if (toggles_[i].keyCode == keyCode) {
                toggles_[i].state = state;
                if (toggles_[i].callback) toggles_[i].callback(keyCode, state);
                return;
            }
        }
    }
    uint8_t getToggleCount() const { return count_; }
};

// ===================================
// Helper 4: Macro (record key presses with timing, play them back)
// Timing is relative to the first recorded key; max span ~49 days.
// ===================================
typedef void (*MacroPlaybackCallback)(uint8_t keyCode);

class Macro {
    struct Step { uint8_t keyCode; unsigned long offset; };
    static const uint8_t MAX_STEPS = 32;

    Step steps_[MAX_STEPS];
    uint8_t stepCount_;
    bool recording_, playing_, full_;
    unsigned long firstTime_, playStart_;
    uint8_t playIndex_;
    MacroPlaybackCallback callback_;

public:
    Macro() : stepCount_(0), recording_(false), playing_(false), full_(false),
              firstTime_(0), playStart_(0), playIndex_(0), callback_(nullptr) {}

    void startRecording(unsigned long currentTime) {
        (void)currentTime;                       // offsets are taken from the first recorded key
        stepCount_ = 0;
        recording_ = true;
        playing_ = false;
        full_ = false;
    }
    void stopRecording() { recording_ = false; }
    bool isRecording() const { return recording_; }
    bool isFull() const { return full_; }        // recording stopped because MAX_STEPS was reached

    void handleKeyEvent(uint8_t keyCode, Event event, unsigned long currentTime) {
        if (!recording_ || event != Event::Pressed) return;
        if (stepCount_ >= MAX_STEPS) { recording_ = false; full_ = true; return; }
        if (stepCount_ == 0) firstTime_ = currentTime;
        steps_[stepCount_].keyCode = keyCode;
        steps_[stepCount_].offset = currentTime - firstTime_;
        stepCount_++;
    }
    void handleKeyEvent(uint8_t keyCode, uint8_t event, unsigned long currentTime) {
        handleKeyEvent(keyCode, static_cast<Event>(event), currentTime);
    }

    void play(MacroPlaybackCallback callback, unsigned long currentTime) {
        if (stepCount_ == 0 || !callback) return;
        recording_ = false;
        callback_ = callback;
        playing_ = true;
        playIndex_ = 0;
        playStart_ = currentTime;
    }

    // Call regularly while playing. Steps that are already due are all played.
    void update(unsigned long currentTime) {
        while (playing_ && playIndex_ < stepCount_ &&
               (currentTime - playStart_) >= steps_[playIndex_].offset) {
            const uint8_t key = steps_[playIndex_].keyCode;
            playIndex_++;
            if (callback_) callback_(key);
        }
        if (playing_ && playIndex_ >= stepCount_) playing_ = false;
    }

#ifdef ARDUINO
    void startRecording() { startRecording(millis()); }
    void play(MacroPlaybackCallback callback) { play(callback, millis()); }
#endif

    bool isPlaying() const { return playing_; }
    void stop() { playing_ = false; recording_ = false; }
    void clear() { stepCount_ = 0; stop(); }
    uint8_t getStepCount() const { return stepCount_; }
};

// ===================================
// Helper 5: RateLimiter (max N clicks per time window)
// maxClicks is limited to 8 (v2.0 silently never limited when > 8).
// ===================================
class RateLimiter {
    static const uint8_t MAX_TRACKED = 8;
    uint8_t maxClicks_;
    uint16_t window_;
    unsigned long stamps_[MAX_TRACKED];
    uint8_t count_;

    static uint8_t limit(uint8_t n) { return n > MAX_TRACKED ? MAX_TRACKED : n; }

public:
    RateLimiter(uint8_t maxClicksPerWindow = 5, uint16_t windowMs = 1000)
        : maxClicks_(limit(maxClicksPerWindow)), window_(windowMs), count_(0) {}

    bool checkClick(unsigned long currentTime) {
        uint8_t valid = 0;
        for (uint8_t i = 0; i < count_; i++)
            if ((currentTime - stamps_[i]) <= window_) stamps_[valid++] = stamps_[i];
        count_ = valid;
        if (count_ >= maxClicks_) return false;
        stamps_[count_++] = currentTime;
        return true;
    }

    void reset() { count_ = 0; }
    uint8_t getCurrentCount() const { return count_; }
    void setLimit(uint8_t maxClicksPerWindow, uint16_t windowMs) {
        maxClicks_ = limit(maxClicksPerWindow);
        window_ = windowMs;
    }
};

// ===================================
// Helper 6: IdleMonitor (two-stage inactivity detection)
//
// Typical use: after `idle1` ms without any key the display / backlight is
// switched off; if the inactivity continues until `idle2` ms the system goes
// to deep sleep. A key pressed while idle only wakes the system up: the key
// has no other action (handleKeyEvent() tells the application to ignore all
// events of that key press).
//
//   state      flags                  entered by
//   Active     -                      any key activity / notifyActivity()
//   Idle1      isIdle1()              idle1 ms without activity  -> onIdle1
//   Idle2      isIdle1() + isIdle2()  idle2 ms without activity  -> onIdle2
//
// Both timeouts are measured from the LAST ACTIVITY (a key event, or a key
// that is still held down). 0 disables a stage; idle2 must be > idle1.
// Call update(now) on every loop iteration and feed every key event to
// handleKeyEvent().
// ===================================
enum class IdleState : uint8_t { Active = 0, Idle1, Idle2 };

typedef void (*IdleCallback)();
typedef void (*WakeCallback)(IdleState from);        // state the system was woken from

class IdleMonitor {
    unsigned long idle1_, idle2_;
    unsigned long lastActivity_;
    IdleState state_;
    IdleCallback onIdle1_, onIdle2_;
    WakeCallback onWake_;
    ErrorCode lastError_;

    // wake-key swallowing
    bool swallowWakeKey_;
    bool swallowing_, awaitClick_, sawHold_;
    uint8_t wakeKey_;
    unsigned long swallowSince_;
    unsigned long swallowTimeout_;

    uint8_t keysDown_;

    void wake(unsigned long now) {
        const IdleState from = state_;
        state_ = IdleState::Active;
        lastActivity_ = now;
        if (from != IdleState::Active && onWake_) onWake_(from);
    }

    void endSwallow() { swallowing_ = false; awaitClick_ = false; sawHold_ = false; }

public:
    explicit IdleMonitor(unsigned long idle1Ms = 0, unsigned long idle2Ms = 0)
        : idle1_(0), idle2_(0), lastActivity_(0), state_(IdleState::Active),
          onIdle1_(nullptr), onIdle2_(nullptr), onWake_(nullptr), lastError_(ErrorCode::OK),
          swallowWakeKey_(true), swallowing_(false), awaitClick_(false), sawHold_(false),
          wakeKey_(NO_KEY), swallowSince_(0), swallowTimeout_(1000), keysDown_(0) {
        setTimeouts(idle1Ms, idle2Ms);
    }

    // 0 = stage disabled. Returns InvalidParameter (and keeps the old values)
    // if both are set and idle2 <= idle1.
    ErrorCode setTimeouts(unsigned long idle1Ms, unsigned long idle2Ms) {
        if (idle1Ms > 0 && idle2Ms > 0 && idle2Ms <= idle1Ms) {
            lastError_ = ErrorCode::InvalidParameter;
            return lastError_;
        }
        idle1_ = idle1Ms;
        idle2_ = idle2Ms;
        return ErrorCode::OK;
    }

    void setIdle1Callback(IdleCallback cb) { onIdle1_ = cb; }
    void setIdle2Callback(IdleCallback cb) { onIdle2_ = cb; }
    void setWakeCallback(WakeCallback cb) { onWake_ = cb; }

    // true (default): the key press that wakes the system and everything that belongs
    // to it (release, click, hold events) is reported as "ignore" by handleKeyEvent()
    void setSwallowWakeKey(bool enable) { swallowWakeKey_ = enable; }

    // Safety net: stop ignoring the wake key's events if its click event does not arrive within this time
    void setSwallowTimeout(unsigned long ms) { swallowTimeout_ = ms; }

    // Start counting from `now` (call once in setup()). Silent: no callbacks.
    void reset(unsigned long now) {
        state_ = IdleState::Active;
        lastActivity_ = now;
        keysDown_ = 0;
        endSwallow();
        lastError_ = ErrorCode::OK;
    }

    // Activity that is not a key (touch, sensor, serial command ...): resets the
    // timers and wakes the system if it is idle. Nothing is swallowed.
    void notifyActivity(unsigned long now) {
        lastActivity_ = now;
        if (state_ != IdleState::Active) wake(now);
    }

    // Feed EVERY key event here. Returns true if the application should process
    // the event, false if it belongs to the key press that woke the system.
    bool handleKeyEvent(uint8_t keyCode, Event event, unsigned long now) {
        if (event == Event::None) return true;

        if (event == Event::Pressed) { if (keysDown_ < 255) keysDown_++; }
        else if (event == Event::Released) { if (keysDown_ > 0) keysDown_--; }

        // A key pressed while idle wakes the system up
        if (event == Event::Pressed && state_ != IdleState::Active) {
            wake(now);                                       // calls the wake callback
            if (!swallowWakeKey_) return true;
            swallowing_ = true;
            awaitClick_ = false;
            sawHold_ = false;
            wakeKey_ = keyCode;
            swallowSince_ = now;
            return false;
        }

        lastActivity_ = now;                                 // any other event is activity

        if (!swallowing_ || keyCode != wakeKey_) return true;

        // Events of the wake key
        switch (event) {
            case Event::Released:
                if (sawHold_) endSwallow();                  // a hold never produces a click
                else { awaitClick_ = true; swallowSince_ = now; }   // wait for its Click event
                break;
            case Event::Click:
            case Event::DoubleClick:
            case Event::TripleClick:
                endSwallow();
                break;
            case Event::HoldMid:
            case Event::HoldLong:
            case Event::HoldVeryLong:
            case Event::HoldRepeat:
                sawHold_ = true;
                break;
            default:                                         // Pressed (2nd press of the same chain)
                break;
        }
        return false;
    }
    bool handleKeyEvent(uint8_t keyCode, uint8_t event, unsigned long now) {
        return handleKeyEvent(keyCode, static_cast<Event>(event), now);
    }

    // Call on every loop iteration
    void update(unsigned long now) {
        if (keysDown_ > 0) lastActivity_ = now;              // a held key is activity

        if (swallowing_ && awaitClick_ && (now - swallowSince_) > swallowTimeout_) endSwallow();

        const unsigned long elapsed = now - lastActivity_;
        if (state_ == IdleState::Active && idle1_ > 0 && elapsed >= idle1_) {
            state_ = IdleState::Idle1;
            if (onIdle1_) onIdle1_();
        }
        if (state_ != IdleState::Idle2 && idle2_ > 0 && elapsed >= idle2_) {
            state_ = IdleState::Idle2;
            if (onIdle2_) onIdle2_();
        }
    }
#ifdef ARDUINO
    void update() { update(millis()); }
#endif

    // --- Queries ---
    IdleState getState() const { return state_; }
    bool isIdle1() const { return state_ != IdleState::Active; }   // also true in Idle2
    bool isIdle2() const { return state_ == IdleState::Idle2; }
    bool isSwallowing() const { return swallowing_; }
    unsigned long getIdleTime(unsigned long now) const { return keysDown_ > 0 ? 0 : now - lastActivity_; }
    unsigned long getIdle1Timeout() const { return idle1_; }
    unsigned long getIdle2Timeout() const { return idle2_; }
    ErrorCode getLastError() const { return lastError_; }
};

// Legacy compatibility
using KeyComboDetector = ComboDetector;
using KeySequenceDetector = SequenceDetector;
using KeyToggle = Toggle;
using KeyMacro = Macro;
using KeyRateLimiter = RateLimiter;

} // namespace SmartKey

#endif // SMARTKEY_KEYBOARD_HELPERS_H
