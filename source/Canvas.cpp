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

juce::String Canvas::shiftSublabel (const ModuleSettings& settings)
{
    // Signed amount, explicit "+" so direction reads at a glance.
    const auto n = juce::String (settings.shiftAmount);
    return settings.shiftAmount > 0 ? "+" + n : n;
}

juce::String Canvas::scaleModSublabel (const ModuleSettings& settings) const
{
    // The scale it snaps to — the module's one meaningful fact at a glance
    // (Off included, since the Scale modulator can pass notes through un-snapped).
    const auto scales = scaleChoices (true);
    return scales[juce::jlimit (0, scales.size() - 1,
                                scaleIndexForOverride (settings.scaleOverride, true))];
}

juce::String Canvas::progressionSublabel (const ModuleSettings& settings)
{
    // Short progressions read whole ("I-IV-V-I"); longer ones summarise.
    if ((int) settings.progSteps.size() > 4)
        return juce::String (settings.progSteps.size()) + " steps";
    juce::StringArray parts;
    for (const auto& s : settings.progSteps)
        parts.add (ModuleOptions::degreeNames()[juce::jlimit (
            0, ModuleOptions::degreeNames().size() - 1, s.degree)]);
    return parts.joinIntoString ("-");
}

juce::String Canvas::chordSublabel (const ModuleSettings& settings)
{
    // The chord itself, e.g. "V 7th" — degree and stacking at a glance.
    return ModuleOptions::degreeNames()[juce::jlimit (
               0, ModuleOptions::degreeNames().size() - 1, settings.chordDegree)]
         + " "
         + ModuleOptions::chordTypeNames()[juce::jlimit (
               0, ModuleOptions::chordTypeNames().size() - 1, settings.chordType)];
}

