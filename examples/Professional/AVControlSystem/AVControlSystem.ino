/*
 * ═══════════════════════════════════════════════════════════════════
 * SmartKey - Audio/Video control system (ValueController showcase)
 * ═══════════════════════════════════════════════════════════════════
 *
 * A complete control system for an audio/video device built from several
 * ValueControllers plus some application logic.
 *
 * Features:
 * - Volume with visual bar, hearing-protection warnings and mute
 * - Brightness with a colored meter
 * - Bass / treble with an equalizer display
 * - Channel selector with favorites (rollover)
 * - Temperature control with safety limits
 * - Presets (load / save)
 * - Simple menu navigation
 * - Double click = extra jump, hold = accelerating auto-repeat
 *
 * No hardware needed: an automated demo runs first (simulated key presses,
 * real time), then keys are read from the Serial Monitor. Letters are
 * case-insensitive; see the key table printed at start-up.
 */

#include <SmartKey.h>

using namespace SmartKey;

// Time used by all callbacks (updated in loop() and in the demo helpers)
static unsigned long g_now = 0;

// ═══════════════════════════════════════════════════════════════════
// SECTION 1: Key definitions (key code = ASCII code of the character)
// ═══════════════════════════════════════════════════════════════════

// Main keys
static const uint8_t KEY_VOL_UP      = 0x2B;    // '+'
static const uint8_t KEY_VOL_DOWN    = 0x2D;    // '-'
static const uint8_t KEY_BRIGHT_UP   = 0x55;    // 'U'
static const uint8_t KEY_BRIGHT_DOWN = 0x44;    // 'D'
static const uint8_t KEY_BASS_UP     = 0x42;    // 'B'
static const uint8_t KEY_BASS_DOWN   = 0x56;    // 'V'
static const uint8_t KEY_TREBLE_UP   = 0x54;    // 'T'
static const uint8_t KEY_TREBLE_DOWN = 0x47;    // 'G'
static const uint8_t KEY_CH_UP       = 0x43;    // 'C'
static const uint8_t KEY_CH_DOWN     = 0x58;    // 'X'
static const uint8_t KEY_TEMP_UP     = 0x52;    // 'R' (right)
static const uint8_t KEY_TEMP_DOWN   = 0x4C;    // 'L' (left)

// Auxiliary keys
static const uint8_t KEY_MUTE        = 0x4D;    // 'M'
static const uint8_t KEY_INFO        = 0x49;    // 'I'
static const uint8_t KEY_MENU        = 0x4E;    // 'N'
static const uint8_t KEY_SAVE        = 0x53;    // 'S'
static const uint8_t KEY_PRESET_1    = 0x31;    // '1'
static const uint8_t KEY_PRESET_2    = 0x32;    // '2'
static const uint8_t KEY_PRESET_3    = 0x33;    // '3'

// ═══════════════════════════════════════════════════════════════════
// SECTION 2: Helper class for visual output
// ═══════════════════════════════════════════════════════════════════

class VisualDisplay {
public:
    // Horizontal bar: value in 0..max
    static void printVerticalBar(int32_t value, int32_t max, int width = 20) {
        if (max <= 0) return;
        Serial.print("[");
        int filled = (value * width) / max;
        for (int i = 0; i < width; i++) {
            if (i < filled) Serial.print("█");
            else Serial.print("░");
        }
        Serial.print("]");
    }

    // Meter with a color indicator
    static void printColoredMeter(int32_t value, int32_t min, int32_t max) {
        int32_t range = max - min;
        if (range <= 0) return;
        int32_t normalized = value - min;
        float percent = (float)normalized / range * 100.0f;

        const char* color;
        if (percent < 33) color = "🟢";         // green
        else if (percent < 66) color = "🟡";    // yellow
        else color = "🔴";                      // red

        Serial.print(color);
        Serial.print(" ");
        printVerticalBar(normalized, range, 15);
    }

    // Bar centered on zero (for the equalizer)
    static void printHorizontalBar(int32_t value, int32_t min, int32_t max, int width) {
        int32_t pos = ((value - min) * width) / (max - min);

        for (int i = 0; i < width; i++) {
            if (i == width / 2) {
                Serial.print("|");
            } else if (value > 0 && i > width / 2 && i <= pos) {
                Serial.print("▓");
            } else if (value < 0 && i < width / 2 && i >= pos) {
                Serial.print("▓");
            } else {
                Serial.print("░");
            }
        }
    }

