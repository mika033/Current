#include "ProgressionStepList.h"
#include "Theme.h"

namespace
{
    // Same square vocabulary as the rest of the plugin's cell rows; a step cell
    // is a touch taller than wide to fit the degree over the octave tag.
    constexpr int kInset      = 14;
    constexpr int kCellW      = 40;
    constexpr int kCellH      = 52;
    constexpr int kCellPitch  = 48;
    constexpr int kCellTopGap = 4;    // above the cell row, inside the inset

    constexpr int kEditorGap  = 20;   // between the cell row and the combos
    constexpr int kEditorH    = 26;
    constexpr int kLabelW     = 52;
    constexpr int kComboW     = 68;
    constexpr int kColGap     = 14;
}

ProgressionStepList::ProgressionStepList (const std::vector<ProgressionStep>& initial)
    : steps (initial)
{
    if (steps.empty())
        steps.push_back ({});   // a progression always keeps one step

    const auto& s = CurrentTheme::active();

    auto initLabel = [this, &s] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredRight);
        l.setColour (juce::Label::textColourId, s.text.withAlpha (0.85f));
        addAndMakeVisible (l);
    };
    initLabel (degreeLabel, "Degree");
    initLabel (octaveLabel, "Octave");

    degreeCombo.addItemList (ModuleOptions::degreeNames(), 1);
    degreeCombo.onChange = [this]
    {
        steps[(size_t) selected].degree = degreeCombo.getSelectedItemIndex();
        repaint();
        if (onChanged)
            onChanged();
        if (onFeedback)
            onFeedback ("Degree: " + degreeCombo.getText(), "prog.degree");
    };
    addAndMakeVisible (degreeCombo);

    juce::StringArray octaves;
    for (int o = -ModuleOptions::kProgOctaveRange; o <= ModuleOptions::kProgOctaveRange; ++o)
        octaves.add (o > 0 ? "+" + juce::String (o) : juce::String (o));
    octaveCombo.addItemList (octaves, 1);
    octaveCombo.onChange = [this]
    {
        steps[(size_t) selected].octave =
            octaveCombo.getSelectedItemIndex() - ModuleOptions::kProgOctaveRange;
        repaint();
        if (onChanged)
            onChanged();
        if (onFeedback)
            onFeedback ("Octave: " + octaveCombo.getText(), "prog.octave");
    };
    addAndMakeVisible (octaveCombo);

    selectStep (0);
}

ProgressionStepList::~ProgressionStepList() = default;

void ProgressionStepList::selectStep (int index)
{
    selected = juce::jlimit (0, (int) steps.size() - 1, index);
    refreshEditor();
    repaint();
}

void ProgressionStepList::refreshEditor()
{
    const auto& step = steps[(size_t) selected];
    degreeCombo.setSelectedItemIndex (
        juce::jlimit (0, ModuleOptions::degreeNames().size() - 1, step.degree),
        juce::dontSendNotification);
    octaveCombo.setSelectedItemIndex (
        juce::jlimit (0, 2 * ModuleOptions::kProgOctaveRange,
                      step.octave + ModuleOptions::kProgOctaveRange),
        juce::dontSendNotification);
}

void ProgressionStepList::addStep()
{
    if ((int) steps.size() >= ModuleOptions::kMaxProgSteps)
    {
        if (onFeedback)
            onFeedback ("A progression holds at most "
                            + juce::String (ModuleOptions::kMaxProgSteps) + " steps", "");
        return;
    }
    // Duplicate the last step, so the append arrow reads as "more of the same";
    // the combos then retype it if a different degree is wanted.
    steps.push_back (steps.back());
    recomputeCellRects();
    selectStep ((int) steps.size() - 1);
    if (onChanged)
        onChanged();
    if (onFeedback)
        onFeedback ("Step " + juce::String ((int) steps.size()) + " added", "prog.add");
}

void ProgressionStepList::removeLast()
{
    if ((int) steps.size() <= 1)   // keep the one-step invariant
    {
        if (onFeedback)
            onFeedback ("A progression keeps at least one step", "");
        return;
    }
    steps.pop_back();
    recomputeCellRects();
    selectStep (juce::jmin (selected, (int) steps.size() - 1));
    if (onChanged)
        onChanged();
    if (onFeedback)
        onFeedback ("Last step removed", "prog.remove");
}

