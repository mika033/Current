#include "Canvas.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProgressionStepList.h"
#include "RhythmizeStepGrid.h"
#include "Theme.h"
#include <cmath>

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
    selectedCable = {};
    cableDrag = {};
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

juce::String Canvas::mirrorSublabel (const ModuleSettings& settings)
{
    // The inversion centre — the module's headline fact at a glance. Off (no
    // inversion) reads as the window instead, since that is all it does then.
    if (settings.mirrorCenter < 0)
        return ModuleOptions::midiNoteName (settings.mirrorLow) + "-"
             + ModuleOptions::midiNoteName (settings.mirrorHigh);
    return ModuleOptions::midiNoteName (settings.mirrorCenter);
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

juce::String Canvas::strumSpreadText (int percent) const
{
    percent = juce::jlimit (0, ModuleOptions::kStrumSpreadMax, percent);
    if (percent == 0)   return "Off";
    if (percent == 50)  return "1/16";
    if (percent == 100) return "1/8";
    // Between the detents: the per-note gap in real time at the current tempo
    // (gap = percent% of an 1/8 note; an 1/8 note is 30000/bpm ms).
    const double bpm = juce::jmax (1.0, proc.getCurrentBpm());
    return juce::String (juce::roundToInt ((double) percent * 300.0 / bpm)) + " ms";
}

juce::String Canvas::strumSublabel (const ModuleSettings& settings) const
{
    // The spread gap — the module's headline setting (Off = bypass).
    return strumSpreadText (settings.strumSpread);
}

juce::String Canvas::harmonizerSublabel (const ModuleSettings& settings)
{
    // The chord type it stacks — the module's headline fact at a glance.
    return ModuleOptions::harmonizerTypeNames()[juce::jlimit (
        0, ModuleOptions::harmonizerTypeNames().size() - 1, settings.harmType)];
}

juce::String Canvas::sublabelFor (const ModuleInstance& m) const
{
    switch (m.type)
    {
        case ModuleType::MidiIn:
        case ModuleType::Output:      return channelSublabel (m.type, m.channel);
        case ModuleType::Random:
        case ModuleType::ScaleGen:
        case ModuleType::Arp:
        case ModuleType::Rhythmize:
        case ModuleType::Lfo:
        case ModuleType::Delay:
        case ModuleType::Quantize:
        case ModuleType::Humanize:    return rateSublabel (m.settings);
        case ModuleType::Shift:       return shiftSublabel (m.settings);
        case ModuleType::Mirror:      return mirrorSublabel (m.settings);
        case ModuleType::Chord:       return chordSublabel (m.settings);
        case ModuleType::Drone:       return droneSublabel (m.settings);
        case ModuleType::Strum:       return strumSublabel (m.settings);
        case ModuleType::Harmonizer:  return harmonizerSublabel (m.settings);
        case ModuleType::ScaleMod:    return scaleModSublabel (m.settings);
        case ModuleType::Progression: return progressionSublabel (m.settings);
    }
    return {};
}

void Canvas::refreshSublabel (int id)
{
    for (const auto& m : proc.modules())
        if (m.id == id)
        {
            for (auto& n : nodes)
                if (n->moduleId() == id)
                    n->setSublabel (sublabelFor (m));
            return;
        }
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

// --- Shared ModuleWindow controls ---------------------------------------------
// The ModuleWindow twins of the InlineDialog helpers above. Same keys, lists,
// and index mappings, so a shared setting reads and writes identically whether a
// module opens the new window or the old dialog. Placement differs (the window
// has fixed menu/grid slots), so these take a slot; the menu helpers know their
// canonical slot (Root/Scale/Rate-or-Length) and fill it directly.

void Canvas::addRootScaleMenu (ModuleWindow& win, const ModuleSettings& s, bool scaleOffersOff)
{
    win.setMenuCombo (0, "root",  choicesWithGlobal (ParamIDs::root), s.rootOverride + 1, "Root");
    win.setMenuCombo (1, "scale", scaleChoices (scaleOffersOff),
                      scaleIndexForOverride (s.scaleOverride, scaleOffersOff), "Scale");
}

void Canvas::readRootScaleMenu (const ModuleWindow& win, ModuleSettings& s, bool scaleOffersOff) const
{
    s.rootOverride  = win.getComboSelectedIndex ("root") - 1;
    s.scaleOverride = scaleOverrideForIndex (win.getComboSelectedIndex ("scale"), scaleOffersOff);
}

void Canvas::addRateMenu (ModuleWindow& win, const ModuleSettings& s)
{
    // The Rate flavour lives in the menu bar's third slot (the note-length grid).
    win.setMenuCombo (2, "rate", ModuleOptions::rateNames(), s.rate, "Rate");
}

void Canvas::readRateMenu (const ModuleWindow& win, ModuleSettings& s)
{
    s.rate = win.getComboSelectedIndex ("rate");
}

void Canvas::addHoldLengthMenu (ModuleWindow& win, const ModuleSettings& s)
{
    // The Length flavour takes the same third menu slot on the bar-based modules
    // (Chord, Drone), so Rate and Length never appear together and the slot's
    // meaning stays "the module's primary time base".
    win.setMenuCombo (2, "holdLength", ModuleOptions::barLengthNames(), s.holdLength, "Length");
}

void Canvas::readHoldLengthMenu (const ModuleWindow& win, ModuleSettings& s)
{
    s.holdLength = win.getComboSelectedIndex ("holdLength");
}

void Canvas::addGateDial (ModuleWindow& win, int slot, const ModuleSettings& s)
{
    // Gate is a dial everywhere it appears; its label reads the active percentage
    // back live ("Gate: 63%") since a dial has no text box.
    auto gateText = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    win.setGridDial (slot, "gate",
                     (double) ModuleOptions::kGatePctMin,
                     (double) ModuleOptions::kGatePctMax, 1.0,
                     s.gate, "Gate", gateText);
}

void Canvas::readGateDial (const ModuleWindow& win, ModuleSettings& s)
{
    s.gate = juce::roundToInt (win.getDialValue ("gate"));
}

void Canvas::addOctavesDial (ModuleWindow& win, int slot, const ModuleSettings& s)
{
    // The 1..4 pattern-span "Octaves" (Scale gen, Arp) — a dial, reading its
    // count back live.
    win.setGridDial (slot, "octaves", 1.0, 4.0, 1.0, (double) s.octaves, "Octaves",
                     [] (double v) { return juce::String (juce::roundToInt (v)); });
}

void Canvas::readOctavesDial (const ModuleWindow& win, ModuleSettings& s)
{
    s.octaves = juce::roundToInt (win.getDialValue ("octaves"));
}

void Canvas::addModeCombo (ModuleWindow& win, int slot, const ModuleSettings& s, int modeCount)
{
    juce::StringArray modes;
    for (int i = 0; i < modeCount && i < ModuleOptions::modeNames().size(); ++i)
        modes.add (ModuleOptions::modeNames()[i]);
    win.setGridCombo (slot, "mode", modes, juce::jlimit (0, modeCount - 1, s.mode), "Mode");
}

void Canvas::readModeCombo (const ModuleWindow& win, ModuleSettings& s)
{
    s.mode = win.getComboSelectedIndex ("mode");
}

void Canvas::addRepeatCombo (ModuleWindow& win, int slot, const ModuleSettings& s)
{
    win.setGridCombo (slot, "repeat", ModuleOptions::repeatNames(), s.repeat, "Repeat");
}

void Canvas::readRepeatCombo (const ModuleWindow& win, ModuleSettings& s)
{
    s.repeat = win.getComboSelectedIndex ("repeat");
}

void Canvas::addHoldRepeatCombo (ModuleWindow& win, int slot, const ModuleSettings& s)
{
    // Chord/Drone keep their Repeat on a distinct key (holdRepeat), same list.
    win.setGridCombo (slot, "holdRepeat", ModuleOptions::repeatNames(), s.holdRepeat, "Repeat");
}

void Canvas::readHoldRepeatCombo (const ModuleWindow& win, ModuleSettings& s)
{
    s.holdRepeat = win.getComboSelectedIndex ("holdRepeat");
}

void Canvas::addAmountDial (ModuleWindow& win, int slot, const juce::String& name,
                            int value, int range)
{
    // The formatter reads the live Scale choice so the unit word matches the
    // shift mode; the Scale-combo callback re-runs it so the word flips the
    // moment Scale changes, not only on a dial turn. Both modules label it
    // "Shift".
    auto* w = &win;
    auto unitText = [w] (double v)
    {
        const int n = juce::roundToInt (v);
        const bool off = scaleOverrideForIndex (w->getComboSelectedIndex ("scale"), true)
                             == ModuleOptions::kScaleOff;
        return (n > 0 ? "+" + juce::String (n) : juce::String (n))
                 + (off ? " semitones" : " steps");
    };
    win.setGridDial (slot, name, (double) -range, (double) range, 1.0,
                     (double) value, "Shift", unitText);
    win.setComboChangeCallback ("scale", [w, name]() { w->refreshDial (name); });
}

int Canvas::readAmountDial (const ModuleWindow& win, const juce::String& name)
{
    return juce::roundToInt (win.getDialValue (name));
}

void Canvas::wireDialog (ModuleWindow* win, int id, const ModuleSettings& snapshot,
                         std::function<void (const ModuleWindow&, ModuleSettings&)> read)
{
    // Settings edits keep notes ringing in the engine (same topologyVersion),
    // so pushing on every dial tick is safe — the graph snapshot is rebaked on
    // the message thread and swapped in atomically.
    auto apply = [this, id, read] (ModuleWindow* w)
    {
        auto ns = proc.getModuleSettings (id);
        read (*w, ns);
        proc.setModuleSettings (id, ns);
        refreshSublabel (id);
    };

    win->onChanged = [apply, win]() { apply (win); };

    win->onResult = [this, id, snapshot, apply] (int result, ModuleWindow* w)
    {
        if (result == 1)
            apply (w);   // OK: keep (a no-op re-push if nothing changed since)
        else
        {
            // Cancel: undo the live pushes.
            proc.setModuleSettings (id, snapshot);
            refreshSublabel (id);
        }
        w->getParentComponent()->removeChildComponent (w);
        delete w;
    };
}

void Canvas::addNodeComponent (const ModuleInstance& instance)
{
    auto node = std::make_unique<ModuleComponent> (instance.id, instance.type);
    node->setTopLeftPosition ((int) instance.x, (int) instance.y);

    node->setSublabel (sublabelFor (instance));

    node->onSelected = [this] (ModuleComponent& n) { selectNode (&n); };

    node->onMoved = [this] (int id, juce::Point<int> pos)
    {
        proc.moveModule (id, (float) pos.getX(), (float) pos.getY());
        repaint();   // the cables track their nodes
    };

    node->onDelete      = [this] (ModuleComponent& n) { requestDeleteNode (n.moduleId()); };
    node->onNodeDrag    = [this] (const juce::MouseEvent& e) { nodeDragUpdate (e); };
    node->onNodeDragEnd = [this] (ModuleComponent& n, const juce::MouseEvent& e) { nodeDragEnd (n, e); };

    node->onPortDragStart = [this] (ModuleComponent& n, const juce::MouseEvent& e) { beginCableDrag (n, e); };
    node->onPortDrag      = [this] (const juce::MouseEvent& e) { updateCableDrag (e); };
    node->onPortDragEnd   = [this] (const juce::MouseEvent& e) { endCableDrag (e); };

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
        if (n.moduleType() == ModuleType::Rhythmize)
        {
            openRhythmizeDialog (n);
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
        if (n.moduleType() == ModuleType::Mirror)
        {
            openMirrorDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Harmonizer)
        {
            openHarmonizerDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Delay)
        {
            openDelayDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Strum)
        {
            openStrumDialog (n);
            return;
        }
        if (n.moduleType() == ModuleType::Humanize)
        {
            openHumanizeDialog (n);
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

    // No pitch and no time base, so the menu bar stays blank; the channel is the
    // module's one control, a grid combo.
    auto* win = owner.showModuleWindow (juce::String (descriptorFor (type).name));
    win->setGridCombo (0, "channel", items, isIn ? chan : chan - 1,
                       isIn ? "Input channel" : "Output channel");
    // The channel filters input on MIDI In but stamps output on Output — two
    // different help lines behind the one control.
    win->setHelpKey ("channel", isIn ? "channel.in" : "channel.out");
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    // The channel lives outside ModuleSettings, so this dialog carries its own
    // live-apply/revert plumbing instead of wireDialog — same contract: every
    // pick is audible immediately, Cancel restores the open-time channel.
    auto apply = [this, id, isIn] (ModuleWindow* w)
    {
        const int idx = w->getComboSelectedIndex ("channel");
        if (idx >= 0)
        {
            proc.setModuleChannel (id, isIn ? idx : idx + 1);
            refreshSublabel (id);
        }
    };
    win->onChanged = [apply, win]() { apply (win); };

    win->onResult = [this, id, chan, apply] (int result, ModuleWindow* w)
    {
        if (result == 1)
            apply (w);
        else
        {
            proc.setModuleChannel (id, chan);
            refreshSublabel (id);
        }
        w->getParentComponent()->removeChildComponent (w);
        delete w;
    };
}

void Canvas::openArpDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A note-emitting Rate modulator with no pitch mapping: Root/Scale stay
    // blank, Rate sits in the menu bar. Grid: mode, octaves (dial), gate (dial),
    // repeat.
    auto* win = owner.showModuleWindow ("Arp");
    addRateMenu (*win, s);
    addModeCombo (*win, 0, s, ModuleOptions::modeNames().size());
    addOctavesDial (*win, 1, s);
    addGateDial (*win, 2, s);
    addRepeatCombo (*win, 3, s);
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readModeCombo (w, ns);
        readRateMenu (w, ns);
        readOctavesDial (w, ns);
        readGateDial (w, ns);
        readRepeatCombo (w, ns);
    });
}

void Canvas::openRhythmizeDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A note-emitting Rate modulator with no pitch mapping, like the Arp:
    // Root/Scale stay blank, Rate sits in the menu bar, Gate is the shared
    // grid dial. The 16-step pattern is the custom body (two rows of eight
    // LAM-style step boxes) above the grid row — the first window to use the
    // body + grid combination.
    auto* win = owner.showModuleWindow ("Rhythmize");
    addRateMenu (*win, s);

    auto body = std::make_unique<RhythmizeStepGrid> (s.rhythmSteps);
    auto* grid = body.get();   // owned by the window; valid inside the reads
    win->setCustomBody (std::move (body), RhythmizeStepGrid::preferredHeight);
    addGateDial (*win, 0, s);

    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    // Rhythmize's Rate is the length of one pattern step, not a firing grid.
    win->setHelpKey ("rate", "rate.rhythmize");

    wireDialog (win, id, s, [grid] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRateMenu (w, ns);
        readGateDial (w, ns);
        ns.rhythmSteps = grid->getPattern();
    });
    // Step toggles live outside the window's own controls, so forward the
    // grid's change and help-bar signals into the window's paths.
    grid->onChanged  = win->onChanged;
    grid->onFeedback = win->onFeedback;

    // Playhead: the step the engine is on is a pure function of its published
    // song position and the module's current rate (mirroring runSteps'
    // song-position derivation), so no per-node feedback is needed and the
    // halo stays correct while Rate is edited live. Mathematical mod, so a
    // negative position (host pre-roll) wraps into the pattern.
    grid->playingStep = [this, id]() -> int
    {
        if (! proc.transportPlaying())
            return -1;
        const double stepQn = ModuleOptions::rateQuarterNotes (proc.getModuleSettings (id).rate);
        const auto k = (juce::int64) std::floor (proc.playheadQn() / stepQn);
        const int  m = (int) (k % (juce::int64) ModuleOptions::kRhythmSteps);
        return m < 0 ? m + ModuleOptions::kRhythmSteps : m;
    };
}

