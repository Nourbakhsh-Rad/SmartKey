/*
 * ═══════════════════════════════════════════════════════════════════
 * SmartKey + Arduino-IRremote: TV remote control (25 buttons)
 * ═══════════════════════════════════════════════════════════════════
 *
 * Manages an old-style TV remote (power, mute, TV/AV, P/N/S, volume,
 * channel, digits 0-9, -/--, display, sleep, surround, I/II) with the
 * SmartKey library, using Arduino-IRremote 4.x only to receive the frames.
 *
 * Needs: Arduino-IRremote >= 4.0  (Library Manager: "IRremote")
 *        https://github.com/Arduino-IRremote/Arduino-IRremote
 * Tested on the logic level only (see "Testing" below); use a board with
 * enough RAM for the many Serial strings (ESP32 / ESP8266 / RP2040 / STM32).
 *
 * Wiring: IR receiver module (e.g. VS1838B / TSOP38238)
 *         OUT -> IR_RECEIVE_PIN, VCC -> 3.3 V (or 5 V), GND -> GND
 *         The "TV power" output is LED_BUILTIN (replace with a relay).
 *
 * ───────────────────────────────────────────────────────────────────
 * IMPORTANT - the IR codes in BUTTONS[] below are PLACEHOLDERS.
 * Every remote sends its own codes. Set LEARN_MODE to 1, upload, open the
 * Serial Monitor (115200) and press the buttons when asked. The sketch prints
 * a ready-to-paste BUTTONS[] table and REMOTE_ADDRESS. Paste them, set
 * LEARN_MODE back to 0.
 * ───────────────────────────────────────────────────────────────────
 *
 * How IR is mapped to SmartKey:
 *   An IR remote only sends frames while a button is held (no "release"
 *   frame). IrKeyScanner turns this into a normal key signal: a key counts as
 *   pressed until no frame has arrived for IR_RELEASE_TIMEOUT_MS. SmartKey's
 *   Manager then gives clicks, holds and auto-repeat for free:
 *     - volume / channel : ValueController (step on press, repeat when held)
 *     - mute / surround  : Toggle helper
 *     - digits           : channel entry (1-2 digits, committed on timeout)
 *     - power            : click = on/off, hold 3 s = factory reset
 *
 * Testing: the logic was run on a PC against a scripted stand-in of the
 * IRremote API (same calls as used here); it was not run with a real remote.
 */

#include <Arduino.h>
#include <IRremote.hpp>     // Arduino-IRremote 4.x
#include <SmartKey.h>

// The alias avoids name clashes between the two libraries.
namespace SK = SmartKey;

// ═══════════════════════════════════════════════════════════════════
// SECTION 1: Settings
// ═══════════════════════════════════════════════════════════════════

#define LEARN_MODE 					false                			// true = learn the codes of YOUR remote

#define LOG_KEY_EVENTS 			1            							// print every SmartKey event (except HoldRepeat)
#define SLEEP_UNIT_MS 			60000UL       						// sleep timer unit: 1 minute

static const uint8_t 				IR_RECEIVE_PIN = 39;
static const uint8_t 				POWER_OUT_PIN = 2; 				// "TV power" output (LED or relay)


// A key is "released" when no IR frame arrived for this long. It must be
// longer than the repeat period of the protocol (~110 ms for NEC / Panasonic)
static const uint16_t IR_RELEASE_TIMEOUT_MS = 200;

static const uint16_t DIGIT_ENTRY_TIMEOUT_MS = 2000; 	// wait for the 2nd channel digit

static unsigned long g_now = 0;     									// time used by the callbacks


// ═══════════════════════════════════════════════════════════════════
// SECTION 2: Key codes (chosen by us) and the IR code table
// ═══════════════════════════════════════════════════════════════════

#include "rmtCodes.h"

static const uint8_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

static uint8_t keyFromIr(uint16_t address, uint16_t command) {
    if (REMOTE_ADDRESS != ANY_ADDRESS && address != REMOTE_ADDRESS) return SK::NO_KEY;
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if (BUTTONS[i].irCommand == command) return BUTTONS[i].key;
    }
    return SK::NO_KEY;
}

