// MIT License
//
// Copyright (c) 2022-2026 The DreamPicoPort Contributors
// https://github.com/OrangeFox86/DreamcastControllerUsbPico
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Presents a USB keyboard to the Dreamcast as a Dreamcast keyboard.
//
// Core 1 runs the USB host stack, core 0 runs the Maple client. A HID boot protocol
// report received on core 1 is copied into the DreamcastKeyboard function, which core 0
// serves back whenever the console sends GET_CONDITION.

#ifndef ENABLE_UNIT_TEST

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/platform.h"

#include "configuration.h"

#include "hal/MapleBus/MapleBusInterface.hpp"
#include "hal/Usb/host_usb_interface.hpp"
#include "KeyboardHost.hpp"

#include "DreamcastMainPeripheral.hpp"
#include "DreamcastKeyboard.hpp"

#include "led.hpp"

#include <memory>
#include <stdio.h>

#if KBD_OLED
#include "Ssd1306.hpp"
#include "acidcat_bitmap.hpp"
#include "hardware/i2c.h"
#include <cstring>

// I2C1 on the xiao's D0 and D1 pads. D4/D5 are the maple bus and D6 is the debug uart, so
// this is the only free adjacent pair left. Same pins usb4maple uses for its display.
#define OLED_SDA_PIN 26
#define OLED_SCL_PIN 27

// Wake button to ground on D2. Internal pull up, so no external resistor. Set to -1 to
// build without a button, in which case the panel only wakes on state changes.
#ifndef OLED_BUTTON_PIN
#define OLED_BUTTON_PIN 28
#endif

// How long the panel stays lit after a wake. Typing resets this, so the typing page
// stays up while you are actually using it.
#define OLED_AWAKE_US 15000000
// Window the words per minute figure is measured over
#define OLED_WPM_WINDOW_US 10000000
// Redraw interval
#define OLED_REDRAW_US 500000
// How long the title page shows at boot before dropping to typing
#define OLED_SPLASH_US 3000000
// Ignore further edges for this long after one is accepted
#define OLED_DEBOUNCE_US 200000

static Ssd1306 oled(i2c1, OLED_SDA_PIN, OLED_SCL_PIN);
#endif

#if KBD_DEBUG || KBD_OLED || KBD_LED_WRITEBACK
// Shared with core 1 for the status line, the display and the lock LED writeback. All printing
// happens on core 1: a blocking uart write on core 0 would wreck maple timing.
static client::DreamcastKeyboard* pDebugKeyboard = nullptr;
static client::DreamcastMainPeripheral* pDebugPeripheral = nullptr;
#endif

#if KBD_OLED
//! Which page the button cycles to
enum class OledPage : uint8_t
{
    kTyping = 0,   //!< why this project exists
    kDebug,        //!< what you switch to when bringing up a new device
    kTitle,        //!< who made it
    kNumPages
};

//! Words per minute over a rolling window.
//!
//! Samples the keystroke counter rather than counting here, because this runs at 2 Hz and
//! fast typing would slip between frames. hid_app counts the key down edges; this reads the
//! total and differences it across the window.
class TypingStats
{
public:
    inline void sample(uint64_t nowUs, uint32_t strokes)
    {
        mSlots[mHead] = strokes;
        mTimes[mHead] = nowUs;
        mHead = (mHead + 1) % kNumSlots;
        if (mFilled < kNumSlots)
        {
            ++mFilled;
        }

        // Oldest sample still inside the window
        uint32_t oldStrokes = strokes;
        uint64_t oldUs = nowUs;
        for (uint8_t i = 0; i < mFilled; ++i)
        {
            const uint8_t idx = (mHead + kNumSlots - 1 - i) % kNumSlots;
            if ((nowUs - mTimes[idx]) <= OLED_WPM_WINDOW_US)
            {
                oldStrokes = mSlots[idx];
                oldUs = mTimes[idx];
            }
        }

        const uint64_t spanUs = (nowUs > oldUs) ? (nowUs - oldUs) : 0;
        if (spanUs < 1000000)
        {
            // Too short a span to divide by without the number jumping around
            mCpm = 0;
        }
        else
        {
            mCpm = static_cast<uint32_t>((strokes - oldStrokes) * 60000000ULL / spanUs);
        }

        // Five characters to a word is the standard definition
        mWpm = mCpm / 5;
        if (mWpm > mPeakWpm)
        {
            mPeakWpm = mWpm;
        }
    }

