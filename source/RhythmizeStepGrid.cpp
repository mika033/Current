#include "RhythmizeStepGrid.h"
#include "Theme.h"

namespace
{
    // Same inset as the window's grid box; the cells divide the remaining
    // width into eight square columns (LAM's grid sizing: stride = width /
    // columns, square rows, a 1px slot inset plus a 1.5px paint inset making
    // the visible gap between boxes).
    constexpr int kInset   = 14;
    constexpr int kTopGap  = 4;
    constexpr int kColumns = 8;
}

RhythmizeStepGrid::RhythmizeStepGrid (const std::array<bool, ModuleOptions::kRhythmSteps>& initial)
    : steps (initial)
{
    startTimerHz (20);   // LAM's playhead poll rate
}

void RhythmizeStepGrid::timerCallback()
{
    // Repaint only when the step actually moved (LAM's repaint gate), so an
    // idle or stopped transport costs nothing.
    const int s = playingStep ? playingStep() : -1;
    if (s != playhead)
    {
        playhead = s;
        repaint();
    }
}

void RhythmizeStepGrid::resized()
{
    const int pitch = (getWidth() - 2 * kInset) / kColumns;
    for (int i = 0; i < ModuleOptions::kRhythmSteps; ++i)
    {
        const int col = i % kColumns;
        const int row = i / kColumns;
        cellRects[(size_t) i] = { kInset + col * pitch + 1,
                                  kInset + kTopGap + row * pitch + 1,
                                  pitch - 2, pitch - 2 };
    }
}

int RhythmizeStepGrid::cellAt (juce::Point<int> p) const
{
    for (int i = 0; i < ModuleOptions::kRhythmSteps; ++i)
        if (cellRects[(size_t) i].contains (p))
            return i;
    return -1;
}

void RhythmizeStepGrid::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();

    // Playhead halo first, behind the opaque cells (LAM's idiom: accent at
    // 0.35 alpha, 3 px overhang, radius 5), so it reads as a glowing border
    // around the playing box.
    if (playhead >= 0 && playhead < ModuleOptions::kRhythmSteps)
    {
        g.setColour (s.accent.withAlpha (0.35f));
        g.fillRoundedRectangle (cellRects[(size_t) playhead].toFloat().expanded (3.0f), 5.0f);
    }

    // LAM's GateButton vocabulary: rounded square, on/off theme fill, uniform
    // black-alpha outline, brighten on hover / darken while pressed. Current
    // has no per-row pattern hues, so "on" wears the accent.
    for (int i = 0; i < ModuleOptions::kRhythmSteps; ++i)
    {
        const auto r = cellRects[(size_t) i].toFloat().reduced (1.5f);
        juce::Colour col = steps[(size_t) i] ? s.accent : s.widgetBg;
        if (i == pressed)
            col = col.darker (0.3f);
        else if (i == hovered)
            col = col.brighter (0.1f);
        g.setColour (col);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
}

void RhythmizeStepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int i = cellAt (e.getPosition());
    if (i < 0)
        return;
    // Toggle on the press, not the release — immediate under a finger, like
    // the canvas's touch-first gestures.
    pressed = i;
    steps[(size_t) i] = ! steps[(size_t) i];
    repaint();
    if (onChanged)
        onChanged();
    if (onFeedback)
        onFeedback ("Step " + juce::String (i + 1)
                        + (steps[(size_t) i] ? ": On" : ": Off"), "rhythm.step");
}

void RhythmizeStepGrid::mouseUp (const juce::MouseEvent&)
{
    pressed = -1;
    repaint();
}

void RhythmizeStepGrid::mouseMove (const juce::MouseEvent& e)
{
    const int i = cellAt (e.getPosition());
    if (i != hovered)
    {
        hovered = i;
        repaint();
    }
}

void RhythmizeStepGrid::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || pressed != -1)
    {
        hovered = -1;
        pressed = -1;
        repaint();
    }
}
