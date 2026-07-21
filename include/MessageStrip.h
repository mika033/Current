#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The one-line help bar at the bottom of the editor (SnorkelAudioStandards
// messaging-area spec): a bordered panel strip showing feedback about the
// last-touched control, composed by MainView::showFeedback as
// "<message> - <description>". Messages are transient; this component owns
// only the display and the expiry timer — composition and the restore-storm
// suppression live in MainView, the one funnel.
class MessageStrip : public juce::Component,
                     private juce::Timer
{
public:
    MessageStrip() = default;

    /** Show a message, restarting the expiry clock. An empty string clears
     *  the bar immediately. */
    void showMessage (const juce::String& text);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    // 3 s like Little Arp Monster (the spec's default is 2 s, but the strip
    // doubles as the manual and the description lines need the reading time).
    static constexpr juce::uint32 kExpiryMs = 3000;

    juce::String message;
    juce::uint32 shownAtMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MessageStrip)
};
