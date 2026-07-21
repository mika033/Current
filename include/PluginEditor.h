#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "InlineDialog.h"
#include "ModuleWindow.h"
#include "CurrentLookAndFeel.h"
#include "MainView.h"

// Phase 2 editor. Derives from DragAndDropContainer so the palette chips and the
// canvas share one drag context (the container must be an ancestor of both).
class CurrentAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::DragAndDropContainer
{
public:
    explicit CurrentAudioProcessorEditor (CurrentAudioProcessor&);
    ~CurrentAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Spawn an inline modal dialog inside the editor's component hierarchy.
     *  Returns a non-owning pointer the caller configures (addButton / onResult)
     *  then disposes of inside its onResult callback. Replaces juce::AlertWindow
     *  per the SnorkelAudioStandards modal-dialog rule. Backs the module
     *  settings dialogs. */
    InlineDialog* showInlineDialog (const juce::String& title,
                                    const juce::String& message = {});

    /** Spawn a structured module-settings window (menu bar + 3x2 grid) inside
     *  the editor. Same overlay/disposal contract as showInlineDialog: the
     *  caller configures it (setMenuCombo / setGridCombo / setGridDial /
     *  addButton / onResult) and disposes of it inside its onResult callback.
     *  The redesigned per-module settings surface. */
    ModuleWindow* showModuleWindow (const juce::String& title);

    /** Re-skin the editor from the Theme parameter: pushes the active scheme into
     *  the shared LookAndFeel and repaints so paint-time theme reads take effect.
     *  Called from the ctor and whenever the theme is switched (the settings
     *  space's Theme button, or an external parameter write). */
    void applyTheme();

private:
    CurrentAudioProcessor& processor;

    // Owned here so it outlives every child; installed via setLookAndFeel and
    // cleared in the dtor.
    CurrentLookAndFeel lookAndFeel;

    MainView mainView;

    static constexpr int kDefaultW = 900;
    static constexpr int kDefaultH = 620;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurrentAudioProcessorEditor)
};
