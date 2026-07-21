#include "ModuleComponent.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"

ModuleComponent::ModuleComponent (int moduleId, ModuleType t)
    : id (moduleId), type (t)
{
    setSize (kSize, kSize);
}

void ModuleComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void ModuleComponent::setSublabel (const juce::String& text)
{
    if (sublabel != text)
    {
        sublabel = text;
        repaint();
    }
}

void ModuleComponent::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    const auto& d = descriptorFor (type);

    // Inset so the selection ring and drop shadow have room inside the bounds.
    auto body = getLocalBounds().toFloat().reduced (8.0f);
    const auto fill = d.familyColour();

    // Body outline as a path so all three shapes share the shadow / fill /
    // selection-ring code. I/O triangles point toward their single port: MIDI In
    // (a source) right, Output (a sink) left.
    const bool pointsRight = (type == ModuleType::MidiIn);
    auto makeShape = [&] (juce::Rectangle<float> r)
    {
        juce::Path path;
        switch (d.kind)
        {
            case ModuleKind::Modulator:
                path.addEllipse (r);
                break;
            case ModuleKind::IO:
                if (pointsRight)
                    path.addTriangle (r.getTopLeft(), r.getBottomLeft(),
                                      { r.getRight(), r.getCentreY() });
                else
                    path.addTriangle (r.getTopRight(), r.getBottomRight(),
                                      { r.getX(), r.getCentreY() });
                break;
            case ModuleKind::Generator:
            default:
                path.addRoundedRectangle (r, 10.0f);
                break;
        }
        return path;
    };

    // Soft shadow to lift the node off the canvas.
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (makeShape (body.translated (0.0f, 2.0f)));

    // Body.
    auto shape = makeShape (body);
    g.setColour (fill);
    g.fillPath (shape);
    g.setColour (s.panelBorder);
    g.strokePath (shape, juce::PathStrokeType (1.5f));

    // Selection ring.
    if (selected)
    {
        g.setColour (s.accent);
        g.strokePath (makeShape (body.expanded (4.0f)), juce::PathStrokeType (2.5f));
    }

    // Label — dark text reads on all three family fills (green/purple/blue).
    // Triangles narrow away from the vertical centre, so the text bands hug the
    // middle and drawFittedText shrinks anything that would poke past the
    // slanted edges.
    g.setColour (juce::Colours::black.withAlpha (0.82f));
    g.setFont (juce::Font (juce::FontOptions (CurrentLookAndFeel::kUiFontSize,
                                              juce::Font::bold)));
    const auto bodyInt = body.toNearestInt();
    // Inside a triangle only ~2/3 of the row width is usable off-centre;
    // drawFittedText then shrinks the font to keep the text on the shape.
    const int labelInset = d.kind == ModuleKind::IO ? bodyInt.getWidth() / 6 : 0;
    if (sublabel.isEmpty())
    {
        g.drawFittedText (d.name, bodyInt.reduced (labelInset, 0),
                          juce::Justification::centred, 1);
    }
    else
    {
        auto nameArea = bodyInt.withHeight (16).withCentre ({ bodyInt.getCentreX(),
                                                              bodyInt.getCentreY() - 7 });
        g.drawFittedText (d.name, nameArea.reduced (labelInset, 0),
                          juce::Justification::centred, 1);

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        auto subArea = bodyInt.withHeight (14).withCentre ({ bodyInt.getCentreX(),
                                                             bodyInt.getCentreY() + 8 });
        g.drawFittedText (sublabel, subArea.reduced (labelInset, 0),
                          juce::Justification::centred, 1);
    }

    // Ports. Inputs sit on the left edge, outputs on the right; on a triangle
    // that lands exactly on its apex.
    const float portR = 4.0f;
    g.setColour (s.port);
    auto drawPort = [&] (float cx, float cy)
    {
        g.fillEllipse (cx - portR, cy - portR, portR * 2, portR * 2);
        g.setColour (s.panelBorder);
        g.drawEllipse (cx - portR, cy - portR, portR * 2, portR * 2, 1.0f);
        g.setColour (s.port);
    };

    const float midY = body.getCentreY();
    if (moduleHasInputPort (type))
        drawPort (body.getX(), midY);          // input, left
    if (moduleHasOutputPort (type))
        drawPort (body.getRight(), midY);      // output, right
}

juce::Point<float> ModuleComponent::inputPortCentre() const
{
    const auto body = getLocalBounds().toFloat().reduced (8.0f);
    return getPosition().toFloat() + juce::Point<float> (body.getX(), body.getCentreY());
}

juce::Point<float> ModuleComponent::outputPortCentre() const
{
    const auto body = getLocalBounds().toFloat().reduced (8.0f);
    return getPosition().toFloat() + juce::Point<float> (body.getRight(), body.getCentreY());
}

void ModuleComponent::mouseDown (const juce::MouseEvent& e)
{
    // A press on the output port starts the connect gesture, not a node move —
    // the canvas draws the live cable and resolves the drop.
    if (moduleHasOutputPort (type) && onPortDragStart != nullptr)
    {
        const auto body = getLocalBounds().toFloat().reduced (8.0f);
        const juce::Point<float> port (body.getRight(), body.getCentreY());
        if (e.position.getDistanceFrom (port) <= (float) kPortHitRadius)
        {
            draggingCable = true;
            onPortDragStart (*this, e);
            return;
        }
    }

    if (onSelected)
        onSelected (*this);
    dragger.startDraggingComponent (this, e);
}

void ModuleComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingCable)
    {
        if (onPortDrag)
            onPortDrag (e);
        return;
    }

    // Keep the node fully inside the canvas — the huge on-screen amounts force
    // the whole component to stay within its parent's bounds.
    juce::ComponentBoundsConstrainer constrainer;
    constrainer.setMinimumOnscreenAmounts (0xffffff, 0xffffff, 0xffffff, 0xffffff);
    dragger.dragComponent (this, e, getParentComponent() != nullptr ? &constrainer : nullptr);

    if (onMoved)
        onMoved (id, getPosition());
}

void ModuleComponent::mouseUp (const juce::MouseEvent& e)
{
    if (draggingCable)
    {
        draggingCable = false;
        if (onPortDragEnd)
            onPortDragEnd (e);
    }
}

void ModuleComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onOpenSettings)
        onOpenSettings (*this);
}
