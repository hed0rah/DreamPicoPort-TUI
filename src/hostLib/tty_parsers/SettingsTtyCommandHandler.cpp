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

#include "SettingsTtyCommandHandler.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace
{
//! Longest key we accept, plus room for a null
const std::size_t kMaxKeyLen = 24;
//! Longest value we accept, plus room for a null
const std::size_t kMaxValLen = 16;
//! Delay in ms before a save or clear reboots, so the response reaches the terminal first
const std::uint32_t kRebootDelayMs = 100;

//! @returns true iff the text parses fully as a base 10 integer
bool parseInt(const char* text, long& out)
{
    if (text == nullptr || *text == 0)
    {
        return false;
    }
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != 0)
    {
        return false;
    }
    out = v;
    return true;
}

//! @returns true iff the text parses as an integer within the inclusive range
bool parseRange(const char* text, long lo, long hi, long& out)
{
    long v = 0;
    if (!parseInt(text, v) || v < lo || v > hi)
    {
        return false;
    }
    out = v;
    return true;
}

//! Matches a key of the form prefix followed by a player index
//! @returns true iff matched, setting idx
bool matchIndexed(const char* key, const char* prefix, long& idx)
{
    const std::size_t plen = std::strlen(prefix);
    if (std::strncmp(key, prefix, plen) != 0)
    {
        return false;
    }
    return parseRange(key + plen, 0, DppSettings::kNumPlayers - 1, idx);
}
} // namespace

SettingsTtyCommandHandler::SettingsTtyCommandHandler() :
    mLoadedSettings(DppSettings::getInitialSettings()),
    mSettings(DppSettings::getInitialSettings())
{}

const char* SettingsTtyCommandHandler::getCommandChars()
{
    return "=";
}

void SettingsTtyCommandHandler::dump(const char* what, const DppSettings& s)
{
    std::printf("settings %s begin\n", what);
    std::printf("cdc %d\n", s.cdcEn ? 1 : 0);
    std::printf("msc %d\n", s.mscEn ? 1 : 0);
    std::printf("webusb %d\n", s.webUsbAnnounceEn ? 1 : 0);
    for (std::uint8_t i = 0; i < DppSettings::kNumPlayers; ++i)
    {
        std::printf("p%u %u\n", i, static_cast<unsigned>(s.playerDetectionModes[i]));
        std::printf("gpioa%u %ld\n", i, static_cast<long>(s.gpioA[i]));
        std::printf("gpiodir%u %ld\n", i, static_cast<long>(s.gpioDir[i]));
        std::printf("dirhigh%u %d\n", i, s.gpioDirOutputHigh[i] ? 1 : 0);
    }
    std::printf("led %ld\n", static_cast<long>(s.usbLedGpio));
    std::printf("simpleled %ld\n", static_cast<long>(s.simpleUsbLedGpio));
    std::printf("dpad %u\n", static_cast<unsigned>(s.dpadType));
    std::printf("settings %s end\n", what);
}

bool SettingsTtyCommandHandler::assign(const char* key, const char* value)
{
    long v = 0;
    long idx = 0;

    if (std::strcmp(key, "cdc") == 0 && parseRange(value, 0, 1, v))
    {
        mSettings.cdcEn = (v != 0);
    }
    else if (std::strcmp(key, "msc") == 0 && parseRange(value, 0, 1, v))
    {
        mSettings.mscEn = (v != 0);
    }
    else if (std::strcmp(key, "webusb") == 0 && parseRange(value, 0, 1, v))
    {
        mSettings.webUsbAnnounceEn = (v != 0);
    }
    else if (std::strcmp(key, "led") == 0 && parseInt(value, v))
    {
        mSettings.usbLedGpio = static_cast<std::int32_t>(v);
    }
    else if (std::strcmp(key, "simpleled") == 0 && parseInt(value, v))
    {
        mSettings.simpleUsbLedGpio = static_cast<std::int32_t>(v);
    }
    else if (std::strcmp(key, "dpad") == 0 &&
             parseRange(value, 0, static_cast<long>(DppSettings::DpadType::kNumDpadTypes) - 1, v))
    {
        mSettings.dpadType = static_cast<DppSettings::DpadType>(v);
    }
    else if (matchIndexed(key, "gpioa", idx) && parseInt(value, v))
    {
        mSettings.gpioA[idx] = static_cast<std::int32_t>(v);
    }
    else if (matchIndexed(key, "gpiodir", idx) && parseInt(value, v))
    {
        mSettings.gpioDir[idx] = static_cast<std::int32_t>(v);
    }
    else if (matchIndexed(key, "dirhigh", idx) && parseRange(value, 0, 1, v))
    {
        mSettings.gpioDirOutputHigh[idx] = (v != 0);
    }
    else if (matchIndexed(key, "p", idx) &&
             parseRange(value, 0,
                        static_cast<long>(DppSettings::PlayerDetectionMode::kNumPlayerDetectionModes) - 1,
                        v))
    {
        mSettings.playerDetectionModes[idx] = static_cast<DppSettings::PlayerDetectionMode>(v);
    }
    else
    {
        return false;
    }

    return true;
}

