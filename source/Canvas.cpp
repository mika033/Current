#include "Canvas.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"

Canvas::Canvas (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor)
    : proc (processor), owner (editor)
{
    setWantsKeyboardFocus (true);
    proc.canvasModelReplaced.addChangeListener (this);
    rebuildFromModel();
}

Canvas::~Canvas()
{
    proc.canvasModelReplaced.removeChangeListener (this);
}

void Canvas::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildFromModel();
    repaint();
}

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

juce::String Canvas::channelSublabel (ModuleType type, int channel)
{
    if (type == ModuleType::MidiIn && channel == 0)
        return "All ch";
    return "Ch " + juce::String (channel);
}

juce::String Canvas::rateSublabel (const ModuleSettings& settings)
{
    return ModuleOptions::rateNames()[settings.rate];
}

juce::StringArray Canvas::choicesWithGlobal (const char* paramID) const
{
    juce::StringArray items { "Global" };
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts().getParameter (paramID)))
        items.addArray (p->choices);
    return items;
}

// --- Shared dialog controls ---------------------------------------------------
// One add/read pair per shared setting, so every module's dialog presents the
// identical control (same key, label, option list, and index mapping).

void Canvas::addRootScaleControls (InlineDialog& dlg, const ModuleSettings& s)
{
    // Combo index 0 = Global = override -1, so index = override + 1.
    dlg.addComboBox ("root",  choicesWithGlobal (ParamIDs::root),  s.rootOverride + 1,  "Root");
    dlg.addComboBox ("scale", choicesWithGlobal (ParamIDs::scale), s.scaleOverride + 1, "Scale");
}

void Canvas::readRootScaleControls (const InlineDialog& dlg, ModuleSettings& s)
{
    s.rootOverride  = dlg.getComboBoxSelectedIndex ("root") - 1;
    s.scaleOverride = dlg.getComboBoxSelectedIndex ("scale") - 1;
}

void Canvas::addRateControl (InlineDialog& dlg, const ModuleSettings& s, int firstRate)
{
    // `firstRate` trims the head of the shared rate list (the Scale generator
    // starts at 1/16), so the combo index is the shared index minus the offset.
    juce::StringArray rates;
    for (int i = firstRate; i < ModuleOptions::rateNames().size(); ++i)
        rates.add (ModuleOptions::rateNames()[i]);
    dlg.addComboBox ("rate", rates, s.rate - firstRate, "Rate");
}

void Canvas::readRateControl (const InlineDialog& dlg, ModuleSettings& s, int firstRate)
{
    s.rate = dlg.getComboBoxSelectedIndex ("rate") + firstRate;
}

void Canvas::addRepeatControl (InlineDialog& dlg, const ModuleSettings& s)
{
    dlg.addComboBox ("repeat", ModuleOptions::repeatNames(), s.repeat, "Repeat");
}

void Canvas::readRepeatControl (const InlineDialog& dlg, ModuleSettings& s)
{
    s.repeat = dlg.getComboBoxSelectedIndex ("repeat");
}

void Canvas::addModeControl (InlineDialog& dlg, const ModuleSettings& s, int modeCount)
{
    // `modeCount` trims the tail of the shared mode list (the Scale generator
    // offers Up/Down only).
    juce::StringArray modes;
    for (int i = 0; i < modeCount && i < ModuleOptions::modeNames().size(); ++i)
        modes.add (ModuleOptions::modeNames()[i]);
    dlg.addComboBox ("mode", modes, juce::jlimit (0, modeCount - 1, s.mode), "Mode");
}

void Canvas::readModeControl (const InlineDialog& dlg, ModuleSettings& s)
{
    s.mode = dlg.getComboBoxSelectedIndex ("mode");
}

void Canvas::addOctavesControl (InlineDialog& dlg, const ModuleSettings& s)
{
    dlg.addComboBox ("octaves", { "1", "2", "3", "4" }, s.octaves - 1, "Octaves");
}

void Canvas::readOctavesControl (const InlineDialog& dlg, ModuleSettings& s)
{
    s.octaves = dlg.getComboBoxSelectedIndex ("octaves") + 1;
}

