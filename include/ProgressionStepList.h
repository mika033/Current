#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "ModuleSettings.h"   // ProgressionStep, ModuleOptions

/** The Progression module's settings body, hosted inside a ModuleWindow in place
 *  of the shared 3x2 grid (ModuleWindow::setCustomBody). A variable-length step
 *  list — the reason Progression couldn't ride the fixed six-cell grid.
 *
 *  Modelled on Little Sequencer's arranger tab: a left-to-right row of step
 *  cells (the scale degree drawn big, the octave offset as a corner tag), and a
 *  trailing append cell whose two halves are action arrows — a right arrow (top)
 *  that adds a step and a left arrow (bottom) that removes the last one. Cells
 *  are hit-tested against cached rects (no child component per cell). Clicking a
 *  cell selects it; the Degree / Octave combos below then edit that step.
 *
 *  Holds its own working copy of the step list; the owning dialog reads it back
 *  with getSteps() on OK. A progression always keeps at least one step, and
 *  never grows past kMaxProgSteps.
 */
class ProgressionStepList : public juce::Component
{
public:
    explicit ProgressionStepList (const std::vector<ProgressionStep>& initial);
    ~ProgressionStepList() override;

    const std::vector<ProgressionStep>& getSteps() const { return steps; }

    /** The body section height this component wants, so the caller can size the
     *  ModuleWindow to it. */
    static constexpr int preferredHeight = 168;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<ProgressionStep> steps;
    int selected = 0;   // 0..count-1

    juce::Label    degreeLabel;
    juce::Label    octaveLabel;
    juce::ComboBox degreeCombo;
    juce::ComboBox octaveCombo;

    // Cached cell rects, rebuilt in resized() / on a step add/remove so paint()
    // and mouseDown() agree. Index 0..count-1 are the step cells; appendCellRect
    // is the trailing add/remove cell.
    std::vector<juce::Rectangle<int>> cellRects;
    juce::Rectangle<int>              appendCellRect;

    void addStep();       // append a step (duplicating the last), select it
    void removeLast();    // drop the last step, keeping at least one
    void selectStep (int index);

    void recomputeCellRects();
    void refreshEditor();  // sync the combos to the selected step

    // Draw the append cell's two action arrows (append on top, delete below),
    // path-drawn to match the arranger reference. addShown hides the top arrow
    // at capacity; delEnabled dims the bottom arrow at the one-step minimum.
    static void drawAppendArrows (juce::Graphics&, juce::Rectangle<float>,
                                  juce::Colour, bool addShown, bool delEnabled);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgressionStepList)
};