static const char* keyName(uint8_t key) {
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        if (BUTTONS[i].key == key) return BUTTONS[i].name;
    }
    return "?";
}


// ═══════════════════════════════════════════════════════════════════
// SECTION 3: IR scanner (IRremote -> SmartKey key signal)
// ═══════════════════════════════════════════════════════════════════

class IrKeyScanner : public SK::KeyScanner {
    uint8_t key_;
    bool active_;
    unsigned long lastFrame_;

public:
    IrKeyScanner() : key_(SK::NO_KEY), active_(false), lastFrame_(0) {}

    bool begin() override {
        IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
        return true;
    }

    // Returns the key that is currently held, or NO_KEY. Call it often.
    uint8_t scan() override {
        const unsigned long now = millis();

        if (IrReceiver.decode()) {
            if (IrReceiver.decodedIRData.protocol != UNKNOWN) {
                if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) {
                    // "Repeat" frame (NEC style): the same key is still held
                    if (active_) lastFrame_ = now;
                } else {
                    // Full frame. Some protocols (e.g. Panasonic) resend the full
                    // frame while the button is held, so this also keeps the key alive.
                    const uint8_t k = keyFromIr(IrReceiver.decodedIRData.address,
                                                IrReceiver.decodedIRData.command);
                    if (k != SK::NO_KEY) {
                        key_ = k;
                        active_ = true;
                        lastFrame_ = now;
                    }
                }
            }
            IrReceiver.resume();    // ready for the next frame
        }

        // No frame for IR_RELEASE_TIMEOUT_MS -> the button was released
        if (active_ && (now - lastFrame_) > IR_RELEASE_TIMEOUT_MS) {
            active_ = false;
        }
        return active_ ? key_ : SK::NO_KEY;
    }
};

IrKeyScanner scanner;

// ═══════════════════════════════════════════════════════════════════
// SECTION 4: LEARN_MODE - find the codes of your remote
// ═══════════════════════════════════════════════════════════════════

#if LEARN_MODE
static void discardFramesFor(unsigned long ms) {
    const unsigned long end = millis() + ms;
    while ((long)(end - millis()) > 0) {
        if (IrReceiver.decode()) IrReceiver.resume();
        delay(1);
    }
}

// Wait for one new (non-repeat, known protocol) frame
static void waitForFrame(uint16_t& address, uint16_t& command) {
    for (;;) {
        if (IrReceiver.decode()) {
            const bool good = IrReceiver.decodedIRData.protocol != UNKNOWN &&
                              !(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT);
            if (good) {
                address = IrReceiver.decodedIRData.address;
                command = IrReceiver.decodedIRData.command;
                Serial.print("   got: ");
                IrReceiver.printIRResultShort(&Serial);
                IrReceiver.resume();
                return;
            }
            IrReceiver.resume();
        }
        delay(1);
    }
}

static void printHex(uint16_t v) {
    Serial.print("0x");
    Serial.print(v, HEX);
}

static void runLearnMode() {
    uint16_t commands[BUTTON_COUNT];
    uint16_t address = 0;

    Serial.println("\n=== LEARN MODE ===");
    Serial.println("Press each button once when asked (short press).\n");

    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        Serial.print("Press: ");
        Serial.println(BUTTONS[i].name);
        uint16_t a = 0;
        waitForFrame(a, commands[i]);
        if (i == 0) address = a;
        discardFramesFor(600);          // ignore the repeat frames of this press
    }

    Serial.println("\n=== Copy this into the sketch ===\n");
    Serial.print("static const uint16_t REMOTE_ADDRESS = ");
    printHex(address);
    Serial.println(";\n");
    Serial.println("static const Button BUTTONS[] = {");
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        Serial.print("    {");
        Serial.print(BUTTONS[i].symbol);
        Serial.print(", \"");
        Serial.print(BUTTONS[i].symbol);
        Serial.print("\", \"");
        Serial.print(BUTTONS[i].name);
        Serial.print("\", ");
        printHex(commands[i]);
        Serial.println(i + 1 < BUTTON_COUNT ? "}," : "}");
    }
    Serial.println("};");

    // Warn about two buttons with the same code
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        for (uint8_t j = i + 1; j < BUTTON_COUNT; j++) {
            if (commands[i] == commands[j]) {
                Serial.print("WARNING: same code for ");
                Serial.print(BUTTONS[i].name);
                Serial.print(" and ");
                Serial.println(BUTTONS[j].name);
            }
        }
    }
    Serial.println("\nDone. Set LEARN_MODE to 0 and upload again.");
}