void Canvas::addNodeComponent (const ModuleInstance& instance)
{
    auto node = std::make_unique<ModuleComponent> (instance.id, instance.type);
    node->setTopLeftPosition ((int) instance.x, (int) instance.y);

    if (instance.type == ModuleType::MidiIn || instance.type == ModuleType::Output)
        node->setSublabel (channelSublabel (instance.type, instance.channel));
    else if (instance.type == ModuleType::Random || instance.type == ModuleType::ScaleGen
             || instance.type == ModuleType::Arp)
        node->setSublabel (rateSublabel (instance.settings));

    node->onSelected = [this] (ModuleComponent& n) { selectNode (&n); };

    node->onMoved = [this] (int id, juce::Point<int> pos)
    {
        proc.moveModule (id, (float) pos.getX(), (float) pos.getY());
    };

    node->onOpenSettings = [this] (ModuleComponent& n)
    {
        if (n.moduleType() == ModuleType::MidiIn || n.moduleType() == ModuleType::Output)
        {
            openChannelDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Arp)
        {
            openArpDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Random)
        {
            openRandomDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::ScaleGen)
        {
            openScaleGenDialog (n);
            return;
        }

        // Other modules: an empty placeholder where their settings will live.
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

void Canvas::openChannelDialog (ModuleComponent& node)
{
    const auto type = node.moduleType();
    const bool isIn = (type == ModuleType::MidiIn);
    const int  id   = node.moduleId();
    const int  chan = proc.getModuleChannel (id);

    // MIDI In offers "All" (stored as channel 0) ahead of 1..16; Output must
    // pick a concrete channel, so its combo index is channel - 1.
    juce::StringArray items;
    if (isIn)
        items.add ("All");
    for (int ch = 1; ch <= 16; ++ch)
        items.add (juce::String (ch));

    auto* dlg = owner.showInlineDialog (juce::String (descriptorFor (type).name) + " settings");
    dlg->addComboBox ("channel", items, isIn ? chan : chan - 1,
                      isIn ? "Input channel" : "Output channel");
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id, isIn] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            const int idx = d->getComboBoxSelectedIndex ("channel");
            if (idx >= 0)
            {
                const int newChannel = isIn ? idx : idx + 1;
                proc.setModuleChannel (id, newChannel);
                for (auto& n : nodes)
                    if (n->moduleId() == id)
                        n->setSublabel (channelSublabel (n->moduleType(), newChannel));
            }
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openArpDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    auto* dlg = owner.showInlineDialog ("Arp settings");
    addModeControl (*dlg, s, ModuleOptions::modeNames().size());
    addRateControl (*dlg, s);
    addOctavesControl (*dlg, s);
    dlg->addComboBox ("gate", ModuleOptions::gateNames(), s.gate, "Gate");
    addRepeatControl (*dlg, s);
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readModeControl (*d, ns);
            readRateControl (*d, ns);
            readOctavesControl (*d, ns);
            ns.gate = d->getComboBoxSelectedIndex ("gate");
            readRepeatControl (*d, ns);
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (rateSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openRandomDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    juce::StringArray noteNames;
    for (int n = 0; n <= 127; ++n)
        noteNames.add (ModuleOptions::midiNoteName (n));

    auto* dlg = owner.showInlineDialog ("Random settings");
    addRootScaleControls (*dlg, s);
    addRateControl (*dlg, s);
    dlg->addComboBox ("from", noteNames, s.rangeFrom, "Range from");
    dlg->addComboBox ("to",   noteNames, s.rangeTo,   "Range to");
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns);
            readRateControl (*d, ns);
            ns.rangeFrom = d->getComboBoxSelectedIndex ("from");
            ns.rangeTo   = d->getComboBoxSelectedIndex ("to");
            // A backwards range is a slip, not an intent — normalise it.
            if (ns.rangeFrom > ns.rangeTo)
                std::swap (ns.rangeFrom, ns.rangeTo);
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (rateSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openScaleGenDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    auto* dlg = owner.showInlineDialog ("Scale settings");
    addRootScaleControls (*dlg, s);
    addModeControl (*dlg, s, ModuleOptions::kScaleModeCount);
    addOctavesControl (*dlg, s);
    dlg->addComboBox ("endOn", { "Root (octave)", "7th (last scale note)" },
                      s.endOnRoot ? 0 : 1, "End on");
    addRateControl (*dlg, s, ModuleOptions::kScaleRateFirst);
    addRepeatControl (*dlg, s);
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns);
            readModeControl (*d, ns);
            readOctavesControl (*d, ns);
            ns.endOnRoot = d->getComboBoxSelectedIndex ("endOn") == 0;
            readRateControl (*d, ns, ModuleOptions::kScaleRateFirst);
            readRepeatControl (*d, ns);
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (rateSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
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
    // Phase 2 convenience so placed nodes aren't permanent while testing the
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

    // Re-read the instance from the model rather than reconstructing it here:
    // addModule computes per-type defaults (I/O channel, generator settings)
    // that the node's sublabel must reflect.
    for (const auto& m : proc.modules())
        if (m.id == id)
            addNodeComponent (m);
    selectNode (nodes.back().get());

    repaint();   // clear the empty-state hint
}
