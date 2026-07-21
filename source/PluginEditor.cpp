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

ModuleWindow* CurrentAudioProcessorEditor::showModuleWindow (const juce::String& title)
{
    auto* win = new ModuleWindow (title);
    addAndMakeVisible (win);
    win->setBounds (getLocalBounds());   // cover the whole editor
    win->toFront (true);
    win->grabKeyboardFocus();

    // The window reports every control touch to the help bar itself (it knows
    // each control's label, value, and help key); opening it announces the
    // live-apply contract once.
    win->onFeedback = [this] (const juce::String& msg, const juce::String& key)
    {
        showFeedback (msg, key);
    };
    showFeedback (title + " settings", "action.window");
    return win;
}

void CurrentAudioProcessorEditor::showFeedback (const juce::String& message)
{
    mainView.showFeedback (message);
}

void CurrentAudioProcessorEditor::showFeedback (const juce::String& message,
                                                juce::StringRef helpKey)
{
    mainView.showFeedback (message, helpKey);
}

void CurrentAudioProcessorEditor::applyTheme()
{
    const int idx = (int) processor.apvts().getRawParameterValue (ParamIDs::theme)->load();
    CurrentTheme::setActive (CurrentTheme::byIndex (idx));
    lookAndFeel.applyScheme (CurrentTheme::active());

    // Repaint invalidates the whole subtree, so every child's paint-time theme
    // read (MainView / MenuBar / Canvas / nodes) lands from this one call.
    repaint();
}
