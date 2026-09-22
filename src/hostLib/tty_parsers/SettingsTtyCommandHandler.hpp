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

#include "hal/Usb/TtyCommandHandler.hpp"
#include "hal/System/DppSettings.hpp"

#include <cstdint>

// Command structure: [whitespace]<command-char>[command]<\n>

//! Settings over the serial console, so the device can be configured without a browser.
//! Mirrors SettingsWebUsbCommandHandler but with a line oriented text protocol that is
//! both typeable by hand and trivial to script against.
class SettingsTtyCommandHandler : public TtyCommandHandler
{
public:
    SettingsTtyCommandHandler();

    virtual ~SettingsTtyCommandHandler() = default;

    //! @returns the string of command characters this parser handles
    virtual const char* getCommandChars() final;

    //! Called when newline reached; submit command and reset
    virtual void submit(const char* chars, uint32_t len) final;

    //! Prints help message for this command
    virtual void printHelp() final;

private:
    //! Prints the given settings as parseable key value lines
    static void dump(const char* what, const DppSettings& settings);

    //! Applies a single "key value" assignment
    //! @param[in] key    null terminated key, already lowercased by the caller
    //! @param[in] value  the value text, may be null when absent
    //! @returns true iff the key was recognised and the value accepted
    bool assign(const char* key, const char* value);

    //! The settings that were in effect at boot
    const DppSettings mLoadedSettings;
    //! The working copy, edited by assignments and written by save
    DppSettings mSettings;
};