void Canvas::openRandomDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // The redesigned window: a thin menu bar (Root / Scale / Rate) over a 3x2
    // grid. Random maps pitch, so it offers the Off (chromatic) scale choice;
    // Gate joins the range notes as a dial (every note-emitting Rate module
    // carries one). Root/Scale/Rate/Gate go through the shared ModuleWindow
    // helpers so they stay identical to the other generators' windows.
    auto* win = owner.showModuleWindow ("Random");

    addRootScaleMenu (*win, s, true);
    addRateMenu (*win, s);

    // Range as dials over the MIDI note span (0..127); the label carries a live
    // note-name readout ("From: C1") since a dial has no text box.
    auto noteText = [] (double v) { return ModuleOptions::midiNoteName (juce::roundToInt (v)); };
    win->setGridDial (0, "from", 0.0, 127.0, 1.0, s.rangeFrom, "From", noteText);
    win->setGridDial (1, "to",   0.0, 127.0, 1.0, s.rangeTo,   "To",   noteText);
    addGateDial (*win, 2, s);

    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
        readRateMenu (w, ns);
        readGateDial (w, ns);
        ns.rangeFrom = juce::roundToInt (w.getDialValue ("from"));
        ns.rangeTo   = juce::roundToInt (w.getDialValue ("to"));
        // A backwards range is a slip, not an intent — normalise it.
        if (ns.rangeFrom > ns.rangeTo)
            std::swap (ns.rangeFrom, ns.rangeTo);
    });
}