#else

// ═══════════════════════════════════════════════════════════════════
// SECTION 5: TV state
// ═══════════════════════════════════════════════════════════════════

static const char* const INPUT_NAMES[]   = {"TV", "AV1", "AV2", "S-Video"};
static const char* const PICTURE_NAMES[] = {"Standard", "Dynamic", "Soft", "Personal"};
static const char* const SOUND_NAMES[]   = {"Standard", "Music", "Speech"};
static const char* const AUDIO_NAMES[]   = {"Stereo", "Mono", "Language I", "Language II"};
static const uint8_t SLEEP_STEPS[]       = {0, 15, 30, 45, 60, 90};   // minutes, 0 = off

template <typename T, size_t N>
static constexpr uint8_t countOf(const T (&)[N]) { return (uint8_t)N; }

static bool powerOn = false;
static bool muted = false;
static bool surround = false;
static uint8_t inputIndex = 0, pictureIndex = 0, soundIndex = 0, audioIndex = 0, sleepIndex = 0;
static unsigned long sleepEnd = 0;

// Channel number entry (digits)
static uint8_t entryDigits[2];
static uint8_t entryCount = 0;
static bool entryActive = false;
static unsigned long entryDeadline = 0;

// --- Controllers ---
static void onVolumeChange(int32_t /*oldValue*/, int32_t newValue);
static void onChannelChange(int32_t /*oldValue*/, int32_t newValue);

static SK::ValueController volumeController(KEY_VOL_UP, KEY_VOL_DOWN, 20, 0, 63, 1, false, onVolumeChange);
static SK::ValueController channelController(KEY_CH_UP, KEY_CH_DOWN, 1, 1, 99, 1, true, onChannelChange);
static SK::ValueControllerManager<2> controllerManager;

// --- Toggles (mute / surround) ---
static SK::Toggle toggles;

static void onMuteToggle(uint8_t /*key*/, bool state) {
    muted = state;
    Serial.println(muted ? "🔇 MUTE ON" : "🔊 MUTE OFF");
}

static void onSurroundToggle(uint8_t /*key*/, bool state) {
    surround = state;
    Serial.println(surround ? "🎧 Surround ON" : "🎧 Surround OFF");
}

static void onVolumeChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.print("🔊 Volume ");
    Serial.print(newValue);
    Serial.print("/63 [");
    for (int i = 0; i < 21; i++) Serial.print(i < (newValue * 21) / 63 ? "█" : "░");
    Serial.println("]");
    if (muted) toggles.setState(KEY_MUTE, false);   // changing the volume cancels mute
}

static void onChannelChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.print("📺 Channel ");
    Serial.println(newValue);
}

// --- Actions ---
static void printStatus() {
    Serial.println("┌──────────── STATUS ────────────┐");
    Serial.print("│ Power: ");   Serial.println(powerOn ? "ON" : "STANDBY");
    Serial.print("│ Input: ");   Serial.println(INPUT_NAMES[inputIndex]);
    Serial.print("│ Channel: "); Serial.println(channelController.getValue());
    Serial.print("│ Volume: ");  Serial.print(volumeController.getValue()); Serial.println(muted ? " (muted)" : "");
    Serial.print("│ Picture: "); Serial.println(PICTURE_NAMES[pictureIndex]);
    Serial.print("│ Sound: ");   Serial.print(SOUND_NAMES[soundIndex]); Serial.println(surround ? " + surround" : "");
    Serial.print("│ Audio: ");   Serial.println(AUDIO_NAMES[audioIndex]);
    Serial.print("│ Sleep: ");
    if (SLEEP_STEPS[sleepIndex] == 0) Serial.println("off");
    else { Serial.print(SLEEP_STEPS[sleepIndex]); Serial.println(" min"); }
    Serial.println("└────────────────────────────────┘");
}