juce::String Canvas::droneSublabel (const ModuleSettings& settings)
{
    return ModuleOptions::droneVoicingNames()[juce::jlimit (
        0, ModuleOptions::droneVoicingNames().size() - 1, settings.droneVoicing)];
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

juce::StringArray Canvas::scaleChoices (bool offersOff) const
{
    // "Global" [+ "Off"] + the named scales, so every module's scale combo is
    // built from one list. Off (chromatic) is inserted right after Global.
    auto items = choicesWithGlobal (ParamIDs::scale);
    if (offersOff)
        items.insert (1, "Off");
    return items;
}

int Canvas::scaleIndexForOverride (int scaleOverride, bool offersOff)
{
    // Global (-1) -> 0. With Off offered: Off (-2) -> 1, named k -> k + 2.
    // Without: named k -> k + 1.
    if (scaleOverride == ModuleOptions::kScaleGlobal) return 0;
    if (offersOff)
        return scaleOverride == ModuleOptions::kScaleOff ? 1 : scaleOverride + 2;
    return scaleOverride + 1;
}

int Canvas::scaleOverrideForIndex (int comboIndex, bool offersOff)
{
    // Inverse of scaleIndexForOverride.
    if (comboIndex <= 0) return ModuleOptions::kScaleGlobal;
    if (offersOff)
        return comboIndex == 1 ? ModuleOptions::kScaleOff : comboIndex - 2;
    return comboIndex - 1;
}

void Canvas::addRootScaleControls (InlineDialog& dlg, const ModuleSettings& s, bool scaleOffersOff)
{
    // Root combo index 0 = Global = override -1, so index = override + 1 (no
    // Off — a "no root" has no meaning). Scale runs through the shared mapping.
    dlg.addComboBox ("root",  choicesWithGlobal (ParamIDs::root), s.rootOverride + 1, "Root");
    dlg.addComboBox ("scale", scaleChoices (scaleOffersOff),
                     scaleIndexForOverride (s.scaleOverride, scaleOffersOff), "Scale");
}

void Canvas::readRootScaleControls (const InlineDialog& dlg, ModuleSettings& s, bool scaleOffersOff) const
{
    s.rootOverride  = dlg.getComboBoxSelectedIndex ("root") - 1;
    s.scaleOverride = scaleOverrideForIndex (dlg.getComboBoxSelectedIndex ("scale"), scaleOffersOff);
}

void Canvas::addRateControl (InlineDialog& dlg, const ModuleSettings& s)
{
    // One canonical rate list (1/32..1/1) everywhere — no module trims it.
    dlg.addComboBox ("rate", ModuleOptions::rateNames(), s.rate, "Rate");
}

void Canvas::readRateControl (const InlineDialog& dlg, ModuleSettings& s)
{
    s.rate = dlg.getComboBoxSelectedIndex ("rate");
}

void Canvas::addGateControl (InlineDialog& dlg, const ModuleSettings& s)
{
    dlg.addComboBox ("gate", ModuleOptions::gateNames(), s.gate, "Gate");
}

void Canvas::readGateControl (const InlineDialog& dlg, ModuleSettings& s)
{
    s.gate = dlg.getComboBoxSelectedIndex ("gate");
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

void Canvas::addHoldControls (InlineDialog& dlg, const ModuleSettings& s)
{
    // The Chord/Drone pacing pair. Length (a finite bar length) is how long the
    // chord/note sounds; Repeat (the shared Repeat list, so it can be Endless)
    // is how often it restarts. Both move in bars — deliberately not the shared
    // note-length rate. Length >= Repeat plays legato back-to-back.
    dlg.addComboBox ("holdLength", ModuleOptions::barLengthNames(), s.holdLength, "Length");
    dlg.addComboBox ("holdRepeat", ModuleOptions::repeatNames(),    s.holdRepeat, "Repeat");
}

void Canvas::readHoldControls (const InlineDialog& dlg, ModuleSettings& s)
{
    s.holdLength = dlg.getComboBoxSelectedIndex ("holdLength");
    s.holdRepeat = dlg.getComboBoxSelectedIndex ("holdRepeat");
}

void Canvas::addNodeComponent (const ModuleInstance& instance)
{
    auto node = std::make_unique<ModuleComponent> (instance.id, instance.type);
    node->setTopLeftPosition ((int) instance.x, (int) instance.y);

    if (instance.type == ModuleType::MidiIn || instance.type == ModuleType::Output)
        node->setSublabel (channelSublabel (instance.type, instance.channel));
    else if (instance.type == ModuleType::Random || instance.type == ModuleType::ScaleGen
             || instance.type == ModuleType::Arp || instance.type == ModuleType::Lfo
             || instance.type == ModuleType::Delay || instance.type == ModuleType::Quantize)
        node->setSublabel (rateSublabel (instance.settings));
    else if (instance.type == ModuleType::Shift)
        node->setSublabel (shiftSublabel (instance.settings));
    else if (instance.type == ModuleType::Chord)
        node->setSublabel (chordSublabel (instance.settings));
    else if (instance.type == ModuleType::Drone)
        node->setSublabel (droneSublabel (instance.settings));
    else if (instance.type == ModuleType::ScaleMod)
        node->setSublabel (scaleModSublabel (instance.settings));
    else if (instance.type == ModuleType::Progression)
        node->setSublabel (progressionSublabel (instance.settings));

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
        if (n.moduleType() == ModuleType::Lfo)
        {
            openLfoDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Quantize)
        {
            openQuantizeDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::ScaleMod)
        {
            openScaleModDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Progression)
        {
            openProgressionDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Shift)
        {
            openShiftDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Delay)
        {
            openDelayDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Chord)
        {
            openChordDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Drone)
        {
            openDroneDialog (n);
            return;
        }

        // Fallback for future module types that don't have a dialog yet.
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
    addGateControl (*dlg, s);
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
            readGateControl (*d, ns);
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

    // First module on the redesigned window: a thin menu bar (Root / Scale /
    // Rate) over a 3x2 grid. Random maps pitch, so it offers the Off (chromatic)
    // scale choice; Gate joins the range notes as a dial (every note-emitting
    // Rate module carries one).
    auto* win = owner.showModuleWindow ("Random");

    // Menu bar. Root index = override + 1 (Global at 0); scale runs through the
    // shared override<->index mapping so Off lands identically to the dialogs.
    win->setMenuCombo (0, "root",  choicesWithGlobal (ParamIDs::root), s.rootOverride + 1, "Root");
    win->setMenuCombo (1, "scale", scaleChoices (true),
                       scaleIndexForOverride (s.scaleOverride, true), "Scale");
    win->setMenuCombo (2, "rate",  ModuleOptions::rateNames(), s.rate, "Rate");

    // Range as dials over the MIDI note span (0..127); the label carries a live
    // note-name readout ("From: C1") since a dial has no text box. Gate reads
    // its percentage back the same way.
    auto noteText = [] (double v) { return ModuleOptions::midiNoteName (juce::roundToInt (v)); };
    auto gateText = [] (double v)
    {
        return ModuleOptions::gateNames()[juce::jlimit (0, ModuleOptions::gateNames().size() - 1,
                                                        juce::roundToInt (v))];
    };
    win->setGridDial (0, "from", 0.0, 127.0, 1.0, s.rangeFrom, "From", noteText);
    win->setGridDial (1, "to",   0.0, 127.0, 1.0, s.rangeTo,   "To",   noteText);
    win->setGridDial (2, "gate", 0.0, (double) (ModuleOptions::gateNames().size() - 1), 1.0,
                      s.gate, "Gate", gateText);

    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the window is up (host state restore), so it is re-looked-up on OK.
    win->onResult = [this, id] (int result, ModuleWindow* w)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            ns.rootOverride  = w->getComboSelectedIndex ("root") - 1;
            ns.scaleOverride = scaleOverrideForIndex (w->getComboSelectedIndex ("scale"), true);
            ns.rate          = w->getComboSelectedIndex ("rate");
            ns.gate          = juce::roundToInt (w->getDialValue ("gate"));
            ns.rangeFrom     = juce::roundToInt (w->getDialValue ("from"));
            ns.rangeTo       = juce::roundToInt (w->getDialValue ("to"));
            // A backwards range is a slip, not an intent — normalise it.
            if (ns.rangeFrom > ns.rangeTo)
                std::swap (ns.rangeFrom, ns.rangeTo);
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (rateSublabel (ns));
        }
        w->getParentComponent()->removeChildComponent (w);
        delete w;
    };
}