void Canvas::openScaleGenDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A scale-walking generator, so no Off (it needs a scale to walk); one
    // canonical Rate list; Gate like every note-emitting Rate module.
    // Menu bar: Root / Scale / Rate. Grid: mode, octaves (dial), end-on, gate
    // (dial), repeat.
    auto* win = owner.showModuleWindow ("Scale");
    addRootScaleMenu (*win, s, false);
    addRateMenu (*win, s);
    addModeCombo (*win, 0, s, ModuleOptions::kScaleModeCount);
    addOctavesDial (*win, 1, s);
    win->setGridCombo (2, "endOn", { "Root (octave)", "7th (last scale note)" },
                       s.endOnRoot ? 0 : 1, "End on");
    addGateDial (*win, 3, s);
    addRepeatCombo (*win, 4, s);
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, false);
        readRateMenu (w, ns);
        readModeCombo (w, ns);
        readOctavesDial (w, ns);
        ns.endOnRoot = w.getComboSelectedIndex ("endOn") == 0;
        readGateDial (w, ns);
        readRepeatCombo (w, ns);
    });
}

void Canvas::openLfoDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // The LFO maps pitch, so it offers Off (chromatic mapping), and it emits
    // notes on a Rate grid, so it carries a Gate. Menu bar: Root / Scale / Rate.
    // Grid (all six cells): shape, cycle length, depth-octaves (dial), depth-
    // steps (dial), gate (dial), phase. Cycle length is the LFO's own bar-based
    // period, distinct from the note-emission Rate in the menu bar.
    auto* win = owner.showModuleWindow ("LFO");
    addRootScaleMenu (*win, s, true);
    addRateMenu (*win, s);

    auto intText = [] (double v) { return juce::String (juce::roundToInt (v)); };
    win->setGridCombo (0, "shape", ModuleOptions::lfoShapeNames(), s.lfoShape, "Shape");
    win->setGridCombo (1, "cycle", ModuleOptions::barLengthNames(), s.lfoCycle, "Cycle length");
    win->setGridDial  (2, "depthOct",   0.0, 4.0, 1.0, (double) s.lfoDepthOct,
                       "Depth (oct)", intText);
    win->setGridDial  (3, "depthSteps", 0.0, 6.0, 1.0, (double) s.lfoDepthSteps,
                       "Depth (steps)", intText);
    addGateDial (*win, 4, s);
    win->setGridCombo (5, "phase", ModuleOptions::lfoPhaseNames(), s.lfoPhase, "Phase (deg)");
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
        readRateMenu (w, ns);
        ns.lfoShape      = w.getComboSelectedIndex ("shape");
        ns.lfoCycle      = w.getComboSelectedIndex ("cycle");
        ns.lfoDepthOct   = juce::roundToInt (w.getDialValue ("depthOct"));
        ns.lfoDepthSteps = juce::roundToInt (w.getDialValue ("depthSteps"));
        readGateDial (w, ns);
        ns.lfoPhase      = w.getComboSelectedIndex ("phase");
    });
}