static void setPower(bool on) {
    powerOn = on;
    digitalWrite(POWER_OUT_PIN, on ? HIGH : LOW);
    entryActive = false;
    if (!on) {
        sleepIndex = 0;                 // sleep timer is cancelled by standby
    }
    Serial.println(on ? "⏻ POWER ON" : "⏻ STANDBY");
}

static void factoryReset() {
    Serial.println("♻️  FACTORY RESET");
    volumeController.setValue(20);
    channelController.setValue(1);
    if (muted) toggles.setState(KEY_MUTE, false);
    if (surround) toggles.setState(KEY_SURROUND, false);
    inputIndex = pictureIndex = soundIndex = audioIndex = sleepIndex = 0;
}

static void normalSettings() {
    // "N": back to normal picture and sound
    pictureIndex = soundIndex = 0;
    if (surround) toggles.setState(KEY_SURROUND, false);
    Serial.println("✨ Picture and sound set to normal");
}

static void cycleSleepTimer(unsigned long now) {
    sleepIndex = (sleepIndex + 1) % countOf(SLEEP_STEPS);
    if (SLEEP_STEPS[sleepIndex] == 0) {
        Serial.println("⏲  Sleep timer off");
    } else {
        sleepEnd = now + (unsigned long)SLEEP_STEPS[sleepIndex] * SLEEP_UNIT_MS;
        Serial.print("⏲  Sleep in ");
        Serial.print(SLEEP_STEPS[sleepIndex]);
        Serial.println(" min");
    }
}

// --- Channel entry with the digit keys ---
static void commitEntry() {
    entryActive = false;
    if (entryCount == 0) return;
    int value = entryDigits[0];
    if (entryCount == 2) value = value * 10 + entryDigits[1];
    entryCount = 0;
    if (value >= 1 && value <= 99) {
        channelController.setValue(value);
    } else {
        Serial.println("❌ Invalid channel");
    }
}

static void startEntry(unsigned long now) {
    entryActive = true;
    entryCount = 0;
    entryDeadline = now + DIGIT_ENTRY_TIMEOUT_MS;
}

