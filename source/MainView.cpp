#include "MainView.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"

MainView::MainView (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor)
    : menuBar (processor, [&editor]() { editor.applyTheme(); }),
      canvas  (processor, editor),
      palette ()
{
    addAndMakeVisible (menuBar);
    addAndMakeVisible (canvas);
    addAndMakeVisible (palette);
}

MainView::~MainView() = default;

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (CurrentTheme::active().windowBg);
}

void MainView::resized()
{
    auto area = getLocalBounds().reduced (kPad);

    menuBar.setBounds (area.removeFromTop (kMenuBarH));
    area.removeFromTop (kGap);

    palette.setBounds (area.removeFromBottom (kPaletteH));
    area.removeFromBottom (kGap);

    canvas.setBounds (area);
}
