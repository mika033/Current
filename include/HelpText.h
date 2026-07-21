#pragma once

#include <juce_core/juce_core.h>

// The per-control help descriptions behind the bottom help bar. The table
// lives in resources/help.json (compiled in via juce_add_binary_data) and is
// lazy-parsed once per process. See the SnorkelAudioStandards messaging-area
// spec: the bar renders "<message> - <description>", so the strip doubles as a
// built-in manual for the last-touched control.
namespace HelpText
{
    /** The description for `key`, or empty when the table has no entry (the
     *  caller then shows the bare message with no tail). */
    juce::String descriptionFor (juce::StringRef key);

    /** Per-option key resolution: returns "<baseKey>.<slug>" (slug =
     *  lower-cased option text, spaces to hyphens) when the table carries that
     *  entry, else baseKey. Lets a combo's options each get their own line
     *  ("scale.minor", "mode.up") while unlisted options fall back to the
     *  control's generic description. */
    juce::String keyForOption (const juce::String& baseKey,
                               const juce::String& optionText);
}
