/**
 * SmartKey unit tests (Unity). Run on the PC with:  pio test -e native
 * All timing is simulated: no real clock is used.
 */
#include <unity.h>
#include <SmartKey.h>

using namespace SmartKey;

// ---------- helpers ----------
static unsigned long now_;
static Event logEv_[64];
static uint8_t logKey_[64];
static int logN_;
static State logSt_[16];
static int logStN_;

static void onEv(uint8_t k, Event e) { if (logN_ < 64) { logEv_[logN_] = e; logKey_[logN_] = k; } logN_++; }
static void onSt(uint8_t, State s, unsigned long) { if (logStN_ < 16) logSt_[logStN_] = s; logStN_++; }

void setUp(void) { now_ = 1000; logN_ = 0; logStN_ = 0; }
void tearDown(void) {}

static void run(Manager& m, uint8_t raw, unsigned long ms) {
    for (unsigned long t = 0; t < ms; t += 10) { now_ += 10; m.update(raw, now_); }
}
template <uint8_t N>
static void runMulti(MultiManager<N>& m, const uint8_t* keys, uint8_t n, unsigned long ms) {
    for (unsigned long t = 0; t < ms; t += 10) { now_ += 10; m.update(keys, n, now_); }
}
static bool events(const Event* exp, int n) {
    if (logN_ != n) return false;
    for (int i = 0; i < n; i++) if (logEv_[i] != exp[i]) return false;
    return true;
}
#define EXPECT_EVENTS(...) do { const Event e_[] = { __VA_ARGS__ }; \
    if (!events(e_, (int)(sizeof(e_) / sizeof(e_[0])))) { \
        printf("  got %d events:", logN_); for (int i = 0; i < logN_ && i < 64; i++) printf(" %s", eventToString(logEv_[i])); printf("\n"); \
        TEST_ASSERT_TRUE(false); } } while (0)

static const uint8_t NK = NO_KEY;

// ---------- Manager ----------
void test_press_release_events() {
    Config c; c.enableDoubleClick = false;
    Manager m(onEv, c);
    run(m, 1, 200); run(m, NK, 200);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Click);
    TEST_ASSERT_EQUAL_UINT8(1, logKey_[0]);
}

void test_glitch_shorter_than_debounce_is_ignored() {
    Manager m(onEv);
    run(m, 1, 20); run(m, NK, 200);
    TEST_ASSERT_EQUAL(0, logN_);
}

void test_single_click_is_deferred_when_double_click_enabled() {
    Manager m(onEv);                       // default: double click enabled
    run(m, 1, 200); run(m, NK, 200);
    EXPECT_EVENTS(Event::Pressed, Event::Released);          // Click not yet reported
    run(m, NK, 300);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Click);
}

void test_double_click_does_not_also_emit_click() {
    Manager m(onEv);
    run(m, 1, 100); run(m, NK, 100); run(m, 1, 100); run(m, NK, 100); run(m, NK, 600);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Pressed, Event::Released, Event::DoubleClick);
}

void test_triple_click() {
    Config c; c.enableTripleClick = true;
    Manager m(onEv, c);
    for (int i = 0; i < 3; i++) { run(m, 1, 100); run(m, NK, 100); }
    run(m, NK, 600);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Pressed, Event::Released,
                  Event::Pressed, Event::Released, Event::TripleClick);
}

void test_two_slow_clicks_are_two_clicks() {
    Manager m(onEv);
    run(m, 1, 100); run(m, NK, 500); run(m, 1, 100); run(m, NK, 500);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Click, Event::Pressed, Event::Released, Event::Click);
}

void test_hold_events_and_no_click_after_hold() {
    Manager m(onEv);
    run(m, 1, 2000);
    EXPECT_EVENTS(Event::Pressed, Event::HoldMid, Event::HoldLong);
    run(m, NK, 800);
    EXPECT_EVENTS(Event::Pressed, Event::HoldMid, Event::HoldLong, Event::Released);
}

void test_hold_repeat() {
    Config c; c.enableHoldRepeat = true; c.enableDoubleClick = false;
    Manager m(onEv, c);
    run(m, 1, 800);
    EXPECT_EVENTS(Event::Pressed, Event::HoldMid, Event::HoldRepeat, Event::HoldRepeat);
}

void test_slow_loop_keeps_hold_event_order() {
    Callbacks cb; cb.onEvent = onEv;
    Manager m(cb);
    m.update(1, 1000); m.update(1, 1060);
    m.update(1, 4500);                       // one very late update
    EXPECT_EVENTS(Event::Pressed, Event::HoldMid, Event::HoldLong, Event::HoldVeryLong);
}

