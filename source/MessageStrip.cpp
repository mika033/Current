#include "MessageStrip.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"

void MessageStrip::showMessage (const juce::String& text)
{
    message   = text;
    shownAtMs = juce::Time::getMillisecondCounter();
    repaint();
    if (message.isNotEmpty())
        startTimer (150);   // coarse poll — expiry precision doesn't matter
    else
        stopTimer();
}

void MessageStrip::timerCallback()
{
    if (juce::Time::getMillisecondCounter() - shownAtMs >= kExpiryMs)
    {
        message.clear();
        stopTimer();
        repaint();
    }
}

void MessageStrip::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();

    // Same panel chrome as the menu bar / palette tray, so the strip reads as
    // a surface of the editor rather than loose text at its bottom edge.
    auto b = getLocalBounds().toFloat();
    g.setColour (s.panelBg);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (s.panelBorder);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

    if (message.isEmpty())
        return;

    g.setColour (s.text);
    g.setFont (juce::Font (juce::FontOptions (CurrentLookAndFeel::kUiFontSize)));
    // Single line; long messages truncate with an ellipsis rather than wrap.
    g.drawText (message, getLocalBounds().reduced (12, 0),
                juce::Justification::centredLeft, true);
}