void Canvas::openQuantizeDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // Re-times passing notes onto the Rate grid (no pitch, no gate — it keeps
    // the played duration). Menu bar: Rate only. Grid: swing, a 0..100% dial
    // reading its percent back live (same control as Humanize's Swing).
    auto* win = owner.showModuleWindow ("Quantize");
    addRateMenu (*win, s);
    win->setHelpKey ("rate", "rate.quantize");   // the grid notes snap to
    win->setGridDial (0, "swing", 0.0, (double) ModuleOptions::kSwingPctMax, 1.0,
                      (double) s.swing, "Swing",
                      [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; });
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRateMenu (w, ns);
        ns.swing = juce::roundToInt (w.getDialValue ("swing"));
    });
}

void Canvas::openScaleModDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A pitch transformer whose only settings are the Root/Scale pair (Off =
    // leave notes chromatic). Those live in the menu bar, so the grid is empty.
    auto* win = owner.showModuleWindow ("Scale");
    addRootScaleMenu (*win, s, true);
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
    });
}

void Canvas::openProgressionDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // The one module whose body doesn't fit the shared 3x2 grid: its step list
    // grows and shrinks, so it rides ModuleWindow's custom-body escape hatch
    // (an arranger-style step row) while keeping the shared menu-bar chrome.
    // A pitch transformer (Off = walk degrees chromatically). Length in the
    // third menu slot is one step's length, drawn from the bar-length list —
    // "Rate" stays reserved for the 1/32..1/1 note flavour.
    auto* win = owner.showModuleWindow ("Progression");
    addRootScaleMenu (*win, s, true);
    win->setMenuCombo (2, "progRate", ModuleOptions::barLengthNames(), s.progRate, "Length");

    auto body = std::make_unique<ProgressionStepList> (s.progSteps);
    auto* stepList = body.get();   // owned by the window; valid inside the reads
    win->setCustomBody (std::move (body), ProgressionStepList::preferredHeight);

    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this, stepList] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
        ns.progRate  = w.getComboSelectedIndex ("progRate");
        ns.progSteps = stepList->getSteps();
    });
    // Step-list edits live outside the window's own controls, so forward its
    // change and help-bar signals into the window's paths.
    stepList->onChanged  = win->onChanged;
    stepList->onFeedback = win->onFeedback;
}