    // Equalizer panel
    static void printEQ(int32_t bass, int32_t treble) {
        Serial.println("\n    ╔═══════════════════╗");
        Serial.println("    ║   EQUALIZER       ║");
        Serial.println("    ╠═══════════════════╣");

        Serial.print("    ║ BASS:   ");
        printHorizontalBar(bass, -12, 12, 10);
        Serial.println(" ║");

        Serial.print("    ║ TREBLE: ");
        printHorizontalBar(treble, -12, 12, 10);
        Serial.println(" ║");

        Serial.println("    ╚═══════════════════╝");
    }
};

// ═══════════════════════════════════════════════════════════════════
// SECTION 3: Preset system (store / recall settings)
// ═══════════════════════════════════════════════════════════════════

struct AudioPreset {
    int32_t volume;
    int32_t bass;
    int32_t treble;
    int32_t brightness;
    char name[16];
};

class PresetManager {
private:
    AudioPreset presets[3];

    void set(uint8_t i, const char* name, int32_t vol, int32_t bass, int32_t treble, int32_t bright) {
        strncpy(presets[i].name, name, sizeof(presets[i].name) - 1);
        presets[i].name[sizeof(presets[i].name) - 1] = '\0';
        presets[i].volume = vol;
        presets[i].bass = bass;
        presets[i].treble = treble;
        presets[i].brightness = bright;
    }

public:
    PresetManager() {
        // Defaults
        set(0, "Movie",  70, 6, -2, 40);
        set(1, "Music",  60, 3,  4, 60);
        set(2, "Gaming", 80, 8,  2, 80);
    }

    AudioPreset getPreset(uint8_t index) {
        if (index < 3) return presets[index];
        return presets[0];
    }

    void savePreset(uint8_t index, int32_t vol, int32_t bass, int32_t treb, int32_t bright) {
        if (index >= 3) return;
        presets[index].volume = vol;
        presets[index].bass = bass;
        presets[index].treble = treb;
        presets[index].brightness = bright;
    }
};

PresetManager presetManager;

// ═══════════════════════════════════════════════════════════════════
// SECTION 4: Controllers with rich callbacks
// ═══════════════════════════════════════════════════════════════════

// --- Volume controller ---
void onVolumeChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.println("\n┌─────────────────────────────────┐");
    Serial.println("│       🔊 VOLUME CONTROL         │");
    Serial.println("├─────────────────────────────────┤");
    Serial.print("│ Level: ");
    Serial.print(newValue);
    Serial.print("%  ");

    VisualDisplay::printVerticalBar(newValue, 100, 15);

    Serial.println(" │");
    Serial.println("└─────────────────────────────────┘");

    if (newValue > 80) {
        Serial.println("  ⚠️  High volume - consider reducing the bass");
    }

    // Hearing-protection warning
    if (newValue > 85) {
        Serial.println("  🔴 WARNING: Hearing protection - high volume!");
    }
}

ValueController volumeController(
    KEY_VOL_UP, KEY_VOL_DOWN,
    50,      // initial: 50%
    0, 100,  // range: 0-100%
    5,       // step: 5%
    false,   // no rollover
    onVolumeChange
);

// --- Brightness controller ---
void onBrightnessChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.println("\n┌─────────────────────────────────┐");
    Serial.println("│       💡 BRIGHTNESS             │");
    Serial.println("├─────────────────────────────────┤");
    Serial.print("│ Level: ");
    Serial.print(newValue);
    Serial.print("%  ");

    VisualDisplay::printColoredMeter(newValue, 0, 100);

    Serial.println(" │");
    Serial.println("└─────────────────────────────────┘");

    // Advice based on brightness
    if (newValue < 20) {
        Serial.println("  💡 Tip: Low brightness - good for night viewing");
    } else if (newValue > 80) {
        Serial.println("  ☀️  Tip: High brightness - good for bright rooms");
    }
}

ValueController brightnessController(
    KEY_BRIGHT_UP, KEY_BRIGHT_DOWN,
    50, 0, 100, 10, false,
    onBrightnessChange
);

// --- Bass controller ---
void onBassChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.print("\n🎵 BASS: ");
    Serial.print(newValue > 0 ? "+" : "");
    Serial.print(newValue);
    Serial.println(" dB");
}

ValueController bassController(
    KEY_BASS_UP, KEY_BASS_DOWN,
    0, -12, 12, 1, false,
    onBassChange
);