    inline uint32_t wpm() const { return mWpm; }
    inline uint32_t cpm() const { return mCpm; }
    inline uint32_t peakWpm() const { return mPeakWpm; }

private:
    //! 500 ms per slot, so this covers 15 s, comfortably more than the window
    static const uint8_t kNumSlots = 30;
    uint32_t mSlots[kNumSlots] = {};
    uint64_t mTimes[kNumSlots] = {};
    uint8_t mHead = 0;
    uint8_t mFilled = 0;
    uint32_t mWpm = 0;
    uint32_t mCpm = 0;
    uint32_t mPeakWpm = 0;
};

static TypingStats typingStats;
// Boot on the title page as a splash, then fall through to typing
static OledPage oledPage = OledPage::kTitle;

//! Redraws the current page. Runs on core 1: a full frame is ~25 ms of blocking i2c, which
//! would blow through the console's ~16 ms poll window if it ran on the maple core. Only
//! changed pages are written, so a steady state refresh costs a fraction of that.
static void oled_task(uint64_t nowUs)
{
    static uint64_t nextUs = 0;
    static uint32_t lastCond = 0;
    static uint64_t lastRateUs = 0;
    static uint64_t sleepAtUs = OLED_AWAKE_US;
    static uint64_t lastPressUs = 0;
    static bool lastMounted = false;
    static bool lastPolling = false;
    static uint32_t lastStrokes = 0;
    static OledPage lastDrawnPage = OledPage::kNumPages;
    static bool splashDone = false;

    const uint32_t strokes = get_keystroke_count();

    // Splash: leave the title up for a few seconds at boot, unless the button already
    // moved off it. After that the button owns the page.
    if (!splashDone && nowUs >= OLED_SPLASH_US)
    {
        splashDone = true;
        if (oledPage == OledPage::kTitle)
        {
            oledPage = OledPage::kTyping;
        }
    }

    // Typing keeps the panel awake, otherwise it blanks mid game
    if (strokes != lastStrokes)
    {
        lastStrokes = strokes;
        sleepAtUs = nowUs + OLED_AWAKE_US;
        oled.setPower(true);
    }

    // Button: wakes when asleep, cycles pages when already awake. A gpio read costs
    // nothing, so this runs every pass rather than on the redraw tick.
    if (OLED_BUTTON_PIN >= 0
        && !gpio_get(OLED_BUTTON_PIN)
        && (nowUs - lastPressUs) > OLED_DEBOUNCE_US)
    {
        lastPressUs = nowUs;
        splashDone = true;
        if (oled.isOn())
        {
            oledPage = static_cast<OledPage>(
                (static_cast<uint8_t>(oledPage) + 1) % static_cast<uint8_t>(OledPage::kNumPages));
        }
        sleepAtUs = nowUs + OLED_AWAKE_US;
        oled.setPower(true);
    }

    // Wake on anything worth noticing, so the interesting moment is never missed
    const bool mounted = is_keyboard_mounted();
    const uint32_t condNow =
        (pDebugKeyboard != nullptr) ? pDebugKeyboard->getGetConditionCount() : 0;
    const bool polling = (condNow != lastCond);
    if (mounted != lastMounted || polling != lastPolling)
    {
        lastMounted = mounted;
        lastPolling = polling;
        sleepAtUs = nowUs + OLED_AWAKE_US;
        oled.setPower(true);
    }

    if (nowUs < nextUs)
    {
        return;
    }
    nextUs = nowUs + OLED_REDRAW_US;

    typingStats.sample(nowUs, strokes);

    if (nowUs >= sleepAtUs)
    {
        // Blank the panel and stop all i2c traffic until something wakes it again
        oled.setPower(false);
        lastCond = condNow;
        return;
    }

    // Poll rate, recomputed here because the debug page reports it
    uint32_t rate = 0;
    if (lastRateUs != 0 && nowUs > lastRateUs)
    {
        rate = static_cast<uint32_t>((condNow - lastCond) * 1000000ULL / (nowUs - lastRateUs));
    }
    lastCond = condNow;
    lastRateUs = nowUs;

    // Switching pages leaves stale rows behind, so wipe on change
    if (oledPage != lastDrawnPage)
    {
        oled.clear();
        lastDrawnPage = oledPage;
    }

    char line[Ssd1306::kNumCols + 1];
    const int16_t player = (pDebugPeripheral != nullptr) ? pDebugPeripheral->getPlayerIndex() : -1;

    if (oledPage == OledPage::kTitle)
    {
        // Mark on the left, text in the ten columns that remain on the right
        oled.bitmap(0, 0, &acidcat_bitmap[0][0], acidcat_width, acidcat_pages);
        oled.text(11, 2, "acidcat");
        oled.text(11, 4, "dc kbd");
        snprintf(line, sizeof(line), "%lus", static_cast<unsigned long>(nowUs / 1000000));
        oled.text(11, 5, line);
    }
    else if (oledPage == OledPage::kTyping)
    {
        snprintf(line, sizeof(line), "WPM %lu", static_cast<unsigned long>(typingStats.wpm()));
        oled.textLine(0, line);
        snprintf(line, sizeof(line), "CPM %lu", static_cast<unsigned long>(typingStats.cpm()));
        oled.textLine(2, line);
        snprintf(line, sizeof(line), "peak %lu wpm",
                 static_cast<unsigned long>(typingStats.peakWpm()));
        oled.textLine(4, line);
        snprintf(line, sizeof(line), "keys %lu", static_cast<unsigned long>(strokes));
        oled.textLine(6, line);
    }
    else
    {
        uint16_t vid = 0;
        uint16_t pid = 0;
        get_keyboard_vid_pid(vid, pid);
        if (is_keyboard_mounted())
        {
            snprintf(line, sizeof(line), "USB %04x:%04x", vid, pid);
        }
        else
        {
            snprintf(line, sizeof(line), "USB no keyboard");
        }
        oled.textLine(0, line);

        const KeyboardHost::Keys keys = get_last_keys();
        snprintf(line, sizeof(line), "KEY %02x %02x%02x%02x%02x%02x%02x",
                 keys.modifiers, keys.keys[0], keys.keys[1], keys.keys[2],
                 keys.keys[3], keys.keys[4], keys.keys[5]);
        oled.textLine(2, line);

        if (condNow == 0)
        {
            snprintf(line, sizeof(line), "BUS idle");
        }
        else
        {
            snprintf(line, sizeof(line), "BUS %lu/s", static_cast<unsigned long>(rate));
        }
        oled.textLine(4, line);

        snprintf(line, sizeof(line), "P%d  up %lus", player,
                 static_cast<unsigned long>(nowUs / 1000000));
        oled.textLine(6, line);
    }

    oled.flushDirty();
}
#endif

