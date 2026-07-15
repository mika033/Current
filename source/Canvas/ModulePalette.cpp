#include "ModulePalette.h"
#include "../UI/ColourScheme.h"

ModulePalette::Chip::Chip (ModuleType type)
    : moduleType (type)
{
    setSize (kModuleBoxSize, kModuleBoxSize);
}

void ModulePalette::Chip::paint (juce::Graphics& g)
{
    paintModuleShape (g, getLocalBounds().toFloat(), moduleType);
}

void ModulePalette::Chip::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
            container->startDragging (juce::var (static_cast<int> (moduleType)), this);

    juce::ignoreUnused (e);
}

ModulePalette::ModulePalette()
{
    for (size_t i = 0; i < chips.size(); ++i)
    {
        chips[i] = std::make_unique<Chip> (paletteModuleTypes()[i]);
        addAndMakeVisible (*chips[i]);
    }
}

void ModulePalette::paint (juce::Graphics& g)
{
    g.setColour (active().windowBg);
    g.fillAll();
}

void ModulePalette::resized()
{
    const int spacing = 24;
    const int totalW = (int) chips.size() * kModuleBoxSize + ((int) chips.size() - 1) * spacing;
    int x = (getWidth() - totalW) / 2;
    const int y = (getHeight() - kModuleBoxSize) / 2;

    for (auto& chip : chips)
    {
        chip->setTopLeftPosition (x, y);
        x += kModuleBoxSize + spacing;
    }
}
