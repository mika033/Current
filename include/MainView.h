#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MenuBar.h"
#include "Canvas.h"
#include "PaletteBar.h"

class CurrentAudioProcessor;
class CurrentAudioProcessorEditor;

// Assembles the Phase 1 editor layout: the global-settings menu bar on top, the
// canvas filling the middle, and the module palette tray along the bottom.
class MainView : public juce::Component
{
public:
    MainView (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MenuBar    menuBar;
    Canvas     canvas;
    PaletteBar palette;

    static constexpr int kMenuBarH = 52;
    static constexpr int kPaletteH = 96;
    static constexpr int kGap      = 10;
    static constexpr int kPad      = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