void Canvas::openShiftDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A pitch transformer carrying the shared Root/Scale pair. Scale = Off means
    // the amount shifts in raw semitones; a scale (Global/named) means it shifts
    // in scale steps. The amount is a dial whose live label reads the active unit
    // back ("Shift: +3 semitones" / "Shift: +3 steps"), shared with Delay.
    auto* win = owner.showModuleWindow ("Shift");
    addRootScaleMenu (*win, s, true);
    addAmountDial (*win, 0, "amount", s.shiftAmount, ModuleOptions::kShiftRange);

    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
        ns.shiftAmount = readAmountDial (w, "amount");
    });
}

void Canvas::openMirrorDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A pitch transformer carrying the shared Root/Scale pair (Off = a chromatic
    // mirror; a scale = a diatonic one that stays in key). No time base, so the
    // menu bar's third slot stays blank. Grid: the Centre note (far-left detent =
    // Off, no inversion), the Low/High register window, and the Bounds mode.
    auto* win = owner.showModuleWindow ("Mirror");
    addRootScaleMenu (*win, s, true);

    // Centre runs one below the note range so the far-left detent reads "Off".
    auto noteText = [] (double v) { return ModuleOptions::midiNoteName (juce::roundToInt (v)); };
    win->setGridDial (0, "center",
                      (double) ModuleOptions::kMirrorCenterOff, 127.0, 1.0,
                      (double) s.mirrorCenter, "Centre",
                      [] (double v)
                      {
                          const int n = juce::roundToInt (v);
                          return n < 0 ? juce::String ("Off") : ModuleOptions::midiNoteName (n);
                      });
    win->setGridDial (1, "low",  0.0, 127.0, 1.0, (double) s.mirrorLow,  "Low",  noteText);
    win->setGridDial (2, "high", 0.0, 127.0, 1.0, (double) s.mirrorHigh, "High", noteText);
    win->setGridCombo (3, "bounds", ModuleOptions::mirrorBoundsNames(),
                       juce::jlimit (0, ModuleOptions::mirrorBoundsNames().size() - 1, s.mirrorBounds),
                       "Bounds");

    // Low and High can't cross: turning one past the other pushes it along, so
    // the window never inverts (setDialValue fires no callback, so no recursion).
    auto* mw = win;
    win->setDialChangeCallback ("low", [mw]()
    {
        if (mw->getDialValue ("low") > mw->getDialValue ("high"))
            mw->setDialValue ("high", mw->getDialValue ("low"));
    });
    win->setDialChangeCallback ("high", [mw]()
    {
        if (mw->getDialValue ("high") < mw->getDialValue ("low"))
            mw->setDialValue ("low", mw->getDialValue ("high"));
    });

    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
        ns.mirrorCenter = juce::roundToInt (w.getDialValue ("center"));
        ns.mirrorLow    = juce::roundToInt (w.getDialValue ("low"));
        ns.mirrorHigh   = juce::roundToInt (w.getDialValue ("high"));
        // The push keeps them ordered live; normalise anyway in case a value
        // arrived some other way.
        if (ns.mirrorLow > ns.mirrorHigh)
            std::swap (ns.mirrorLow, ns.mirrorHigh);
        ns.mirrorBounds = w.getComboSelectedIndex ("bounds");
    });
}

void Canvas::openDelayDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // Delay maps pitch through its per-echo shift, so it carries the shared
    // Root/Scale pair (Off = shift in semitones, a scale = shift in degrees).
    // Menu bar: Root / Scale / Rate. Grid: feedback (a 0..90% dial — the cap
    // keeps the echo tail finite), plus the per-echo shift as the same
    // live-unit amount dial Shift uses.
    auto* win = owner.showModuleWindow ("Delay");
    addRootScaleMenu (*win, s, true);
    addRateMenu (*win, s);
    win->setHelpKey ("rate", "rate.delay");   // the echo spacing
    win->setGridDial (0, "feedback", 0.0, (double) ModuleOptions::kFeedbackPctMax, 1.0,
                      (double) s.delayFeedback, "Feedback",
                      [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; });
    addAmountDial (*win, 1, "shift", s.delayShift, ModuleOptions::kDelayShiftRange);

    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
        readRateMenu (w, ns);
        ns.delayFeedback = juce::roundToInt (w.getDialValue ("feedback"));
        ns.delayShift    = readAmountDial (w, "shift");
    });
}

void Canvas::openStrumDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A time modulator with no pitch and no single time base, so the menu bar
    // stays blank; its six controls fill the grid. Direction and Repeat are the
    // shared controls (identical to the Arp's), so they read the same everywhere.
    // Layout follows the strum gesture: Spread / Direction / Curve on top, then
    // the shaping row Velocity / Jitter / Repeat.
    auto* win = owner.showModuleWindow ("Strum");

    // Spread: the tempo-synced gap between consecutive fanned notes, stored as
    // a percent of an 1/8 note. The readout names the musical detents (Off /
    // 1/16 / 1/8) and shows ms at the current tempo in between.
    win->setGridDial (0, "spread", 0.0, (double) ModuleOptions::kStrumSpreadMax, 1.0,
                      (double) s.strumSpread, "Spread",
                      [this] (double v) { return strumSpreadText (juce::roundToInt (v)); });
    addModeCombo (*win, 1, s, ModuleOptions::modeNames().size());   // Direction (shared Mode)
    win->setGridCombo (2, "curve", ModuleOptions::strumCurveNames(), s.strumCurve, "Curve");
    // Velocity tilt: signed dial reading a signed percent (− fades, + swells).
    win->setGridDial (3, "velTilt",
                      (double) -ModuleOptions::kStrumVelTiltRange,
                      (double)  ModuleOptions::kStrumVelTiltRange, 1.0,
                      (double) s.strumVelTilt, "Velocity",
                      [] (double v)
                      {
                          const int p = juce::roundToInt (v);
                          return (p > 0 ? "+" + juce::String (p) : juce::String (p)) + "%";
                      });
    win->setGridDial (4, "jitter", 0.0, (double) ModuleOptions::kStrumJitterMax, 1.0,
                      (double) s.strumJitter, "Jitter",
                      [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; });
    addRepeatCombo (*win, 5, s);
    // The shared Mode is Strum's fan direction and its Repeat re-strums a held
    // chord — both mean something different here than on the Arp.
    win->setHelpKey ("mode",   "direction");
    win->setHelpKey ("repeat", "repeat.strum");
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [] (const ModuleWindow& w, ModuleSettings& ns)
    {
        ns.strumSpread  = juce::roundToInt (w.getDialValue ("spread"));
        readModeCombo (w, ns);   // Direction -> shared mode
        ns.strumCurve   = w.getComboSelectedIndex ("curve");
        ns.strumVelTilt = juce::roundToInt (w.getDialValue ("velTilt"));
        ns.strumJitter  = juce::roundToInt (w.getDialValue ("jitter"));
        readRepeatCombo (w, ns);
    });
}

