// Headless smoke test for the Engine. Exercises the module behaviours —
// defaults and user settings (rates, ranges, modes, repeat windows, gates) —
// and, above all, asserts the safety property that matters for a MIDI effect:
// every note-on the engine emits is balanced by a note-off, so nothing hangs —
// across pass-through, modulators, stepped modules, and a transport stop.
// Built only when CURRENT_BUILD_TESTS=ON.
//
// The engine runs a wired GraphSnapshot; most tests here describe their setup
// as a TestConfig (which module types are present + their settings) that
// graphFromConfig() lays out as an explicit graph in the classic order
// (MIDI In -> Arp -> Harmonizer, generators merging in -> pitch mods ->
// Quantize -> Delay -> Strum -> Humanize -> Output). That keeps each test
// about one module's behaviour; the dedicated graph-routing section at the end
// exercises fan-out/fan-in and the strict sink semantics directly.
//
// Not a unit-test framework — just asserts + a pass/fail summary, so it can run
// in CI without extra deps.

#include "Engine.h"
#include "ScaleTables.h"
#include "ModuleSettings.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

namespace
{
    int failures = 0;

    // The per-module settings of the old fixed-chain config, kept as the
    // tests' compact way of describing a setup. graphFromConfig() turns it
    // into an explicit wired graph.
    struct TestConfig
    {
        bool hasArp         = false;
        bool hasRandom      = false;
        bool hasScaleGen    = false;
        bool hasLfo         = false;
        bool hasChord       = false;
        bool hasDrone       = false;
        bool hasQuantize    = false;
        bool hasScaleMod    = false;
        bool hasProgression = false;
        bool hasShift       = false;
        bool hasMirror      = false;
        bool hasHarmonizer  = false;
        bool hasDelay       = false;
        bool hasStrum       = false;
        bool hasHumanize    = false;
        bool hasMidiIn      = false;
        bool hasOutput      = false;

        int    arpMode     = 0;
        double arpStepQn   = 0.25;
        int    arpOctaves  = 1;
        double arpGateFrac = 0.5;
        double arpRepeatQn = 0.0;

        int    randomRoot     = -1;
        int    randomScale    = -1;
        double randomStepQn   = 0.25;
        double randomGateFrac = 0.5;
        int    randomFrom     = 24;
        int    randomTo       = 48;

        int    scaleRoot      = -1;
        int    scaleScale     = -1;
        double scaleStepQn    = 0.5;
        double scaleGateFrac  = 0.5;
        double scaleRepeatQn  = 4.0;
        int    scaleOctaves   = 1;
        int    scaleMode      = 0;
        bool   scaleEndOnRoot = true;

        int    lfoRoot       = -1;
        int    lfoScale      = -1;
        double lfoStepQn     = 0.25;
        double lfoGateFrac   = 0.5;
        double lfoCycleQn    = 4.0;
        int    lfoShape      = 0;
        int    lfoDepthOct   = 1;
        int    lfoDepthSteps = 0;
        double lfoPhase      = 0.0;

        int    chordRoot      = -1;
        int    chordScale     = -1;
        int    chordDegree    = 0;
        int    chordType      = 0;
        int    chordInversion = 0;
        double chordLengthQn  = 4.0;
        double chordPeriodQn  = 4.0;

        int    droneVoicing  = 0;
        int    droneRoot     = -1;
        int    droneScale    = -1;
        int    droneOctave   = 0;
        double droneLengthQn = 16.0;
        double dronePeriodQn = 16.0;

        double quantStepQn = 0.25;
        double quantSwing  = 0.0;

        int scaleModRoot  = -1;
        int scaleModScale = -1;

        int    progRoot      = -1;
        int    progScale     = -1;
        double progRateQn    = 4.0;
        int    progStepCount = 0;
        std::array<int, 8> progDegrees {};
        std::array<int, 8> progOctaves {};

        int shiftAmount = 0;
        int shiftScale  = -1;
        int shiftRoot   = -1;

        int mirrorCenter = 60;
        int mirrorLow    = 36;
        int mirrorHigh   = 84;
        int mirrorBounds = 0;
        int mirrorScale  = -1;
        int mirrorRoot   = -1;

        int harmType      = 0;
        int harmInversion = 0;
        int harmMode      = 0;
        int harmScale     = -1;
        int harmRoot      = -1;

        double delayTimeQn   = 0.5;
        double delayFeedback = 0.5;
        int    delayShift    = 0;
        int    delayScale    = -1;
        int    delayRoot     = -1;

        double strumSpreadSec = 0.04;
        int    strumMode      = 0;
        int    strumCurve     = 0;
        double strumVelTilt   = 0.0;
        double strumJitter    = 0.0;
        double strumRepeatQn  = 0.0;

        double humanizeStepQn  = 0.25;
        double humanizeSwing   = 0.0;
        double humanizeLayback = 0.0;
        double humanizeAccent  = 0.0;
        double humanizeTimeJit = 0.0;
        double humanizeVelJit  = 0.0;
        double humanizeLenJit  = 0.0;

        std::uint16_t inChannelMask  = 0xffff;
        std::uint16_t outChannelMask = 0;
    };

    // Lay the config out as an explicit graph in the classic chain order.
    // Node ids are assigned by creation order, so the same config shape always
    // yields the same ids (and topologyVersion stays 1) — per-node engine
    // state then survives settings edits between process calls, exactly like
    // the plugin's own settings edits.
    GraphSnapshot graphFromConfig (const TestConfig& cfg)
    {
        GraphSnapshot g;
        g.topologyVersion = 1;

        auto add = [&g] (ModuleType t, std::function<void (ModuleParams&)> fill = {})
        {
            GraphNode n;
            n.id   = (int) g.nodes.size() + 1;
            n.type = t;
            if (fill)
                fill (n.params);
            g.nodes.push_back (std::move (n));
            return (int) g.nodes.size() - 1;
        };

        // The current chain tails; a chained module consumes all of them
        // (fan-in) and becomes the single new tail.
        std::vector<int> heads;
        auto chainInto = [&] (int idx)
        {
            for (int h : heads)
                g.nodes[(size_t) idx].inputs.push_back (h);
            heads.assign (1, idx);
        };

        // MIDI In: one node per admitted channel (the old mask semantics);
        // no flag = an accept-everything input.
        if (cfg.hasMidiIn && cfg.inChannelMask != 0xffff)
        {
            for (int ch = 1; ch <= 16; ++ch)
                if ((cfg.inChannelMask & (1u << (ch - 1))) != 0)
                    heads.push_back (add (ModuleType::MidiIn,
                                          [ch] (ModuleParams& p) { p.channel = ch; }));
        }
        else
        {
            heads.push_back (add (ModuleType::MidiIn,
                                  [] (ModuleParams& p) { p.channel = 0; }));
        }

        if (cfg.hasArp)
            chainInto (add (ModuleType::Arp, [&] (ModuleParams& p)
            {
                p.mode = cfg.arpMode; p.stepQn = cfg.arpStepQn; p.octaves = cfg.arpOctaves;
                p.gateFrac = cfg.arpGateFrac; p.repeatQn = cfg.arpRepeatQn;
            }));
        if (cfg.hasHarmonizer)
            chainInto (add (ModuleType::Harmonizer, [&] (ModuleParams& p)
            {
                p.harmType = cfg.harmType; p.harmInversion = cfg.harmInversion;
                p.harmMode = cfg.harmMode; p.root = cfg.harmRoot; p.scale = cfg.harmScale;
            }));

        if (cfg.hasRandom)
            heads.push_back (add (ModuleType::Random, [&] (ModuleParams& p)
            {
                p.root = cfg.randomRoot; p.scale = cfg.randomScale;
                p.stepQn = cfg.randomStepQn; p.gateFrac = cfg.randomGateFrac;
                p.rangeFrom = cfg.randomFrom; p.rangeTo = cfg.randomTo;
            }));
        if (cfg.hasScaleGen)
            heads.push_back (add (ModuleType::ScaleGen, [&] (ModuleParams& p)
            {
                p.root = cfg.scaleRoot; p.scale = cfg.scaleScale;
                p.stepQn = cfg.scaleStepQn; p.gateFrac = cfg.scaleGateFrac;
                p.repeatQn = cfg.scaleRepeatQn; p.octaves = cfg.scaleOctaves;
                p.mode = cfg.scaleMode; p.endOnRoot = cfg.scaleEndOnRoot;
            }));
        if (cfg.hasLfo)
            heads.push_back (add (ModuleType::Lfo, [&] (ModuleParams& p)
            {
                p.root = cfg.lfoRoot; p.scale = cfg.lfoScale;
                p.stepQn = cfg.lfoStepQn; p.gateFrac = cfg.lfoGateFrac;
                p.lfoCycleQn = cfg.lfoCycleQn; p.lfoShape = cfg.lfoShape;
                p.lfoDepthOct = cfg.lfoDepthOct; p.lfoDepthSteps = cfg.lfoDepthSteps;
                p.lfoPhase = cfg.lfoPhase;
            }));
        if (cfg.hasChord)
            heads.push_back (add (ModuleType::Chord, [&] (ModuleParams& p)
            {
                p.root = cfg.chordRoot; p.scale = cfg.chordScale;
                p.chordDegree = cfg.chordDegree; p.chordType = cfg.chordType;
                p.chordInversion = cfg.chordInversion;
                p.holdLengthQn = cfg.chordLengthQn; p.holdPeriodQn = cfg.chordPeriodQn;
            }));

        if (cfg.hasScaleMod)
            chainInto (add (ModuleType::ScaleMod, [&] (ModuleParams& p)
            {
                p.root = cfg.scaleModRoot; p.scale = cfg.scaleModScale;
            }));
        if (cfg.hasProgression)
            chainInto (add (ModuleType::Progression, [&] (ModuleParams& p)
            {
                p.root = cfg.progRoot; p.scale = cfg.progScale;
                p.progRateQn = cfg.progRateQn; p.progStepCount = cfg.progStepCount;
                for (int i = 0; i < 8; ++i)
                {
                    p.progDegrees[(size_t) i] = cfg.progDegrees[(size_t) i];
                    p.progOctaves[(size_t) i] = cfg.progOctaves[(size_t) i];
                }
            }));
        if (cfg.hasShift)
            chainInto (add (ModuleType::Shift, [&] (ModuleParams& p)
            {
                p.shiftAmount = cfg.shiftAmount; p.root = cfg.shiftRoot; p.scale = cfg.shiftScale;
            }));
        if (cfg.hasMirror)
            chainInto (add (ModuleType::Mirror, [&] (ModuleParams& p)
            {
                p.mirrorCenter = cfg.mirrorCenter; p.mirrorLow = cfg.mirrorLow;
                p.mirrorHigh = cfg.mirrorHigh; p.mirrorBounds = cfg.mirrorBounds;
                p.root = cfg.mirrorRoot; p.scale = cfg.mirrorScale;
            }));
        if (cfg.hasQuantize)
            chainInto (add (ModuleType::Quantize, [&] (ModuleParams& p)
            {
                p.stepQn = cfg.quantStepQn; p.swing = cfg.quantSwing;
            }));
        if (cfg.hasDelay)
            chainInto (add (ModuleType::Delay, [&] (ModuleParams& p)
            {
                p.stepQn = cfg.delayTimeQn; p.delayFeedback = cfg.delayFeedback;
                p.delayShift = cfg.delayShift; p.root = cfg.delayRoot; p.scale = cfg.delayScale;
            }));
        // The Drone joins downstream of Quantize/Delay, mirroring the old
        // chain's deliberate bypass (a held pad is not material to re-time or
        // echo); a user is free to wire it differently in the plugin.
        if (cfg.hasDrone)
            heads.push_back (add (ModuleType::Drone, [&] (ModuleParams& p)
            {
                p.droneVoicing = cfg.droneVoicing; p.root = cfg.droneRoot;
                p.scale = cfg.droneScale; p.droneOctave = cfg.droneOctave;
                p.holdLengthQn = cfg.droneLengthQn; p.holdPeriodQn = cfg.dronePeriodQn;
            }));
        if (cfg.hasStrum)
            chainInto (add (ModuleType::Strum, [&] (ModuleParams& p)
            {
                p.strumSpreadSec = cfg.strumSpreadSec; p.mode = cfg.strumMode;
                p.strumCurve = cfg.strumCurve; p.strumVelTilt = cfg.strumVelTilt;
                p.strumJitter = cfg.strumJitter; p.repeatQn = cfg.strumRepeatQn;
            }));
        if (cfg.hasHumanize)
            chainInto (add (ModuleType::Humanize, [&] (ModuleParams& p)
            {
                p.stepQn = cfg.humanizeStepQn; p.swing = cfg.humanizeSwing;
                p.humanizeLayback = cfg.humanizeLayback; p.humanizeAccent = cfg.humanizeAccent;
                p.humanizeTimeJit = cfg.humanizeTimeJit; p.humanizeVelJit = cfg.humanizeVelJit;
                p.humanizeLenJit = cfg.humanizeLenJit;
            }));

        // Outputs: one sink per stamped channel, each fed by every chain tail
        // (fan-out). No flag = a channel-1 sink, since without any Output
        // nothing leaves the graph at all.
        auto addOutput = [&] (int ch)
        {
            const int idx = add (ModuleType::Output,
                                 [ch] (ModuleParams& p) { p.channel = ch; });
            for (int h : heads)
                g.nodes[(size_t) idx].inputs.push_back (h);
        };
        if (cfg.hasOutput && cfg.outChannelMask != 0)
        {
            for (int ch = 1; ch <= 16; ++ch)
                if ((cfg.outChannelMask & (1u << (ch - 1))) != 0)
                    addOutput (ch);
        }
        else
        {
            addOutput (1);
        }
        return g;
    }