void test_state_callback_reports_pressed_and_idle() {
    Callbacks cb; cb.onStateChange = onSt;
    Manager m(cb);
    run(m, 1, 700); run(m, NK, 100);
    TEST_ASSERT_EQUAL(3, logStN_);
    TEST_ASSERT_EQUAL((int)State::Pressed, (int)logSt_[0]);
    TEST_ASSERT_EQUAL((int)State::HoldMid, (int)logSt_[1]);
    TEST_ASSERT_EQUAL((int)State::Idle, (int)logSt_[2]);
}

void test_release_duration_uses_passed_time() {
    static unsigned long dur; dur = 0;
    Callbacks cb; cb.onRelease = [](uint8_t, unsigned long d) { dur = d; };
    Manager m(cb);
    run(m, 1, 300); run(m, NK, 100);
    TEST_ASSERT_TRUE(dur >= 250 && dur <= 350);
}

void test_changing_key_releases_old_and_presses_new() {
    Config c; c.enableDoubleClick = false;
    Manager m(onEv, c);
    run(m, 1, 200); run(m, 2, 200); run(m, NK, 200);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Click, Event::Pressed, Event::Released, Event::Click);
    TEST_ASSERT_EQUAL_UINT8(1, logKey_[0]);
    TEST_ASSERT_EQUAL_UINT8(2, logKey_[3]);
}

void test_time_counter_wraparound() {
    // start a little before the counter overflows: all timing uses unsigned subtraction
    Config c; c.enableTripleClick = true;
    Manager m(onEv, c);
    now_ = ~0UL - 120;
    run(m, 1, 100); run(m, NK, 100); run(m, 1, 100); run(m, NK, 100); run(m, NK, 600);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Pressed, Event::Released, Event::DoubleClick);
    TEST_ASSERT_TRUE(now_ < 2000);                           // the counter really wrapped
}

void test_config_validation() {
    Manager m;
    Config bad; bad.midPressTime = 2000;     // > longPressTime
    TEST_ASSERT_EQUAL((int)ErrorCode::InvalidParameter, (int)m.setConfig(bad));
    TEST_ASSERT_EQUAL((int)ErrorCode::InvalidParameter, (int)m.getLastError());
    TEST_ASSERT_EQUAL(500, m.getConfig().midPressTime);      // unchanged
    Config ok; ok.debounceTime = 20;
    TEST_ASSERT_EQUAL((int)ErrorCode::OK, (int)m.setConfig(ok));
    TEST_ASSERT_EQUAL(20, m.getConfig().debounceTime);
}

void test_user_context_callback() {
    static int hits; hits = 0;
    int dummy = 0;
    Manager m;
    m.setEventCallbackEx([](void* u, uint8_t, Event e) { if (e == Event::Pressed) (*static_cast<int*>(u))++; hits++; }, &dummy);
    run(m, 1, 200);
    TEST_ASSERT_EQUAL(1, dummy);
    TEST_ASSERT_EQUAL(1, hits);
}

// ---------- MultiManager + ComboDetector ----------
static ComboDetector* combo_; static int comboHits_;
static void comboFeed(uint8_t k, Event e) { onEv(k, e); combo_->handleKeyEvent(k, e); }
static void onCombo(uint8_t*, uint8_t) { comboHits_++; }

void test_multimanager_two_keys_and_combo() {
    ComboDetector cd; combo_ = &cd; comboHits_ = 0;
    cd.registerCombo2(1, 2, onCombo);
    MultiManager<4> mm(comboFeed);
    const uint8_t k1[] = {1};
    const uint8_t k12[] = {1, 2};
    runMulti(mm, k1, 1, 200);                // key 1 first
    runMulti(mm, k12, 2, 200);               // then key 2 joins
    TEST_ASSERT_EQUAL(2, mm.getActiveKeyCount());
    TEST_ASSERT_EQUAL(1, comboHits_);
    runMulti(mm, k1, 1, 200);                // key 2 released
    TEST_ASSERT_EQUAL(1, mm.getActiveKeyCount());
    TEST_ASSERT_TRUE(mm.isKeyPressed(1));
    TEST_ASSERT_FALSE(mm.isKeyPressed(2));
    runMulti(mm, k12, 2, 200);               // pressed again -> combo again
    TEST_ASSERT_EQUAL(2, comboHits_);
}