#if KBD_LED_WRITEBACK
//! Pushes the console's lock LED state out to the USB keyboard.
//!
//! The console owns this state. Pressing caps lock only sends a usage code; the host
//! decides whether the light comes on and tells the keyboard via an output report. Runs on
//! core 1 because it is a blocking usb transfer, and only fires on change so a steady state
//! costs one comparison.
static void led_writeback_task()
{
    static uint8_t lastLeds = 0xFF;

    if (pDebugKeyboard == nullptr || !is_keyboard_mounted())
    {
        // Force a resend once a keyboard reappears
        lastLeds = 0xFF;
        return;
    }

    const client::DreamcastKeyboard::LedState& s = pDebugKeyboard->getLedState();
    // Num, caps and scroll happen to share bit positions between the two protocols. Kana
    // does not: the Dreamcast uses 0x20 where HID uses 0x10.
    const uint8_t leds =
        (s.numLockLedOn ? 0x01 : 0) |
        (s.capsLockLedOn ? 0x02 : 0) |
        (s.scrollLockLedOn ? 0x04 : 0) |
        (s.kanaLedOn ? 0x10 : 0);

    if (leds != lastLeds)
    {
        if (set_keyboard_leds(leds))
        {
            lastLeds = leds;
        }
    }
}
#endif

