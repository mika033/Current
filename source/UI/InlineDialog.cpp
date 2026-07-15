#include "InlineDialog.h"
#include "ColourScheme.h"
#include "PluginLookAndFeel.h"

InlineDialog::InlineDialog()
{
    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { dismiss(); };
    setVisible (false);
    setAlwaysOnTop (true);
}

void InlineDialog::show (const juce::String& title, const juce::String& message)
{
    dialogTitle = title;
    dialogMessage = message;
    setVisible (true);
    resized();
    toFront (false);
}

void InlineDialog::dismiss()
{
    setVisible (false);
}

void InlineDialog::paint (juce::Graphics& g)
{
    const auto& cs = active();

    // Dim the whole overlay so the card reads as modal.
    g.fillAll (juce::Colours::black.withAlpha (0.35f));

    // Section-box recipe per panels-controls.md §1.1.
    g.setColour (cs.sectionBoxBg);
    g.fillRoundedRectangle (cardBounds.toFloat(), 6.0f);
    g.setColour (cs.panelBorder);
    g.drawRoundedRectangle (cardBounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

    // Dialog title: 17pt bold per typography.md §1.4 / modal-dialogs.md §1.3.
    g.setColour (cs.text);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    auto textArea = cardBounds.reduced (20).withTrimmedBottom (44);
    g.drawText (dialogTitle, textArea.removeFromTop (28), juce::Justification::centredLeft);

    // Body copy stays on kUiFontSize per modal-dialogs.md §1.3.
    g.setFont (juce::Font (juce::FontOptions (kUiFontSize)));
    g.drawFittedText (dialogMessage, textArea, juce::Justification::topLeft, 4);
}

void InlineDialog::resized()
{
    auto bounds = getLocalBounds();
    const int cardW = juce::jmin (360, bounds.getWidth() - 40);
    const int cardH = 160;
    cardBounds = juce::Rectangle<int> (cardW, cardH).withCentre (bounds.getCentre());

    closeButton.setBounds (cardBounds.getRight() - 90, cardBounds.getBottom() - 40, 70, 25);
}

void InlineDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds.contains (e.getPosition()))
        dismiss();
}