void Canvas::openHumanizeDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A final-stage performance-feel modulator: no pitch (Root/Scale blank), and
    // Rate is the groove grid swing & accent lock to. The grid is laid out in
    // signal-flow order — top row = the structured groove (Swing, Lay-back,
    // Accent), bottom row = the random touch (Timing, Velocity, Length). Every
    // amount is a 0..100% dial reading its percent back live.
    auto pct = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    const double mx = (double) ModuleOptions::kSwingPctMax;
    auto* win = owner.showModuleWindow ("Humanize");
    addRateMenu (*win, s);
    win->setHelpKey ("rate", "rate.humanize");   // the groove grid
    win->setGridDial (0, "swing",   0.0, mx, 1.0, (double) s.swing,           "Swing",    pct);
    win->setGridDial (1, "layback", 0.0, mx, 1.0, (double) s.humanizeLayback, "Lay-back", pct);
    win->setGridDial (2, "accent",  0.0, mx, 1.0, (double) s.humanizeAccent,  "Accent",   pct);
    win->setGridDial (3, "timeJit", 0.0, mx, 1.0, (double) s.humanizeTimeJit, "Timing",   pct);
    win->setGridDial (4, "velJit",  0.0, mx, 1.0, (double) s.humanizeVelJit,  "Velocity", pct);
    win->setGridDial (5, "lenJit",  0.0, mx, 1.0, (double) s.humanizeLenJit,  "Length",   pct);
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRateMenu (w, ns);
        ns.swing           = juce::roundToInt (w.getDialValue ("swing"));
        ns.humanizeLayback = juce::roundToInt (w.getDialValue ("layback"));
        ns.humanizeAccent  = juce::roundToInt (w.getDialValue ("accent"));
        ns.humanizeTimeJit = juce::roundToInt (w.getDialValue ("timeJit"));
        ns.humanizeVelJit  = juce::roundToInt (w.getDialValue ("velJit"));
        ns.humanizeLenJit  = juce::roundToInt (w.getDialValue ("lenJit"));
    });
}

void Canvas::openHarmonizerDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // A pitch modulator that stacks a chord on each played note. Root/Scale in
    // the menu bar (Off = chromatic stacking, so it can harmonise any input);
    // the third menu slot stays blank (it has no time base of its own — it rides
    // each note's timing). Grid: Type, Inversion, Mode (Add/Replace/Top).
    auto* win = owner.showModuleWindow ("Harmonizer");
    addRootScaleMenu (*win, s, true);
    win->setGridCombo (0, "type", ModuleOptions::harmonizerTypeNames(), s.harmType, "Type");
    win->setGridCombo (1, "inversion", ModuleOptions::chordInversionNames(),
                       s.harmInversion, "Inversion");
    win->setGridCombo (2, "mode", ModuleOptions::harmonizerModeNames(), s.harmMode, "Mode");
    // Harmonizer's Mode is how held notes interact, not a pattern direction.
    win->setHelpKey ("mode", "harmmode");
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, true);
        ns.harmType      = w.getComboSelectedIndex ("type");
        ns.harmInversion = w.getComboSelectedIndex ("inversion");
        ns.harmMode      = w.getComboSelectedIndex ("mode");
    });
}

void Canvas::openChordDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // Builds a chord from the scale — no Off. A bar-based module, so its time
    // base is Length (menu slot 3); holdRepeat sits in the grid. Menu bar:
    // Root / Scale / Length. Grid: degree, type, inversion, repeat.
    auto* win = owner.showModuleWindow ("Chord");
    addRootScaleMenu (*win, s, false);
    addHoldLengthMenu (*win, s);
    win->setGridCombo (0, "degree", ModuleOptions::degreeNames(), s.chordDegree, "Degree");
    win->setGridCombo (1, "type", ModuleOptions::chordTypeNames(), s.chordType, "Type");
    win->setGridCombo (2, "inversion", ModuleOptions::chordInversionNames(),
                       s.chordInversion, "Inversion");
    addHoldRepeatCombo (*win, 3, s);
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, false);
        readHoldLengthMenu (w, ns);
        ns.chordDegree    = w.getComboSelectedIndex ("degree");
        ns.chordType      = w.getComboSelectedIndex ("type");
        ns.chordInversion = w.getComboSelectedIndex ("inversion");
        readHoldRepeatCombo (w, ns);
    });
}

