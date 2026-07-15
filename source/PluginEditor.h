#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/PluginLookAndFeel.h"
#include "UI/GlobalSettingsBar.h"
#include "UI/InlineDialog.h"
#include "Canvas/CanvasComponent.h"
#include "Canvas/ModulePalette.h"

// Editor layout per SnorkelAudioStandards design/resize-scaling.md: a fixed
// native canvas (settings bar + canvas + palette, top to bottom) laid out
// once, then uniformly scaled to whatever size the host window is. Desktop
// only for Phase 1 - no iOS/AUv3 opening-size handling yet (see
// CMakeLists.txt comment on FORMATS).
class CurrentAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     public juce::DragAndDropContainer
{
public:
    explicit CurrentAudioProcessorEditor (CurrentAudioProcessor&);
    ~CurrentAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Entry point for Current's inline-dialog helper, named here per
    // design/modal-dialogs.md §1.2 (documented alongside InlineDialog in
    // this repo's CLAUDE.md).
    void showInlineDialog (const juce::String& title, const juce::String& message);

private:
    void applyTheme();

    CurrentAudioProcessor& processorRef;
    PluginLookAndFeel lookAndFeel;

    juce::Component content;
    GlobalSettingsBar settingsBar;
    CanvasComponent canvas;
    ModulePalette palette;
    InlineDialog inlineDialog;

    static constexpr int nativeW = 1100;
    static constexpr int nativeH = 750;
    static constexpr int settingsBarH = 44;
    static constexpr int paletteH = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurrentAudioProcessorEditor)
};