void Canvas::openScaleGenDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A scale-walking generator, so no Off (it needs a scale to walk); one
    // canonical Rate list; Gate like every note-emitting Rate module.
    auto* dlg = owner.showInlineDialog ("Scale settings");
    addRootScaleControls (*dlg, s, false);
    addModeControl (*dlg, s, ModuleOptions::kScaleModeCount);
    addOctavesControl (*dlg, s);
    dlg->addComboBox ("endOn", { "Root (octave)", "7th (last scale note)" },
                      s.endOnRoot ? 0 : 1, "End on");
    addRateControl (*dlg, s);
    addGateControl (*dlg, s);
    addRepeatControl (*dlg, s);
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, false);
            readModeControl (*d, ns);
            readOctavesControl (*d, ns);
            ns.endOnRoot = d->getComboBoxSelectedIndex ("endOn") == 0;
            readRateControl (*d, ns);
            readGateControl (*d, ns);
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

void Canvas::openLfoDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // The LFO maps pitch, so it offers Off (chromatic mapping), and it emits
    // notes on a Rate grid, so it carries a Gate.
    auto* dlg = owner.showInlineDialog ("LFO settings");
    addRootScaleControls (*dlg, s, true);
    dlg->addComboBox ("shape", ModuleOptions::lfoShapeNames(), s.lfoShape, "Shape");
    dlg->addComboBox ("cycle", ModuleOptions::barLengthNames(), s.lfoCycle, "Cycle length");
    dlg->addComboBox ("depthOct",   { "0", "1", "2", "3", "4" },
                      s.lfoDepthOct, "Depth (octaves)");
    dlg->addComboBox ("depthSteps", { "0", "1", "2", "3", "4", "5", "6" },
                      s.lfoDepthSteps, "Depth (scale steps)");
    addRateControl (*dlg, s);
    addGateControl (*dlg, s);
    dlg->addComboBox ("phase", ModuleOptions::lfoPhaseNames(), s.lfoPhase,
                      "Phase (degrees)");
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, true);
            ns.lfoShape      = d->getComboBoxSelectedIndex ("shape");
            ns.lfoCycle      = d->getComboBoxSelectedIndex ("cycle");
            ns.lfoDepthOct   = d->getComboBoxSelectedIndex ("depthOct");
            ns.lfoDepthSteps = d->getComboBoxSelectedIndex ("depthSteps");
            readRateControl (*d, ns);
            readGateControl (*d, ns);
            ns.lfoPhase      = d->getComboBoxSelectedIndex ("phase");
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (rateSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openQuantizeDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    auto* dlg = owner.showInlineDialog ("Quantize settings",
                                        "Notes are moved onto the rate grid; "
                                        "swing pushes every second grid step late.");
    addRateControl (*dlg, s);
    dlg->addComboBox ("swing", ModuleOptions::swingNames(), s.swing, "Swing");
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRateControl (*d, ns);
            ns.swing = d->getComboBoxSelectedIndex ("swing");
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (rateSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openScaleModDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A pitch transformer, so it offers Off (pass notes through un-snapped).
    auto* dlg = owner.showInlineDialog ("Scale settings",
                                        "Notes passing through are forced onto "
                                        "this scale (Off leaves them chromatic).");
    addRootScaleControls (*dlg, s, true);
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, true);
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (scaleModSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openProgressionDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    juce::StringArray octaves;
    for (int o = -ModuleOptions::kProgOctaveRange; o <= ModuleOptions::kProgOctaveRange; ++o)
        octaves.add (o > 0 ? "+" + juce::String (o) : juce::String (o));

    // A pitch transformer (Off = walk degrees chromatically). Its per-step
    // cadence is "Step Length", drawn from the bar-length list — "Rate" stays
    // reserved for the 1/32..1/1 note flavour.
    auto* dlg = owner.showInlineDialog ("Progression settings",
                                        "Each step transposes passing notes to its "
                                        "scale degree; Step Length is one step's length.");
    addRootScaleControls (*dlg, s, true);
    dlg->addComboBox ("progRate", ModuleOptions::barLengthNames(), s.progRate, "Step Length");

    // One row per step: degree + octave side by side. The row set is edited
    // live by the Add/Remove buttons; combo names are indexed so OK can read
    // whatever count the user ended up with.
    auto addStepRow = [dlg, octaves] (int index, const ProgressionStep& step)
    {
        dlg->addComboBox ("deg" + juce::String (index), ModuleOptions::degreeNames(),
                          step.degree, "Step " + juce::String (index + 1));
        dlg->addComboBox ("oct" + juce::String (index), octaves,
                          step.octave + ModuleOptions::kProgOctaveRange, "Octave", true);
    };
    for (int i = 0; i < (int) s.progSteps.size(); ++i)
        addStepRow (i, s.progSteps[(size_t) i]);

    // The live row count, shared by the two utility buttons and the OK read.
    auto stepCount = std::make_shared<int> ((int) s.progSteps.size());

    dlg->addUtilityButton ("Add step", [stepCount, addStepRow]()
    {
        if (*stepCount >= ModuleOptions::kMaxProgSteps)
            return;
        addStepRow ((*stepCount)++, {});
    });
    dlg->addUtilityButton ("Remove", [dlg, stepCount]()
    {
        if (*stepCount <= 1)   // a progression always keeps one step
            return;
        --(*stepCount);
        dlg->removeComboBox ("deg" + juce::String (*stepCount));
        dlg->removeComboBox ("oct" + juce::String (*stepCount));
    });

    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    dlg->onResult = [this, id, stepCount] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, true);
            ns.progRate = d->getComboBoxSelectedIndex ("progRate");
            ns.progSteps.clear();
            for (int i = 0; i < *stepCount; ++i)
                ns.progSteps.push_back ({
                    d->getComboBoxSelectedIndex ("deg" + juce::String (i)),
                    d->getComboBoxSelectedIndex ("oct" + juce::String (i))
                        - ModuleOptions::kProgOctaveRange });
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (progressionSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openShiftDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    juce::StringArray amounts;
    for (int a = -ModuleOptions::kShiftRange; a <= ModuleOptions::kShiftRange; ++a)
        amounts.add (a > 0 ? "+" + juce::String (a) : juce::String (a));

    // A pitch transformer carrying the shared Root/Scale pair (Off = shift in
    // semitones, a scale = shift in degrees). The amount stays a combo here;
    // its live unit-label dial belongs to the ModuleWindow rollout.
    auto* dlg = owner.showInlineDialog ("Shift settings",
                                        "With a scale, the amount shifts in scale steps; "
                                        "with scale Off, in semitones.");
    addRootScaleControls (*dlg, s, true);
    dlg->addComboBox ("amount", amounts, s.shiftAmount + ModuleOptions::kShiftRange,
                      "Amount");
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, true);
            ns.shiftAmount = d->getComboBoxSelectedIndex ("amount") - ModuleOptions::kShiftRange;
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (shiftSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openDelayDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    juce::StringArray shifts;
    for (int a = -ModuleOptions::kDelayShiftRange; a <= ModuleOptions::kDelayShiftRange; ++a)
        shifts.add (a > 0 ? "+" + juce::String (a) : juce::String (a));

    // Delay maps pitch through its per-echo shift, so it carries the shared
    // Root/Scale pair (Off = shift in semitones, a scale = shift in degrees).
    auto* dlg = owner.showInlineDialog ("Delay settings",
                                        "Feedback sets how quickly the repeats fade; a shift "
                                        "moves each repeat (semitones with scale Off, else "
                                        "scale steps).");
    addRootScaleControls (*dlg, s, true);
    addRateControl (*dlg, s);
    dlg->addComboBox ("feedback", ModuleOptions::feedbackNames(), s.delayFeedback,
                      "Feedback");
    dlg->addComboBox ("shift", shifts, s.delayShift + ModuleOptions::kDelayShiftRange,
                      "Shift");
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, true);
            readRateControl (*d, ns);
            ns.delayFeedback = d->getComboBoxSelectedIndex ("feedback");
            ns.delayShift    = d->getComboBoxSelectedIndex ("shift")
                                 - ModuleOptions::kDelayShiftRange;
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (rateSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openChordDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    auto* dlg = owner.showInlineDialog ("Chord settings",
                                        "A chord built on the scale degree, restarting "
                                        "every Repeat and sounding for Length.");
    addRootScaleControls (*dlg, s, false);   // builds a chord from the scale — no Off
    dlg->addComboBox ("degree", ModuleOptions::degreeNames(), s.chordDegree, "Degree");
    dlg->addComboBox ("type", ModuleOptions::chordTypeNames(), s.chordType, "Type");
    dlg->addComboBox ("inversion", ModuleOptions::chordInversionNames(),
                      s.chordInversion, "Inversion");
    addHoldControls (*dlg, s);
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, false);
            ns.chordDegree    = d->getComboBoxSelectedIndex ("degree");
            ns.chordType      = d->getComboBoxSelectedIndex ("type");
            ns.chordInversion = d->getComboBoxSelectedIndex ("inversion");
            readHoldControls (*d, ns);
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (chordSublabel (ns));
        }
        d->getParentComponent()->removeChildComponent (d);
        delete d;
    };
}

