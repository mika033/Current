#include "Canvas.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"

Canvas::Canvas (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor)
    : proc (processor), owner (editor)
{
    setWantsKeyboardFocus (true);
    rebuildFromModel();
}

Canvas::~Canvas() = default;

juce::var Canvas::makeDragDescription (ModuleType type)
{
    // A tagged string keeps the description self-describing and cheap to match
    // in isInterestedInDragSource without a shared enum cast through var.
    return juce::var ("module:" + moduleTypeToString (type));
}

void Canvas::rebuildFromModel()
{
    nodes.clear();
    selectedNode = nullptr;
    for (const auto& m : proc.modules())
        addNodeComponent (m);
}

void Canvas::addNodeComponent (const ModuleInstance& instance)
{
    auto node = std::make_unique<ModuleComponent> (instance.id, instance.type);
    node->setTopLeftPosition ((int) instance.x, (int) instance.y);

    node->onSelected = [this] (ModuleComponent& n) { selectNode (&n); };

    node->onMoved = [this] (int id, juce::Point<int> pos)
    {
        proc.moveModule (id, (float) pos.getX(), (float) pos.getY());
    };

    node->onOpenSettings = [this] (ModuleComponent& n)
    {
        // Phase 1: an empty placeholder where the module's settings will live.
        auto* dlg = owner.showInlineDialog (juce::String (descriptorFor (n.moduleType()).name)
                                            + " settings",
                                            "Settings for this module will appear here in a later phase.");
        dlg->addButton ("Close", 0);
        dlg->onResult = [] (int, InlineDialog* d)
        {
            d->getParentComponent()->removeChildComponent (d);
            delete d;
        };
    };

    addAndMakeVisible (node.get());
    nodes.push_back (std::move (node));
}

void Canvas::selectNode (ModuleComponent* node)
{
    if (selectedNode == node)
        return;

    if (selectedNode != nullptr)
        selectedNode->setSelected (false);

    selectedNode = node;

    if (selectedNode != nullptr)
        selectedNode->setSelected (true);
}

void Canvas::deleteSelected()
{
    // Phase 1 convenience so placed nodes aren't permanent while testing the
    // canvas. Removes from both the view and the model.
    if (selectedNode == nullptr)
        return;

    const int id = selectedNode->moduleId();

    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        if (it->get() == selectedNode)
        {
            nodes.erase (it);
            break;
        }
    }
    selectedNode = nullptr;

    proc.removeModule (id);
}

void Canvas::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    auto b = getLocalBounds().toFloat();

    g.setColour (s.canvasBg);
    g.fillRoundedRectangle (b, 6.0f);

    // Faint alignment grid.
    g.setColour (s.canvasGrid);
    constexpr int step = 32;
    for (int x = step; x < getWidth(); x += step)
        g.drawVerticalLine (x, 0.0f, (float) getHeight());
    for (int y = step; y < getHeight(); y += step)
        g.drawHorizontalLine (y, 0.0f, (float) getWidth());

    // Drop hint while a palette item hovers.
    g.setColour (dragHovering ? s.accent : s.panelBorder);
    g.drawRoundedRectangle (b.reduced (0.75f), 6.0f, dragHovering ? 2.5f : 1.0f);

    // Empty-state hint.
    if (nodes.empty() && ! dragHovering)
    {
        g.setColour (s.text.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawText ("Drag a module here from the palette below",
                    getLocalBounds(), juce::Justification::centred);
    }
}

void Canvas::resized() {}

void Canvas::mouseDown (const juce::MouseEvent&)
{
    // Click on empty canvas clears selection and takes focus (so Delete works).
    selectNode (nullptr);
    grabKeyboardFocus();
}

bool Canvas::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    return false;
}

// --- Drag & drop ------------------------------------------------------------

bool Canvas::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith ("module:");
}

void Canvas::itemDragEnter (const SourceDetails&)
{
    dragHovering = true;
    repaint();
}

void Canvas::itemDragExit (const SourceDetails&)
{
    dragHovering = false;
    repaint();
}

void Canvas::itemDropped (const SourceDetails& details)
{
    dragHovering = false;

    const auto typeStr = details.description.toString().fromFirstOccurrenceOf ("module:", false, false);
    const auto type    = moduleTypeFromString (typeStr);

    // Centre the new node under the drop point, clamped fully inside the canvas.
    auto drop = details.localPosition;   // already in Canvas coordinates
    int x = drop.getX() - ModuleComponent::kSize / 2;
    int y = drop.getY() - ModuleComponent::kSize / 2;
    x = juce::jlimit (0, juce::jmax (0, getWidth()  - ModuleComponent::kSize), x);
    y = juce::jlimit (0, juce::jmax (0, getHeight() - ModuleComponent::kSize), y);

    const int id = proc.addModule (type, (float) x, (float) y);

    ModuleInstance inst;
    inst.id = id;
    inst.type = type;
    inst.x = (float) x;
    inst.y = (float) y;
    addNodeComponent (inst);
    selectNode (nodes.back().get());

    repaint();   // clear the empty-state hint
}
