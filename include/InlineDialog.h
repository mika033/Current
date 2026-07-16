#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/** Modal dialog that lives inside the plugin editor's component hierarchy (NOT a
 *  top-level OS window). This is the plugin's own inline-dialog helper — the
 *  SnorkelAudioStandards rule forbids juce::AlertWindow / juce::DialogWindow
 *  because a plugin-driven desktop window is unreliable in some hosts (AUv3 on
 *  iPad especially) and never picks up the plugin's theme. A child component
 *  repaints from CurrentTheme::active() every frame, so theme swaps land for
 *  free.
 *
 *  In Phase 2 this backs the empty "settings placeholder" a module opens on
 *  double-click; the message/text-field plumbing is carried over from Little Arp
 *  Monster so later phases can grow real settings dialogs on the same base.
 *
 *  Usage:
 *      auto* d = editor.showInlineDialog("Arp settings");
 *      d->addButton("Close", 0);
 *      d->onResult = [](int, InlineDialog* dlg) {
 *          dlg->getParentComponent()->removeChildComponent(dlg);
 *          delete dlg;
 *      };
 *
 *  Click-outside the panel cancels (result 0). Esc cancels. Return triggers the
 *  first button added (conventionally the affirmative action). */
class InlineDialog : public juce::Component
{
public:
    InlineDialog (const juce::String& title, const juce::String& message = "");
    ~InlineDialog() override;

    /** Add a labeled single-line text editor. The label sits above the editor.
     *  `name` is the lookup key for getTextEditorContents. */
    void addTextEditor (const juce::String& name,
                        const juce::String& initialText,
                        const juce::String& label);
    juce::String getTextEditorContents (const juce::String& name) const;

    /** Add an action button. `returnValue` is what gets passed to onResult when
     *  the button is clicked. By convention the first button added is the
     *  affirmative (Return key triggers it). */
    void addButton (const juce::String& text, int returnValue);

    /** Fired when the user clicks a button, hits Esc / Return, or clicks outside
     *  the panel. The caller owns disposal: remove + delete the dialog inside
     *  the callback (so it can read getTextEditorContents first). */
    std::function<void (int, InlineDialog*)> onResult;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

private:
    juce::String titleText;
    juce::String messageText;

    struct TextFieldEntry
    {
        juce::String                      name;
        juce::String                      label;
        std::unique_ptr<juce::Label>      labelComp;
        std::unique_ptr<juce::TextEditor> editor;
    };
    juce::OwnedArray<TextFieldEntry> textFields;

    struct ButtonEntry
    {
        int                               returnValue = 0;
        std::unique_ptr<juce::TextButton> button;
    };
    juce::OwnedArray<ButtonEntry> buttons;

    juce::Rectangle<int> panelBounds;

    static constexpr int panelWidth       = 390;
    static constexpr int titleHeight      = 30;
    static constexpr int fieldHeight      = 28;
    static constexpr int fieldLabelHeight = 18;
    static constexpr int buttonHeight     = 28;
    static constexpr int padding          = 16;
    static constexpr int fieldSpacing     = 6;
    static constexpr int buttonSpacing    = 10;
    static constexpr int messageButtonGap = 14;

    int calculatePanelHeight() const;

    // Wrapped-message support: the message may span several lines, so it is laid
    // out at the panel width and the panel sized to the measured height.
    // layoutMessage is the single source shared by measurement and drawing so
    // they can't drift.
    juce::TextLayout layoutMessage (int width) const;
    int messageTextHeight() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InlineDialog)
};
