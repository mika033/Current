#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MenuBar.h"
#include "Canvas.h"
#include "PaletteBar.h"
#include "SettingsView.h"
#include "MessageStrip.h"

class CurrentAudioProcessor;
class CurrentAudioProcessorEditor;

// Assembles the Phase 2 editor layout: the global-settings menu bar on top, the
// canvas filling the middle, the module palette tray along the bottom, and the
// one-line help bar beneath everything. The menu bar's Settings button swaps
// the canvas + palette (a palette over a settings page would invite meaningless
// drags) for the settings space, and back; the menu bar and the help bar stay
// through the swap.
class MainView : public juce::Component
{
public:
    MainView (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor);
    ~MainView() override;

    /** The messaging-area funnel (see the standards repo's messaging-area
     *  spec): every feedback message in the editor lands here. Renders
     *  "<message> - <description from help.json>" in the bottom help bar;
     *  an unknown/empty helpKey shows the bare message. Suppressed while the
     *  processor is restoring state, so a project load doesn't flash a random
     *  parameter's value. */
    void showFeedback (const juce::String& message, juce::StringRef helpKey = {});

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CurrentAudioProcessor& proc;

    MenuBar      menuBar;
    Canvas       canvas;
    PaletteBar   palette;
    SettingsView settings;
    MessageStrip helpBar;

    bool settingsOpen = false;
    void toggleSettings();

    static constexpr int kMenuBarH = 52;
    // Tall enough for the filter row + full-size chips + the strip's scrollbar.
    static constexpr int kPaletteH = 130;
    static constexpr int kHelpBarH = 34;
    static constexpr int kGap      = 10;
    static constexpr int kPad      = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
