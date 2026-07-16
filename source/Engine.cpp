#include "Engine.h"
#include "ScaleTables.h"

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Engine::reset()
{
    held.fill (false);
    activeGen.clear();
    samplesToNextStep = 0.0;
    arpIndex = 0;
    wasPlaying = false;
}

int Engine::mapPitch (int note, int root, int scaleIndex,
                      bool globalQuantize, const Config& cfg) const
{
    int p = note;
    if (cfg.hasQuantize || globalQuantize)
        p = ScaleTables::snapToScale (p, root, scaleIndex);
    if (cfg.hasShift)
        p = juce::jlimit (0, 127, p + 12);   // fixed +1 octave default
    return p;
}

void Engine::flushGeneratedNotes (juce::MidiBuffer& midi, int sample)
{
    for (const auto& a : activeGen)
        midi.addEvent (juce::MidiMessage::noteOff (a.channel, a.note), sample);
    activeGen.clear();
}

void Engine::process (juce::MidiBuffer& midi,
                      int numSamples,
                      const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                      int root, int scaleIndex, bool globalQuantize,
                      const Config& cfg)
{
    // No modules on the canvas → true pass-through. If modules were just removed
    // while notes were still sounding, release them so nothing hangs.
    if (! cfg.anyModule())
    {
        if (! activeGen.empty())
            flushGeneratedNotes (midi, 0);
        held.fill (false);
        wasPlaying = false;
        return;
    }

    bool isPlaying = false;
    double bpm = 120.0;
    if (pos.hasValue())
    {
        isPlaying = pos->getIsPlaying();
        if (auto b = pos->getBpm())
            if (*b > 0.0)
                bpm = *b;
    }

    // 1/16-note grid.
    const double samplesPerStep = juce::jmax (1.0, (60.0 / bpm) * sr / 4.0);
    const int    gateSamples    = juce::jmax (1, (int) (samplesPerStep * 0.5));

    // Transport start: align the step clock so a step fires at sample 0.
    if (isPlaying && ! wasPlaying)
    {
        samplesToNextStep = 0.0;
        arpIndex = 0;
    }

    // Capture the host's incoming events, then rebuild the buffer.
    juce::MidiBuffer incoming;
    incoming.swapWith (midi);

    // Transport stop: everything stops and generated notes are released.
    if (! isPlaying && wasPlaying)
        flushGeneratedNotes (midi, 0);
    wasPlaying = isPlaying;

    // While the arp is running it consumes the host notes (they are its input),
    // so they don't also pass straight through. When stopped, host notes pass
    // through so live playing stays audible.
    const bool swallowHostNotes = cfg.hasArp && isPlaying;

    // --- Host events: track held notes + pass through (mapped) --------------
    for (const auto meta : incoming)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;

        if (m.isNoteOn())
        {
            held[(size_t) m.getNoteNumber()] = true;
            if (! swallowHostNotes)
                midi.addEvent (juce::MidiMessage::noteOn (
                                   m.getChannel(),
                                   mapPitch (m.getNoteNumber(), root, scaleIndex, globalQuantize, cfg),
                                   (juce::uint8) m.getVelocity()),
                               s);
        }
        else if (m.isNoteOff())
        {
            held[(size_t) m.getNoteNumber()] = false;
            if (! swallowHostNotes)
                midi.addEvent (juce::MidiMessage::noteOff (
                                   m.getChannel(),
                                   mapPitch (m.getNoteNumber(), root, scaleIndex, globalQuantize, cfg)),
                               s);
        }
        else
        {
            midi.addEvent (m, s);   // CC / pitch-bend / etc. pass through untouched
        }
    }

    // --- Generators: fire on the step grid ----------------------------------
    if (isPlaying && (cfg.hasArp || cfg.hasRandom))
    {
        std::vector<int> heldList;
        if (cfg.hasArp)
            for (int n = 0; n < 128; ++n)
                if (held[(size_t) n])
                    heldList.push_back (n);   // ascending

        double sPos = samplesToNextStep;
        while (sPos < (double) numSamples)
        {
            const int s = (int) sPos;

            if (cfg.hasArp && ! heldList.empty())
            {
                const int raw = heldList[(size_t) (arpIndex % (int) heldList.size())];
                ++arpIndex;
                const int p = mapPitch (raw, root, scaleIndex, globalQuantize, cfg);
                midi.addEvent (juce::MidiMessage::noteOn (1, p, (juce::uint8) 100), s);
                activeGen.push_back ({ p, 1, s + gateSamples });
            }

            if (cfg.hasRandom)
            {
                const int cand = 48 + rng.nextInt (25);              // MIDI 48..72
                const int raw  = ScaleTables::snapToScale (cand, root, scaleIndex);
                const int p    = mapPitch (raw, root, scaleIndex, globalQuantize, cfg);
                midi.addEvent (juce::MidiMessage::noteOn (1, p, (juce::uint8) 100), s);
                activeGen.push_back ({ p, 1, s + gateSamples });
            }

            sPos += samplesPerStep;
        }
        samplesToNextStep = sPos - (double) numSamples;
    }

    // --- Release generated notes whose gate ends this block ------------------
    for (auto it = activeGen.begin(); it != activeGen.end();)
    {
        if (it->samplesLeft < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (it->channel, it->note),
                           juce::jmax (0, it->samplesLeft));
            it = activeGen.erase (it);
        }
        else
        {
            it->samplesLeft -= numSamples;
            ++it;
        }
    }
}