void test_multimanager_slot_overflow_reports_error() {
    MultiManager<2> mm(onEv);
    const uint8_t keys[] = {1, 2, 3};
    runMulti(mm, keys, 3, 200);
    TEST_ASSERT_EQUAL((int)ErrorCode::BufferFull, (int)mm.getLastError());
    TEST_ASSERT_EQUAL(2, mm.getActiveKeyCount());
}

void test_multimanager_clicks_per_key() {
    Config c; c.enableDoubleClick = false;
    MultiManager<4> mm(onEv, c);
    const uint8_t k1[] = {1}, k2[] = {2};
    runMulti(mm, k1, 1, 200); runMulti(mm, nullptr, 0, 200);
    runMulti(mm, k2, 1, 200); runMulti(mm, nullptr, 0, 200);
    EXPECT_EVENTS(Event::Pressed, Event::Released, Event::Click, Event::Pressed, Event::Released, Event::Click);
}

// ---------- ValueController ----------
static int vcCalls_;
static void onVal(int32_t, int32_t) { vcCalls_++; }

void test_value_inc_dec_and_limits() {
    vcCalls_ = 0;
    ValueController v(1, 2, 50, 0, 100, 10, false, onVal);
    v.handleKeyEvent(1, Event::Pressed, 0);  TEST_ASSERT_EQUAL(60, v.getValue());
    v.handleKeyEvent(2, Event::Pressed, 0);  v.handleKeyEvent(2, Event::Pressed, 0);  TEST_ASSERT_EQUAL(40, v.getValue());
    v.setValue(95); v.handleKeyEvent(1, Event::Pressed, 0);  TEST_ASSERT_EQUAL(100, v.getValue());
    TEST_ASSERT_TRUE(v.isAtMax());
    int calls = vcCalls_;
    v.handleKeyEvent(1, Event::Pressed, 0);  TEST_ASSERT_EQUAL(calls, vcCalls_);   // no change -> no callback
}

void test_value_rollover() {
    ValueController v(1, 2, 0, 0, 9, 1, true);
    v.changeValue(-1); TEST_ASSERT_EQUAL(9, v.getValue());
    v.changeValue(1);  TEST_ASSERT_EQUAL(0, v.getValue());
    v.changeValue(25); TEST_ASSERT_EQUAL(5, v.getValue());
    v.changeValue(-12); TEST_ASSERT_EQUAL(3, v.getValue());
}

void test_value_percentage_and_swapped_range() {
    ValueController v(1, 2, 50, 100, 0);     // min/max swapped on purpose
    TEST_ASSERT_EQUAL(0, v.getMin()); TEST_ASSERT_EQUAL(100, v.getMax());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 50.0, v.getPercentage());
    v.setPercentage(25.0f); TEST_ASSERT_EQUAL(25, v.getValue());
    v.setPercentage(500.0f); TEST_ASSERT_EQUAL(100, v.getValue());
}

void test_value_hold_acceleration_levels_and_reset() {
    ValueController v(1, 2, 0, 0, 10000, 1);
    v.setHoldSettings(0, 1);
    v.handleKeyEvent(1, Event::HoldMid, 0);
    v.handleKeyEvent(1, Event::HoldRepeat, 100);   TEST_ASSERT_EQUAL(1, v.getValue());
    v.handleKeyEvent(1, Event::HoldLong, 100);
    v.handleKeyEvent(1, Event::HoldRepeat, 200);   TEST_ASSERT_EQUAL(4, v.getValue());
    v.handleKeyEvent(1, Event::HoldVeryLong, 200);
    v.handleKeyEvent(1, Event::HoldRepeat, 300);   TEST_ASSERT_EQUAL(9, v.getValue());
    v.handleKeyEvent(1, Event::Released, 300);
    // regression: v2.0 kept the x5 acceleration for the NEXT hold
    v.setValue(0);
    v.handleKeyEvent(1, Event::HoldMid, 10000);
    v.handleKeyEvent(1, Event::HoldRepeat, 10100); TEST_ASSERT_EQUAL(1, v.getValue());
}

void test_value_hold_interval_rate_limits() {
    ValueController v(1, 2, 0, 0, 100, 1);
    v.setHoldSettings(250, 1);
    v.handleKeyEvent(1, Event::HoldMid, 0);
    v.handleKeyEvent(1, Event::HoldRepeat, 100);   TEST_ASSERT_EQUAL(0, v.getValue());
    v.handleKeyEvent(1, Event::HoldRepeat, 250);   TEST_ASSERT_EQUAL(1, v.getValue());
}