void Canvas::openDroneDialog (ModuleComponent& node)
{
    const int  id = node.moduleId();
    const auto s  = proc.getModuleSettings (id);

    // Holds a scale voicing — no Off. A bar-based module: Length in the menu
    // bar, holdRepeat in the grid. Menu bar: Root / Scale / Length. Grid:
    // voicing, octave (dial), repeat.
    auto* win = owner.showModuleWindow ("Drone");
    addRootScaleMenu (*win, s, false);
    addHoldLengthMenu (*win, s);
    win->setGridCombo (0, "voicing", ModuleOptions::droneVoicingNames(),
                       s.droneVoicing, "Voicing");
    // Octave is a signed transpose offset (-2..+2) — a dial reading "+N" back,
    // matching the design's octave-dial rule.
    win->setGridDial (1, "octave",
                      (double) -ModuleOptions::kDroneOctaveRange,
                      (double)  ModuleOptions::kDroneOctaveRange, 1.0,
                      (double) s.droneOctave, "Octave",
                      [] (double v)
                      {
                          const int o = juce::roundToInt (v);
                          return o > 0 ? "+" + juce::String (o) : juce::String (o);
                      });
    addHoldRepeatCombo (*win, 2, s);
    win->addButton ("OK", 1);
    win->addButton ("Cancel", 0);

    wireDialog (win, id, s, [this] (const ModuleWindow& w, ModuleSettings& ns)
    {
        readRootScaleMenu (w, ns, false);
        readHoldLengthMenu (w, ns);
        ns.droneVoicing = w.getComboSelectedIndex ("voicing");
        ns.droneOctave  = juce::roundToInt (w.getDialValue ("octave"));
        readHoldRepeatCombo (w, ns);
    });
}

void Canvas::selectNode (ModuleComponent* node)
{
    // Node and cable selection are mutually exclusive.
    if (node != nullptr && selectedCable.valid)
    {
        selectedCable = {};
        repaint();
    }

    if (selectedNode == node)
        return;

    if (selectedNode != nullptr)
        selectedNode->setSelected (false);

    selectedNode = node;

    if (selectedNode != nullptr)
    {
        selectedNode->setSelected (true);
        // Selecting a node reads its one-line description into the help bar —
        // the canvas side of the built-in module manual.
        owner.showFeedback (descriptorFor (node->moduleType()).name,
                            moduleHelpKey (node->moduleType()));
    }
}

void Canvas::deleteSelected()
{
    if (selectedNode != nullptr)
        deleteNode (selectedNode->moduleId());
}

void Canvas::deleteNode (int id)
{
    // Name captured before removal so the help bar can announce what went.
    juce::String name;
    for (const auto& m : proc.modules())
        if (m.id == id)
            name = descriptorFor (m.type).name;

    // Removes from both the view and the model; the model drops the module's
    // cables with it.
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        if ((*it)->moduleId() == id)
        {
            if (selectedNode == it->get())
                selectedNode = nullptr;
            nodes.erase (it);
            break;
        }
    }

    proc.removeModule (id);
    repaint();   // the module's cables disappear with it

    if (name.isNotEmpty())
        owner.showFeedback (name + " removed");
}

void Canvas::requestDeleteNode (int id)
{
    // Deferred one message-loop tick: the request arrives from inside the
    // doomed node's own mouse callback, and the component must not die there.
    juce::Component::SafePointer<Canvas> safe (this);
    juce::MessageManager::callAsync ([safe, id]
    {
        if (safe != nullptr)
            safe->deleteNode (id);
    });
}

void Canvas::nodeDragUpdate (const juce::MouseEvent& e)
{
    if (setRemoveZoneState != nullptr && isOverRemoveZone != nullptr)
        setRemoveZoneState (true, isOverRemoveZone (e.getScreenPosition()));
}

void Canvas::nodeDragEnd (ModuleComponent& node, const juce::MouseEvent& e)
{
    if (setRemoveZoneState != nullptr)
        setRemoveZoneState (false, false);

    // Releasing with the pointer over the tray deletes the node (the node
    // itself stays clamped inside the canvas — the pointer is the gesture).
    if (isOverRemoveZone != nullptr && isOverRemoveZone (e.getScreenPosition()))
        requestDeleteNode (node.moduleId());
}

// --- Wiring -------------------------------------------------------------------

ModuleComponent* Canvas::nodeForId (int id) const
{
    for (const auto& n : nodes)
        if (n->moduleId() == id)
            return n.get();
    return nullptr;
}

juce::Rectangle<float> Canvas::selectedCableBadge() const
{
    if (! selectedCable.valid)
        return {};

    auto* fromNode = nodeForId (selectedCable.fromId);
    auto* toNode   = nodeForId (selectedCable.toId);
    if (fromNode == nullptr || toNode == nullptr)
        return {};

    const auto path = cablePath (fromNode->outputPortCentre(), toNode->inputPortCentre());
    const auto mid  = path.getPointAlongPath (path.getLength() * 0.5f);
    return juce::Rectangle<float> (24.0f, 24.0f).withCentre (mid);
}

juce::Path Canvas::cablePath (juce::Point<float> from, juce::Point<float> to) const
{
    // A horizontal-tangent bezier: cables leave an output port rightward and
    // enter an input port leftward, whatever the nodes' relative positions.
    const float dx = juce::jmax (40.0f, std::abs (to.x - from.x) * 0.5f);
    juce::Path p;
    p.startNewSubPath (from);
    p.cubicTo (from.translated (dx, 0.0f), to.translated (-dx, 0.0f), to);
    return p;
}

