#include "PaletteBar.h"
#include "Canvas.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"

namespace
{
    // Fixed chip size — chips never shrink; the strip scrolls instead.
    constexpr int kChipW      = 84;
    constexpr int kChipGap    = 12;
    constexpr int kFilterRowH = 20;
    constexpr int kFilterGap  = 4;    // between the filter row and the strip
    constexpr int kScrollBarH = 8;
}

// --- PaletteItem ------------------------------------------------------------

PaletteBar::PaletteItem::PaletteItem (PaletteBar& owner, ModuleType t)
    : bar (owner), type (t) {}

void PaletteBar::PaletteItem::mouseDown (const juce::MouseEvent&)
{
    // Pressing a chip shows the module's one-line description — the palette
    // doubles as the module manual (the canvas node's selection message fires
    // the same key).
    if (bar.onFeedback)
        bar.onFeedback (descriptorFor (type).name, moduleHelpKey (type));
}

void PaletteBar::PaletteItem::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    const auto& d = descriptorFor (type);

    // A compact glyph (shape + colour) above the module name, so the chip reads
    // as "this is what lands on the canvas".
    auto area = getLocalBounds().reduced (6);
    auto glyph = area.removeFromTop (area.getHeight() - 20).toFloat();
    // 80%: full-bleed glyphs read too heavy now that chips never shrink.
    const float sz = 0.8f * (float) juce::jmin (glyph.getWidth(), glyph.getHeight());
    auto shape = juce::Rectangle<float> (sz, sz).withCentre (glyph.getCentre());

    // Same shape language as the canvas nodes: square / circle / triangle
    // (MIDI In points right, Output left).
    juce::Path glyphPath;
    switch (d.kind)
    {
        case ModuleKind::Modulator:
            glyphPath.addEllipse (shape);
            break;
        case ModuleKind::IO:
            if (type == ModuleType::MidiIn)
                glyphPath.addTriangle (shape.getTopLeft(), shape.getBottomLeft(),
                                       { shape.getRight(), shape.getCentreY() });
            else
                glyphPath.addTriangle (shape.getTopRight(), shape.getBottomRight(),
                                       { shape.getX(), shape.getCentreY() });
            break;
        case ModuleKind::Generator:
        default:
            glyphPath.addRoundedRectangle (shape, 6.0f);
            break;
    }

    g.setColour (d.familyColour());
    g.fillPath (glyphPath);
    g.setColour (s.panelBorder);
    g.strokePath (glyphPath, juce::PathStrokeType (1.2f));

    g.setColour (s.text);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (d.shortName, area, juce::Justification::centred);
}

void PaletteBar::PaletteItem::mouseDrag (const juce::MouseEvent&)
{
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (! container->isDragAndDropActive())
        {
            // Drag a snapshot of the chip so there's a visible ghost following
            // the cursor onto the canvas.
            auto image = createComponentSnapshot (getLocalBounds());
            container->startDragging (Canvas::makeDragDescription (type), this,
                                      juce::ScaledImage (image), true);
        }
    }
}

// --- PaletteBar -------------------------------------------------------------

PaletteBar::PaletteBar()
    : ioFilter ("In/Out"), genFilter ("Generators"), modFilter ("Modulators")
{
    viewport.setViewedComponent (&strip, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (kScrollBarH);
    // Chip drags must always start the module drag, never a strip pan — the
    // scrollbar is the (touch-reachable) scroll affordance.
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible (viewport);

    for (auto* t : { &ioFilter, &genFilter, &modFilter })
    {
        t->setToggleState (true, juce::dontSendNotification);
        t->onClick = [this, t]
        {
            rebuildStrip();
            if (onFeedback)
                onFeedback (t->getButtonText()
                                + (t->getToggleState() ? ": shown" : ": hidden"),
                            "action.filter");
        };
        addAndMakeVisible (*t);
    }

    for (const auto& d : moduleCatalogue())
    {
        auto item = std::make_unique<PaletteItem> (*this, d.type);
        strip.addAndMakeVisible (item.get());
        items.push_back (std::move (item));
    }
}

PaletteBar::~PaletteBar() = default;

void PaletteBar::setRemoveDragState (bool armed, bool hot)
{
    if (removeArmed == armed && removeHot == hot)
        return;
    removeArmed = armed;
    removeHot   = hot;
    repaint();
}

bool PaletteBar::kindShown (ModuleKind kind) const
{
    switch (kind)
    {
        case ModuleKind::IO:        return ioFilter.getToggleState();
        case ModuleKind::Generator: return genFilter.getToggleState();
        case ModuleKind::Modulator: return modFilter.getToggleState();
    }
    return true;
}

void PaletteBar::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    auto b = getLocalBounds().toFloat();
    g.setColour (s.panelBg);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (s.panelBorder);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
}

void PaletteBar::paintOverChildren (juce::Graphics& g)
{
    if (! removeArmed && ! removeHot)
        return;

    const auto& s = CurrentTheme::active();
    auto b = getLocalBounds().toFloat();

    if (removeHot)
    {
        // Pointer over the tray mid-drag: releasing deletes, so shout it.
        g.setColour (s.danger.withAlpha (0.85f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText ("Release to remove", getLocalBounds(), juce::Justification::centred);
    }
    else
    {
        // A node drag is in flight elsewhere: a quiet cue that the tray is a
        // drop target for removal.
        g.setColour (s.danger.withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (1.0f), 6.0f, 1.5f);
    }
}

void PaletteBar::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    auto filterRow = area.removeFromTop (kFilterRowH);
    ioFilter .setBounds (filterRow.removeFromLeft (70));
    filterRow.removeFromLeft (8);
    genFilter.setBounds (filterRow.removeFromLeft (105));
    filterRow.removeFromLeft (8);
    modFilter.setBounds (filterRow.removeFromLeft (110));

    area.removeFromTop (kFilterGap);
    viewport.setBounds (area);
    rebuildStrip();
}

void PaletteBar::rebuildStrip()
{
    // Chip height leaves the scrollbar's band free even when it isn't shown,
    // so toggling filters never makes the chips jump.
    const int chipH = juce::jmax (40, viewport.getHeight() - kScrollBarH);

    int x = 0;
    for (auto& item : items)
    {
        const bool shown = kindShown (item->kind());
        item->setVisible (shown);
        if (! shown)
            continue;
        item->setBounds (x, 0, kChipW, chipH);
        x += kChipW + kChipGap;
    }

    strip.setSize (juce::jmax (0, x - kChipGap), chipH);
}
