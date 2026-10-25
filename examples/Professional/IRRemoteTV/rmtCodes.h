// ═══════════════════════════════════════════════════════════════════
// key codes and the IR code table
// ═══════════════════════════════════════════════════════════════════

struct Button {
    uint8_t key;            // SmartKey key code
    const char* symbol;     // C++ name (printed by LEARN_MODE)
    const char* name;       // label on the remote
    uint16_t irCommand;     // IRremote "command" value  <-- PLACEHOLDER, see above
};

//
#if (1)
	// SmartKey key codes for the 25 buttons. Digits use their own value 0-9
	enum RemoteKey : uint8_t {
	    KEY_0 = 0,
	    KEY_1,
	    KEY_2,
	    KEY_3,
	    KEY_4,
	    KEY_5,
	    KEY_6,
	    KEY_7,
	    KEY_8,
	    KEY_9,
	    
	    KEY_POWER = 10,     // red button
	    KEY_MUTE,           // speaker with slash
	    KEY_TV_AV,          // TV/AV (input select)
	    KEY_PICTURE,        // "P"  - picture mode
	    KEY_NORMAL,         // "N"  - back to normal settings
	    KEY_SOUND,          // "S"  - sound mode
	    KEY_VOL_UP,         // + (volume)
	    KEY_VOL_DOWN,       // - (volume)
	    KEY_CH_UP,          // ^ (channel)
	    KEY_CH_DOWN,        // v (channel)
	    KEY_DASH,           // -/-- (two-digit channel)
	    KEY_DISPLAY,        // screen with "+" (on-screen display)
	    KEY_SLEEP,          // clock icon (sleep timer)
	    KEY_SURROUND,       // SURROUND
	    KEY_AUDIO           // I/II (audio mode / language)
	};
	
	// Only accept frames with this address (0xFFFF = accept any remote)
	// LEARN_MODE prints the value for your remote
	static const uint16_t ANY_ADDRESS = 0xFFFF;
	static const uint16_t REMOTE_ADDRESS = 0x08;
	
	// Order = layout of the remote (also the order asked in LEARN_MODE)
	static const Button BUTTONS[] = {
	    {KEY_POWER,    "KEY_POWER",    "POWER",        0x3D},
	    {KEY_MUTE,     "KEY_MUTE",     "MUTE",         0x32},
	    {KEY_TV_AV,    "KEY_TV_AV",    "TV/AV",        0x05},
	    {KEY_PICTURE,  "KEY_PICTURE",  "P (picture)",  0x06},
	    {KEY_NORMAL,   "KEY_NORMAL",   "N (normal)",   0x0C},
	    {KEY_SOUND,    "KEY_SOUND",    "S (sound)",    0x07},
	    {KEY_VOL_UP,   "KEY_VOL_UP",   "VOL +",        0x20},
	    {KEY_VOL_DOWN, "KEY_VOL_DOWN", "VOL -",        0x21},
	    {KEY_CH_UP,    "KEY_CH_UP",    "CH ^",         0x34},
	    {KEY_CH_DOWN,  "KEY_CH_DOWN",  "CH v",         0x35},
	    {KEY_1,        "KEY_1",        "1",            0x10},
	    {KEY_2,        "KEY_2",        "2",            0x11},
	    {KEY_3,        "KEY_3",        "3",            0x12},
	    {KEY_4,        "KEY_4",        "4",            0x13},
	    {KEY_5,        "KEY_5",        "5",            0x14},
	    {KEY_6,        "KEY_6",        "6",            0x15},
	    {KEY_7,        "KEY_7",        "7",            0x16},
	    {KEY_8,        "KEY_8",        "8",            0x17},
	    {KEY_9,        "KEY_9",        "9",            0x18},
	    {KEY_0,        "KEY_0",        "0",            0x19},
	    {KEY_DASH,     "KEY_DASH",     "-/--",         0x3B},
	    {KEY_DISPLAY,  "KEY_DISPLAY",  "DISPLAY",      0x39},
	    {KEY_SLEEP,    "KEY_SLEEP",    "SLEEP",        0x0F},
	    {KEY_SURROUND, "KEY_SURROUND", "SURROUND",     0x31},
	    {KEY_AUDIO,    "KEY_AUDIO",    "I/II",         0x33}
	};

#else
	// SmartKey key codes for the 17 buttons. Digits use their own value 0-9
	enum RemoteKey : uint8_t {
	    KEY_0 = 0,
	    KEY_1,
	    KEY_2,
	    KEY_3,
	    KEY_4,
	    KEY_5,
	    KEY_6,
	    KEY_7,
	    KEY_8,
	    KEY_9,
	    
	    KEY_STAR = 10,     	// *
	    KEY_HASH,           // #
	    KEY_UP,          		// ^ (up)
	    KEY_DOWN,        		// v (down)
	    KEY_LEFT,         	// < (left)
	    KEY_RIGHT,          // > (right)
	    KEY_OK         			// OK
	};
	
	// Only accept frames with this address (0xFFFF = accept any remote)
	// LEARN_MODE prints the value for your remote
	static const uint16_t ANY_ADDRESS = 0xFFFF;
	static const uint16_t REMOTE_ADDRESS = 0x00;
	
	// Order = layout of the remote (also the order asked in LEARN_MODE)
	static const Button BUTTONS[] = {
	    {KEY_1,        "KEY_1",        "1",            0x45},
	    {KEY_2,        "KEY_2",        "2",            0x46},
	    {KEY_3,        "KEY_3",        "3",            0x47},
	    {KEY_4,        "KEY_4",        "4",            0x44},
	    {KEY_5,        "KEY_5",        "5",            0x40},
	    {KEY_6,        "KEY_6",        "6",            0x43},
	    {KEY_7,        "KEY_7",        "7",            0x07},
	    {KEY_8,        "KEY_8",        "8",            0x15},
	    {KEY_9,        "KEY_9",        "9",            0x09},
	    {KEY_0,        "KEY_0",        "0",            0x19},
	    {KEY_STAR,     "KEY_STAR",     "STAR",         0x16},
	    {KEY_HASH,     "KEY_HASH",     "HASH",         0x0D},
	    {KEY_UP,       "KEY_UP",       "UP",           0x18},
	    {KEY_DOWN,     "KEY_DOWN",     "DOWN",         0x52},
	    {KEY_LEFT,     "KEY_LEFT",     "LEFT",         0x08},
	    {KEY_RIGHT,    "KEY_RIGHT",    "RIGHT",        0x5A},
	    {KEY_OK,       "KEY_OK",       "OK",           0x1C}
	};
	
#endif