void Canvas::openDroneDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    juce::StringArray octaves;
    for (int o = -ModuleOptions::kDroneOctaveRange; o <= ModuleOptions::kDroneOctaveRange; ++o)
        octaves.add (o > 0 ? "+" + juce::String (o) : juce::String (o));

    auto* dlg = owner.showInlineDialog ("Drone settings",
                                        "Holds its voicing for Length, restarting every "
                                        "Repeat — and immediately when the harmony changes.");
    addRootScaleControls (*dlg, s, false);   // holds a scale voicing — no Off
    dlg->addComboBox ("voicing", ModuleOptions::droneVoicingNames(),
                      s.droneVoicing, "Voicing");
    dlg->addComboBox ("octave", octaves,
                      s.droneOctave + ModuleOptions::kDroneOctaveRange, "Octave");
    addHoldControls (*dlg, s);
    dlg->addButton ("OK", 1);
    dlg->addButton ("Cancel", 0);

    // Captures the id, not the node — the node can be deleted or rebuilt while
    // the dialog is up (host state restore), so it is re-looked-up on OK.
    dlg->onResult = [this, id] (int result, InlineDialog* d)
    {
        if (result == 1)
        {
            auto ns = proc.getModuleSettings (id);
            readRootScaleControls (*d, ns, false);
            ns.droneVoicing = d->getComboBoxSelectedIndex ("voicing");
            ns.droneOctave  = d->getComboBoxSelectedIndex ("octave")
                                - ModuleOptions::kDroneOctaveRange;
            readHoldControls (*d, ns);
            proc.setModuleSettings (id, ns);
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (droneSublabel (ns));
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
