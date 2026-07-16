#include "PaletteBar.h"
#include "Canvas.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"

// --- PaletteItem ------------------------------------------------------------

PaletteBar::PaletteItem::PaletteItem (ModuleType t) : type (t) {}

void PaletteBar::PaletteItem::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    const auto& d = descriptorFor (type);

    // A compact glyph (shape + colour) above the module name, so the chip reads
    // as "this is what lands on the canvas".
    auto area = getLocalBounds().reduced (6);
    auto glyph = area.removeFromTop (area.getHeight() - 20).toFloat();
    const float sz = (float) juce::jmin (glyph.getWidth(), glyph.getHeight());
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
    g.drawText (d.name, area, juce::Justification::centred);
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
{
    for (const auto& d : moduleCatalogue())
    {
        auto item = std::make_unique<PaletteItem> (d.type);
        addAndMakeVisible (item.get());
        items.push_back (std::move (item));
    }
}

PaletteBar::~PaletteBar() = default;

void PaletteBar::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    auto b = getLocalBounds().toFloat();
    g.setColour (s.panelBg);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (s.panelBorder);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
}

void PaletteBar::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    const int n = (int) items.size();
    if (n == 0)
        return;

    constexpr int itemW = 84;
    constexpr int gap   = 12;

    int x = area.getX();
    for (auto& item : items)
    {
        item->setBounds (x, area.getY(), itemW, area.getHeight());
        x += itemW + gap;
    }
}
