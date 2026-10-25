#ifndef SMARTKEY_CORE_H
#define SMARTKEY_CORE_H

/**
 * SmartKey core: events, config, debouncer, per-key state machine,
 * Manager (single key) and MultiManager (several simultaneous keys).
 *
 * Design rules (since v2.1):
 *  - The library NEVER calls millis(). Every time-dependent method receives
 *    the current time, so behaviour is deterministic and testable on a PC.
 *    (Convenience overloads without a time argument exist only when ARDUINO is defined.)
 *  - User callbacks are never called from inside a spin-lock / critical section.
 *  - No dynamic allocation.
 */

#include <stdint.h>

#if defined(ARDUINO)
    #include <Arduino.h>
#endif

#define SMARTKEY_VERSION_MAJOR 2
#define SMARTKEY_VERSION_MINOR 2
#define SMARTKEY_VERSION_PATCH 0
#define SMARTKEY_VERSION "2.2.0"

// Optional thread safety (ESP32 / FreeRTOS only): define SMARTKEY_THREAD_SAFE=1
// before including the library. A recursive mutex is used, so callbacks may
// safely call the query functions of the same manager. update() must be
// called from task context (not from an ISR).
#ifndef SMARTKEY_THREAD_SAFE
    #define SMARTKEY_THREAD_SAFE 0
#endif

#if SMARTKEY_THREAD_SAFE
    #if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
        #include "freertos/FreeRTOS.h"
        #include "freertos/semphr.h"
    #else
        #error "SMARTKEY_THREAD_SAFE=1 is only supported on ESP32 (FreeRTOS)."
    #endif
#endif

namespace SmartKey {

// Constants
static const uint8_t NO_KEY = 0xFF;

// Event types
enum class Event : uint8_t {
    None = 0,
    Pressed,        // Stable press (after debounce)
    Released,       // Stable release
    Click,          // Single click
    DoubleClick,    // Double click
    TripleClick,    // Triple click
    HoldMid,        // Reached mid hold time
    HoldLong,       // Reached long hold time
    HoldVeryLong,   // Reached very long hold time
    HoldRepeat      // Periodic repeat while held (needs Config::enableHoldRepeat)
};

// Key state
enum class State : uint8_t {
    Idle,
    Pressed,
    HoldMid,
    HoldLong,
    HoldVeryLong
};

// Error codes
enum class ErrorCode : uint8_t {
    OK = 0,
    InvalidParameter,
    NotInitialized,
    AlreadyInitialized,
    BufferFull,
    NotFound,
    TimeoutError,
    Busy
};

/**
 * Timing configuration.
 *
 * NOTE (2.1): when double click is enabled, a single Click is reported only
 * after doubleClickTime has elapsed without a second press (otherwise a
 * double click would also produce a Click). If you need the lowest possible
 * click latency set enableDoubleClick = false.
 */
struct Config {
    uint16_t debounceTime = 50;
    uint16_t midPressTime = 500;
    uint16_t longPressTime = 1500;
    uint16_t veryLongPressTime = 3000;
    uint16_t doubleClickTime = 300;     // max gap between 1st release and 2nd press
    uint16_t tripleClickTime = 400;     // max gap between 2nd release and 3rd press
    uint16_t holdRepeatInterval = 100;

    bool enableDoubleClick = true;
    bool enableTripleClick = false;     // requires enableDoubleClick
    bool enableHoldRepeat = false;
    uint8_t maxClickCount = 3;          // upper limit (1..3) of the click chain

    ErrorCode validate() const {
        if (!(midPressTime < longPressTime && longPressTime < veryLongPressTime)) return ErrorCode::InvalidParameter;
        if (maxClickCount == 0) return ErrorCode::InvalidParameter;
        if (enableDoubleClick && doubleClickTime == 0) return ErrorCode::InvalidParameter;
        if (enableTripleClick && (!enableDoubleClick || tripleClickTime == 0)) return ErrorCode::InvalidParameter;
        if (enableHoldRepeat && holdRepeatInterval == 0) return ErrorCode::InvalidParameter;
        return ErrorCode::OK;
    }