void SettingsTtyCommandHandler::submit(const char* chars, uint32_t len)
{
    std::printf("\n");

    const char* iter = chars;
    const char* const eol = chars + len;

    // Skip the command character itself
    if (iter < eol && *iter == getCommandChars()[0])
    {
        ++iter;
    }

    while (iter < eol && std::isspace(static_cast<unsigned char>(*iter)))
    {
        ++iter;
    }

    // A bare command dumps the working settings
    if (iter >= eol)
    {
        dump("working", mSettings);
        return;
    }

    // Split into key and optional value
    char key[kMaxKeyLen] = {};
    std::size_t k = 0;
    while (iter < eol && !std::isspace(static_cast<unsigned char>(*iter)) && k < (kMaxKeyLen - 1))
    {
        key[k++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*iter)));
        ++iter;
    }
    key[k] = 0;

    while (iter < eol && std::isspace(static_cast<unsigned char>(*iter)))
    {
        ++iter;
    }

    char value[kMaxValLen] = {};
    std::size_t n = 0;
    while (iter < eol && !std::isspace(static_cast<unsigned char>(*iter)) && n < (kMaxValLen - 1))
    {
        value[n++] = *iter++;
    }
    value[n] = 0;

    // Verbs that take no value
    if (std::strcmp(key, "help") == 0)
    {
        printHelp();
        return;
    }
    else if (std::strcmp(key, "loaded") == 0)
    {
        dump("loaded", mLoadedSettings);
        return;
    }
    else if (std::strcmp(key, "defaults") == 0)
    {
        dump("defaults", DppSettings());
        return;
    }
    else if (std::strcmp(key, "revert") == 0)
    {
        mSettings = mLoadedSettings;
        std::printf("reverted to the settings loaded at boot\n");
        return;
    }
    else if (std::strcmp(key, "save") == 0)
    {
        if (!mSettings.makeValid())
        {
            std::printf("warning: settings were adjusted to be valid\n");
        }
        dump("saving", mSettings);
        std::printf("saving and rebooting\n");
        mSettings.requestSave(kRebootDelayMs);
        return;
    }
    else if (std::strcmp(key, "clear") == 0)
    {
        std::printf("clearing to defaults and rebooting\n");
        DppSettings::requestClear(kRebootDelayMs);
        return;
    }

    if (n == 0)
    {
        std::printf("error: %s needs a value\n", key);
        return;
    }

    if (assign(key, value))
    {
        std::printf("ok %s %s\n", key, value);
        std::printf("note: not saved yet, use save\n");
    }
    else
    {
        std::printf("error: bad key or value: %s %s\n", key, value);
    }
}

void SettingsTtyCommandHandler::printHelp()
{
    std::printf("=: settings\n");
    std::printf("  =                  show working settings\n");
    std::printf("  =loaded            show settings loaded at boot\n");
    std::printf("  =defaults          show factory defaults\n");
    std::printf("  =revert            discard edits\n");
    std::printf("  =save              validate, write to flash, reboot\n");
    std::printf("  =clear             restore defaults and reboot\n");
    std::printf("  =cdc 0|1           usb cdc serial\n");
    std::printf("  =msc 0|1           usb mass storage\n");
    std::printf("  =webusb 0|1        browser landing page announcement\n");
    std::printf("  =led <gpio>        usb activity led, -1 disables\n");
    std::printf("  =simpleled <gpio>  simple usb led, -1 disables\n");
    std::printf("  =dpad 0|1|2        hat, buttons, both\n");
    std::printf("  =p<0-3> <mode>     player detection mode\n");
    std::printf("  =gpioa<0-3> <gpio> maple bus A pin, B is next\n");
    std::printf("  =gpiodir<0-3> <n>  direction pin, -1 disables\n");
    std::printf("  =dirhigh<0-3> 0|1  direction pin polarity\n");
}
