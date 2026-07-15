#include "CanvasComponent.h"
#include "../UI/ColourScheme.h"

CanvasComponent::CanvasComponent()
{
    setInterceptsMouseClicks (true, true);
}

void CanvasComponent::paint (juce::Graphics& g)
{
    g.setColour (active().windowBg);
    g.fillAll();
}

bool CanvasComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.isInt();
}

void CanvasComponent::itemDropped (const SourceDetails& details)
{
    const auto type = static_cast<ModuleType> (static_cast<int> (details.description));
    addModule (type, details.localPosition);
}

void CanvasComponent::addModule (ModuleType type, juce::Point<int> centrePosition)
{
    auto module = std::make_unique<ModuleComponent> (type);
    module->setCentrePosition (centrePosition);

    module->onOpenSettings = [this] (ModuleComponent& m)
    {
        if (onRequestModuleSettings)
            onRequestModuleSettings (infoFor (m.getType()).name);
    };

    addAndMakeVisible (*module);
    modules.push_back (std::move (module));
}