static void onDigit(uint8_t digit, unsigned long now) {
    if (!entryActive) startEntry(now);
    entryDigits[entryCount++] = digit;
    entryDeadline = now + DIGIT_ENTRY_TIMEOUT_MS;

    Serial.print("⌨️  Channel: ");
    Serial.print(entryDigits[0]);
    if (entryCount == 2) Serial.println(entryDigits[1]);
    else Serial.println('-');                                  // '-' = second digit still expected

    if (entryCount == 2) commitEntry();
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 6: SmartKey event handling
// ═══════════════════════════════════════════════════════════════════

static SK::Manager keyManager;

static void onKeyEvent(uint8_t keyCode, SK::Event event) {
#if LOG_KEY_EVENTS
    if (event != SK::Event::HoldRepeat) {
        Serial.print("[");
        Serial.print(keyName(keyCode));
        Serial.print(" ");
        Serial.print(SK::eventToString(event));
        Serial.println("]");
    }
#endif

    // Standby: only the power button works (like a real TV)
    if (!powerOn) {
        if (keyCode == KEY_POWER && event == SK::Event::Click) setPower(true);
        return;
    }

    // Volume and channel keys: step on press, repeat while held
    controllerManager.handleKeyEvent(keyCode, event, g_now);

    // Digits react immediately on Pressed (no waiting for a click)
    if (keyCode <= KEY_9 && event == SK::Event::Pressed) {
        onDigit(keyCode, g_now);
        return;
    }

    // Mute and surround
    toggles.handleKeyEvent(keyCode, event);

    // Power button: hold 3 s = factory reset
    if (keyCode == KEY_POWER && event == SK::Event::HoldVeryLong) {
        factoryReset();
        return;
    }

    // Everything else acts on Click
    if (event != SK::Event::Click) return;

    switch (keyCode) {
        case KEY_POWER:
            setPower(false);
            break;
        case KEY_TV_AV:
            inputIndex = (inputIndex + 1) % countOf(INPUT_NAMES);
            Serial.print("🔌 Input: ");
            Serial.println(INPUT_NAMES[inputIndex]);
            break;
        case KEY_PICTURE:
            pictureIndex = (pictureIndex + 1) % countOf(PICTURE_NAMES);
            Serial.print("🖼  Picture: ");
            Serial.println(PICTURE_NAMES[pictureIndex]);
            break;
        case KEY_NORMAL:
            normalSettings();
            break;
        case KEY_SOUND:
            soundIndex = (soundIndex + 1) % countOf(SOUND_NAMES);
            Serial.print("🎵 Sound: ");
            Serial.println(SOUND_NAMES[soundIndex]);
            break;
        case KEY_AUDIO:
            audioIndex = (audioIndex + 1) % countOf(AUDIO_NAMES);
            Serial.print("🗣  Audio: ");
            Serial.println(AUDIO_NAMES[audioIndex]);
            break;
        case KEY_DASH:
            startEntry(g_now);
            Serial.println("⌨️  Channel: --");
            break;
        case KEY_DISPLAY:
            printStatus();
            break;
        case KEY_SLEEP:
            cycleSleepTimer(g_now);
            break;
        default:
            break;
    }
}


#endif // LEARN_MODE

// ═══════════════════════════════════════════════════════════════════
// SECTION 7: Setup and loop
// ═══════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);     // wait for the Serial Monitor (up to 3 s)

    pinMode(POWER_OUT_PIN, OUTPUT);
    digitalWrite(POWER_OUT_PIN, LOW);

    Serial.println("\n=== SmartKey + IRremote TV remote ===");

    scanner.begin();

#if LEARN_MODE
    runLearnMode();
    for (;;) delay(1000);

#else

    // Manager configuration for an IR remote:
    //  - debounce 0: IR frames are clean and IrKeyScanner already adds the
    //    "release timeout"
    //  - no double click: clicks are reported immediately
    //  - hold repeat: drives the auto-repeat of volume and channel
    SK::Config cfg;
    cfg.debounceTime = 0;
    cfg.enableDoubleClick = false;
    cfg.enableHoldRepeat = true;
    cfg.holdRepeatInterval = 100;
    if (keyManager.setConfig(cfg) != SK::ErrorCode::OK) {
        Serial.println("Invalid SmartKey configuration!");
    }
    keyManager.setEventCallback(onKeyEvent);

    // Hold settings: minimum ms between two repeat steps, base acceleration
    // (the speed grows x3 after 1.5 s and x5 after 3 s of holding)
    volumeController.setHoldSettings(100, 1);   // volume: 10 steps per second
    channelController.setHoldSettings(400, 1);  // channel: 2.5 steps per second
    controllerManager.addController(&volumeController);
    controllerManager.addController(&channelController);

    toggles.registerToggle(KEY_MUTE, false, onMuteToggle);
    toggles.registerToggle(KEY_SURROUND, false, onSurroundToggle);

    Serial.println("Standby. Press POWER.");
    Serial.println("Tip: set LEARN_MODE to 1 to read the codes of your own remote.");
    
#endif // LEARN_MODE
}

void loop() {
	#if LEARN_MODE
		delay(100);
		
	#else
    g_now = millis();

    // IR frames -> held key -> SmartKey
    keyManager.update(scanner.scan(), g_now);

    // Channel entry: commit a single digit after the timeout
    if (entryActive && (long)(g_now - entryDeadline) >= 0) {
        commitEntry();
    }

    // Sleep timer
    if (powerOn && SLEEP_STEPS[sleepIndex] != 0 && (long)(g_now - sleepEnd) >= 0) {
        Serial.println("⏲  Sleep timer expired");
        setPower(false);
    }
    
  #endif // LEARN_MODE
}