// --- Treble controller ---
void onTrebleChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.print("🎵 TREBLE: ");
    Serial.print(newValue > 0 ? "+" : "");
    Serial.print(newValue);
    Serial.println(" dB");

    // Show the full equalizer
    VisualDisplay::printEQ(bassController.getValue(), newValue);
}

ValueController trebleController(
    KEY_TREBLE_UP, KEY_TREBLE_DOWN,
    0, -12, 12, 1, false,
    onTrebleChange
);

// --- Channel controller ---
void onChannelChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.println("\n┌─────────────────────────────────┐");
    Serial.println("│       📺 CHANNEL                │");
    Serial.println("├─────────────────────────────────┤");
    Serial.print("│ CH: ");
    Serial.print(newValue);

    // Channel names (simulated)
    const char* channelNames[] = {
        "", "BBC One", "CNN", "ESPN", "Discovery", "HBO"
    };

    if (newValue >= 1 && newValue <= 5) {
        Serial.print(" - ");
        Serial.print(channelNames[newValue]);
    }

    Serial.println("                │");
    Serial.println("└─────────────────────────────────┘");

    // Favorite channels
    if (newValue == 1 || newValue == 3 || newValue == 5) {
        Serial.println("  ⭐ Favorite channel!");
    }
}

ValueController channelController(
    KEY_CH_UP, KEY_CH_DOWN,
    1, 1, 999, 1, true,  // with rollover
    onChannelChange
);

// --- Temperature controller (for devices with temperature control) ---
void onTempChange(int32_t /*oldValue*/, int32_t newValue) {
    Serial.println("\n┌─────────────────────────────────┐");
    Serial.println("│       🌡️  TEMPERATURE           │");
    Serial.println("├─────────────────────────────────┤");
    Serial.print("│ Temp: ");
    Serial.print(newValue);
    Serial.print("°C  ");

    VisualDisplay::printColoredMeter(newValue, 16, 30);

    Serial.println(" │");
    Serial.println("└─────────────────────────────────┘");

    // Safety warnings
    if (newValue < 18) {
        Serial.println("  ❄️  Too cold! Risk of condensation");
    } else if (newValue > 26) {
        Serial.println("  🔥 Too hot! Risk of overheating");
    } else if (newValue >= 20 && newValue <= 22) {
        Serial.println("  ✅ Optimal temperature range");
    }
}

ValueController tempController(
    KEY_TEMP_UP, KEY_TEMP_DOWN,
    22, 16, 30, 1, false,
    onTempChange
);

// ═══════════════════════════════════════════════════════════════════
// SECTION 5: Manager and integration
// ═══════════════════════════════════════════════════════════════════

ValueControllerManager<6> controllerManager;
Manager keyManager;

// Current system mode
enum SystemMode {
    MODE_NORMAL,
    MODE_MENU
};

SystemMode currentMode = MODE_NORMAL;
bool isMuted = false;
int32_t savedVolume = 0;

static const char* const MENU_ITEMS[] = {"Picture", "Sound", "Channels", "System"};
static const int8_t MENU_COUNT = 4;
int8_t menuIndex = 0;

void toggleMute() {
    isMuted = !isMuted;

    if (isMuted) {
        savedVolume = volumeController.getValue();
        volumeController.setValue(0);
        Serial.println("\n🔇 MUTED");
    } else {
        volumeController.setValue(savedVolume);
        Serial.println("\n🔊 UNMUTED");
    }
}

void showInfo() {
    Serial.println("\n╔═══════════════════════════════════════════╗");
    Serial.println("║         SYSTEM INFORMATION                ║");
    Serial.println("╠═══════════════════════════════════════════╣");

    Serial.print("║ Volume:     ");
    Serial.print(volumeController.getValue());
    Serial.print("% (");
    Serial.print(volumeController.getPercentage(), 1);
    Serial.println("%)         ║");

    Serial.print("║ Brightness: ");
    Serial.print(brightnessController.getValue());
    Serial.println("%                        ║");

    Serial.print("║ Bass:       ");
    Serial.print(bassController.getValue() > 0 ? "+" : "");
    Serial.print(bassController.getValue());
    Serial.println(" dB                         ║");

    Serial.print("║ Treble:     ");
    Serial.print(trebleController.getValue() > 0 ? "+" : "");
    Serial.print(trebleController.getValue());
    Serial.println(" dB                         ║");

    Serial.print("║ Channel:    ");
    Serial.print(channelController.getValue());
    Serial.println("                             ║");

    Serial.print("║ Temp:       ");
    Serial.print(tempController.getValue());
    Serial.println("°C                          ║");

    Serial.print("║ Muted:      ");
    Serial.println(isMuted ? "YES                          ║" : "NO                           ║");

    Serial.println("╚═══════════════════════════════════════════╝");
}

