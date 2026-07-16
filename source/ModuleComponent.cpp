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

void ModuleComponent::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    const auto& d = descriptorFor (type);

    // Inset so the selection ring and drop shadow have room inside the bounds.
    auto body = getLocalBounds().toFloat().reduced (8.0f);
    const auto fill    = d.familyColour();
    const bool isCircle = (d.kind == ModuleKind::Modulator);

    // Soft shadow to lift the node off the canvas.
    {
        juce::Path shadow;
        auto sb = body.translated (0.0f, 2.0f);
        if (isCircle) shadow.addEllipse (sb);
        else          shadow.addRoundedRectangle (sb, 10.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillPath (shadow);
    }

    // Body.
    if (isCircle)
    {
        g.setColour (fill);
        g.fillEllipse (body);
        g.setColour (s.panelBorder);
        g.drawEllipse (body, 1.5f);
    }
    else
    {
        g.setColour (fill);
        g.fillRoundedRectangle (body, 10.0f);
        g.setColour (s.panelBorder);
        g.drawRoundedRectangle (body, 10.0f, 1.5f);
    }

    // Selection ring.
    if (selected)
    {
        g.setColour (s.accent);
        auto ring = body.expanded (4.0f);
        if (isCircle) g.drawEllipse (ring, 2.5f);
        else          g.drawRoundedRectangle (ring, 12.0f, 2.5f);
    }

    // Label — dark text reads on all three family fills (green/purple/blue).
    g.setColour (juce::Colours::black.withAlpha (0.82f));
    g.setFont (juce::Font (juce::FontOptions (CurrentLookAndFeel::kUiFontSize,
                                              juce::Font::bold)));
    g.drawText (d.name, body.toNearestInt(), juce::Justification::centred);

    // Ports (decorative in Phase 1). Generators expose an output on the right;
    // modulators an input on the left and an output on the right.
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
    if (d.kind == ModuleKind::Modulator || d.kind == ModuleKind::IO)
        drawPort (body.getX(), midY);          // input, left
    if (d.kind == ModuleKind::Generator || d.kind == ModuleKind::Modulator)
        drawPort (body.getRight(), midY);      // output, right
}

void ModuleComponent::mouseDown (const juce::MouseEvent& e)
{
    if (onSelected)
        onSelected (*this);
    dragger.startDraggingComponent (this, e);
}

void ModuleComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Keep the node fully inside the canvas — the huge on-screen amounts force
    // the whole component to stay within its parent's bounds.
    juce::ComponentBoundsConstrainer constrainer;
    constrainer.setMinimumOnscreenAmounts (0xffffff, 0xffffff, 0xffffff, 0xffffff);
    dragger.dragComponent (this, e, getParentComponent() != nullptr ? &constrainer : nullptr);

    if (onMoved)
        onMoved (id, getPosition());
}

void ModuleComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onOpenSettings)
        onOpenSettings (*this);
}
