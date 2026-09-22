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

#pragma once

#include <stdint.h>

class KeyboardHost
{
public:
    inline KeyboardHost() {}
    inline virtual ~KeyboardHost() {}

    //! Number of simultaneously reported keys in HID boot protocol
    static const uint8_t NUM_KEYS = 6;

    //! Standard HID boot protocol keyboard state
    struct Keys
    {
        //! Modifier bit field, matching the HID boot protocol layout:
        //! bit 0: left ctrl   bit 4: right ctrl
        //! bit 1: left shift  bit 5: right shift
        //! bit 2: left alt    bit 6: right alt
        //! bit 3: left gui    bit 7: right gui
        //! The Dreamcast keyboard uses the same layout for bits 0 through 6; bit 7 is
        //! the Japanese S2 key rather than right gui.
        uint8_t modifiers;
        //! Currently pressed usage codes, 0 for unused slots. 0x01 in every slot means
        //! the keyboard is reporting rollover error rather than real key presses.
        uint8_t keys[NUM_KEYS];
    };

    //! Set updated key state
    //! @param[in] keys  Updated key state
    virtual void setKeys(const Keys& keys) = 0;
};

void set_keyboard_host(KeyboardHost* keyboard);

//! Pushes lock LED state to the mounted keyboard. The console owns this state, not the
//! keyboard: pressing caps lock only sends a usage code, and the host decides whether the
//! light comes on. Bit field matches the HID output report:
//! bit 0 num lock, bit 1 caps lock, bit 2 scroll lock, bit 3 compose, bit 4 kana.
//! @returns true iff a report was queued
bool set_keyboard_leds(uint8_t leds);

//! Enable printf tracing of keyboard mount and report events. Only call this from a build
//! that has stdio configured, since the prints block on the uart.
void set_keyboard_debug(bool enable);

//! @returns true iff a HID keyboard is currently enumerated
bool is_keyboard_mounted();

//! @returns the most recent report received from the keyboard, zeroed if none
KeyboardHost::Keys get_last_keys();

//! @returns usb vid and pid of the mounted keyboard, both 0 if none
void get_keyboard_vid_pid(uint16_t& vid, uint16_t& pid);

//! @returns running count of key down edges since boot. Counted here rather than in the
//! display task because the display redraws at 2 Hz and fast typing would slip between
//! frames. Modifiers are not counted; they are not characters.
uint32_t get_keystroke_count();