    // Longest click chain that is actually detected (1, 2 or 3)
    uint8_t effectiveMaxClicks() const {
        uint8_t m = enableTripleClick ? 3 : (enableDoubleClick ? 2 : 1);
        return m > maxClickCount ? maxClickCount : m;
    }
};

// Callback type definitions
typedef void (*EventCallback)(uint8_t keyCode, Event event);
typedef void (*EventCallbackEx)(void* user, uint8_t keyCode, Event event);   // with user context
typedef void (*StateCallback)(uint8_t keyCode, State state, unsigned long duration);
typedef void (*PressCallback)(uint8_t keyCode);
typedef void (*ReleaseCallback)(uint8_t keyCode, unsigned long pressDuration);
typedef void (*ClickCallback)(uint8_t keyCode, uint8_t clickCount);
typedef void (*HoldCallback)(uint8_t keyCode, Event holdType);

struct Callbacks {
    EventCallback onEvent = nullptr;        // All events
    StateCallback onStateChange = nullptr;  // State change
    PressCallback onPress = nullptr;
    ReleaseCallback onRelease = nullptr;    // With press duration
    ClickCallback onClick = nullptr;        // Click / double / triple (with count)
    HoldCallback onHold = nullptr;          // Hold events only
    EventCallbackEx onEventEx = nullptr;    // All events, with user pointer (e.g. `this`)
    void* userData = nullptr;
};

namespace detail {

// ---- Locking (no-op unless SMARTKEY_THREAD_SAFE) ----
#if SMARTKEY_THREAD_SAFE
class Lock {
    SemaphoreHandle_t m_;
public:
    Lock() : m_(xSemaphoreCreateRecursiveMutex()) {}
    ~Lock() { if (m_) vSemaphoreDelete(m_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    void take() { if (m_) xSemaphoreTakeRecursive(m_, portMAX_DELAY); }
    void give() { if (m_) xSemaphoreGiveRecursive(m_); }
};
#else
struct Lock { void take() {} void give() {} };
#endif

class Guard {
    Lock& l_;
public:
    explicit Guard(Lock& l) : l_(l) { l_.take(); }
    ~Guard() { l_.give(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

// ---- Event dispatch ----
inline void emitEvent(const Callbacks& cb, uint8_t key, Event ev, unsigned long duration, uint8_t clicks) {
    if (cb.onEvent) cb.onEvent(key, ev);
    if (cb.onEventEx) cb.onEventEx(cb.userData, key, ev);
    switch (ev) {
        case Event::Pressed:
            if (cb.onPress) cb.onPress(key);
            break;
        case Event::Released:
            if (cb.onRelease) cb.onRelease(key, duration);
            break;
        case Event::Click:
        case Event::DoubleClick:
        case Event::TripleClick:
            if (cb.onClick) cb.onClick(key, clicks);
            break;
        case Event::HoldMid:
        case Event::HoldLong:
        case Event::HoldVeryLong:
        case Event::HoldRepeat:
            if (cb.onHold) cb.onHold(key, ev);
            break;
        default:
            break;
    }
}

// ---- Debouncer: reports a value only after it was stable for debounceMs ----
class Debouncer {
    uint8_t raw_, stable_;
    unsigned long changedAt_;
public:
    explicit Debouncer(uint8_t initial = NO_KEY) : raw_(initial), stable_(initial), changedAt_(0) {}
    void reset(uint8_t v) { raw_ = v; stable_ = v; changedAt_ = 0; }
    uint8_t update(uint8_t v, unsigned long now, uint16_t debounceMs) {
        if (v != raw_) { raw_ = v; changedAt_ = now; }
        if (raw_ != stable_ && (now - changedAt_) >= debounceMs) stable_ = raw_;
        return stable_;
    }
    uint8_t stable() const { return stable_; }
};

// ---- KeyTracker: state machine of ONE key (click chain + hold events) ----
class KeyTracker {
    uint8_t key_;
    State state_;
    bool active_, mid_, long_, veryLong_;
    uint8_t clicks_, clickKey_;
    unsigned long pressStart_, releaseTime_, lastRepeat_;

    static Event clickEvent(uint8_t n) {
        return n >= 3 ? Event::TripleClick : (n == 2 ? Event::DoubleClick : Event::Click);
    }

    void setState(State s, unsigned long now, const Callbacks& cb) {
        if (state_ == s) return;
        state_ = s;
        if (cb.onStateChange) cb.onStateChange(key_, s, now - pressStart_);
    }

    void flushClicks(const Callbacks& cb) {
        if (clicks_ == 0) return;
        const uint8_t n = clicks_;
        clicks_ = 0;                       // clear first: callbacks may re-enter
        emitEvent(cb, clickKey_, clickEvent(n), 0, n);
    }

public:
    KeyTracker() { reset(); }

    void reset() {
        key_ = NO_KEY; state_ = State::Idle;
        active_ = mid_ = long_ = veryLong_ = false;
        clicks_ = 0; clickKey_ = NO_KEY;
        pressStart_ = releaseTime_ = lastRepeat_ = 0;
    }

    bool active() const { return active_; }
    bool idle() const { return !active_ && clicks_ == 0; }      // nothing pending
    uint8_t key() const { return active_ ? key_ : NO_KEY; }
    State state() const { return state_; }
    bool held() const { return active_ && (mid_ || long_ || veryLong_); }
    uint8_t clickCount() const { return clicks_; }
    unsigned long duration(unsigned long now) const { return active_ ? now - pressStart_ : 0; }

    void press(uint8_t key, unsigned long now, const Callbacks& cb) {
        if (clicks_ > 0 && clickKey_ != key) flushClicks(cb);   // other key: old chain is over
        key_ = key;
        active_ = true;
        pressStart_ = now;
        lastRepeat_ = now;
        mid_ = long_ = veryLong_ = false;
        setState(State::Pressed, now, cb);
        emitEvent(cb, key, Event::Pressed, 0, 0);
    }

    void release(unsigned long now, const Config& cfg, const Callbacks& cb) {
        if (!active_) return;
        const uint8_t key = key_;
        const unsigned long dur = now - pressStart_;
        const bool wasHeld = mid_ || long_ || veryLong_;
        active_ = false;

        emitEvent(cb, key, Event::Released, dur, 0);

        if (wasHeld) {
            clicks_ = 0;                                         // a hold is never a click
        } else {
            clicks_++;
            clickKey_ = key;
            releaseTime_ = now;
            if (clicks_ >= cfg.effectiveMaxClicks()) flushClicks(cb);   // chain complete -> report now
            // otherwise wait: tick() reports it when the click window expires
        }
        setState(State::Idle, now, cb);
    }

    // Call on every update: hold events and expiry of a pending click chain.
    void tick(unsigned long now, const Config& cfg, const Callbacks& cb) {
        if (active_) {
            const unsigned long d = now - pressStart_;
            // Independent ifs (not else-if): after a long pause the events still
            // arrive in order Mid -> Long -> VeryLong.
            if (!mid_ && d >= cfg.midPressTime) {
                mid_ = true;
                lastRepeat_ = now;
                flushClicks(cb);                                 // "click, then hold": report the click first
                setState(State::HoldMid, now, cb);
                emitEvent(cb, key_, Event::HoldMid, 0, 0);
            }
            if (!long_ && d >= cfg.longPressTime) {
                long_ = true;
                setState(State::HoldLong, now, cb);
                emitEvent(cb, key_, Event::HoldLong, 0, 0);
            }
            if (!veryLong_ && d >= cfg.veryLongPressTime) {
                veryLong_ = true;
                setState(State::HoldVeryLong, now, cb);
                emitEvent(cb, key_, Event::HoldVeryLong, 0, 0);
            }
            if (cfg.enableHoldRepeat && mid_ && cfg.holdRepeatInterval > 0 &&
                (now - lastRepeat_) >= cfg.holdRepeatInterval) {
                lastRepeat_ = now;
                emitEvent(cb, key_, Event::HoldRepeat, 0, 0);
            }
        } else if (clicks_ > 0) {
            const unsigned long window = (clicks_ == 1) ? cfg.doubleClickTime : cfg.tripleClickTime;
            if ((now - releaseTime_) > window) flushClicks(cb);
        }
    }
};

} // namespace detail

// ===================================
// Common base: config + callbacks + error
// ===================================
class ManagerBase {
protected:
    Config config_;
    Callbacks cb_;
    ErrorCode lastError_;
    mutable detail::Lock lock_;

    ManagerBase(const Callbacks& cbs, const Config& cfg)
        : config_(cfg), cb_(cbs), lastError_(ErrorCode::OK) {
        const ErrorCode e = cfg.validate();
        if (e != ErrorCode::OK) { config_ = Config(); lastError_ = e; }   // fall back to defaults
    }

    static Callbacks makeCallbacks(EventCallback simple) {
        Callbacks c;
        c.onEvent = simple;
        return c;
    }

public:
    // Returns InvalidParameter (and keeps the old config) if cfg is inconsistent.
    ErrorCode setConfig(const Config& cfg) {
        detail::Guard g(lock_);
        const ErrorCode e = cfg.validate();
        if (e != ErrorCode::OK) { lastError_ = e; return e; }
        config_ = cfg;
        return ErrorCode::OK;
    }
    Config getConfig() const { detail::Guard g(lock_); return config_; }

    void setCallbacks(const Callbacks& cbs) { detail::Guard g(lock_); cb_ = cbs; }
    void setEventCallback(EventCallback cb) { detail::Guard g(lock_); cb_.onEvent = cb; }
    void setEventCallbackEx(EventCallbackEx cb, void* user) { detail::Guard g(lock_); cb_.onEventEx = cb; cb_.userData = user; }
    void setStateCallback(StateCallback cb) { detail::Guard g(lock_); cb_.onStateChange = cb; }
    void setPressCallback(PressCallback cb) { detail::Guard g(lock_); cb_.onPress = cb; }
    void setReleaseCallback(ReleaseCallback cb) { detail::Guard g(lock_); cb_.onRelease = cb; }
    void setClickCallback(ClickCallback cb) { detail::Guard g(lock_); cb_.onClick = cb; }
    void setHoldCallback(HoldCallback cb) { detail::Guard g(lock_); cb_.onHold = cb; }

    ErrorCode getLastError() const { return lastError_; }
};

// ===================================
// Manager: ONE key at a time (a scanner returns a single key code)
// ===================================
class Manager : public ManagerBase {
    detail::Debouncer deb_;
    detail::KeyTracker trk_;

public:
    Manager(EventCallback simpleCallback = nullptr, Config cfg = Config())
        : ManagerBase(makeCallbacks(simpleCallback), cfg) {}

    Manager(const Callbacks& cbs, Config cfg = Config())
        : ManagerBase(cbs, cfg) {}

    void reset() {
        detail::Guard g(lock_);
        deb_.reset(NO_KEY);
        trk_.reset();
        lastError_ = ErrorCode::OK;
    }

    // Main update: raw key code from the hardware (NO_KEY if nothing pressed).
    void update(uint8_t rawKeyCode, unsigned long currentTime) {
        detail::Guard g(lock_);
        trk_.tick(currentTime, config_, cb_);
        const uint8_t stable = deb_.update(rawKeyCode, currentTime, config_.debounceTime);
        const uint8_t current = trk_.key();
        if (stable != current) {
            if (current != NO_KEY) trk_.release(currentTime, config_, cb_);
            if (stable != NO_KEY) trk_.press(stable, currentTime, cb_);
        }
    }
#ifdef ARDUINO
    void update(uint8_t rawKeyCode) { update(rawKeyCode, millis()); }
#endif

    // --- Queries ---
    bool isAnyKeyActive() const { detail::Guard g(lock_); return trk_.active(); }
    uint8_t getCurrentKey() const { detail::Guard g(lock_); return trk_.key(); }
    State getCurrentState() const { detail::Guard g(lock_); return trk_.state(); }
    unsigned long getCurrentPressDuration(unsigned long currentTime) const { detail::Guard g(lock_); return trk_.duration(currentTime); }
    bool isCurrentKeyHeld() const { detail::Guard g(lock_); return trk_.held(); }
    uint8_t getCurrentClickCount() const { detail::Guard g(lock_); return trk_.clickCount(); }

    // Release the current key now (e.g. when the UI switches screen).
    void forceRelease(unsigned long currentTime) {
        detail::Guard g(lock_);
        trk_.release(currentTime, config_, cb_);
    }
};

// ===================================
// MultiManager: several simultaneous keys (needed for combos)
// Feed it with the list of ALL keys currently pressed
// (KeyScanner::scanMulti). Every key has its own debounce and click chain.
// ===================================
template <uint8_t MAX_KEYS = 4>
class MultiManager : public ManagerBase {
    static_assert(MAX_KEYS > 0 && MAX_KEYS <= 64, "MAX_KEYS must be 1..64");

    struct Slot {
        bool used;
        uint8_t code;
        detail::Debouncer deb;
        detail::KeyTracker trk;
        Slot() : used(false), code(NO_KEY) {}
    };
    Slot slots_[MAX_KEYS];

    static bool contains(const uint8_t* keys, uint8_t n, uint8_t code) {
        for (uint8_t i = 0; i < n; i++) if (keys[i] == code) return true;
        return false;
    }
    int8_t find(uint8_t code) const {
        for (uint8_t i = 0; i < MAX_KEYS; i++) if (slots_[i].used && slots_[i].code == code) return (int8_t)i;
        return -1;
    }

public:
    MultiManager(EventCallback simpleCallback = nullptr, Config cfg = Config())
        : ManagerBase(makeCallbacks(simpleCallback), cfg) {}
    MultiManager(const Callbacks& cbs, Config cfg = Config())
        : ManagerBase(cbs, cfg) {}

    void reset() {
        detail::Guard g(lock_);
        for (uint8_t i = 0; i < MAX_KEYS; i++) { slots_[i].used = false; slots_[i].trk.reset(); }
        lastError_ = ErrorCode::OK;
    }

    // keys/count: every key that is currently pressed. If more than MAX_KEYS
    // distinct keys are active at once the extra ones are ignored and
    // getLastError() reports BufferFull.
    void update(const uint8_t* keys, uint8_t count, unsigned long currentTime) {
        detail::Guard g(lock_);

        for (uint8_t i = 0; i < count; i++) {                    // allocate slots for new keys
            const uint8_t k = keys[i];
            if (k == NO_KEY || find(k) >= 0) continue;
            int8_t freeIdx = -1;
            for (uint8_t j = 0; j < MAX_KEYS; j++) if (!slots_[j].used) { freeIdx = (int8_t)j; break; }
            if (freeIdx < 0) { lastError_ = ErrorCode::BufferFull; continue; }
            Slot& s = slots_[freeIdx];
            s.used = true; s.code = k; s.deb.reset(0); s.trk.reset();
        }

        for (uint8_t j = 0; j < MAX_KEYS; j++) {
            Slot& s = slots_[j];
            if (!s.used) continue;
            const bool present = contains(keys, count, s.code);
            s.trk.tick(currentTime, config_, cb_);
            const uint8_t stable = s.deb.update(present ? 1 : 0, currentTime, config_.debounceTime);
            if (stable == 1 && !s.trk.active()) s.trk.press(s.code, currentTime, cb_);
            else if (stable == 0 && s.trk.active()) s.trk.release(currentTime, config_, cb_);
            if (!present && stable == 0 && s.trk.idle()) s.used = false;   // free the slot
        }
    }
#ifdef ARDUINO
    void update(const uint8_t* keys, uint8_t count) { update(keys, count, millis()); }
#endif

    uint8_t getActiveKeyCount() const {
        detail::Guard g(lock_);
        uint8_t n = 0;
        for (uint8_t i = 0; i < MAX_KEYS; i++) if (slots_[i].used && slots_[i].trk.active()) n++;
        return n;
    }
    bool isKeyPressed(uint8_t code) const {
        detail::Guard g(lock_);
        const int8_t i = find(code);
        return i >= 0 && slots_[i].trk.active();
    }
    State getKeyState(uint8_t code) const {
        detail::Guard g(lock_);
        const int8_t i = find(code);
        return i >= 0 ? slots_[i].trk.state() : State::Idle;
    }
};

// --- Helper functions for converting enum to string (for debugging) ---

inline const char* eventToString(Event event) {
    switch (event) {
        case Event::None: return "None";
        case Event::Pressed: return "Pressed";
        case Event::Released: return "Released";
        case Event::Click: return "Click";
        case Event::DoubleClick: return "DoubleClick";
        case Event::TripleClick: return "TripleClick";
        case Event::HoldMid: return "HoldMid";
        case Event::HoldLong: return "HoldLong";
        case Event::HoldVeryLong: return "HoldVeryLong";
        case Event::HoldRepeat: return "HoldRepeat";
        default: return "Unknown";
    }
}

inline const char* stateToString(State state) {
    switch (state) {
        case State::Idle: return "Idle";
        case State::Pressed: return "Pressed";
        case State::HoldMid: return "HoldMid";
        case State::HoldLong: return "HoldLong";
        case State::HoldVeryLong: return "HoldVeryLong";
        default: return "Unknown";
    }
}

inline const char* errorToString(ErrorCode err) {
    switch (err) {
        case ErrorCode::OK: return "OK";
        case ErrorCode::InvalidParameter: return "Invalid Parameter";
        case ErrorCode::NotInitialized: return "Not Initialized";
        case ErrorCode::AlreadyInitialized: return "Already Initialized";
        case ErrorCode::BufferFull: return "Buffer Full";
        case ErrorCode::NotFound: return "Not Found";
        case ErrorCode::TimeoutError: return "Timeout";
        case ErrorCode::Busy: return "Busy";
        default: return "Unknown Error";
    }
}

// --- Legacy compatibility (v1 / v2.0 names) ---
using KeyEvent = Event;
using KeyState = State;
using KeyConfig = Config;
using KeyCallbacks = Callbacks;
using KeyCallback = EventCallback;
using KeyStateCallback = StateCallback;
using KeyPressCallback = PressCallback;
using KeyReleaseCallback = ReleaseCallback;
using KeyClickCallback = ClickCallback;
using KeyHoldCallback = HoldCallback;
using SmartKeyManager = Manager;

static constexpr Event kEventNone = Event::None;
static constexpr Event kEventPressed = Event::Pressed;
static constexpr Event kEventReleased = Event::Released;
static constexpr Event kEventClick = Event::Click;
static constexpr Event kEventDoubleClick = Event::DoubleClick;
static constexpr Event kEventTripleClick = Event::TripleClick;
static constexpr Event kEventHoldMid = Event::HoldMid;
static constexpr Event kEventHoldLong = Event::HoldLong;
static constexpr Event kEventHoldVeryLong = Event::HoldVeryLong;
static constexpr Event kEventHoldRepeat = Event::HoldRepeat;

static constexpr State kStateIdle = State::Idle;
static constexpr State kStatePressed = State::Pressed;
static constexpr State kStateHoldMid = State::HoldMid;
static constexpr State kStateHoldLong = State::HoldLong;
static constexpr State kStateHoldVeryLong = State::HoldVeryLong;

inline const char* keyEventToString(Event event) { return eventToString(event); }
inline const char* keyStateToString(State state) { return stateToString(state); }

} // namespace SmartKey

#endif // SMARTKEY_CORE_H
