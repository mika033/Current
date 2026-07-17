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
 *  Backs every module-settings dialog; the message/text-field plumbing is
 *  carried over from Little Arp Monster, and the combo rows can be added and
 *  removed after showing (see addComboBox / removeComboBox), which is what
 *  dynamic dialogs like the Progression's step list build on.
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

    /** Add a labeled drop-down with one entry per item. The label sits above
     *  the combo, like addTextEditor. `selectedIndex` is 0-based into `items`;
     *  read the choice back with getComboBoxSelectedIndex. `sameRow` packs the
     *  combo onto the previous combo's row (the row splits its width evenly)
     *  instead of starting a new one — for tightly coupled pairs like the
     *  Progression's degree + octave. Works after the dialog is shown, too:
     *  the panel re-lays itself out, which is what lets dialogs grow and
     *  shrink rows dynamically. */
    void addComboBox (const juce::String& name,
                      const juce::StringArray& items,
                      int selectedIndex,
                      const juce::String& label,
                      bool sameRow = false);
    int getComboBoxSelectedIndex (const juce::String& name) const;

    /** Remove a combo added with addComboBox (no-op for unknown names). The
     *  panel re-lays itself out when shown — the dynamic counterpart of adding
     *  post-show. */
    void removeComboBox (const juce::String& name);

    /** Add an action button. `returnValue` is what gets passed to onResult when
     *  the button is clicked. By convention the first button added is the
     *  affirmative (Return key triggers it). */
    void addButton (const juce::String& text, int returnValue);

    /** Add a button that does NOT close the dialog: `onClick` runs and the
     *  dialog stays up. Laid out bottom-left, away from the action buttons.
     *  For in-dialog editing actions (the Progression's add/remove step). */
    void addUtilityButton (const juce::String& text, std::function<void()> onClick);

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

    struct ComboEntry
    {
        juce::String                    name;
        std::unique_ptr<juce::Label>    labelComp;
        std::unique_ptr<juce::ComboBox> combo;
        bool                            sameRow = false;   // packs onto the
                                                           // previous combo's row
    };
    juce::OwnedArray<ComboEntry> combos;

    struct ButtonEntry
    {
        int                               returnValue = 0;
        std::unique_ptr<juce::TextButton> button;
    };
    juce::OwnedArray<ButtonEntry> buttons;
    juce::OwnedArray<ButtonEntry> utilityButtons;   // non-closing, bottom-left

    // Combo rows, with sameRow entries folded into their predecessor's row.
    int comboRowCount() const;

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