void test_value_double_click_jump_is_opt_in() {
    ValueController v(1, 2, 0, 0, 100, 1);
    v.handleKeyEvent(1, Event::Pressed, 0); v.handleKeyEvent(1, Event::Pressed, 0);
    v.handleKeyEvent(1, Event::DoubleClick, 0);
    TEST_ASSERT_EQUAL(2, v.getValue());
    v.setMultiClickJump(3);
    v.handleKeyEvent(1, Event::DoubleClick, 0);
    TEST_ASSERT_EQUAL(5, v.getValue());
}

void test_value_manager_template_alias() {
    ValueController a(1, 2, 0), b(3, 4, 0);
    KeyValueControllerManager<4> mgr;           // v2.0 examples use this form
    TEST_ASSERT_TRUE(mgr.addController(&a));
    TEST_ASSERT_TRUE(mgr.addController(&b));
    mgr.handleKeyEvent(3, Event::Pressed, 0);
    TEST_ASSERT_EQUAL(0, a.getValue());
    TEST_ASSERT_EQUAL(1, b.getValue());
}

// ---------- Sequence / Toggle / Macro / RateLimiter ----------
static int seqHits_;
static void onSeq(uint8_t) { seqHits_++; }

void test_sequence_basic_and_restart() {
    seqHits_ = 0;
    SequenceDetector sd; const uint8_t k[] = {1, 2, 3};
    sd.registerSequence(7, k, 3, 200, onSeq);
    sd.handleKeyEvent(1, Event::Pressed, 0); sd.handleKeyEvent(2, Event::Pressed, 100); sd.handleKeyEvent(3, Event::Pressed, 200);
    TEST_ASSERT_EQUAL(1, seqHits_);
    sd.handleKeyEvent(1, Event::Pressed, 1000); sd.handleKeyEvent(1, Event::Pressed, 1050);   // wrong key restarts
    sd.handleKeyEvent(2, Event::Pressed, 1100); sd.handleKeyEvent(3, Event::Pressed, 1150);
    TEST_ASSERT_EQUAL(2, seqHits_);
}

void test_sequence_respects_its_own_max_interval() {
    seqHits_ = 0;
    SequenceDetector sd; const uint8_t k[] = {1, 2};
    sd.registerSequence(7, k, 2, 200, onSeq);
    sd.handleKeyEvent(1, Event::Pressed, 0); sd.handleKeyEvent(2, Event::Pressed, 800);       // regression: was hardcoded 1000
    TEST_ASSERT_EQUAL(0, seqHits_);
}

static bool togState_;
static void onTog(uint8_t, bool s) { togState_ = s; }

void test_toggle_only_on_click() {
    Toggle t; t.registerToggle(5, false, onTog);
    t.handleKeyEvent(5, Event::Pressed);   TEST_ASSERT_FALSE(t.getState(5));
    t.handleKeyEvent(5, Event::Click);     TEST_ASSERT_TRUE(t.getState(5)); TEST_ASSERT_TRUE(togState_);
    t.handleKeyEvent(5, Event::Click);     TEST_ASSERT_FALSE(t.getState(5));
}

static uint8_t played_[8]; static int playedN_;
static void onPlay(uint8_t k) { if (playedN_ < 8) played_[playedN_] = k; playedN_++; }

void test_macro_record_and_timed_playback() {
    playedN_ = 0;
    Macro m;
    m.startRecording(1000);
    m.handleKeyEvent(5, Event::Pressed, 1000);
    m.handleKeyEvent(6, Event::Pressed, 1250);
    m.handleKeyEvent(7, Event::Pressed, 1500);
    m.handleKeyEvent(8, Event::Released, 1600);              // ignored
    m.stopRecording();
    TEST_ASSERT_EQUAL(3, m.getStepCount());
    m.play(onPlay, 5000);
    m.update(5000);  TEST_ASSERT_EQUAL(1, playedN_);
    m.update(5249);  TEST_ASSERT_EQUAL(1, playedN_);
    m.update(5250);  TEST_ASSERT_EQUAL(2, playedN_);
    m.update(9000);  TEST_ASSERT_EQUAL(3, playedN_);         // late update plays everything that is due
    TEST_ASSERT_FALSE(m.isPlaying());
    TEST_ASSERT_EQUAL_UINT8(5, played_[0]); TEST_ASSERT_EQUAL_UINT8(7, played_[2]);
}

