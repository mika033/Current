#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "ModuleSettings.h"   // ModuleOptions::kRhythmSteps

/** The Rhythmize module's settings body, hosted inside a ModuleWindow above the
 *  shared grid row (ModuleWindow::setCustomBody): the 16-step on/off pattern as
 *  two rows of eight step boxes, styled after Little Arp Monster's pattern-grid
 *  cells (rounded squares, on/off theme fill, uniform black-alpha outline).
 *  Clicking a box toggles its step. Cells are hit-tested against cached rects
 *  (no child component per cell), like ProgressionStepList.
 *
 *  While the transport runs, a translucent accent halo marks the step the
 *  engine is on (LAM's playhead idiom: drawn behind the opaque cells so it
 *  reads as a glowing border around the playing box), polled from
 *  `playingStep` on a 20 Hz timer with a changed-step repaint gate.
 *
 *  Holds its own working copy of the pattern; the owning dialog reads it back
 *  with getPattern() on OK.
 */
class RhythmizeStepGrid : public juce::Component,
                          private juce::Timer
{
public:
    explicit RhythmizeStepGrid (const std::array<bool, ModuleOptions::kRhythmSteps>& initial);

    const std::array<bool, ModuleOptions::kRhythmSteps>& getPattern() const { return steps; }

    /** Fired after any step toggle. The owning dialog uses it to push the
     *  settings to the engine live. */
    std::function<void()> onChanged;

    /** Help-bar reporting (message, help key) for the step toggles. Wired by
     *  the owning dialog. */
    std::function<void (const juce::String&, const juce::String&)> onFeedback;

    /** Polled at timer rate for the playhead: return the step the engine is on
     *  (0..15), or -1 for no playhead (transport stopped). Unset = no playhead
     *  ever shown. */
    std::function<int()> playingStep;

    /** The body section height this component wants, so the caller can size the
     *  ModuleWindow to it (two square cell rows at the window's fixed width). */
    static constexpr int preferredHeight = 142;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    std::array<bool, ModuleOptions::kRhythmSteps> steps;

    // Cached cell rects, rebuilt in resized() so paint() and the mouse
    // handlers agree.
    std::array<juce::Rectangle<int>, ModuleOptions::kRhythmSteps> cellRects {};

    int hovered = -1;   // cell under the mouse, for the LAM hover brighten
    int pressed = -1;   // cell held down, for the LAM press darken

    // The step whose halo is currently drawn; -1 = none. Only ever set from
    // the timer poll, so paint() and the poll agree.
    int playhead = -1;

    void timerCallback() override;
    int cellAt (juce::Point<int> p) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmizeStepGrid)
};