void toggleMenu() {
    if (currentMode == MODE_NORMAL) {
        currentMode = MODE_MENU;
        menuIndex = 0;
        Serial.println("\n📋 MENU MODE - use U/D to navigate, N to exit");
        Serial.print("   > ");
        Serial.println(MENU_ITEMS[menuIndex]);
    } else {
        currentMode = MODE_NORMAL;
        Serial.println("\n✅ Normal mode");
    }
}

void saveCurrentSettings() {
    Serial.println("\n💾 Settings saved to Preset 3");
    presetManager.savePreset(2,
        volumeController.getValue(),
        bassController.getValue(),
        trebleController.getValue(),
        brightnessController.getValue()
    );
}

void loadPreset(uint8_t index) {
    AudioPreset preset = presetManager.getPreset(index);

    Serial.println("\n┌─────────────────────────────────┐");
    Serial.print("│ Loading Preset ");
    Serial.print(index + 1);
    Serial.print(": ");
    Serial.print(preset.name);
    Serial.println("       │");
    Serial.println("└─────────────────────────────────┘");

    volumeController.setValue(preset.volume);
    bassController.setValue(preset.bass);
    trebleController.setValue(preset.treble);
    brightnessController.setValue(preset.brightness);

    Serial.println("✅ Preset loaded successfully!");
    showInfo();
}

// Auxiliary keys react to Click
void handleSpecialKeys(uint8_t keyCode) {
    switch (keyCode) {
        case KEY_MUTE:
            toggleMute();
            break;

        case KEY_INFO:
            showInfo();
            break;

        case KEY_MENU:
            toggleMenu();
            break;

        case KEY_SAVE:
            saveCurrentSettings();
            break;

        case KEY_PRESET_1:
        case KEY_PRESET_2:
        case KEY_PRESET_3:
            loadPreset(keyCode - KEY_PRESET_1);
            break;

        default:
            break;
    }
}