void test_macro_long_gap_does_not_overflow() {
    playedN_ = 0;
    Macro m; m.startRecording(0);
    m.handleKeyEvent(1, Event::Pressed, 0);
    m.handleKeyEvent(2, Event::Pressed, 70000);              // > 65535 ms (v2.0 truncated this)
    m.play(onPlay, 0);
    m.update(69999); TEST_ASSERT_EQUAL(1, playedN_);
    m.update(70000); TEST_ASSERT_EQUAL(2, playedN_);
}

void test_ratelimiter_window_and_clamp() {
    RateLimiter r(3, 1000);
    TEST_ASSERT_TRUE(r.checkClick(0)); TEST_ASSERT_TRUE(r.checkClick(100)); TEST_ASSERT_TRUE(r.checkClick(200));
    TEST_ASSERT_FALSE(r.checkClick(300));
    TEST_ASSERT_TRUE(r.checkClick(1250));                    // oldest click left the window
    RateLimiter big(20, 1000);                               // clamped to 8 (v2.0 never limited)
    int ok = 0; for (int i = 0; i < 12; i++) if (big.checkClick(i)) ok++;
    TEST_ASSERT_EQUAL(8, ok);
}

// ---------- IdleMonitor ----------
static int idle1Hits_, idle2Hits_, wakeHits_, order_[8], orderN_;
static IdleState wakeFrom_;
static void onIdle1Cb() { idle1Hits_++; if (orderN_ < 8) order_[orderN_++] = 1; }
static void onIdle2Cb() { idle2Hits_++; if (orderN_ < 8) order_[orderN_++] = 2; }
static void onWakeCb(IdleState from) { wakeHits_++; wakeFrom_ = from; if (orderN_ < 8) order_[orderN_++] = 9; }

static void idleSetup(IdleMonitor& m) {
    idle1Hits_ = idle2Hits_ = wakeHits_ = orderN_ = 0;
    wakeFrom_ = IdleState::Active;
    m.setIdle1Callback(onIdle1Cb);
    m.setIdle2Callback(onIdle2Cb);
    m.setWakeCallback(onWakeCb);
    m.reset(0);
}

void test_idle_two_stages_fire_once_at_their_time() {
    IdleMonitor m(1000, 3000); idleSetup(m);
    m.update(999);   TEST_ASSERT_EQUAL(0, idle1Hits_);   TEST_ASSERT_FALSE(m.isIdle1());
    m.update(1000);  TEST_ASSERT_EQUAL(1, idle1Hits_);   TEST_ASSERT_TRUE(m.isIdle1()); TEST_ASSERT_FALSE(m.isIdle2());
    m.update(2999);  TEST_ASSERT_EQUAL(0, idle2Hits_);
    m.update(3000);  TEST_ASSERT_EQUAL(1, idle2Hits_);   TEST_ASSERT_TRUE(m.isIdle2()); TEST_ASSERT_TRUE(m.isIdle1());
    m.update(9000);                                      // no repeated callbacks
    TEST_ASSERT_EQUAL(1, idle1Hits_); TEST_ASSERT_EQUAL(1, idle2Hits_);
    TEST_ASSERT_EQUAL((int)IdleState::Idle2, (int)m.getState());
}

void test_idle_late_update_fires_both_in_order() {
    IdleMonitor m(1000, 3000); idleSetup(m);
    m.update(5000);                                      // one very late loop iteration
    TEST_ASSERT_EQUAL(2, orderN_);
    TEST_ASSERT_EQUAL(1, order_[0]); TEST_ASSERT_EQUAL(2, order_[1]);
}

void test_idle_key_activity_restarts_the_timers() {
    IdleMonitor m(1000, 3000); idleSetup(m);
    TEST_ASSERT_TRUE(m.handleKeyEvent(1, Event::Pressed, 800));
    TEST_ASSERT_TRUE(m.handleKeyEvent(1, Event::Released, 900));
    m.update(1500);  TEST_ASSERT_EQUAL(0, idle1Hits_);   // would have fired at 1000 without the key
    m.update(1900);  TEST_ASSERT_EQUAL(1, idle1Hits_);   // 900 + 1000
}

void test_idle_held_key_is_activity() {
    IdleMonitor m(1000, 3000); idleSetup(m);
    m.handleKeyEvent(1, Event::Pressed, 100);
    m.update(5000);                                      // key still down, no events in between
    TEST_ASSERT_EQUAL(0, idle1Hits_);
    TEST_ASSERT_EQUAL(0, m.getIdleTime(5000));
    m.handleKeyEvent(1, Event::Released, 5000);
    m.update(5999);  TEST_ASSERT_EQUAL(0, idle1Hits_);
    m.update(6000);  TEST_ASSERT_EQUAL(1, idle1Hits_);
}

