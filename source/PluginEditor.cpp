#include "PluginEditor.h"
#include "Theme.h"

CurrentAudioProcessorEditor::CurrentAudioProcessorEditor (CurrentAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      mainView (p, *this)
{
    setLookAndFeel (&lookAndFeel);

    // Sync the active theme to the persisted Theme parameter before the first
    // paint, so a project saved in Light doesn't flash Dark on open.
    applyTheme();

    addAndMakeVisible (mainView);

    setResizable (true, true);
    setResizeLimits (720, 500, 1600, 1100);
    setSize (kDefaultW, kDefaultH);
}

CurrentAudioProcessorEditor::~CurrentAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void CurrentAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (CurrentTheme::active().windowBg);
}

void CurrentAudioProcessorEditor::resized()
{
    mainView.setBounds (getLocalBounds());
}

InlineDialog* CurrentAudioProcessorEditor::showInlineDialog (const juce::String& title,
                                                             const juce::String& message)
{
    auto* dlg = new InlineDialog (title, message);
    addAndMakeVisible (dlg);
    dlg->setBounds (getLocalBounds());   // cover the whole editor
    dlg->toFront (true);
    dlg->grabKeyboardFocus();
    return dlg;
}

void CurrentAudioProcessorEditor::applyTheme()
{
    const int idx = (int) processor.apvts().getRawParameterValue (ParamIDs::theme)->load();
    CurrentTheme::setActive (CurrentTheme::byIndex (idx));
    lookAndFeel.applyScheme (CurrentTheme::active());

    // Repaint the whole tree so paint-time theme reads land everywhere.
    repaint();
    for (int i = 0; i < getNumChildComponents(); ++i)
        if (auto* c = getChildComponent (i))
            c->repaint();
}
