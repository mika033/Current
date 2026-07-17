// Headless smoke test for the Engine. Exercises the module behaviours —
// defaults and user settings (rates, ranges, modes, repeat windows, gates) —
// and, above all, asserts the safety property that matters for a MIDI effect:
// every note-on the engine emits is balanced by a note-off, so nothing hangs —
// across pass-through, modulators, stepped modules, and a transport stop.
// Built only when CURRENT_BUILD_TESTS=ON.
//
// Not a unit-test framework — just asserts + a pass/fail summary, so it can run
// in CI without extra deps.

#include "Engine.h"
#include "ScaleTables.h"
#include "ModuleSettings.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool cond, const juce::String& what)
    {
        if (! cond)
        {
            std::cout << "  FAIL: " << what << std::endl;
            ++failures;
        }
    }

    juce::Optional<juce::AudioPlayHead::PositionInfo> playing (bool isPlaying, double bpm = 120.0)
    {
        juce::AudioPlayHead::PositionInfo pi;
        pi.setBpm (bpm);
        pi.setIsPlaying (isPlaying);
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

    // --- 1. No modules: pass-through, note untouched ------------------------
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg;   // all false
        auto midi = noteOnBuf (60);
        e.process (midi, block, playing (false), 0, 0, false, cfg);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        bool found60 = false;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn() && meta.getMessage().getNoteNumber() == 60)
                found60 = true;
        check (ons == 1 && found60, "pass-through keeps the note-on at pitch 60");
    }

    // --- 2. Shift: settings-driven transpose, off follows the on ------------
    {
        // Chromatic (scale Off): plain semitone transpose.
        auto shiftedPitches = [&] (int amount, int shiftScale, int inNote,
                                   int globalRoot, int globalScale)
        {
            Engine e; e.prepare (sr);
            Engine::Config cfg;
            cfg.hasShift    = true;
            cfg.shiftAmount = amount;
            cfg.shiftScale  = shiftScale;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn  (1, inNote, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, inNote), 256);
            e.process (midi, block, playing (false), globalRoot, globalScale, false, cfg);
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

    // --- 3. Quantize module: snaps an out-of-scale note to C major ----------
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg; cfg.hasQuantize = true;
        auto midi = noteOnBuf (61);   // C# — not in C major
        e.process (midi, block, playing (false), /*root*/0, /*scale*/0, /*globalQ*/false, cfg);
        int p = -1;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) p = meta.getMessage().getNoteNumber();
        check (p == 60 || p == 62, "Quantize snaps 61 to a C-major neighbour (60/62)");
        check (ScaleTables::isInScale (p, 0, 0), "quantized pitch is in scale");
    }

    // --- 4. Random generator: produces notes while playing, none hang -------
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg; cfg.hasRandom = true;
        int ons = 0, offs = 0;
        for (int i = 0; i < 200; ++i)   // ~2.3 s at 120bpm / 512 blocks
        {
            juce::MidiBuffer midi;
            e.process (midi, block, playing (true), 0, 0, false, cfg);
            tally (midi, ons, offs);
        }
        // Stop: the engine must release everything still sounding.
        {
            juce::MidiBuffer midi;
            e.process (midi, block, playing (false), 0, 0, false, cfg);
            tally (midi, ons, offs);
        }
        check (ons > 0, "Random generated some notes while playing");
        check (ons == offs, "Random: every note-on balanced by a note-off after stop");
    }

    // --- 5. Arp: swallows held host notes, emits arp notes, all balanced ----
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg; cfg.hasArp = true;

        int ons = 0, offs = 0;
        // Hold a triad on the first block.
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
            e.process (midi, block, playing (true), 0, 0, false, cfg);
            // Host notes are swallowed (arp input), so no pitch-60/64/67 passthrough
            // note-ons besides the arp's own — just tally overall balance below.
            tally (midi, ons, offs);
        }
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            e.process (midi, block, playing (true), 0, 0, false, cfg);
            tally (midi, ons, offs);
        }
        { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); tally (midi, ons, offs); }

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
            Engine::Config cfg;
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
                e.process (midi, block, playing (true), 0, 0, false, cfg);
                tally (midi, ons, offs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            for (int i = 0; i < 2000 && pitches.size() < count; ++i)
            {
                juce::MidiBuffer midi;
                e.process (midi, block, playing (true), 0, 0, false, cfg);
                tally (midi, ons, offs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); tally (midi, ons, offs); }
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
        Engine::Config cfg;
        cfg.hasMidiIn = true;
        cfg.inChannelMask = (std::uint16_t) (1u << (2 - 1));   // channel 2 only

        auto midi = noteOnBuf (60, 0, /*chan*/1);
        e.process (midi, block, playing (false), 0, 0, false, cfg);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        check (ons == 0, "MIDI In on ch 2 drops a ch-1 note");

        auto midi2 = noteOnBuf (60, 0, /*chan*/2);
        e.process (midi2, block, playing (false), 0, 0, false, cfg);
        ons = 0; offs = 0; tally (midi2, ons, offs);
        check (ons == 1, "MIDI In on ch 2 passes a ch-2 note");
    }

    // --- 7. Output: restamps channel, note-off follows ----------------------
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) (1u << (5 - 1));   // channel 5

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        e.process (midi, block, playing (false), 0, 0, false, cfg);
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
        Engine::Config cfg;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) ((1u << (2 - 1)) | (1u << (3 - 1)));

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        e.process (midi, block, playing (false), 0, 0, false, cfg);
        int ons = 0, offs = 0; tally (midi, ons, offs);
        check (ons == 2 && offs == 2, "two Outputs duplicate the note onto both channels");
    }

    // --- 9. Output channel changed mid-note: off still matches the on -------
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) (1u << (2 - 1));   // on goes out on ch 2

        auto midi = noteOnBuf (60, 0, 1);
        e.process (midi, block, playing (false), 0, 0, false, cfg);

        cfg.outChannelMask = (std::uint16_t) (1u << (7 - 1));   // user edits to ch 7
        juce::MidiBuffer offBuf;
        offBuf.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        e.process (offBuf, block, playing (false), 0, 0, false, cfg);

        int offCh = -1;
        for (const auto meta : offBuf)
            if (meta.getMessage().isNoteOff())
                offCh = meta.getMessage().getChannel();
        check (offCh == 2, "note-off released on the channel the note-on used (ch 2)");
    }

    // --- 10. Random through an Output: generated notes balanced on its ch ---
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg;
        cfg.hasRandom = true;
        cfg.hasOutput = true;
        cfg.outChannelMask = (std::uint16_t) (1u << (4 - 1));

        int ons = 0, offs = 0; bool allCh4 = true;
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            e.process (midi, block, playing (true), 0, 0, false, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().getChannel() != 4)
                    allCh4 = false;
        }
        { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); tally (midi, ons, offs); }
        check (ons > 0 && allCh4, "generated notes leave on the Output's channel");
        check (ons == offs, "generated notes balanced through the Output after stop");
    }

    // --- 11. Random settings: range and scale override are respected --------
    {
        // Pin the range to a single pitch with a chromatic scale: every note
        // the generator emits must be exactly that pitch.
        Engine e; e.prepare (sr);
        Engine::Config cfg;
        cfg.hasRandom   = true;
        cfg.randomScale = 8;    // Chromatic
        cfg.randomFrom  = 60;
        cfg.randomTo    = 60;

        int ons = 0, offs = 0; bool all60 = true;
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            e.process (midi, block, playing (true), 0, 0, false, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn() && meta.getMessage().getNoteNumber() != 60)
                    all60 = false;
        }
        { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); tally (midi, ons, offs); }
        check (ons > 0 && all60, "Random with range 60..60 emits only pitch 60");
        check (ons == offs, "Random settings: balanced after stop");
    }
    {
        // Root override D + major, wide range: everything lands in D major even
        // though the global root says C.
        Engine e; e.prepare (sr);
        Engine::Config cfg;
        cfg.hasRandom   = true;
        cfg.randomRoot  = 2;    // D
        cfg.randomScale = 0;    // Major
        cfg.randomFrom  = 48;
        cfg.randomTo    = 72;

        bool allInDMajor = true; bool inRange = true; int ons = 0, offs = 0;
        for (int i = 0; i < 100; ++i)
        {
            juce::MidiBuffer midi;
            e.process (midi, block, playing (true), /*global root*/0, /*global scale*/0, false, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                {
                    const int n = meta.getMessage().getNoteNumber();
                    if (! ScaleTables::isInScale (n, 2, 0)) allInDMajor = false;
                    if (n < 48 || n > 72) inRange = false;
                }
        }
        { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); }
        check (ons > 0 && allInDMajor, "Random root/scale override draws from D major");
        check (inRange, "Random stays inside its note range");
    }

    // --- 12. Random rate: 1/8 emits half as many notes as 1/16 --------------
    {
        auto countOns = [&] (double stepQn)
        {
            Engine e; e.prepare (sr);
            Engine::Config cfg;
            cfg.hasRandom = true;
            cfg.randomStepQn = stepQn;
            int ons = 0, offs = 0;
            for (int i = 0; i < 200; ++i)
            {
                juce::MidiBuffer midi;
                e.process (midi, block, playing (true), 0, 0, false, cfg);
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
        Engine::Config cfg;
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
            e.process (midi, block, playing (true), 0, 0, false, cfg);
            tally (midi, ons, offs);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    pitches.push_back (meta.getMessage().getNoteNumber());
        }
        { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); tally (midi, ons, offs); }

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
            Engine::Config cfg;
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
                e.process (midi, block, playing (true), 0, 0, false, cfg);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); }
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
        Engine::Config cfg;
        cfg.hasScaleGen    = true;
        cfg.scaleStepQn    = 1.0;
        cfg.scaleRepeatQn  = 0.0;   // Endless
        cfg.scaleOctaves   = 1;
        cfg.scaleEndOnRoot = true;

        std::vector<int> pitches;
        for (int i = 0; i < 2000 && pitches.size() < 10; ++i)
        {
            juce::MidiBuffer midi;
            e.process (midi, block, playing (true), 0, 0, false, cfg);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    pitches.push_back (meta.getMessage().getNoteNumber());
        }
        { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); }
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
            Engine::Config cfg;
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
                e.process (midi, block, playing (true), 0, 0, false, cfg);
                tally (midi, ons, offs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        pitches.push_back (meta.getMessage().getNoteNumber());
            }
            { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); tally (midi, ons, offs); }
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
            Engine::Config cfg;
            cfg.hasLfo   = true;
            cfg.lfoShape = ModuleOptions::kLfoRandom;
            bool allInScale = true;
            int rons = 0, roffs = 0;
            for (int i = 0; i < 200; ++i)
            {
                juce::MidiBuffer midi;
                e.process (midi, block, playing (true), 0, 0, false, cfg);
                tally (midi, rons, roffs);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn()
                        && ! ScaleTables::isInScale (meta.getMessage().getNoteNumber(), 0, 0))
                        allInScale = false;
            }
            { juce::MidiBuffer midi; e.process (midi, block, playing (false), 0, 0, false, cfg); tally (midi, rons, roffs); }
            check (rons > 0 && allInScale, "LFO random shape stays in scale");
            check (rons == roffs, "LFO random shape: balanced after stop");
        }
    }

    // --- 15. ScaleTables spot checks -----------------------------------------
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