void test_idle_wake_key_is_swallowed_with_its_click() {
    IdleMonitor m(1000, 3000); idleSetup(m);
    m.update(1000);                                      // Idle1 (display off)
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Pressed, 1500));   // wakes the system, key ignored
    TEST_ASSERT_EQUAL(1, wakeHits_); TEST_ASSERT_EQUAL((int)IdleState::Idle1, (int)wakeFrom_);
    TEST_ASSERT_EQUAL((int)IdleState::Active, (int)m.getState());
    TEST_ASSERT_TRUE(m.isSwallowing());
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Released, 1600));
    TEST_ASSERT_TRUE(m.isSwallowing());                  // the Click event is still to come
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Click, 1900));
    TEST_ASSERT_FALSE(m.isSwallowing());
    TEST_ASSERT_TRUE(m.handleKeyEvent(1, Event::Pressed, 3000));    // normal key press again
    TEST_ASSERT_EQUAL(1, wakeHits_);
}

void test_idle_wake_from_second_stage_and_timers_restart() {
    IdleMonitor m(1000, 3000); idleSetup(m);
    m.update(3000);                                      // Idle2 (deep sleep requested)
    TEST_ASSERT_FALSE(m.handleKeyEvent(2, Event::Pressed, 3500));
    TEST_ASSERT_EQUAL((int)IdleState::Idle2, (int)wakeFrom_);
    m.handleKeyEvent(2, Event::Released, 3600); m.handleKeyEvent(2, Event::Click, 3600);
    idle1Hits_ = 0;
    m.update(4400);  TEST_ASSERT_EQUAL(0, idle1Hits_);
    m.update(4600);  TEST_ASSERT_EQUAL(1, idle1Hits_);   // 3600 + 1000
}

void test_idle_wake_key_hold_is_swallowed_until_release() {
    IdleMonitor m(1000, 0); idleSetup(m);
    m.update(1000);
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Pressed, 1200));
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::HoldMid, 1700));
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::HoldRepeat, 1800));
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Released, 2500));  // a hold never gives a click
    TEST_ASSERT_FALSE(m.isSwallowing());
    TEST_ASSERT_TRUE(m.handleKeyEvent(1, Event::Pressed, 2600));
}

void test_idle_other_keys_are_not_swallowed() {
    IdleMonitor m(1000, 0); idleSetup(m);
    m.update(1000);
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Pressed, 1200));   // wake key
    TEST_ASSERT_TRUE(m.handleKeyEvent(2, Event::Pressed, 1300));    // another key while the first is held
    TEST_ASSERT_TRUE(m.handleKeyEvent(2, Event::Released, 1350));
    TEST_ASSERT_TRUE(m.handleKeyEvent(2, Event::Click, 1350));
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Released, 1400));
    TEST_ASSERT_FALSE(m.handleKeyEvent(1, Event::Click, 1400));
}

void test_idle_swallow_can_be_disabled_and_times_out() {
    IdleMonitor a(1000, 0); idleSetup(a);
    a.setSwallowWakeKey(false);
    a.update(1000);
    TEST_ASSERT_TRUE(a.handleKeyEvent(1, Event::Pressed, 1200));    // wakes, but the key is processed
    TEST_ASSERT_EQUAL(1, wakeHits_);

    IdleMonitor b(1000, 0); idleSetup(b);
    b.update(1000);
    b.handleKeyEvent(1, Event::Pressed, 1200); b.handleKeyEvent(1, Event::Released, 1300);
    TEST_ASSERT_TRUE(b.isSwallowing());
    b.update(2400);                                      // no Click event ever came
    TEST_ASSERT_FALSE(b.isSwallowing());
}