// Main callback
void onKeyEvent(uint8_t keyCode, Event event) {
    // In menu mode U/D navigate the menu instead of changing the brightness
    if (currentMode == MODE_MENU && (keyCode == KEY_BRIGHT_UP || keyCode == KEY_BRIGHT_DOWN)) {
        if (event == Event::Pressed) {
            menuIndex = (menuIndex + (keyCode == KEY_BRIGHT_UP ? MENU_COUNT - 1 : 1)) % MENU_COUNT;
            Serial.print("   > ");
            Serial.println(MENU_ITEMS[menuIndex]);
        }
        return;
    }

    // Forward to the controllers (the time is passed explicitly so
    // hold-repeat pacing uses the same clock as the manager)
    controllerManager.handleKeyEvent(keyCode, event, g_now);

    // Auxiliary keys
    if (event == Event::Click) {
        handleSpecialKeys(keyCode);
    }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 6: Input and demo helpers
// ═══════════════════════════════════════════════════════════════════

// Typed characters act as keys (ASCII code = key code)
SerialScanner scanner;

// Keep `key` pressed (NO_KEY = nothing pressed) for `ms` milliseconds.
// A single update() call is not enough: debouncing needs a stable level.
static void feed(uint8_t key, unsigned long ms) {
    const unsigned long end = millis() + ms;
    while ((long)(end - millis()) > 0) {
        g_now = millis();
        keyManager.update(key, g_now);
        delay(10);
    }
}

// Press for holdMs, then release and wait gapMs.
static void tap(uint8_t key, unsigned long holdMs, unsigned long gapMs) {
    feed(key, holdMs);
    feed(NO_KEY, gapMs);
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 7: Setup and main loop
// ═══════════════════════════════════════════════════════════════════

void runAdvancedDemo();

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Header
    Serial.println("\n");
    Serial.println("╔═══════════════════════════════════════════════════════════╗");
    Serial.println("║                                                           ║");
    Serial.println("║     🎬 ADVANCED AUDIO/VIDEO CONTROL SYSTEM 🎬            ║");
    Serial.println("║                                                           ║");
    Serial.println("║           ValueController - Advanced Demo                 ║");
    Serial.println("║                                                           ║");
    Serial.println("╚═══════════════════════════════════════════════════════════╝");

    keyManager.setEventCallback(onKeyEvent);

    // Manager configuration:
    //  - hold repeat is required for the controllers' auto-repeat
    //  - triple click is enabled for the extra-jump feature below
    // Note: with double/triple click enabled a Click (used by Mute, Info,
    // Menu, Save and Presets) is reported after the click window expires.
    Config config = keyManager.getConfig();
    config.enableHoldRepeat = true;
    config.holdRepeatInterval = 50;
    config.enableTripleClick = true;
    keyManager.setConfig(config);

    // Hold settings per controller (minimum ms between steps, base acceleration)
    volumeController.setHoldSettings(50, 1);
    brightnessController.setHoldSettings(50, 2);
    bassController.setHoldSettings(100, 1);
    trebleController.setHoldSettings(100, 1);
    channelController.setHoldSettings(30, 1);
    tempController.setHoldSettings(200, 1);

    // Optional extra jump: double click adds 2 steps, triple click adds 5
    volumeController.setMultiClickJump(2, 5);

    // Add to the manager
    controllerManager.addController(&volumeController);
    controllerManager.addController(&brightnessController);
    controllerManager.addController(&bassController);
    controllerManager.addController(&trebleController);
    controllerManager.addController(&channelController);
    controllerManager.addController(&tempController);

    // Show the keys
    Serial.println("\n┌─────────────────────────────────────────────────────────┐");
    Serial.println("│                    CONTROL KEYS                         │");
    Serial.println("├─────────────────────────────────────────────────────────┤");
    Serial.println("│  Volume:      '+' / '-'                                 │");
    Serial.println("│  Brightness:  'U' / 'D'                                 │");
    Serial.println("│  Bass:        'B' / 'V'                                 │");
    Serial.println("│  Treble:      'T' / 'G'                                 │");
    Serial.println("│  Channel:     'C' / 'X'                                 │");
    Serial.println("│  Temperature: 'R' / 'L'                                 │");
    Serial.println("├─────────────────────────────────────────────────────────┤");
    Serial.println("│  Mute:        'M'                                       │");
    Serial.println("│  Info:        'I'                                       │");
    Serial.println("│  Menu:        'N'                                       │");
    Serial.println("│  Save:        'S'                                       │");
    Serial.println("│  Presets:     '1', '2', '3'                             │");
    Serial.println("└─────────────────────────────────────────────────────────┘");

    Serial.println("\n💡 TIP: Hold keys for rapid changes!");
    Serial.println("💡 TIP: Volume: double click adds 2 extra steps, triple click adds 5");

    // Automated demo
    Serial.println("\n\n🎬 Starting automated demo...\n");
    delay(1000);
    runAdvancedDemo();
}

void loop() {
    g_now = millis();

    uint8_t keyCode = scanner.scan();
    if (keyCode >= 'a' && keyCode <= 'z') keyCode -= 32;    // case-insensitive
    keyManager.update(keyCode, g_now);

    delay(10);
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 8: Automated demo
// ═══════════════════════════════════════════════════════════════════

void runAdvancedDemo() {
    // Demo 1: volume control
    Serial.println("═══════════════════════════════════════════");
    Serial.println("DEMO 1: Volume Control");
    Serial.println("═══════════════════════════════════════════\n");

    for (int i = 0; i < 3; i++) {
        tap(KEY_VOL_UP, 100, 450);      // three separate clicks
    }
    Serial.println("-- double click: +1 step per press, +2 extra steps --");
    tap(KEY_VOL_UP, 80, 100);
    tap(KEY_VOL_UP, 80, 600);

    // Demo 2: brightness with hold
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("DEMO 2: Brightness (hold)");
    Serial.println("═══════════════════════════════════════════\n");

    feed(KEY_BRIGHT_UP, 900);
    feed(NO_KEY, 500);

    // Demo 3: equalizer
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("DEMO 3: Equalizer Adjustment");
    Serial.println("═══════════════════════════════════════════\n");

    // Bass up
    for (int i = 0; i < 3; i++) {
        tap(KEY_BASS_UP, 100, 450);
    }

    // Treble up
    for (int i = 0; i < 2; i++) {
        tap(KEY_TREBLE_UP, 100, 450);
    }

    // Demo 4: channel surfing
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("DEMO 4: Channel Surfing");
    Serial.println("═══════════════════════════════════════════\n");

    for (int i = 0; i < 3; i++) {
        tap(KEY_CH_UP, 100, 450);
    }

    // Demo 5: presets
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("DEMO 5: Loading Presets");
    Serial.println("═══════════════════════════════════════════\n");

    loadPreset(0);  // Movie mode
    delay(1000);

    loadPreset(1);  // Music mode
    delay(1000);

    // Demo 6: mute
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("DEMO 6: Mute Function");
    Serial.println("═══════════════════════════════════════════\n");

    tap(KEY_MUTE, 100, 450);    // mute
    delay(1000);
    tap(KEY_MUTE, 100, 450);    // unmute

    // Demo 7: menu navigation
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("DEMO 7: Menu Navigation");
    Serial.println("═══════════════════════════════════════════");

    tap(KEY_MENU, 100, 450);        // enter the menu
    tap(KEY_BRIGHT_DOWN, 100, 100);
    tap(KEY_BRIGHT_DOWN, 100, 100);
    tap(KEY_MENU, 100, 450);        // leave the menu

    // Final info
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("DEMO COMPLETE - Final System Status");
    Serial.println("═══════════════════════════════════════════\n");

    showInfo();

    Serial.println("\n\n✅ Demo finished! You can now control it manually.");
    Serial.println("Press 'I' anytime to see the system info.\n");
}

/*
 * ═══════════════════════════════════════════════════════════════════
 * Sample output (first part of the automated demo)
 * ═══════════════════════════════════════════════════════════════════
 *
 *
 *
 * ╔═══════════════════════════════════════════════════════════╗
 * ║                                                           ║
 * ║     🎬 ADVANCED AUDIO/VIDEO CONTROL SYSTEM 🎬            ║
 * ║                                                           ║
 * ║           ValueController - Advanced Demo                 ║
 * ║                                                           ║
 * ╚═══════════════════════════════════════════════════════════╝
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │                    CONTROL KEYS                         │
 * ├─────────────────────────────────────────────────────────┤
 * │  Volume:      '+' / '-'                                 │
 * │  Brightness:  'U' / 'D'                                 │
 * │  Bass:        'B' / 'V'                                 │
 * │  Treble:      'T' / 'G'                                 │
 * │  Channel:     'C' / 'X'                                 │
 * │  Temperature: 'R' / 'L'                                 │
 * ├─────────────────────────────────────────────────────────┤
 * │  Mute:        'M'                                       │
 * │  Info:        'I'                                       │
 * │  Menu:        'N'                                       │
 * │  Save:        'S'                                       │
 * │  Presets:     '1', '2', '3'                             │
 * └─────────────────────────────────────────────────────────┘
 *
 * 💡 TIP: Hold keys for rapid changes!
 * 💡 TIP: Volume: double click adds 2 extra steps, triple click adds 5
 *
 *
 * 🎬 Starting automated demo...
 *
 * ═══════════════════════════════════════════
 * DEMO 1: Volume Control
 * ═══════════════════════════════════════════
 *
 *
 * ┌─────────────────────────────────┐
 * │       🔊 VOLUME CONTROL         │
 * ├─────────────────────────────────┤
 * │ Level: 55%  [████████░░░░░░░] │
 * └─────────────────────────────────┘
 *
 * ┌─────────────────────────────────┐
 * │       🔊 VOLUME CONTROL         │
 * ├─────────────────────────────────┤
 * │ Level: 60%  [█████████░░░░░░] │
 * └─────────────────────────────────┘
 *
 * ┌─────────────────────────────────┐
 * │       🔊 VOLUME CONTROL         │
 * ├─────────────────────────────────┤
 * │ Level: 65%  [█████████░░░░░░] │
 * └─────────────────────────────────┘
 * -- double click: +1 step per press, +2 extra steps --
 *
 * ┌─────────────────────────────────┐
 * │       🔊 VOLUME CONTROL         │
 * ├─────────────────────────────────┤
 * │ Level: 70%  [██████████░░░░░] │
 * └─────────────────────────────────┘
 *
 * ┌─────────────────────────────────┐
 * │       🔊 VOLUME CONTROL         │
 * ├─────────────────────────────────┤
 * │ Level: 75%  [███████████░░░░] │
 * └─────────────────────────────────┘
 *
 * ┌─────────────────────────────────┐
 * │       🔊 VOLUME CONTROL         │
 * ├─────────────────────────────────┤
 * │ Level: 85%  [████████████░░░] │
 * └─────────────────────────────────┘
 *   ⚠️  High volume - consider reducing the bass
 * [... output truncated ...]
 */