// Second Core Process: USB host
void core1()
{
    set_sys_clock_khz(CPU_FREQ_KHZ, true);

    usb_init();

#if KBD_OLED
    if (OLED_BUTTON_PIN >= 0)
    {
        gpio_init(OLED_BUTTON_PIN);
        gpio_set_dir(OLED_BUTTON_PIN, false);
        gpio_pull_up(OLED_BUTTON_PIN);
    }
    // Bring the panel up from this core so all i2c traffic stays off the maple core
    if (!oled.init())
    {
        printf("[oled] no panel found on i2c1 (sda gp%d scl gp%d)\r\n",
               OLED_SDA_PIN, OLED_SCL_PIN);
    }
#endif

#if KBD_DEBUG
    uint64_t nextStatusUs = 0;
#endif

    while (true)
    {
        usb_task(time_us_64());

#if KBD_OLED
        oled_task(time_us_64());
#endif

#if KBD_LED_WRITEBACK
        led_writeback_task();
#endif

#if KBD_DEBUG
        uint64_t now = time_us_64();
        if (now >= nextStatusUs)
        {
            nextStatusUs = now + 1000000;
            printf("[status] t=%llus usb_kbd=%s maple: player=%d get_cond=%lu set_cond=%lu\r\n",
                   (unsigned long long)(now / 1000000),
                   is_keyboard_mounted() ? "yes" : "no",
                   (pDebugPeripheral != nullptr) ? pDebugPeripheral->getPlayerIndex() : -1,
                   (unsigned long)((pDebugKeyboard != nullptr) ? pDebugKeyboard->getGetConditionCount() : 0),
                   (unsigned long)((pDebugKeyboard != nullptr) ? pDebugKeyboard->getSetConditionCount() : 0));
        }
#endif
    }
}

// First Core Process: Maple client
void core0()
{
    set_sys_clock_khz(CPU_FREQ_KHZ, true);

    std::shared_ptr<MapleBusInterface> bus =
        create_maple_bus(P1_BUS_START_PIN, P1_DIR_PIN, DIR_OUT_HIGH);

    // Main peripheral (address of 0x20) with 1 function: keyboard.
    // A real Dreamcast keyboard has no expansion sockets, so there are no sub peripherals.
    // The version string is not a dump of real hardware; the console keys off the device
    // name and function code, not this. See DREAMCAST.md.
    client::DreamcastMainPeripheral mainPeripheral(
        bus,
        0x20,
        0xFF,
        0x00,
        "Keyboard",
        "Version 1.000,DreamPicoPort USB HID keyboard bridge",
        // Measured on a bench supply at 5V. The rp2040 with a lit oled is 20 mA. The rest
        // is whatever keyboard is attached, and that dominates: a plain membrane one adds
        // 10 mA and peaks at 40 while typing, an rgb one sits at 140 and bounces as the
        // lighting animates. Declared for the rgb case, since under declaring is the
        // impolite direction when the console fuses one 5V rail across all four ports.
        150.0,
        200.0);

    // Layout is what the console asks the keyboard to declare. A japanese release may
    // expect Japan / Key106; override with -DKBD_LANGUAGE and -DKBD_TYPE at configure time.
    std::shared_ptr<client::DreamcastKeyboard> keyboard =
        std::make_shared<client::DreamcastKeyboard>(
            static_cast<client::DreamcastKeyboard::Language>(KBD_LANGUAGE),
            static_cast<client::DreamcastKeyboard::Type>(KBD_TYPE),
            true,   // num lock LED
            true,   // caps lock LED
            true,   // scroll lock LED
            false,  // kana LED
            false,  // power LED
            false,  // shift LED
            false); // keyboard does not control its own LEDs
    set_keyboard_host(keyboard.get());
    mainPeripheral.addFunction(keyboard);

#if KBD_DEBUG || KBD_OLED || KBD_LED_WRITEBACK
    pDebugKeyboard = keyboard.get();
    pDebugPeripheral = &mainPeripheral;
#endif

#if KBD_DEBUG
    set_keyboard_debug(true);
    printf("\r\n[boot] dreampicoport usb keyboard bridge\r\n"
           "[boot] sdcka=gp%d sdckb=gp%d dir_pin=%d clk=%dkHz\r\n",
           P1_BUS_START_PIN, P1_BUS_START_PIN + 1, P1_DIR_PIN, CPU_FREQ_KHZ);
#endif

    multicore_launch_core1(core1);

    while(true)
    {
        mainPeripheral.task(time_us_64());
        led_task(0);
    }
}

int main()
{
#if KBD_DEBUG
    stdio_init_all();
#endif
    led_init();
    core0();
    return 0;
}

#endif
