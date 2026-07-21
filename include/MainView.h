#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MenuBar.h"
#include "Canvas.h"
#include "PaletteBar.h"
#include "SettingsView.h"

class CurrentAudioProcessor;
class CurrentAudioProcessorEditor;

// Assembles the Phase 2 editor layout: the global-settings menu bar on top, the
// canvas filling the middle, and the module palette tray along the bottom. The
// menu bar's Settings button swaps everything below the bar (canvas + palette —
// a palette over a settings page would invite meaningless drags) for the
// settings space, and back.
class MainView : public juce::Component
{
public:
    MainView (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MenuBar      menuBar;
    Canvas       canvas;
    PaletteBar   palette;
    SettingsView settings;

    bool settingsOpen = false;
    void toggleSettings();

    static constexpr int kMenuBarH = 52;
    // Tall enough for the filter row + full-size chips + the strip's scrollbar.
    static constexpr int kPaletteH = 130;
    static constexpr int kGap      = 10;
    static constexpr int kPad      = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
