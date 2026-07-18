#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

/** The redesigned per-module settings window. Every module edits its settings
 *  through this one component, so the whole plugin shares one look and feel
 *  (the InlineDialog stacked-combo dialogs are being migrated onto it, one
 *  module at a time).
 *
 *  Structure, top to bottom:
 *   - a title (the module name),
 *   - a thin menu bar echoing the global menu bar: three fixed slots holding
 *     Root, Scale, and a Rate (or Length) combo, laid out label-left at the
 *     global bar's dimensions. A slot a module doesn't use is left blank so
 *     the row still reads as the shared menu bar,
 *   - a 3x2 grid of cells for the module's own controls, each a combo or a
 *     dial with its label above (the Little Arp Monster section-box layout).
 *     Dials are used where a value reads well as a knob (octaves, gate); the
 *     rest are combos. Unused cells are left blank,
 *   - an action-button row (OK / Cancel).
 *
 *  Like InlineDialog it is a modal overlay inside the editor (NOT an OS window,
 *  per the SnorkelAudioStandards modal-dialog rule): it dims and covers the
 *  whole editor, cancels on Esc / click-outside, and confirms on Return. The
 *  caller owns disposal — remove + delete inside onResult, after reading the
 *  control values back.
 *
 *  Usage:
 *      auto* w = editor.showModuleWindow ("Random");
 *      w->setMenuCombo (0, "root", rootItems, sel, "Root");
 *      w->setGridCombo (0, "from", noteNames, from, "Range from");
 *      w->addButton ("OK", 1);
 *      w->addButton ("Cancel", 0);
 *      w->onResult = [this, id] (int result, ModuleWindow* d) { ... };
 */
class ModuleWindow : public juce::Component
{
public:
    static constexpr int kMenuSlots = 3;   // Root, Scale, Rate/Length
    static constexpr int kGridSlots = 6;   // 3 columns x 2 rows

    explicit ModuleWindow (const juce::String& title);
    ~ModuleWindow() override;

    /** Fill a menu-bar slot (0..2) with a combo. Slots left unset stay blank.
     *  `name` is the lookup key for getComboSelectedIndex. */
    void setMenuCombo (int slot,
                       const juce::String& name,
                       const juce::StringArray& items,
                       int selectedIndex,
                       const juce::String& label);

    /** Fill a grid cell (0..5, row-major: 0-2 top row, 3-5 bottom) with a combo.
     *  Cells left unset stay blank. */
    void setGridCombo (int slot,
                       const juce::String& name,
                       const juce::StringArray& items,
                       int selectedIndex,
                       const juce::String& label);

    /** Fill a grid cell (0..5) with a rotary dial over an integer-friendly
     *  range. Read the value back (rounded by the caller) with getDialValue.
     *  `valueText`, when given, formats the current value into the cell's
     *  label so it reads e.g. "From: C2" and tracks the dial live — a dial has
     *  no text box, so this is its only on-window readout. */
    void setGridDial (int slot,
                      const juce::String& name,
                      double minValue, double maxValue, double interval,
                      double value,
                      const juce::String& label,
                      std::function<juce::String (double)> valueText = {});

    int    getComboSelectedIndex (const juce::String& name) const;
    double getDialValue (const juce::String& name) const;

    /** Re-run a dial cell's value formatter to rebuild its label. Public so a
     *  caller can refresh a dial whose readout depends on another control — e.g.
     *  Shift's amount reads "semitones" or "steps" off the Scale combo. No-op
     *  for an unknown name or a dial without a formatter. */
    void refreshDial (const juce::String& name);

    /** Install a callback fired when the named combo's selection changes, so one
     *  control can react to another (Shift/Delay use it to reword the amount
     *  dial's unit when Scale flips to/from Off). */
    void setComboChangeCallback (const juce::String& name, std::function<void()> cb);

    /** Add an action button. The first button added is the affirmative one
     *  (Return triggers it), conventionally OK. */
    void addButton (const juce::String& text, int returnValue);

    /** Fired on a button click, Esc / Return, or a click outside the panel.
     *  Esc / click-outside pass 0 (Cancel by convention). */
    std::function<void (int, ModuleWindow*)> onResult;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

private:
    juce::String titleText;

    struct ComboSlot
    {
        juce::String                    name;
        std::unique_ptr<juce::Label>    label;
        std::unique_ptr<juce::ComboBox> combo;
        bool filled() const { return combo != nullptr; }
    };

    // A grid cell is either a combo or a dial (or empty). Both pointers are
    // never set at once.
    struct GridCell
    {
        juce::String                    name;
        std::unique_ptr<juce::Label>    label;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::Slider>   dial;
        // For dials with a live readout: the static control name and the
        // value formatter, so the label can be rebuilt as "name: value" on
        // every dial move.
        juce::String                          baseLabel;
        std::function<juce::String (double)>  dialFormat;
        bool filled() const { return combo != nullptr || dial != nullptr; }
    };

    struct ButtonEntry
    {
        int                               returnValue = 0;
        std::unique_ptr<juce::TextButton> button;
    };

    std::array<ComboSlot, kMenuSlots> menuSlots;
    std::array<GridCell,  kGridSlots> gridCells;
    juce::OwnedArray<ButtonEntry>     buttons;

    juce::Label& makeLabel (std::unique_ptr<juce::Label>& holder, const juce::String& text,
                            juce::Justification just);
    void configureDial (juce::Slider& dial);
    // Rebuild a dial cell's label as "name: value" from its formatter (no-op
    // for a cell without one). Called on construction and on every dial move.
    void refreshDialLabel (GridCell& cell);

    // Cached geometry, computed in resized(), consumed by paint() so the panel
    // frame, the menu-bar strip, and the grid box draw without recomputing.
    juce::Rectangle<int> panelBounds;
    juce::Rectangle<int> menuStripBounds;
    juce::Rectangle<int> gridBoxBounds;

    int calculatePanelHeight() const;

    // Layout constants. Root/Scale reuse the global menu bar's dimensions
    // (labelW / comboW / rowH), per the design brief.
    static constexpr int panelWidth    = 500;
    static constexpr int padding       = 16;
    static constexpr int titleHeight   = 30;

    static constexpr int menuRowH      = 26;   // = global menu bar control height
    static constexpr int menuLabelW    = 46;   // = global menu bar labelW
    static constexpr int menuComboW    = 92;   // = global menu bar comboW
    static constexpr int menuLabelGap  = 4;
    static constexpr int menuGroupGap  = 10;
    static constexpr int menuStripInsetX = 10;
    static constexpr int menuStripInsetY = 6;
    static constexpr int menuStripH    = menuRowH + menuStripInsetY * 2;

    static constexpr int gridInset     = 14;   // section-box inner inset (LAM)
    static constexpr int cellLabelH    = 14;
    static constexpr int cellLabelGap  = 4;
    static constexpr int dialSize      = 70;
    static constexpr int comboH        = 26;
    static constexpr int controlAreaH  = dialSize;
    static constexpr int cellRowH      = cellLabelH + cellLabelGap + controlAreaH;
    static constexpr int colGap        = 12;
    static constexpr int rowGap        = 12;

    static constexpr int sectionGap    = 12;   // between title/menu/grid/buttons
    static constexpr int buttonHeight  = 28;
    static constexpr int buttonWidth   = 80;
    static constexpr int buttonSpacing = 10;

    static constexpr float windowCorner  = 8.0f;
    static constexpr float sectionCorner = 5.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleWindow)
};