void Canvas::paintCables (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();

    for (const auto& c : proc.connections())
    {
        auto* fromNode = nodeForId (c.fromId);
        auto* toNode   = nodeForId (c.toId);
        if (fromNode == nullptr || toNode == nullptr)
            continue;

        const auto path = cablePath (fromNode->outputPortCentre(), toNode->inputPortCentre());
        const bool sel  = selectedCable.valid && selectedCable.fromId == c.fromId
                                              && selectedCable.toId == c.toId;
        g.setColour (sel ? s.accent : s.port);
        g.strokePath (path, juce::PathStrokeType (sel ? 3.0f : 2.0f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        if (sel)
        {
            // Selection swaps the flow arrow for an ✕ badge at the midpoint —
            // tapping it deletes the connection. Port-coloured (the cable's
            // own neutral), with the canvas colour as the ✕ ink so contrast
            // holds in both themes.
            const auto badge = selectedCableBadge();
            g.setColour (s.port);
            g.fillEllipse (badge);
            g.setColour (s.panelBorder);
            g.drawEllipse (badge, 1.0f);

            g.setColour (s.canvasBg);
            const auto cross = badge.reduced (badge.getWidth() * 0.32f);
            g.drawLine ({ cross.getTopLeft(), cross.getBottomRight() }, 2.0f);
            g.drawLine ({ cross.getTopRight(), cross.getBottomLeft() }, 2.0f);
            continue;
        }

        // Flow arrow at the cable's midpoint (the requirements' signal-
        // direction cue), oriented along the curve.
        const float len   = path.getLength();
        const auto  mid   = path.getPointAlongPath (len * 0.5f);
        const auto  ahead = path.getPointAlongPath (juce::jmin (len, len * 0.5f + 6.0f));
        const float angle = std::atan2 (ahead.y - mid.y, ahead.x - mid.x);
        juce::Path arrow;
        arrow.addTriangle (6.0f, 0.0f, -4.0f, 4.5f, -4.0f, -4.5f);
        g.fillPath (arrow, juce::AffineTransform::rotation (angle)
                               .translated (mid.x, mid.y));
    }

    // The in-flight connect gesture: a live cable from the source's output
    // port to the mouse.
    if (cableDrag.active)
        if (auto* fromNode = nodeForId (cableDrag.fromId))
        {
            g.setColour (s.accent);
            g.strokePath (cablePath (fromNode->outputPortCentre(), cableDrag.toPos),
                          juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        }
}

void Canvas::beginCableDrag (ModuleComponent& fromNode, const juce::MouseEvent& e)
{
    cableDrag.active = true;
    cableDrag.fromId = fromNode.moduleId();
    cableDrag.toPos  = e.getEventRelativeTo (this).position;
    repaint();

    owner.showFeedback ("Release on the module that should receive the notes");
}

void Canvas::updateCableDrag (const juce::MouseEvent& e)
{
    if (! cableDrag.active)
        return;
    cableDrag.toPos = e.getEventRelativeTo (this).position;
    repaint();
}

void Canvas::endCableDrag (const juce::MouseEvent& e)
{
    if (! cableDrag.active)
        return;
    cableDrag.active = false;

    const auto pos = e.getEventRelativeTo (this).position;

    // Dropping anywhere on a module that has an input port connects to it —
    // one input per node makes the whole node an unambiguous, touch-friendly
    // target (with the port's enlarged radius as a fallback just outside the
    // bounds). The processor validates (ports, duplicates, cycles); a refused
    // drop simply snaps back.
    for (const auto& n : nodes)
    {
        if (n->moduleId() == cableDrag.fromId || ! moduleHasInputPort (n->moduleType()))
            continue;
        const bool onNode = n->getBounds().toFloat().contains (pos);
        const bool onPort = n->inputPortCentre().getDistanceFrom (pos)
                                <= (float) ModuleComponent::kPortHitRadius;
        if (onNode || onPort)
        {
            const auto* from = nodeForId (cableDrag.fromId);
            if (proc.addConnection (cableDrag.fromId, n->moduleId()))
                owner.showFeedback ("Connected "
                                        + juce::String (from != nullptr
                                              ? descriptorFor (from->moduleType()).name : "module")
                                        + " to "
                                        + juce::String (descriptorFor (n->moduleType()).name),
                                    "action.connect");
            else
                owner.showFeedback ("Can't connect - the cable would double an "
                                    "existing one or create a loop");
            break;
        }
    }
    repaint();
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

    // Cables, under the nodes (children paint after this method).
    paintCables (g);

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

void Canvas::mouseDown (const juce::MouseEvent& e)
{
    // The selected cable's ✕ badge eats the tap first — it can overlap other
    // cables, and it is the touch path to deleting the connection.
    if (selectedCable.valid
        && selectedCableBadge().expanded (4.0f).contains (e.position))
    {
        proc.removeConnection (selectedCable.fromId, selectedCable.toId);
        selectedCable = {};
        repaint();
        owner.showFeedback ("Connection removed");
        return;
    }

    // A click near a cable selects it (checked before the empty-canvas
    // deselect, since cables live on the canvas background). The tolerance is
    // fingertip-sized, not pointer-sized.
    for (const auto& c : proc.connections())
    {
        auto* fromNode = nodeForId (c.fromId);
        auto* toNode   = nodeForId (c.toId);
        if (fromNode == nullptr || toNode == nullptr)
            continue;
        const auto path = cablePath (fromNode->outputPortCentre(), toNode->inputPortCentre());
        juce::Point<float> nearest;
        path.getNearestPoint (e.position, nearest);
        if (nearest.getDistanceFrom (e.position) <= 12.0f)
        {
            selectNode (nullptr);
            selectedCable = { true, c.fromId, c.toId };
            grabKeyboardFocus();
            repaint();
            owner.showFeedback ("Cable: "
                                    + juce::String (descriptorFor (fromNode->moduleType()).name)
                                    + " to "
                                    + juce::String (descriptorFor (toNode->moduleType()).name),
                                "action.cable");
            return;
        }
    }

    // Click on empty canvas clears selection and takes focus (so Delete works).
    selectedCable = {};
    selectNode (nullptr);
    grabKeyboardFocus();
    repaint();
}

bool Canvas::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selectedCable.valid)
        {
            proc.removeConnection (selectedCable.fromId, selectedCable.toId);
            selectedCable = {};
            repaint();
            owner.showFeedback ("Connection removed");
            return true;
        }
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

    // After selectNode's description message, so the placement hint wins.
    owner.showFeedback (juce::String (descriptorFor (type).name) + " added",
                        "action.add");
}
