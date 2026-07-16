// Headless smoke test for the Phase 2 Engine. Exercises the fixed-default
// module behaviours and, above all, asserts the safety property that matters
// for a MIDI effect: every note-on the engine emits is balanced by a note-off,
// so nothing hangs — across pass-through, modulators, generators, and a
// transport stop. Built only when CURRENT_BUILD_TESTS=ON.
//
// Not a unit-test framework — just asserts + a pass/fail summary, so it can run
// in CI without extra deps.

#include "Engine.h"
#include "ScaleTables.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

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

    // --- 2. Shift: +12, and the matching note-off is also shifted -----------
    {
        Engine e; e.prepare (sr);
        Engine::Config cfg; cfg.hasShift = true;
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
        e.process (midi, block, playing (false), 0, 0, false, cfg);
        int onPitch = -1, offPitch = -1;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())  onPitch  = m.getNoteNumber();
            if (m.isNoteOff()) offPitch = m.getNoteNumber();
        }
        check (onPitch == 72,  "Shift maps note-on 60 -> 72");
        check (offPitch == 72, "Shift maps the note-off to 72 as well (no hang)");
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

    // --- 6. ScaleTables spot checks -----------------------------------------
    {
        check (ScaleTables::isInScale (60, 0, 0), "C is in C major");
        check (! ScaleTables::isInScale (61, 0, 0), "C# is not in C major");
        check (ScaleTables::snapToScale (61, 0, 0) == 60 || ScaleTables::snapToScale (61, 0, 0) == 62,
               "snap 61 -> 60/62 in C major");
    }

    if (failures == 0)
    {
        std::cout << "engine_smoketest: ALL PASS" << std::endl;
        return 0;
    }
    std::cout << "engine_smoketest: " << failures << " FAILURE(S)" << std::endl;
    return 1;
}
