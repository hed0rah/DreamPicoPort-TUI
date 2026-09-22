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

#include <cstdint>

struct i2c_inst;

//! Minimal SSD1306 driver for a 128x64 I2C OLED, written against hardware_i2c so that
//! nothing external is pulled in.
//!
//! Timing matters here. A full frame is 1024 bytes, which at 400 kHz is roughly 25 ms of
//! blocking I2C. That is longer than the Dreamcast's ~16 ms poll interval, so a full flush
//! must never happen on the core running the Maple client. Call flushDirty() from the USB
//! core instead: it writes only the 128 byte pages that actually changed, so a typical
//! status update costs a few milliseconds rather than 25.
class Ssd1306
{
public:
    //! Display geometry. Only the common 128x64 part is supported.
    static const std::uint8_t kWidth = 128;
    static const std::uint8_t kHeight = 64;
    //! The panel is addressed in horizontal bands of 8 rows
    static const std::uint8_t kNumPages = kHeight / 8;
    //! Characters are 5 pixels wide plus one blank column
    static const std::uint8_t kCharWidth = 6;
    //! Characters that fit across the panel
    static const std::uint8_t kNumCols = kWidth / kCharWidth;

    //! @param[in] i2c      the i2c instance to drive, already selected by the caller
    //! @param[in] sdaPin   gpio carrying SDA
    //! @param[in] sclPin   gpio carrying SCL
    //! @param[in] addr     7 bit address, 0x3C on most modules and 0x3D on a few
    Ssd1306(i2c_inst* i2c, std::uint32_t sdaPin, std::uint32_t sclPin, std::uint8_t addr = 0x3C);

    //! Brings up the panel. Safe to call again to recover a display that lost power.
    //! @returns true iff the panel acknowledged its address
    bool init();

    //! @returns true iff init() found a panel
    inline bool isPresent() const { return mPresent; }

    //! Clears the framebuffer. Does not touch the panel until a flush.
    void clear();

    //! Draws text into the framebuffer, clipped at the edges of the panel
    //! @param[in] col   character column, 0 to kNumCols-1
    //! @param[in] page  text row, 0 to kNumPages-1
    //! @param[in] text  null terminated ASCII, characters outside 0x20 to 0x7E draw blank
    void text(std::uint8_t col, std::uint8_t page, const char* text);

    //! Draws text and blanks the rest of the line, so shorter text erases what it replaces
    void textLine(std::uint8_t page, const char* text);

    //! Blits a page aligned 1 bit bitmap into the framebuffer. The bitmap format matches
    //! the framebuffer exactly, column major with one byte per 8 vertical pixels, so this
    //! is a memcpy per page rather than per pixel work.
    //! @param[in] x      left edge in pixels
    //! @param[in] page   top edge, in 8 pixel bands
    //! @param[in] data   bitmap, pages rows of w bytes
    //! @param[in] w      width in pixels
    //! @param[in] pages  height in 8 pixel bands
    void bitmap(std::uint8_t x, std::uint8_t page, const std::uint8_t* data,
                std::uint8_t w, std::uint8_t pages);

    //! Writes only the pages whose contents changed since the last flush.
    //! @returns number of pages written, 0 when nothing changed
    std::uint8_t flushDirty();

    //! Writes every page. Costs roughly 25 ms; never call this from the Maple core.
    void flushAll();

    //! Turns the panel on or off. Off blanks it and drops the module to microamps, which
    //! matters because the Dreamcast fuses one 5V rail across all four controller ports,
    //! and because a static status page will burn into an OLED given long enough.
    //! Cheap: a single command byte, no frame traffic.
    void setPower(bool on);

    //! @returns true iff the panel is currently lit
    inline bool isOn() const { return mOn; }

private:
    //! Sends a single command byte
    bool command(std::uint8_t c);

    //! Writes one 128 byte page to the panel
    bool writePage(std::uint8_t page);

    i2c_inst* const mI2c;
    const std::uint32_t mSdaPin;
    const std::uint32_t mSclPin;
    const std::uint8_t mAddr;
    bool mPresent;
    bool mOn;
    //! One byte per column per page; each byte is 8 vertical pixels
    std::uint8_t mBuffer[kNumPages][kWidth];
    //! Set when a page differs from what the panel currently shows
    bool mDirty[kNumPages];
};