    // Run one block through a graph laid out from the config. Rebuilt every
    // call (cheap), matching how the plugin republishes on settings edits.
    void processCfg (Engine& e, juce::MidiBuffer& midi, int numSamples,
                     const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                     int root, int scaleIndex, const TestConfig& cfg)
    {
        const auto g = graphFromConfig (cfg);
        e.process (midi, numSamples, pos, root, scaleIndex, &g);
    }

    void check (bool cond, const juce::String& what)
    {
        if (! cond)
        {
            std::cout << "  FAIL: " << what << std::endl;
            ++failures;
        }
    }

    // A position without a ppq value: the engine falls back to counting from
    // transport start, which is what most tests below rely on (their expected
    // samples are all relative to the moment play was pressed).
    juce::Optional<juce::AudioPlayHead::PositionInfo> playing (bool isPlaying, double bpm = 120.0)
    {
        juce::AudioPlayHead::PositionInfo pi;
        pi.setBpm (bpm);
        pi.setIsPlaying (isPlaying);
        return juce::Optional<juce::AudioPlayHead::PositionInfo> (pi);
    }

    // A full host position with a ppq value, for the anchoring tests.
    juce::Optional<juce::AudioPlayHead::PositionInfo> playingAt (double ppq, double bpm = 120.0)
    {
        juce::AudioPlayHead::PositionInfo pi;
        pi.setBpm (bpm);
        pi.setIsPlaying (true);
        pi.setPpqPosition (ppq);
        return juce::Optional<juce::AudioPlayHead::PositionInfo> (pi);
    }

    // Count note-ons / note-offs in a buffer.
    void tally (const juce::MidiBuffer& b, int& ons, int& offs)
    {
        for (const auto meta : b)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())  ++ons;
            if (m.isNoteOff()) ++offs;
        }
    }

    juce::MidiBuffer noteOnBuf (int note, int sample = 0, int chan = 1, int vel = 100)
    {
        juce::MidiBuffer b;
        b.addEvent (juce::MidiMessage::noteOn (chan, note, (juce::uint8) vel), sample);
        return b;
    }
}