void test_idle_notify_activity_and_validation() {
    IdleMonitor m(1000, 3000); idleSetup(m);
    m.update(1000);
    m.notifyActivity(1500);                              // e.g. a touch or a serial command
    TEST_ASSERT_EQUAL(1, wakeHits_);
    TEST_ASSERT_FALSE(m.isSwallowing());
    TEST_ASSERT_FALSE(m.isIdle1());
    TEST_ASSERT_EQUAL((int)ErrorCode::InvalidParameter, (int)m.setTimeouts(3000, 1000));
    TEST_ASSERT_EQUAL(1000, m.getIdle1Timeout());        // unchanged
    TEST_ASSERT_EQUAL((int)ErrorCode::OK, (int)m.setTimeouts(0, 2000));   // only the second stage
    m.reset(0); idle1Hits_ = idle2Hits_ = 0;
    m.update(2000);
    TEST_ASSERT_EQUAL(0, idle1Hits_); TEST_ASSERT_EQUAL(1, idle2Hits_);
}

static int processedEvents_;
static IdleMonitor* idleMon_;
static void idleFeed(uint8_t k, Event e) { if (idleMon_->handleKeyEvent(k, e, now_)) processedEvents_++; }

void test_idle_with_real_manager_swallows_the_whole_wake_tap() {
    IdleMonitor mon(1000, 0); idleSetup(mon); mon.reset(now_); idleMon_ = &mon; processedEvents_ = 0;
    Manager m(idleFeed);                                 // default config: Click is deferred 300 ms
    run(m, NK, 1100); mon.update(now_);                  // idle: display off
    TEST_ASSERT_TRUE(mon.isIdle1());
    run(m, 1, 100); run(m, NK, 600); mon.update(now_);   // wake tap
    TEST_ASSERT_EQUAL(0, processedEvents_);              // Pressed, Released and Click all ignored
    TEST_ASSERT_FALSE(mon.isIdle1());
    TEST_ASSERT_FALSE(mon.isSwallowing());
    run(m, 1, 100); run(m, NK, 600);                     // next tap is a normal one
    TEST_ASSERT_EQUAL(3, processedEvents_);              // Pressed, Released, Click
}

// ---------- Scanners (hardware independent parts) ----------
void test_analog_centered_no_underflow() {
    AnalogScanner s(0, 8, 50);
    TEST_ASSERT_TRUE(s.addKeyCentered(20, 1));               // 20-50 used to wrap to 65506
    TEST_ASSERT_TRUE(s.addKeyCentered(65530, 2));            // 65530+50 used to wrap to 44
    TEST_ASSERT_EQUAL_UINT8(1, s.classify(0));
    TEST_ASSERT_EQUAL_UINT8(1, s.classify(70));
    TEST_ASSERT_EQUAL_UINT8(NO_KEY, s.classify(71));
    TEST_ASSERT_EQUAL_UINT8(2, s.classify(65535));
}

void test_matrix_rejects_bad_size() {
    const uint8_t rows[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9}, cols[2] = {10, 11};
    MatrixScanner ok(rows, 2, cols, 2);
    MatrixScanner bad(rows, 9, cols, 2);
    TEST_ASSERT_TRUE(ok.getError() != nullptr);              // "not initialized" until begin()
    TEST_ASSERT_FALSE(bad.begin());
    TEST_ASSERT_TRUE(bad.getError() != nullptr);
}

void test_binary_code_active_low_decoding() {
    const uint8_t pins[4] = {1, 2, 3, 4};
    BinaryCodeScanner s(pins, 4, true, 0);
    TEST_ASSERT_EQUAL_UINT8(NO_KEY, s.decode(0x0F));         // all lines pulled up = idle
    TEST_ASSERT_EQUAL_UINT8(1, s.decode(0x0E));              // line 0 pulled low
    TEST_ASSERT_EQUAL_UINT8(5, s.decode(0x0A));              // lines 0 and 2 low
    TEST_ASSERT_EQUAL_UINT8(15, s.decode(0x00));             // all lines low
    TEST_ASSERT_EQUAL_UINT8(1, s.decode(0xFE));              // bits above the line count are ignored
}

void test_binary_code_active_high_and_custom_idle() {
    const uint8_t pins[4] = {1, 2, 3, 4};
    BinaryCodeScanner direct(pins, 4, false, 0);
    TEST_ASSERT_EQUAL_UINT8(NO_KEY, direct.decode(0x00));
    TEST_ASSERT_EQUAL_UINT8(9, direct.decode(0x09));
    BinaryCodeScanner idle15(pins, 4, false, 0x0F);          // 0b1111 means "no key", 0 is a real key
    TEST_ASSERT_EQUAL_UINT8(NO_KEY, idle15.decode(0x0F));
    TEST_ASSERT_EQUAL_UINT8(0, idle15.decode(0x00));
}

