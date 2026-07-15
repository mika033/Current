#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Current's inline-dialog helper, per SnorkelAudioStandards
// design/modal-dialogs.md §1.2: never juce::AlertWindow / DialogWindow
// (broken under AUv3 on iPad, ignores the plugin theme). Paints inside the
// editor's own bounds instead. Entry point is PluginEditor::showInlineDialog
// (documented in Current's CLAUDE.md alongside this class per the standard).
class InlineDialog : public juce::Component
{
public:
    InlineDialog();

    void show (const juce::String& title, const juce::String& message);
    void dismiss();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String dialogTitle, dialogMessage;
    juce::TextButton closeButton { "Close" };
    juce::Rectangle<int> cardBounds;
};