int main()
{
    const int    block = 512;
    const double sr    = 44100.0;

    // --- 1. Default patch (MIDI In -> Output): pass-through -----------------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg;   // all false = the wired MIDI In -> Output pass-through
        auto midi = noteOnBuf (60);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        bool found60 = false;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn() && meta.getMessage().getNoteNumber() == 60)
                found60 = true;
        check (ons == 1 && found60, "MIDI In -> Output passes the note-on at pitch 60");
    }

    // --- 1b. Strict graph semantics: an empty graph is silent ---------------
    {
        Engine e; e.prepare (sr);
        GraphSnapshot empty;
        empty.topologyVersion = 1;
        auto midi = noteOnBuf (60);
        e.process (midi, block, playing (false), 0, 0, &empty);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        check (ons == 0 && offs == 0, "an empty graph drops the input (no implicit routing)");
    }

    // --- 1c. A source with no Output wired reaches nothing ------------------
    {
        Engine e; e.prepare (sr);
        GraphSnapshot g;
        g.topologyVersion = 1;
        GraphNode in;
        in.id = 1; in.type = ModuleType::MidiIn;   // admitted, but wired nowhere
        g.nodes.push_back (in);
        auto midi = noteOnBuf (60);
        e.process (midi, block, playing (false), 0, 0, &g);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        check (ons == 0, "a MIDI In wired to nothing emits nothing");
    }

    // --- 1d. Fan-out and fan-in: one source, two Shift branches, one sink ---
    {
        // MIDI In fans out into Shift +12 and Shift +7 (chromatic); both merge
        // into one Output. One played C4 must come out as both C5 and G4, and
        // the note-off must release both.
        Engine e; e.prepare (sr);
        GraphSnapshot g;
        g.topologyVersion = 1;
        auto addNode = [&g] (int id, ModuleType t)
        {
            GraphNode n; n.id = id; n.type = t;
            g.nodes.push_back (n);
            return (int) g.nodes.size() - 1;
        };
        const int inIdx  = addNode (1, ModuleType::MidiIn);
        const int upIdx  = addNode (2, ModuleType::Shift);
        const int dnIdx  = addNode (3, ModuleType::Shift);
        const int outIdx = addNode (4, ModuleType::Output);
        g.nodes[(size_t) upIdx].params.shiftAmount = 12;
        g.nodes[(size_t) upIdx].params.scale       = ModuleOptions::kScaleOff;
        g.nodes[(size_t) upIdx].inputs = { inIdx };
        g.nodes[(size_t) dnIdx].params.shiftAmount = 7;
        g.nodes[(size_t) dnIdx].params.scale       = ModuleOptions::kScaleOff;
        g.nodes[(size_t) dnIdx].inputs = { inIdx };
        g.nodes[(size_t) outIdx].params.channel = 1;
        g.nodes[(size_t) outIdx].inputs = { upIdx, dnIdx };

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        e.process (midi, block, playing (false), 0, 0, &g);
        std::vector<int> ons, offs;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())  ons.push_back (m.getNoteNumber());
            if (m.isNoteOff()) offs.push_back (m.getNoteNumber());
        }
        std::sort (ons.begin(), ons.end());
        std::sort (offs.begin(), offs.end());
        check (ons == std::vector<int> { 67, 72 },
               "fan-out through two Shift branches yields both shifted notes");
        check (offs == std::vector<int> { 67, 72 },
               "fan-in releases both branch notes on the one note-off");
    }

    // --- 1e. Topology change: sounding notes are cut, none hang -------------
    {
        // A note sounds through the default patch; the patch is then rewired
        // (new topologyVersion). The engine must release the sounding note at
        // once — and swallow the host's later note-off (its bookkeeping died
        // with the old topology).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        int ons = 0, offs = 0;
        {
            auto midi = noteOnBuf (60);
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        {
            auto g = graphFromConfig (cfg);
            g.topologyVersion = 2;   // the rewire
            juce::MidiBuffer midi;
            e.process (midi, block, playing (false), 0, 0, &g);
            tally (midi, ons, offs);
            juce::MidiBuffer offBuf;
            offBuf.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            e.process (offBuf, block, playing (false), 0, 0, &g);
            tally (offBuf, ons, offs);
        }
        check (ons == 1 && offs == 1,
               "a topology change releases the sounding note exactly once");
    }

    // --- 2. Shift: settings-driven transpose, off follows the on ------------
    {
        // Chromatic (scale Off): plain semitone transpose.
        auto shiftedPitches = [&] (int amount, int shiftScale, int inNote,
                                   int globalRoot, int globalScale)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasShift    = true;
            cfg.shiftAmount = amount;
            cfg.shiftScale  = shiftScale;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn  (1, inNote, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, inNote), 256);
            processCfg (e, midi, block, playing (false), globalRoot, globalScale, cfg);
            int onPitch = -1, offPitch = -1;
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())  onPitch  = m.getNoteNumber();
                if (m.isNoteOff()) offPitch = m.getNoteNumber();
            }
            return std::pair<int, int> (onPitch, offPitch);
        };

        auto [on, off] = shiftedPitches (12, ModuleOptions::kScaleOff, 60, 0, 0);
        check (on == 72,  "Shift chromatic +12 maps note-on 60 -> 72");
        check (off == 72, "Shift maps the note-off to 72 as well (no hang)");
        check (shiftedPitches (-3, ModuleOptions::kScaleOff, 60, 0, 0).first == 57,
               "Shift chromatic -3 maps 60 -> 57");

        // Scale degrees on the global scale (-1 = Global): C major, +2 degrees
        // from C is E; -1 degree from C is B below.
        check (shiftedPitches (2, -1, 60, 0, 0).first == 64,
               "Shift +2 degrees in C major maps C4 -> E4");
        check (shiftedPitches (-1, -1, 60, 0, 0).first == 59,
               "Shift -1 degree in C major maps C4 -> B3");
        // Out-of-scale input snaps into the scale as part of the degree walk.
        {
            const int p = shiftedPitches (1, -1, 61, 0, 0).first;
            check (ScaleTables::isInScale (p, 0, 0) && (p == 62 || p == 64),
                   "Shift by degrees snaps out-of-scale C#4 into C major first");
        }
        // Named-scale override wins over the global scale: +1 degree in D
        // major from D4 is E4, even with the global scale set elsewhere.
        check (shiftedPitches (1, 0, 62, 2, 7).first == 64,
               "Shift degree walk follows its own scale, not the global");
        // Amount 0 is a strict no-op even for out-of-scale notes.
        check (shiftedPitches (0, -1, 61, 0, 0).first == 61,
               "Shift amount 0 leaves an out-of-scale note untouched");
    }

    // --- 3. Scale modulator: snaps an out-of-scale note onto its scale -------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg; cfg.hasScaleMod = true;
        auto midi = noteOnBuf (61);   // C# — not in C major
        processCfg (e, midi, block, playing (false), /*root*/0, /*scale*/0, cfg);
        int p = -1;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) p = meta.getMessage().getNoteNumber();
        check (p == 60 || p == 62, "Scale mod snaps 61 to a C-major neighbour (60/62)");
        check (ScaleTables::isInScale (p, 0, 0), "snapped pitch is in scale");
    }
    {
        // Root/scale override wins over the globals: D major keeps F#4.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasScaleMod   = true;
        cfg.scaleModRoot  = 2;   // D
        cfg.scaleModScale = 0;   // Major
        auto midi = noteOnBuf (66);   // F# — in D major, not in C major
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        int p = -1;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) p = meta.getMessage().getNoteNumber();
        check (p == 66, "Scale mod override (D major) passes F#4 untouched");
    }

    // --- 3b. Quantize: re-times note-ons onto its grid, swing shifts odd steps
    {
        // A note played 1000 samples after transport start must wait for the
        // next grid point: 1/4 grid at 120bpm = 22050 samples. The host off
        // arrives 500 samples after the on, so the emitted note keeps that
        // duration (off at 22550).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasQuantize = true;
        cfg.quantStepQn = 1.0;   // 1/4 grid = 22050 samples at 120bpm/44.1k

        std::vector<std::pair<int, bool>> events;   // (absolute sample, isOn)
        int blockStart = 0;
        auto run = [&] (juce::MidiBuffer&& in)
        {
            juce::MidiBuffer midi (in);
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn() || meta.getMessage().isNoteOff())
                    events.push_back ({ blockStart + meta.samplePosition,
                                        meta.getMessage().isNoteOn() });
            blockStart += block;
        };

        {
            juce::MidiBuffer in;
            in.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 1000);
            in.addEvent (juce::MidiMessage::noteOff (1, 60), 1500);
            run (std::move (in));
        }
        for (int i = 0; i < 60; ++i)
            run (juce::MidiBuffer());

        check (events.size() == 2, "Quantize emits exactly one on/off pair");
        if (events.size() == 2)
        {
            check (events[0].second && events[0].first == 22050,
                   "Quantize defers the note-on to the next grid point (22050)");
            check (! events[1].second && events[1].first == 22550,
                   "Quantize keeps the played duration (off 500 samples later)");
        }
    }
    {
        // A note exactly on an even grid point passes with no delay.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasQuantize = true;
        cfg.quantStepQn = 1.0;
        auto midi = noteOnBuf (60, 0);
        processCfg (e, midi, block, playing (true), 0, 0, cfg);
        bool onAtZero = false;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn() && meta.samplePosition == 0)
                onAtZero = true;
        check (onAtZero, "Quantize passes an on-grid note through unmoved");
    }
    {
        // Swing: a generator running at the quantize rate keeps even steps in
        // place and lands odd steps late by swing/2 of a step (the pair-based
        // model). Scale gen at 1/4 + quantize 1/4 with 60% swing: step k sits
        // at k*22050, odd steps shifted +0.3*22050 = 6615.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasScaleGen    = true;
        cfg.scaleStepQn    = 1.0;
        cfg.scaleRepeatQn  = 0.0;
        cfg.hasQuantize    = true;
        cfg.quantStepQn    = 1.0;
        cfg.quantSwing     = 0.6;

        std::vector<int> onSamples;
        int blockStart = 0;
        for (int i = 0; i < 200 && onSamples.size() < 4; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    onSamples.push_back (blockStart + meta.samplePosition);
            blockStart += block;
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); }

        check (onSamples.size() == 4
                   && onSamples[0] == 0     && onSamples[1] == 22050 + 6615
                   && onSamples[2] == 44100 && onSamples[3] == 66150 + 6615,
               "Quantize 60% swing delays odd steps by 30% of a step");
    }
    {
        // Transport stop discards a deferred note: nothing ever sounds and
        // nothing hangs.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasQuantize = true;
        cfg.quantStepQn = 1.0;
        int ons = 0, offs = 0;
        {
            auto midi = noteOnBuf (60, 100);   // waits for sample 22050
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        check (ons == 0 && offs == 0,
               "Quantize: transport stop discards the deferred note");
    }

    // --- 3c. Progression: transposes to the current step's degree/octave -----
    {
        // Two steps (I, then V) at 1 qn each. A note at transport start passes
        // untouched (degree I is a strict no-op); a note one step later is
        // moved 4 scale degrees up (C4 -> G4 in C major).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasProgression = true;
        cfg.progRateQn     = 1.0;
        cfg.progStepCount  = 2;
        cfg.progDegrees[0] = 0; cfg.progOctaves[0] = 0;
        cfg.progDegrees[1] = 4; cfg.progOctaves[1] = 0;   // V

        std::vector<int> onPitches;
        for (int i = 0; i < 200 && onPitches.size() < 2; ++i)
        {
            auto midi = noteOnBuf (60, 0);
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    onPitches.push_back (meta.getMessage().getNoteNumber());
            // Release before the next block so each on is a fresh note.
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            processCfg (e, off, 22050 - block, playing (true), 0, 0, cfg);
        }
        check (onPitches.size() == 2 && onPitches[0] == 60 && onPitches[1] == 67,
               "Progression I -> V: C4 passes, then lands on G4");
    }
    {
        // Octave offset is chromatic: the degree walk (which would snap to the
        // scale) only runs for degree != 0, so step I at +1 octave lifts an
        // out-of-scale C#4 straight to C#5.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasProgression = true;
        cfg.progRateQn     = 4.0;
        cfg.progStepCount  = 1;
        cfg.progDegrees[0] = 0;
        cfg.progOctaves[0] = 1;
        auto midi = noteOnBuf (61);
        processCfg (e, midi, block, playing (true), 0, 0, cfg);
        int p = -1;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) p = meta.getMessage().getNoteNumber();
        { juce::MidiBuffer m2; processCfg (e, m2, block, playing (false), 0, 0, cfg); }
        check (p == 73, "Progression octave +1 lifts C#4 to C#5 chromatically");
    }

    // --- 4. Random generator: produces notes while playing, none hang -------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg; cfg.hasRandom = true;
        int ons = 0, offs = 0;
        for (int i = 0; i < 200; ++i)   // ~2.3 s at 120bpm / 512 blocks
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        // Stop: the engine must release everything still sounding.
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        check (ons > 0, "Random generated some notes while playing");
        check (ons == offs, "Random: every note-on balanced by a note-off after stop");
    }

    // --- 5. Arp: swallows held host notes, emits arp notes, all balanced ----
    {
        Engine e; e.prepare (sr);
        TestConfig cfg; cfg.hasArp = true;

        int ons = 0, offs = 0;
        // Hold a triad on the first block.
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            // Host notes are swallowed (arp input), so no pitch-60/64/67 passthrough
            // note-ons besides the arp's own — just tally overall balance below.
            tally (midi, ons, offs);
        }
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }

        check (ons > 0,     "Arp emitted notes from the held chord");
        check (ons == offs, "Arp: every note-on balanced by a note-off after stop");
    }

    // --- 5b. Arp settings: mode, octaves, repeat window, full gate ----------
    {
        // Collect the first `count` arp pitches for a held C-E-G triad under
        // the given settings.
        auto arpPitches = [&] (int mode, int octaves, double repeatQn, double gateFrac,
                               size_t count, int& onsOut, int& offsOut)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasArp      = true;
            cfg.arpMode     = mode;
            cfg.arpOctaves  = octaves;
            cfg.arpRepeatQn = repeatQn;
            cfg.arpGateFrac = gateFrac;
            cfg.arpStepQn   = 0.25;   // 1/16

            std::vector<int> pitches;
            int ons = 0, offs = 0;
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
                processCfg (e, midi, block, playing (true), 0, 0, cfg);
                tally (midi, ons, offs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            for (int i = 0; i < 2000 && pitches.size() < count; ++i)
            {
                juce::MidiBuffer midi;
                processCfg (e, midi, block, playing (true), 0, 0, cfg);
                tally (midi, ons, offs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }
            onsOut = ons; offsOut = offs;
            return pitches;
        };

        int ons = 0, offs = 0;
        check (arpPitches (ModuleOptions::kModeUp, 1, 0.0, 0.5, 6, ons, offs)
                   == std::vector<int> { 60, 64, 67, 60, 64, 67 },
               "Arp Up cycles the held triad ascending");
        check (arpPitches (ModuleOptions::kModeDown, 1, 0.0, 0.5, 6, ons, offs)
                   == std::vector<int> { 67, 64, 60, 67, 64, 60 },
               "Arp Down cycles the held triad descending");
        check (arpPitches (ModuleOptions::kModeUpDown, 1, 0.0, 0.5, 8, ons, offs)
                   == std::vector<int> { 60, 64, 67, 64, 60, 64, 67, 64 },
               "Arp Up-Down bounces without doubling the endpoints");
        check (arpPitches (ModuleOptions::kModeUp, 2, 0.0, 0.5, 7, ons, offs)
                   == std::vector<int> { 60, 64, 67, 72, 76, 79, 60 },
               "Arp octaves=2 extends the walk an octave up");
        // Repeat 1/2 = 8 sixteenth steps: the walk restarts mid-cycle at the
        // window boundary (after 60 64 67 60 64 67 60 64 comes 60 again).
        check (arpPitches (ModuleOptions::kModeUp, 1, 2.0, 0.5, 11, ons, offs)
                   == std::vector<int> { 60, 64, 67, 60, 64, 67, 60, 64,
                                         60, 64, 67 },
               "Arp repeat 1/2 resets the walk every 8 steps");
        // Full gate: the note-off is capped one sample short of the step, so
        // everything still balances.
        arpPitches (ModuleOptions::kModeUp, 1, 0.0, 1.0, 8, ons, offs);
        check (ons > 0 && ons == offs, "Arp 100% gate: balanced after stop");
    }

    // --- 6. MIDI In: channel filter drops non-matching input ----------------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasMidiIn = true;
        cfg.inChannelMask = (std::uint16_t) (1u << (2 - 1));   // channel 2 only

        auto midi = noteOnBuf (60, 0, /*chan*/1);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        check (ons == 0, "MIDI In on ch 2 drops a ch-1 note");

        auto midi2 = noteOnBuf (60, 0, /*chan*/2);
        processCfg (e, midi2, block, playing (false), 0, 0, cfg);
        ons = 0; offs = 0; tally (midi2, ons, offs);
        check (ons == 1, "MIDI In on ch 2 passes a ch-2 note");
    }

    // --- 7. Output: restamps channel, note-off follows ----------------------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) (1u << (5 - 1));   // channel 5

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        int onCh = -1, offCh = -1;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())  onCh  = m.getChannel();
            if (m.isNoteOff()) offCh = m.getChannel();
        }
        check (onCh == 5,  "Output restamps the note-on to ch 5");
        check (offCh == 5, "Output restamps the note-off to ch 5 (no hang)");
    }

    // --- 8. Two Outputs: stream duplicated onto both channels ---------------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) ((1u << (2 - 1)) | (1u << (3 - 1)));

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        check (ons == 2 && offs == 2, "two Outputs duplicate the note onto both channels");
    }

    // --- 9. Output channel changed mid-note: off still matches the on -------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) (1u << (2 - 1));   // on goes out on ch 2

        auto midi = noteOnBuf (60, 0, 1);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);

        cfg.outChannelMask = (std::uint16_t) (1u << (7 - 1));   // user edits to ch 7
        juce::MidiBuffer offBuf;
        offBuf.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        processCfg (e, offBuf, block, playing (false), 0, 0, cfg);

        int offCh = -1;
        for (const auto meta : offBuf)
            if (meta.getMessage().isNoteOff())
                offCh = meta.getMessage().getChannel();
        check (offCh == 2, "note-off released on the channel the note-on used (ch 2)");
    }

    // --- 10. Random through an Output: generated notes balanced on its ch ---
    {
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasRandom = true;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) (1u << (4 - 1));

        int ons = 0, offs = 0; bool allCh4 = true;
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().getChannel() != 4)
                    allCh4 = false;
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }
        check (ons > 0 && allCh4, "generated notes leave on the Output's channel");
        check (ons == offs, "generated notes balanced through the Output after stop");
    }

    // --- 11. Random settings: range and scale override are respected --------
    {
        // Pin the range to a single pitch with a chromatic scale: every note
        // the generator emits must be exactly that pitch.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasRandom   = true;
        cfg.randomScale = 8;    // Chromatic
        cfg.randomFrom  = 60;
        cfg.randomTo    = 60;

        int ons = 0, offs = 0; bool all60 = true;
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn() && meta.getMessage().getNoteNumber() != 60)
                    all60 = false;
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }
        check (ons > 0 && all60, "Random with range 60..60 emits only pitch 60");
        check (ons == offs, "Random settings: balanced after stop");
    }
    {
        // Root override D + major, wide range: everything lands in D major even
        // though the global root says C.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasRandom   = true;
        cfg.randomRoot  = 2;    // D
        cfg.randomScale = 0;    // Major
        cfg.randomFrom  = 48;
        cfg.randomTo    = 72;

        bool allInDMajor = true; bool inRange = true; int ons = 0, offs = 0;
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), /*global root*/0, /*global scale*/0, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                {
                    const int n = meta.getMessage().getNoteNumber();
                    if (! ScaleTables::isInScale (n, 2, 0)) allInDMajor = false;
                    if (n < 48 || n > 72) inRange = false;
                }
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); }
        check (ons > 0 && allInDMajor, "Random root/scale override draws from D major");
        check (inRange, "Random stays inside its note range");
    }

    // --- 12. Random rate: 1/8 emits half as many notes as 1/16 --------------
    {
        auto countOns = [&] (double stepQn)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasRandom = true;
            cfg.randomStepQn = stepQn;
            int ons = 0, offs = 0;
            for (int i = 0; i < 200; ++i)
            {
                juce::MidiBuffer midi;
                processCfg (e, midi, block, playing (true), 0, 0, cfg);
                tally (midi, ons, offs);
            }
            return ons;
        };
        const int at16 = countOns (0.25);
        const int at8  = countOns (0.5);
        check (at8 > 0 && std::abs (at16 - 2 * at8) <= 2,
               "Random 1/8 rate fires half as often as 1/16");
    }

    // --- 13. Scale generator: pattern, end-on, repeat, balance --------------
    {
        // C major, up, 1 octave, end on the octave root, 1/4 steps, repeat
        // every bar (4 steps): the pattern (8 notes) is cut to C D E F, then
        // repeats. Collect the first 8 note-ons and check the wrap.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasScaleGen   = true;
        cfg.scaleStepQn   = 1.0;   // 1/4 notes
        cfg.scaleRepeatQn = 4.0;   // 1 bar
        cfg.scaleOctaves  = 1;
        cfg.scaleEndOnRoot = true;

        std::vector<int> pitches;
        int ons = 0, offs = 0;
        for (int i = 0; i < 800 && pitches.size() < 8; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    pitches.push_back (meta.getMessage().getNoteNumber());
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }

        const std::vector<int> expected { 48, 50, 52, 53, 48, 50, 52, 53 };
        check (pitches == expected,
               "Scale gen: 1-bar repeat truncates C-major walk to C D E F and wraps");
        check (ons == offs, "Scale gen: balanced after stop");
    }
    {
        // Full pattern shape: 4-bar repeat window (16 steps) leaves room for
        // the whole octave + octave root, then rests. Down mode reverses it.
        auto firstPitches = [&] (bool down, bool endOnRoot, size_t count)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasScaleGen    = true;
            cfg.scaleStepQn    = 1.0;
            cfg.scaleRepeatQn  = 16.0;   // 4 bars = 16 steps
            cfg.scaleOctaves   = 1;
            cfg.scaleMode      = down ? ModuleOptions::kModeDown : ModuleOptions::kModeUp;
            cfg.scaleEndOnRoot = endOnRoot;

            std::vector<int> pitches;
            for (int i = 0; i < 2000 && pitches.size() < count; ++i)
            {
                juce::MidiBuffer midi;
                processCfg (e, midi, block, playing (true), 0, 0, cfg);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); }
            return pitches;
        };

        check (firstPitches (false, true, 8)
                   == std::vector<int> { 48, 50, 52, 53, 55, 57, 59, 60 },
               "Scale gen up + end-on-root walks C major and caps with C4");
        check (firstPitches (false, false, 7)
                   == std::vector<int> { 48, 50, 52, 53, 55, 57, 59 },
               "Scale gen end-on-7th stops at B (no octave root)");
        check (firstPitches (true, true, 8)
                   == std::vector<int> { 60, 59, 57, 55, 53, 52, 50, 48 },
               "Scale gen down plays the same notes reversed");
    }
    {
        // Endless repeat: the 8-note pattern loops back-to-back, no rests and
        // no truncation.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasScaleGen    = true;
        cfg.scaleStepQn    = 1.0;
        cfg.scaleRepeatQn  = 0.0;   // Endless
        cfg.scaleOctaves   = 1;
        cfg.scaleEndOnRoot = true;

        std::vector<int> pitches;
        for (int i = 0; i < 2000 && pitches.size() < 10; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    pitches.push_back (meta.getMessage().getNoteNumber());
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); }
        check (pitches == std::vector<int> { 48, 50, 52, 53, 55, 57, 59, 60, 48, 50 },
               "Scale gen Endless loops the pattern back-to-back");
    }

    // --- 14. LFO generator: shapes, depth, phase, balance --------------------
    {
        // Collect the first `count` LFO pitches under the given settings.
        // Defaults here: C major (globals), 1/4-note grid, 1-bar cycle -> a
        // 16-step cycle sampled every 4 steps... (stepQn 1.0, cycleQn 4.0 = 4
        // steps per cycle, so x advances 0, .25, .5, .75 per note).
        auto lfoPitches = [&] (int shape, int depthOct, int depthSteps,
                               double phaseFrac, size_t count,
                               int& onsOut, int& offsOut)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasLfo        = true;
            cfg.lfoShape      = shape;
            cfg.lfoDepthOct   = depthOct;
            cfg.lfoDepthSteps = depthSteps;
            cfg.lfoPhase      = phaseFrac;
            cfg.lfoStepQn     = 1.0;
            cfg.lfoCycleQn    = 4.0;

            std::vector<int> pitches;
            int ons = 0, offs = 0;
            for (int i = 0; i < 2000 && pitches.size() < count; ++i)
            {
                juce::MidiBuffer midi;
                processCfg (e, midi, block, playing (true), 0, 0, cfg);
                tally (midi, ons, offs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }
            onsOut = ons; offsOut = offs;
            return pitches;
        };

        int ons = 0, offs = 0;

        // Square, depth 1 octave, C major: +1 octave for the first half cycle,
        // -1 octave for the second (centre C3 = 48, so 60 60 36 36).
        check (lfoPitches (ModuleOptions::kLfoSquare, 1, 0, 0.0, 4, ons, offs)
                   == std::vector<int> { 60, 60, 36, 36 },
               "LFO square depth 1 oct alternates centre +/- an octave");
        check (ons == offs, "LFO: balanced after stop");

        // Phase 180 starts the square in its low half.
        check (lfoPitches (ModuleOptions::kLfoSquare, 1, 0, 0.5, 4, ons, offs)
                   == std::vector<int> { 36, 36, 60, 60 },
               "LFO phase 180 starts the square half a cycle in");

        // Sine at x = 0, .25, .5, .75: values 0, +1, 0, -1 -> C3, C4, C3, C2.
        check (lfoPitches (ModuleOptions::kLfoSine, 1, 0, 0.0, 4, ons, offs)
                   == std::vector<int> { 48, 60, 48, 36 },
               "LFO sine sweeps centre, +1 oct, centre, -1 oct");

        // Saw Up starts at -1 and climbs; x = 0, .25, .5, .75 -> -1, -.5, 0,
        // +.5. Depth 2 steps: -2, -1, 0, +1 degrees from C3 in C major.
        check (lfoPitches (ModuleOptions::kLfoSawUp, 0, 2, 0.0, 4, ons, offs)
                   == std::vector<int> { 45, 47, 48, 50 },
               "LFO saw up climbs through scale degrees");

        // Depth 0: every note is the centre.
        check (lfoPitches (ModuleOptions::kLfoTriangle, 0, 0, 0.0, 4, ons, offs)
                   == std::vector<int> { 48, 48, 48, 48 },
               "LFO depth 0 pins every note to the centre");

        // Depth mixes octaves and steps: 1 oct + 2 steps in C major = 9
        // degrees; the square's opening +1 lands 9 degrees above C3, which is
        // E4 (C3 +7 = C4, +8 = D4, +9 = E4 = 64).
        check (lfoPitches (ModuleOptions::kLfoSquare, 1, 2, 0.0, 1, ons, offs).front() == 64,
               "LFO depth 1 oct + 2 steps reaches 9 degrees up");

        // Random shape: still in scale, still balanced.
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasLfo   = true;
            cfg.lfoShape = ModuleOptions::kLfoRandom;
            bool allInScale = true;
            int rons = 0, roffs = 0;
            for (int i = 0; i < 200; ++i)
            {
                juce::MidiBuffer midi;
                processCfg (e, midi, block, playing (true), 0, 0, cfg);
                tally (midi, rons, roffs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn()
                        && ! ScaleTables::isInScale (meta.getMessage().getNoteNumber(), 0, 0))
                        allInScale = false;
            }
            { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, rons, roffs); }
            check (rons > 0 && allInScale, "LFO random shape stays in scale");
            check (rons == roffs, "LFO random shape: balanced after stop");
        }
    }

    // --- 14b. Chord generator: diatonic stack, inversion, legato repeat ------
    {
        // Collect the first `count` note-ons for a chord with the given
        // degree/type/inversion at a 1-bar length and period (legato).
        auto chordOns = [&] (int degree, int type, int inversion, size_t count)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasChord       = true;
            cfg.chordDegree    = degree;
            cfg.chordType      = type;
            cfg.chordInversion = inversion;
            cfg.chordLengthQn  = 4.0;
            cfg.chordPeriodQn  = 4.0;

            std::vector<int> pitches;
            int ons = 0, offs = 0;
            for (int i = 0; i < 400 && pitches.size() < count; ++i)
            {
                juce::MidiBuffer midi;
                processCfg (e, midi, block, playing (true), 0, 0, cfg);
                tally (midi, ons, offs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }
            check (ons == offs, "Chord: balanced after stop");
            return pitches;
        };

        // I triad in C major from the C3 centre, repeating each bar.
        check (chordOns (0, 0, 0, 6) == std::vector<int> { 48, 52, 55, 48, 52, 55 },
               "Chord I triad in C major = C3 E3 G3, repeating legato");
        // V 7th stays diatonic: G B D F.
        check (chordOns (4, 1, 0, 4) == std::vector<int> { 55, 59, 62, 65 },
               "Chord V 7th in C major = G B D F");
        // 1st inversion lifts the lowest tone an octave (emitted in stack order).
        check (chordOns (0, 0, 1, 3) == std::vector<int> { 60, 52, 55 },
               "Chord 1st inversion lifts the root an octave");
        // The 5th (power chord) is a two-note stack.
        check (chordOns (0, 4, 0, 2) == std::vector<int> { 48, 55 },
               "Chord 5th emits the two-note power chord");
    }

    // --- 14c. Drone: holds its voicing, re-triggers on harmony change --------
    {
        // Root voicing holds one note; a mid-hold root change must release it
        // and start the new pitch immediately (not wait for the next period).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasDrone      = true;
        cfg.droneVoicing  = ModuleOptions::kVoicingRoot;
        cfg.droneLengthQn = 16.0;   // 4 bars
        cfg.dronePeriodQn = 16.0;

        std::vector<std::pair<int, bool>> events;   // (pitch, isOn)
        auto run = [&] (bool isPlaying)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (isPlaying), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn() || meta.getMessage().isNoteOff())
                    events.push_back ({ meta.getMessage().getNoteNumber(),
                                        meta.getMessage().isNoteOn() });
        };

        run (true);
        check (events == std::vector<std::pair<int, bool>> { { 48, true } },
               "Drone starts holding its root (C3) at transport start");

        for (int i = 0; i < 10; ++i) run (true);
        check (events.size() == 1, "Drone keeps holding steadily mid-period");

        cfg.droneRoot = 2;   // user edits the root to D mid-hold
        run (true);
        check (events == std::vector<std::pair<int, bool>> {
                   { 48, true }, { 48, false }, { 50, true } },
               "Drone re-triggers immediately when the root changes");

        run (false);
        check (events.size() == 4 && events[3] == std::pair<int, bool> (50, false),
               "Drone: transport stop releases the held note");
    }
    {
        // Voicing spot checks: triad stacks scale degrees, Root+5th holds the
        // snapped perfect fifth.
        auto droneOns = [&] (int voicing)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasDrone     = true;
            cfg.droneVoicing = voicing;
            std::vector<int> pitches;
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    pitches.push_back (meta.getMessage().getNoteNumber());
            { juce::MidiBuffer m2; processCfg (e, m2, block, playing (false), 0, 0, cfg); }
            return pitches;
        };
        check (droneOns (ModuleOptions::kVoicingTriad) == std::vector<int> { 48, 52, 55 },
               "Drone triad voicing holds C3 E3 G3 in C major");
        check (droneOns (ModuleOptions::kVoicingRootFifth) == std::vector<int> { 48, 55 },
               "Drone Root+5th holds the perfect fifth in C major");
        check (droneOns (ModuleOptions::kVoicingRootOctave) == std::vector<int> { 48, 60 },
               "Drone Root+Octave holds C3 and C4");
    }

    // --- 15. Delay: echo decay, per-echo shift, chain termination ------------
    {
        // One host note (on + off), then empty blocks until the echo chain has
        // played out; collect the echoes' (pitch, velocity) pairs.
        auto delayEchoes = [&] (double fb, int shift, int srcNote,
                                int& onsOut, int& offsOut)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasDelay      = true;
            cfg.delayTimeQn   = 0.5;   // 1/8 at 120bpm = 11025 samples
            cfg.delayFeedback = fb;
            cfg.delayShift    = shift;
            // Scale Off = the per-echo shift moves in raw semitones, which is
            // what these pitch checks assume (the degree mode is checked below).
            cfg.delayScale    = ModuleOptions::kScaleOff;

            std::vector<std::pair<int, int>> echoes;
            int ons = 0, offs = 0;
            auto collect = [&] (const juce::MidiBuffer& b, bool skipSource)
            {
                for (const auto meta : b)
                    if (meta.getMessage().isNoteOn())
                        if (! (skipSource && meta.getMessage().getNoteNumber() == srcNote
                                          && meta.getMessage().getVelocity() == 100))
                            echoes.push_back ({ meta.getMessage().getNoteNumber(),
                                                (int) meta.getMessage().getVelocity() });
            };
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn  (1, srcNote, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOff (1, srcNote), 256);
                processCfg (e, midi, block, playing (false), 0, 0, cfg);
                tally (midi, ons, offs);
                collect (midi, true);
            }
            for (int i = 0; i < 600; ++i)
            {
                juce::MidiBuffer midi;
                processCfg (e, midi, block, playing (false), 0, 0, cfg);
                tally (midi, ons, offs);
                collect (midi, false);
            }
            onsOut = ons; offsOut = offs;
            return echoes;
        };

        int ons = 0, offs = 0;

        // Feedback 50%: velocities halve (roundToInt rounds half-to-even, so
        // 12.5 -> 12) until they cross the floor of 5 — four echoes, all at
        // the source pitch with shift 0.
        check (delayEchoes (0.5, 0, 60, ons, offs)
                   == std::vector<std::pair<int, int>> { { 60, 50 }, { 60, 25 },
                                                         { 60, 12 }, { 60, 6 } },
               "Delay fb 50%: four echoes with halving velocities");
        check (ons == 5 && ons == offs, "Delay: source + echoes all balanced");

        // Shift +12: each echo an octave above the one before.
        {
            const auto e12 = delayEchoes (0.5, 12, 60, ons, offs);
            check (e12.size() == 4
                       && e12[0].first == 72 && e12[1].first == 84
                       && e12[2].first == 96 && e12[3].first == 108,
                   "Delay shift +12 climbs an octave per echo");
        }

        // Lower feedback = fewer repeats (25%: 25, 6 -> two echoes).
        check (delayEchoes (0.25, 0, 60, ons, offs).size() == 2,
               "Delay fb 25% yields a shorter chain");

        // An echo that would leave the MIDI range ends the chain (no clamp).
        check (delayEchoes (0.5, 12, 120, ons, offs).empty(),
               "Delay shift past 127 ends the chain instead of clamping");

        // With a scale active the shift moves in degrees: +1 degree per echo in
        // C Major from C3 climbs C3 -> D3 -> E3 -> F3 (the diatonic steps).
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasDelay      = true;
            cfg.delayTimeQn   = 0.5;
            cfg.delayFeedback = 0.5;
            cfg.delayShift    = 1;
            cfg.delayScale    = -1;   // Global = C Major here
            std::vector<int> pitches;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn  (1, 48, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 48), 256);
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            for (int i = 0; i < 600; ++i)
            {
                juce::MidiBuffer m;
                processCfg (e, m, block, playing (false), 0, 0, cfg);
                for (const auto meta : m)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            check (pitches == std::vector<int> { 50, 52, 53, 55 },
                   "Delay shift +1 with a scale climbs by scale degrees");
        }
    }

    // --- 15b. Delay: transport stop discards buffered echoes -----------------
    {
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasDelay    = true;
        cfg.delayTimeQn = 0.5;

        int ons = 0, offs = 0;
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
            processCfg (e, midi, block, playing (true), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        // Stop before the first echo (11025 samples away) fires: the pending
        // echo is discarded, so only the source note ever sounds.
        for (int i = 0; i < 200; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        check (ons == 1 && offs == 1,
               "Delay: transport stop discards the buffered echo");
    }

    // --- 15c. Humanize: neutral pass-through, accent, swing warp, balance ----
    {
        // All amounts zero: the note passes through untouched in time, pitch,
        // and velocity (and balanced).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasHumanize = true;   // every humanize* amount defaults to 0
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        processCfg (e, midi, block, playingAt (0.0), 0, 0, cfg);
        int onS = -1, onV = -1, offS = -1, onP = -1;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())  { onS = meta.samplePosition; onV = m.getVelocity(); onP = m.getNoteNumber(); }
            if (m.isNoteOff()) { offS = meta.samplePosition; }
        }
        check (onS == 0 && onP == 60 && onV == 100 && offS == 256,
               "Humanize neutral (0%): note passes through untouched");
    }
    {
        // Accent: on a strong beat (even step of the pair) velocity is boosted,
        // on a weak beat (odd step) it is cut. Full accent = +/-40%.
        auto accentVel = [&] (double ppq)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasHumanize    = true;
            cfg.humanizeStepQn = 0.25;   // the beat grid
            cfg.humanizeAccent = 1.0;    // full depth
            auto midi = noteOnBuf (60, 0);
            processCfg (e, midi, block, playingAt (ppq), 0, 0, cfg);
            int v = -1;
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn()) v = meta.getMessage().getVelocity();
            return v;
        };
        check (accentVel (0.0) == 127,
               "Humanize accent lifts a strong-beat note (100 * 1.4 -> clamp 127)");
        check (accentVel (0.25) == 60,
               "Humanize accent cuts a weak-beat note (100 * 0.6 = 60)");
    }
    {
        // Swing warp: a note on an even (on-beat) grid boundary stays put; a
        // note on an odd (off-beat) boundary is pushed late by swing/2 of a
        // step, exactly like the Quantize swing but as a nudge, not a snap.
        // 1/4 grid at 120bpm = 22050 samples; 60% swing = +0.3 step = 6615.
        auto swungOnset = [&] (double ppq)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasHumanize    = true;
            cfg.humanizeStepQn = 1.0;
            cfg.humanizeSwing  = 0.6;
            int abs = -1, blockStart = 0;
            for (int i = 0; i < 40 && abs < 0; ++i)
            {
                juce::MidiBuffer midi;
                if (i == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                processCfg (e, midi, block, playingAt (ppq + (double) blockStart / 22050.0), 0, 0, cfg);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        abs = blockStart + meta.samplePosition;
                blockStart += block;
            }
            { juce::MidiBuffer m; processCfg (e, m, block, playing (false), 0, 0, cfg); }
            return abs;
        };
        check (swungOnset (0.0) == 0,
               "Humanize swing leaves an on-beat note in place");
        check (swungOnset (1.0) == 6615,
               "Humanize swing nudges an off-beat note late by swing/2 of a step");
    }
    {
        // The whole warp + buffer + transport-stop path stays note-balanced: run
        // a Random generator through a fully-cranked Humanize for a while, then
        // stop. Every note-on must still be matched by a note-off.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasRandom       = true;
        cfg.hasHumanize     = true;
        cfg.humanizeStepQn  = 0.25;
        cfg.humanizeSwing   = 0.7;
        cfg.humanizeLayback = 0.5;
        cfg.humanizeAccent  = 0.5;
        cfg.humanizeTimeJit = 0.6;
        cfg.humanizeVelJit  = 0.6;
        cfg.humanizeLenJit  = 0.6;

        int ons = 0, offs = 0;
        double qn = 0.0;
        for (int i = 0; i < 300; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playingAt (qn), 0, 0, cfg);
            tally (midi, ons, offs);
            qn += (double) block / 22050.0;
        }
        // A stop, then idle blocks to let any buffered offs drain.
        for (int i = 0; i < 20; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            tally (midi, ons, offs);
        }
        check (ons > 0, "Humanize: notes flowed through while playing");
        check (ons == offs, "Humanize: every note-on balanced by a note-off after stop");
    }
    {
        // Determinism: feeding the SAME fixed note stream through Humanize over
        // the same song positions must produce byte-identical output on two runs
        // — that is what makes the humanized feel repeat on every loop pass
        // instead of shimmering. (A fixed host note stream, not the stochastic
        // Random generator, so the only variable under test is Humanize itself.)
        auto run = [&] ()
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasHumanize     = true;
            cfg.humanizeStepQn  = 0.25;
            cfg.humanizeTimeJit = 0.7;
            cfg.humanizeVelJit  = 0.7;
            cfg.humanizeLenJit  = 0.7;
            std::vector<std::tuple<int, int, int>> evs;   // (absSample, pitch, vel)
            int blockStart = 0;
            double qn = 0.0;
            for (int i = 0; i < 120; ++i)
            {
                juce::MidiBuffer midi;
                // One fixed note per block: on at the block start, off mid-block.
                midi.addEvent (juce::MidiMessage::noteOn  (1, 60 + (i % 5), (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOff (1, 60 + (i % 5)), 200);
                processCfg (e, midi, block, playingAt (qn), 0, 0, cfg);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        evs.push_back ({ blockStart + meta.samplePosition,
                                         meta.getMessage().getNoteNumber(),
                                         (int) meta.getMessage().getVelocity() });
                blockStart += block;
                qn += (double) block / 22050.0;
            }
            return evs;
        };
        check (run() == run() && ! run().empty(),
               "Humanize jitter is deterministic (repeats identically per loop)");
    }

    // --- 15d. Strum: fans a chord over the spread window, direction, balance -
    {
        // A 3-note chord played together, spread 100 ms. Collect the resulting
        // note-ons (absolute sample, pitch, velocity) as the fan plays out, then
        // release and drain; report the on/off tally.
        auto runStrum = [&] (int mode, double spreadSec, double velTilt,
                             std::vector<std::tuple<int, int, int>>& ons,
                             int& onsN, int& offsN)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasStrum       = true;
            cfg.strumMode      = mode;
            cfg.strumSpreadSec = spreadSec;
            cfg.strumVelTilt   = velTilt;
            int blockStart = 0, on = 0, off = 0;
            auto pump = [&] (juce::MidiBuffer&& in)
            {
                juce::MidiBuffer midi (in);
                processCfg (e, midi, block, playing (false), 0, 0, cfg);
                for (const auto meta : midi)
                {
                    const auto m = meta.getMessage();
                    if (m.isNoteOn())
                    { ons.push_back ({ blockStart + meta.samplePosition, m.getNoteNumber(), (int) m.getVelocity() }); ++on; }
                    if (m.isNoteOff()) ++off;
                }
                blockStart += block;
            };
            {
                juce::MidiBuffer in;
                in.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                in.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
                in.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
                pump (std::move (in));
            }
            for (int i = 0; i < 20; ++i) pump (juce::MidiBuffer());
            {
                juce::MidiBuffer in;
                in.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                in.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
                in.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
                pump (std::move (in));
            }
            for (int i = 0; i < 20; ++i) pump (juce::MidiBuffer());
            onsN = on; offsN = off;
        };

        {
            std::vector<std::tuple<int, int, int>> ons; int on = 0, off = 0;
            runStrum (ModuleOptions::kModeUp, 0.10, 0.0, ons, on, off);
            check (ons.size() == 3, "Strum emits one note-on per chord note");
            if (ons.size() == 3)
            {
                check (std::get<1> (ons[0]) == 60 && std::get<1> (ons[1]) == 64
                           && std::get<1> (ons[2]) == 67,
                       "Strum Up fans the chord low to high");
                check (std::get<0> (ons[0]) < std::get<0> (ons[1])
                           && std::get<0> (ons[1]) < std::get<0> (ons[2]),
                       "Strum spreads the notes out in time");
                check (std::get<0> (ons[0]) == (int) std::llround (0.03 * sr),
                       "Strum's first note lands after the fixed detection window");
            }
            check (on == 3 && on == off, "Strum: chord balanced after release");
        }
        {
            std::vector<std::tuple<int, int, int>> ons; int on = 0, off = 0;
            runStrum (ModuleOptions::kModeDown, 0.10, 0.0, ons, on, off);
            check (ons.size() == 3 && std::get<1> (ons[0]) == 67 && std::get<1> (ons[2]) == 60,
                   "Strum Down fans the chord high to low");
        }
        {
            std::vector<std::tuple<int, int, int>> ons; int on = 0, off = 0;
            runStrum (ModuleOptions::kModeUp, 0.10, 1.0, ons, on, off);
            check (ons.size() == 3 && std::get<2> (ons[2]) > std::get<2> (ons[0]),
                   "Strum velocity tilt +100% swells across the fan");
        }
    }
    {
        // Spread 0 = bypass: the chord passes through together at sample 0.
        Engine e; e.prepare (sr);
        TestConfig cfg; cfg.hasStrum = true; cfg.strumSpreadSec = 0.0;
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        int ons = 0, at0 = 0;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) { ++ons; if (meta.samplePosition == 0) ++at0; }
        check (ons == 2 && at0 == 2, "Strum spread 0 passes the chord through together (bypass)");
    }
    {
        // Repeat: a held chord is re-strummed on the 1/2-bar grid — more strikes
        // than the chord has notes — and it all balances once released + stopped.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasStrum       = true;
        cfg.strumSpreadSec = 0.02;
        cfg.strumRepeatQn  = 2.0;   // 1/2 bar
        int on = 0, off = 0;
        double qn = 0.0;
        auto pump = [&] (juce::MidiBuffer&& in, bool isPlaying)
        {
            juce::MidiBuffer midi (in);
            processCfg (e, midi, block, isPlaying ? playingAt (qn) : playing (false), 0, 0, cfg);
            for (const auto meta : midi)
            {
                if (meta.getMessage().isNoteOn())  ++on;
                if (meta.getMessage().isNoteOff()) ++off;
            }
            if (isPlaying) qn += (double) block / 22050.0;
        };
        {
            juce::MidiBuffer in;
            in.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            in.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            in.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
            pump (std::move (in), true);
        }
        for (int i = 0; i < 150; ++i) pump (juce::MidiBuffer(), true);
        {
            juce::MidiBuffer in;
            in.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            in.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
            in.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
            pump (std::move (in), true);
        }
        for (int i = 0; i < 40; ++i) pump (juce::MidiBuffer(), true);    // drain the delayed offs
        for (int i = 0; i < 10; ++i) pump (juce::MidiBuffer(), false);   // stop
        check (on > 3, "Strum Repeat re-strums the held chord (more strikes than notes)");
        check (on == off, "Strum Repeat: balanced after release and stop");
    }

    // --- 16. Host-position anchoring: mid-bar start and loop wrap ------------
    {
        // Play pressed mid-bar (host ppq starts at 2.5): the first step must
        // land on the song's next grid point — beat 3, half a beat = 11025
        // samples into playback — not at sample 0. And the pattern joins
        // mid-window: with a Scale gen at 1/4 steps and a 1-bar repeat
        // (window C D E F), beat 3 is the window's 4th step, F (53).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasScaleGen    = true;
        cfg.scaleStepQn    = 1.0;
        cfg.scaleRepeatQn  = 4.0;
        cfg.scaleOctaves   = 1;
        cfg.scaleEndOnRoot = true;

        const double qnPerBlock = (double) block / 22050.0;
        double qn = 2.5;
        int firstOnAbs = -1, firstPitch = -1, blockStart = 0;
        for (int i = 0; i < 200 && firstOnAbs < 0; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playingAt (qn), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                {
                    firstOnAbs = blockStart + meta.samplePosition;
                    firstPitch = meta.getMessage().getNoteNumber();
                    break;
                }
            blockStart += block;
            qn += qnPerBlock;
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); }
        check (firstOnAbs == 11025,
               "mid-bar start: first step lands on the song's next grid point");
        check (firstPitch == 53,
               "mid-bar start: pattern joins the repeat window mid-way (F)");
    }
    {
        // Host loop wrap: the ppq jumps back and the pattern position must
        // follow. The "host" loops the first half of bar 0 (ppq 0..2) over a
        // Scale gen with a 1-bar window (C D E F): only C and D may ever
        // sound, alternating — freewheeling counters would drift onto E/F.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasScaleGen    = true;
        cfg.scaleStepQn    = 1.0;
        cfg.scaleRepeatQn  = 4.0;
        cfg.scaleOctaves   = 1;
        cfg.scaleEndOnRoot = true;

        const double qnPerBlock = (double) block / 22050.0;
        double qn = 0.0;
        std::vector<int> pitches;
        for (int i = 0; i < 800 && pitches.size() < 6; ++i)
        {
            if (qn + qnPerBlock > 2.0)
                qn = 0.0;   // the wrap
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playingAt (qn), 0, 0, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    pitches.push_back (meta.getMessage().getNoteNumber());
            qn += qnPerBlock;
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); }
        check (pitches == std::vector<int> { 48, 50, 48, 50, 48, 50 },
               "loop wrap: pattern position follows the host ppq back");
    }
    {
        // The processor-level fallback contract (finding 2): a synthesized
        // position with isPlaying true and an accumulating ppq drives the
        // generators exactly like a host transport. (A null position still
        // produces nothing — that is the processor's cue to synthesize.)
        Engine e; e.prepare (sr);
        TestConfig cfg; cfg.hasRandom = true;
        int ons = 0, offs = 0;
        double qn = 0.0;
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            processCfg (e, midi, block, playingAt (qn), 0, 0, cfg);
            tally (midi, ons, offs);
            qn += (double) block / 22050.0;
        }
        { juce::MidiBuffer midi; processCfg (e, midi, block, playing (false), 0, 0, cfg); tally (midi, ons, offs); }
        check (ons > 0 && ons == offs,
               "synthesized internal-transport position drives the generators");

        juce::MidiBuffer midi;
        juce::Optional<juce::AudioPlayHead::PositionInfo> nullPos;
        processCfg (e, midi, block, nullPos, 0, 0, cfg);
        int nOns = 0, nOffs = 0; tally (midi, nOns, nOffs);
        check (nOns == 0, "a null position (no playhead) generates nothing");
    }

    // --- 16b. Mirror: invert around a centre, then window the result --------
    {
        // One note through a Mirror, reading back the emitted on/off pitches.
        auto mirrored = [&] (int inNote, int center, int low, int high,
                             int bounds, int mScale, int mRoot,
                             int globalRoot, int globalScale)
        {
            Engine e; e.prepare (sr);
            TestConfig cfg;
            cfg.hasMirror    = true;
            cfg.mirrorCenter = center;
            cfg.mirrorLow    = low;
            cfg.mirrorHigh   = high;
            cfg.mirrorBounds = bounds;
            cfg.mirrorScale  = mScale;
            cfg.mirrorRoot   = mRoot;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn  (1, inNote, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, inNote), 256);
            processCfg (e, midi, block, playing (false), globalRoot, globalScale, cfg);
            int on = -1, off = -1, ons = 0;
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())  { on = m.getNoteNumber(); ++ons; }
                if (m.isNoteOff()) off = m.getNoteNumber();
            }
            return std::tuple<int, int, int> (on, off, ons);
        };

        // Chromatic invert around C4 (60): E4 (64) -> A♭3 (56), a wide window so
        // no folding. Note-off follows the note-on (no hang).
        {
            auto [on, off, ons] = mirrored (64, 60, 24, 96,
                                            ModuleOptions::kMirrorLimit,
                                            ModuleOptions::kScaleOff, -1, 0, 0);
            check (on == 56 && off == 56 && ons == 1,
                   "Mirror chromatic: E4 inverts around C4 to A flat 3 (56)");
        }
        // Diatonic invert in C major around C4: E4 is +2 degrees, so it mirrors
        // to -2 degrees = A3 (57), staying in key (chromatic would give 56).
        {
            auto [on, off, ons] = mirrored (64, 60, 24, 96,
                                            ModuleOptions::kMirrorLimit,
                                            -1 /*global scale*/, -1, 0, 0);
            check (on == 57 && off == 57,
                   "Mirror diatonic: E4 mirrors to A3 (57) in C major, in key");
        }
        // Centre Off = no inversion: the note only meets the window (in range,
        // so it passes untouched).
        {
            auto [on, off, ons] = mirrored (64, ModuleOptions::kMirrorCenterOff,
                                            24, 96, ModuleOptions::kMirrorLimit,
                                            ModuleOptions::kScaleOff, -1, 0, 0);
            check (on == 64, "Mirror centre Off passes the note through the window");
        }
        // Limit drops an out-of-window result: invert 72 around 60 -> 48, window
        // 55..96 excludes it, so nothing is emitted (and no dangling note-off).
        {
            auto [on, off, ons] = mirrored (72, 60, 55, 96,
                                            ModuleOptions::kMirrorLimit,
                                            ModuleOptions::kScaleOff, -1, 0, 0);
            check (ons == 0 && off == -1,
                   "Mirror Limit drops an out-of-window note (no on, no off)");
        }
        // Fold reflects that same out-of-window result back in: 48 is below the
        // low edge 55 by 7, so it folds to 55 + 7 = 62.
        {
            auto [on, off, ons] = mirrored (72, 60, 55, 96,
                                            ModuleOptions::kMirrorFold,
                                            ModuleOptions::kScaleOff, -1, 0, 0);
            check (on == 62 && off == 62,
                   "Mirror Fold reflects a below-window note back inside (62)");
        }
    }

    // --- 16c. Harmonizer: stacks a chord on the played note, modes, balance --
    {
        // Add mode, Triad in C major: playing C4 (60) yields C4 + E4 + G4, and
        // releasing the key releases all three (no hang).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasHarmonizer = true;
        cfg.harmType      = 0;   // Triad
        cfg.harmMode      = ModuleOptions::kHarmAdd;
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        std::vector<int> ons, offs;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())  ons.push_back (m.getNoteNumber());
            if (m.isNoteOff()) offs.push_back (m.getNoteNumber());
        }
        std::sort (ons.begin(), ons.end());
        std::sort (offs.begin(), offs.end());
        check (ons == std::vector<int> { 60, 64, 67 },
               "Harmonizer Add: C4 triad in C major = C4 E4 G4");
        check (offs == std::vector<int> { 60, 64, 67 },
               "Harmonizer Add: releasing the key releases the whole stack (no hang)");
    }
    {
        // Scale Off = fixed chromatic intervals: a Triad on C# (61) is a major
        // triad in semitones, C#-F-G# (61, 65, 68), regardless of key.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasHarmonizer = true;
        cfg.harmType      = 0;   // Triad
        cfg.harmScale     = ModuleOptions::kScaleOff;
        auto midi = noteOnBuf (61);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        std::vector<int> ons;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) ons.push_back (meta.getMessage().getNoteNumber());
        std::sort (ons.begin(), ons.end());
        check (ons == std::vector<int> { 61, 65, 68 },
               "Harmonizer Scale=Off stacks fixed chromatic intervals on any note");
    }
    {
        // Replace mode (Octave type): a new note cuts the previous note and its
        // stack. Play C4 (-> C4 C5), then C3 (-> cut C4/C5, sound C3 C4).
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasHarmonizer = true;
        cfg.harmType      = ModuleOptions::kHarmOctave;
        cfg.harmMode      = ModuleOptions::kHarmReplace;
        int on = 0, off = 0;
        auto pump = [&] (juce::MidiBuffer&& in, std::vector<int>* onsOut, std::vector<int>* offsOut)
        {
            juce::MidiBuffer midi (in);
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())  { ++on;  if (onsOut)  onsOut->push_back (m.getNoteNumber()); }
                if (m.isNoteOff()) { ++off; if (offsOut) offsOut->push_back (m.getNoteNumber()); }
            }
        };
        std::vector<int> b1, b2offs, b2ons;
        pump (noteOnBuf (60), &b1, nullptr);
        std::sort (b1.begin(), b1.end());
        check (b1 == std::vector<int> { 60, 72 }, "Harmonizer Replace: first note sounds with its octave");
        pump (noteOnBuf (48), &b2ons, &b2offs);
        std::sort (b2offs.begin(), b2offs.end());
        std::sort (b2ons.begin(), b2ons.end());
        check (b2offs == std::vector<int> { 60, 72 },
               "Harmonizer Replace: a new note cuts the previous note and its stack");
        check (b2ons == std::vector<int> { 48, 60 },
               "Harmonizer Replace: the new note sounds with its own stack");
        // Release both keys, then check nothing hung (the cut C4's off finds no
        // match and is harmless).
        {
            juce::MidiBuffer in;
            in.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            in.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
            pump (std::move (in), nullptr, nullptr);
        }
        check (on == off, "Harmonizer Replace: balanced after release (no hang)");
    }
    {
        // Top mode (Octave type): only the highest held note is harmonised.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasHarmonizer = true;
        cfg.harmType      = ModuleOptions::kHarmOctave;
        cfg.harmMode      = ModuleOptions::kHarmTop;
        int on = 0, off = 0;
        auto pump = [&] (juce::MidiBuffer&& in, std::vector<int>* onsOut, std::vector<int>* offsOut)
        {
            juce::MidiBuffer midi (in);
            processCfg (e, midi, block, playing (false), 0, 0, cfg);
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())  { ++on;  if (onsOut)  onsOut->push_back (m.getNoteNumber()); }
                if (m.isNoteOff()) { ++off; if (offsOut) offsOut->push_back (m.getNoteNumber()); }
            }
        };
        std::vector<int> b1, b2ons, b2offs, b3ons, b3offs, b4ons;
        pump (noteOnBuf (60), &b1, nullptr);   // 60 top -> 60, 72
        std::sort (b1.begin(), b1.end());
        check (b1 == std::vector<int> { 60, 72 }, "Harmonizer Top: the first note is the top and is harmonised");
        pump (noteOnBuf (64), &b2ons, &b2offs); // 64 new top: 60 loses its voice (off 72), 64 -> 64, 76
        std::sort (b2ons.begin(), b2ons.end());
        check (b2offs == std::vector<int> { 72 },
               "Harmonizer Top: the old top loses its voices when a higher note arrives");
        check (b2ons == std::vector<int> { 64, 76 },
               "Harmonizer Top: the new highest note takes the harmony");
        // A note below the top passes through dry (just itself, no voice, no off).
        pump (noteOnBuf (55), &b3ons, &b3offs);
        check (b3ons == std::vector<int> { 55 } && b3offs.empty(),
               "Harmonizer Top: a note below the top passes through un-harmonised");
        // Release the top (64): 60 becomes the top again and is re-harmonised.
        {
            juce::MidiBuffer in; in.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
            pump (std::move (in), &b4ons, nullptr);
        }
        check (b4ons == std::vector<int> { 72 },
               "Harmonizer Top: releasing the top re-harmonises the next-highest note");
        // Release the rest; everything must balance.
        {
            juce::MidiBuffer in;
            in.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            in.addEvent (juce::MidiMessage::noteOff (1, 55), 0);
            pump (std::move (in), nullptr, nullptr);
        }
        check (on == off, "Harmonizer Top: balanced after all keys released (no hang)");
    }
    {
        // Harmonised voices ride the pitch chain: a Shift after the Harmonizer
        // transposes the whole stack. Add-mode Octave on C4 (60, 72) + Shift +2
        // semitones (Scale Off) -> 62, 74.
        Engine e; e.prepare (sr);
        TestConfig cfg;
        cfg.hasHarmonizer = true;
        cfg.harmType      = ModuleOptions::kHarmOctave;
        cfg.hasShift      = true;
        cfg.shiftAmount   = 2;
        cfg.shiftScale    = ModuleOptions::kScaleOff;
        auto midi = noteOnBuf (60);
        processCfg (e, midi, block, playing (false), 0, 0, cfg);
        std::vector<int> ons;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) ons.push_back (meta.getMessage().getNoteNumber());
        std::sort (ons.begin(), ons.end());
        check (ons == std::vector<int> { 62, 74 },
               "Harmonizer voices run through the downstream pitch chain (Shift)");
    }

    // --- 17. ScaleTables spot checks -----------------------------------------
    {
        check (ScaleTables::isInScale (60, 0, 0), "C is in C major");
        check (! ScaleTables::isInScale (61, 0, 0), "C# is not in C major");
        check (ScaleTables::snapToScale (61, 0, 0) == 60 || ScaleTables::snapToScale (61, 0, 0) == 62,
               "snap 61 -> 60/62 in C major");
        check (ScaleTables::stepInScale (60, 0, 0, 7) == 72,
               "step +7 degrees in C major = +1 octave");
        check (ScaleTables::stepInScale (60, 0, 0, -7) == 48,
               "step -7 degrees in C major = -1 octave");
        check (ScaleTables::stepInScale (127, 0, 0, 5) == 127,
               "degree walk clamps at the top of the MIDI range");
    }

    if (failures == 0)
    {
        std::cout << "engine_smoketest: ALL PASS" << std::endl;
        return 0;
    }
    std::cout << "engine_smoketest: " << failures << " FAILURE(S)" << std::endl;
    return 1;
}