void test_binary_code_data_valid_and_bad_size() {
    const uint8_t pins[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    BinaryCodeScanner s(pins, 4);
    TEST_ASSERT_EQUAL_UINT8(NO_KEY, s.decode(0x0E, false));  // data-valid line inactive
    TEST_ASSERT_EQUAL_UINT8(1, s.decode(0x0E, true));
    BinaryCodeScanner tooMany(pins, 9);
    TEST_ASSERT_FALSE(tooMany.begin());
    TEST_ASSERT_TRUE(tooMany.getError() != nullptr);
    BinaryCodeScanner none(nullptr, 4);
    TEST_ASSERT_FALSE(none.begin());
    BinaryCodeScanner ok(pins, 8);
    TEST_ASSERT_TRUE(ok.begin());
}

void test_gpio_capacity_limit() {
    GPIOScanner g(2);
    TEST_ASSERT_TRUE(g.addPin(1, 10)); TEST_ASSERT_TRUE(g.addPin(2, 11));
    TEST_ASSERT_FALSE(g.addPin(3, 12));
    TEST_ASSERT_EQUAL(2, g.getPinCount());
}

// ---------- runner ----------
static void runAll() {
    UNITY_BEGIN();
    RUN_TEST(test_press_release_events);
    RUN_TEST(test_glitch_shorter_than_debounce_is_ignored);
    RUN_TEST(test_single_click_is_deferred_when_double_click_enabled);
    RUN_TEST(test_double_click_does_not_also_emit_click);
    RUN_TEST(test_triple_click);
    RUN_TEST(test_two_slow_clicks_are_two_clicks);
    RUN_TEST(test_hold_events_and_no_click_after_hold);
    RUN_TEST(test_hold_repeat);
    RUN_TEST(test_slow_loop_keeps_hold_event_order);
    RUN_TEST(test_state_callback_reports_pressed_and_idle);
    RUN_TEST(test_release_duration_uses_passed_time);
    RUN_TEST(test_changing_key_releases_old_and_presses_new);
    RUN_TEST(test_time_counter_wraparound);
    RUN_TEST(test_config_validation);
    RUN_TEST(test_user_context_callback);
    RUN_TEST(test_multimanager_two_keys_and_combo);
    RUN_TEST(test_multimanager_slot_overflow_reports_error);
    RUN_TEST(test_multimanager_clicks_per_key);
    RUN_TEST(test_value_inc_dec_and_limits);
    RUN_TEST(test_value_rollover);
    RUN_TEST(test_value_percentage_and_swapped_range);
    RUN_TEST(test_value_hold_acceleration_levels_and_reset);
    RUN_TEST(test_value_hold_interval_rate_limits);
    RUN_TEST(test_value_double_click_jump_is_opt_in);
    RUN_TEST(test_value_manager_template_alias);
    RUN_TEST(test_sequence_basic_and_restart);
    RUN_TEST(test_sequence_respects_its_own_max_interval);
    RUN_TEST(test_toggle_only_on_click);
    RUN_TEST(test_macro_record_and_timed_playback);
    RUN_TEST(test_macro_long_gap_does_not_overflow);
    RUN_TEST(test_ratelimiter_window_and_clamp);
    RUN_TEST(test_idle_two_stages_fire_once_at_their_time);
    RUN_TEST(test_idle_late_update_fires_both_in_order);
    RUN_TEST(test_idle_key_activity_restarts_the_timers);
    RUN_TEST(test_idle_held_key_is_activity);
    RUN_TEST(test_idle_wake_key_is_swallowed_with_its_click);
    RUN_TEST(test_idle_wake_from_second_stage_and_timers_restart);
    RUN_TEST(test_idle_wake_key_hold_is_swallowed_until_release);
    RUN_TEST(test_idle_other_keys_are_not_swallowed);
    RUN_TEST(test_idle_swallow_can_be_disabled_and_times_out);
    RUN_TEST(test_idle_notify_activity_and_validation);
    RUN_TEST(test_idle_with_real_manager_swallows_the_whole_wake_tap);
    RUN_TEST(test_analog_centered_no_underflow);
    RUN_TEST(test_matrix_rejects_bad_size);
    RUN_TEST(test_binary_code_active_low_decoding);
    RUN_TEST(test_binary_code_active_high_and_custom_idle);
    RUN_TEST(test_binary_code_data_valid_and_bad_size);
    RUN_TEST(test_gpio_capacity_limit);
    UNITY_END();
}

#ifdef ARDUINO
void setup() { delay(2000); runAll(); }
void loop() {}
#else
int main() { runAll(); return 0; }
#endif
