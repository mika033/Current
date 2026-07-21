#include "MainView.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HelpText.h"
#include "Theme.h"

MainView::MainView (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor)
    : proc     (processor),
      menuBar  (processor, [this]() { toggleSettings(); }),
      canvas   (processor, editor),
      palette  (),
      settings (processor, [&editor]() { editor.applyTheme(); })
{
    addAndMakeVisible (menuBar);
    addAndMakeVisible (canvas);
    addAndMakeVisible (palette);
    addAndMakeVisible (helpBar);
    addChildComponent (settings);   // hidden until the Settings button opens it

    // Route the children's feedback into the one showFeedback funnel. Canvas
    // and the module windows go through the editor's forwarders instead (they
    // already hold an editor reference).
    auto feedback = [this] (const juce::String& msg, const juce::String& key)
    {
        showFeedback (msg, key);
    };
    menuBar.onFeedback  = feedback;
    palette.onFeedback  = feedback;
    settings.onFeedback = feedback;

    // Dragging a canvas node onto the palette tray deletes it. The canvas owns
    // the gesture but only this view knows both components, so it provides the
    // tray hit-test (screen coords — the two don't share a parent space) and
    // relays the remove-zone highlight.
    canvas.isOverRemoveZone = [this] (juce::Point<int> screenPos)
    {
        return palette.isShowing() && palette.getScreenBounds().contains (screenPos);
    };
    canvas.setRemoveZoneState = [this] (bool armed, bool hot)
    {
        palette.setRemoveDragState (armed, hot);
    };
}

MainView::~MainView() = default;

void MainView::showFeedback (const juce::String& message, juce::StringRef helpKey)
{
    // A host state-restore retriggers every APVTS attachment within
    // milliseconds; without this gate the user would see a random parameter's
    // value pop up after opening a project.
    if (proc.isRestoringState())
        return;

    // "<message> - <description>", ASCII hyphen (JUCE's default font has no
    // em-dash glyph — the standards repo's no-em-dashes rule).
    const auto desc = HelpText::descriptionFor (helpKey);
    helpBar.showMessage (desc.isEmpty() ? message : message + " - " + desc);
}

void MainView::toggleSettings()
{
    settingsOpen = ! settingsOpen;
    canvas.setVisible (! settingsOpen);
    palette.setVisible (! settingsOpen);
    settings.setVisible (settingsOpen);
    menuBar.setSettingsOpen (settingsOpen);

    if (settingsOpen)
        showFeedback ("Settings", "action.settings");
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (CurrentTheme::active().windowBg);
}

void MainView::resized()
{
    auto area = getLocalBounds().reduced (kPad);

    menuBar.setBounds (area.removeFromTop (kMenuBarH));
    area.removeFromTop (kGap);

    // The help bar spans the editor's bottom edge and stays through the
    // settings swap (one strip per editor, per the messaging-area spec).
    helpBar.setBounds (area.removeFromBottom (kHelpBarH));
    area.removeFromBottom (kGap);

    // The settings space always spans the full remaining area (canvas +
    // palette region); visibility, not bounds, decides which surface shows.
    settings.setBounds (area);

    palette.setBounds (area.removeFromBottom (kPaletteH));
    area.removeFromBottom (kGap);

    canvas.setBounds (area);
}
