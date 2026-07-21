#include "MainView.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"

MainView::MainView (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor)
    : menuBar  (processor, [this]() { toggleSettings(); }),
      canvas   (processor, editor),
      palette  (),
      settings (processor, [&editor]() { editor.applyTheme(); })
{
    addAndMakeVisible (menuBar);
    addAndMakeVisible (canvas);
    addAndMakeVisible (palette);
    addChildComponent (settings);   // hidden until the Settings button opens it
}

MainView::~MainView() = default;

void MainView::toggleSettings()
{
    settingsOpen = ! settingsOpen;
    canvas.setVisible (! settingsOpen);
    palette.setVisible (! settingsOpen);
    settings.setVisible (settingsOpen);
    menuBar.setSettingsOpen (settingsOpen);
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

    // The settings space always spans the full below-the-bar area (canvas +
    // palette region); visibility, not bounds, decides which surface shows.
    settings.setBounds (area);

    palette.setBounds (area.removeFromBottom (kPaletteH));
    area.removeFromBottom (kGap);

    canvas.setBounds (area);
}