void ProgressionStepList::resized()
{
    recomputeCellRects();

    // Editor row: [Degree][combo]   [Octave][combo], below the cell row.
    const int editorY = kInset + kCellTopGap + kCellH + kEditorGap;
    int x = kInset;
    degreeLabel.setBounds (x, editorY, kLabelW, kEditorH);
    x += kLabelW + 4;
    degreeCombo.setBounds (x, editorY, kComboW, kEditorH);
    x += kComboW + kColGap;
    octaveLabel.setBounds (x, editorY, kLabelW, kEditorH);
    x += kLabelW + 4;
    octaveCombo.setBounds (x, editorY, kComboW, kEditorH);
}

void ProgressionStepList::recomputeCellRects()
{
    cellRects.assign (steps.size(), {});
    const int startX = kInset;
    const int startY = kInset + kCellTopGap;
    for (int i = 0; i < (int) steps.size(); ++i)
        cellRects[(size_t) i] = { startX + i * kCellPitch, startY, kCellW, kCellH };

    // Append cell sits in the column just past the last step; always shown (at
    // capacity it loses its add arrow and stays a remove-only handle).
    appendCellRect = { startX + (int) steps.size() * kCellPitch, startY, kCellW, kCellH };
}

void ProgressionStepList::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();

    for (int i = 0; i < (int) steps.size(); ++i)
    {
        const auto& step = steps[(size_t) i];
        auto r = cellRects[(size_t) i].toFloat();
        const bool isSel = (i == selected);

        g.setColour (s.widgetBg);
        g.fillRoundedRectangle (r, 5.0f);
        const float outline = isSel ? 2.5f : 1.0f;
        g.setColour (isSel ? s.accent : s.panelBorder);
        g.drawRoundedRectangle (r.reduced (outline * 0.5f), 5.0f, outline);

        // Degree, drawn big and centred (Roman, matching the combo below).
        g.setColour (s.text);
        g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawText (ModuleOptions::degreeNames()[juce::jlimit (
                        0, ModuleOptions::degreeNames().size() - 1, step.degree)],
                    r, juce::Justification::centred);

        // Octave only flagged when it's not the default 0, as a compact
        // top-right tag (the Octave combo below spells it out).
        if (step.octave != 0)
        {
            g.setColour (s.text.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (step.octave > 0 ? "+" + juce::String (step.octave)
                                        : juce::String (step.octave),
                        r.reduced (4.0f, 3.0f), juce::Justification::topRight);
        }
    }

    // Append cell: same square, split into an add half (top) and a remove half
    // (bottom) by a faint divider, each drawn as an arrow.
    {
        auto r = appendCellRect.toFloat();
        g.setColour (s.widgetBg);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (s.panelBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

        g.setColour (s.text.withAlpha (0.4f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 5.0f, r.getCentreY(),
                                            r.getWidth() - 10.0f, 1.0f));

        drawAppendArrows (g, r, s.text,
                          (int) steps.size() < ModuleOptions::kMaxProgSteps,
                          (int) steps.size() > 1);
    }
}

void ProgressionStepList::drawAppendArrows (juce::Graphics& g, juce::Rectangle<float> r,
                                            juce::Colour col, bool addShown, bool delEnabled)
{
    const float half = 7.0f;   // shaft reaches ±half from the arrow centre
    const float head = 5.0f;   // barb length

    auto arrow = [&] (juce::Rectangle<float> zone, bool pointsRight, bool enabled)
    {
        const float cx   = zone.getCentreX();
        const float cy   = zone.getCentreY();
        const float tip  = pointsRight ? cx + half : cx - half;
        const float tail = pointsRight ? cx - half : cx + half;
        const float back = pointsRight ? tip - head : tip + head;

        juce::Path p;
        p.startNewSubPath (tail, cy);
        p.lineTo          (tip,  cy);          // shaft
        p.startNewSubPath (back, cy - head);
        p.lineTo          (tip,  cy);          // upper barb
        p.lineTo          (back, cy + head);   // lower barb

        g.setColour (col.withAlpha (enabled ? 0.9f : 0.3f));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    };

    const float halfH = r.getHeight() * 0.5f;
    if (addShown)
        arrow (r.withHeight (halfH), true, true);            // top: add
    arrow (r.withTrimmedTop (halfH), false, delEnabled);     // bottom: remove
}

void ProgressionStepList::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    for (int i = 0; i < (int) steps.size(); ++i)
        if (cellRects[(size_t) i].contains (p))
        {
            selectStep (i);
            if (onFeedback)
                onFeedback ("Step " + juce::String (i + 1) + ": "
                                + ModuleOptions::degreeNames()[juce::jlimit (
                                      0, ModuleOptions::degreeNames().size() - 1,
                                      steps[(size_t) i].degree)],
                            "prog.step");
            return;
        }

    if (appendCellRect.contains (p))
    {
        if (p.getY() < appendCellRect.getCentreY())
            addStep();
        else
            removeLast();
    }
}
