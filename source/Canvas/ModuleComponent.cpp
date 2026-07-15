#include "ModuleComponent.h"
#include "../UI/ColourScheme.h"
#include "../UI/PluginLookAndFeel.h"

void paintModuleShape (juce::Graphics& g, juce::Rectangle<float> bounds, ModuleType type)
{
    const auto& cs = active();
    const auto& info = infoFor (type);

    g.setColour (info.kind == ModuleKind::generator ? cs.generatorFill : cs.modulatorFill);

    if (info.kind == ModuleKind::generator)
        g.fillRoundedRectangle (bounds, 6.0f);
    else
        g.fillEllipse (bounds);

    g.setColour (cs.widgetOutline);
    if (info.kind == ModuleKind::generator)
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.5f);
    else
        g.drawEllipse (bounds.reduced (0.5f), 1.5f);

    g.setColour (cs.text);
    g.setFont (juce::Font (juce::FontOptions (kUiFontSize)));
    g.drawFittedText (info.name, bounds.reduced (6.0f).toNearestInt(), juce::Justification::centred, 2);
}

ModuleComponent::ModuleComponent (ModuleType type)
    : moduleType (type)
{
    setSize (kModuleBoxSize, kModuleBoxSize);
}

void ModuleComponent::paint (juce::Graphics& g)
{
    paintModuleShape (g, getLocalBounds().toFloat(), moduleType);
}

void ModuleComponent::mouseDown (const juce::MouseEvent& e)
{
    dragger.startDraggingComponent (this, e);

    // Keeps the whole module box on-canvas: the constrainer requires
    // getHeight()/getWidth() worth of the component to stay visible on
    // every edge, which for a box that size means "fully inside the parent".
    constrainer.setMinimumOnscreenAmounts (getHeight(), getWidth(), getHeight(), getWidth());
}

void ModuleComponent::mouseDrag (const juce::MouseEvent& e)
{
    dragger.dragComponent (this, e, &constrainer);
}

void ModuleComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onOpenSettings)
        onOpenSettings (*this);
}
