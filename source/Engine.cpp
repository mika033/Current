#include "Engine.h"
#include "ScaleTables.h"

namespace
{
    // Run `emit(channel)` once per Output-module channel, or once with the
    // event's own channel when no Output module is narrowing things (mask 0).
    template <typename Fn>
    void forEachOutChannel (std::uint16_t outMask, int ownChannel, Fn&& emit)
    {
        if (outMask == 0)
        {
            emit (ownChannel);
            return;
        }
        for (int ch = 1; ch <= 16; ++ch)
            if ((outMask & (1u << (ch - 1))) != 0)
                emit (ch);
    }

    bool inputAccepts (std::uint16_t inMask, int channel)
    {
        return channel >= 1 && channel <= 16
            && (inMask & (1u << (channel - 1))) != 0;
    }
}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Engine::reset()
{
    held.fill (false);
    activeGen.clear();
    activePass.clear();
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

void Engine::flushPassedNotes (juce::MidiBuffer& midi, int sample)
{
    for (const auto& p : activePass)
        midi.addEvent (juce::MidiMessage::noteOff (p.outChannel, p.outNote), sample);
    activePass.clear();
}

void Engine::process (juce::MidiBuffer& midi,
                      int numSamples,
                      const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                      int root, int scaleIndex, bool globalQuantize,
                      const Config& cfg)
{
    // No modules on the canvas → true pass-through. If modules were just removed
    // while notes were still sounding, release them (generated and mapped
    // pass-through alike) so nothing hangs — the raw note-offs arriving from now
    // on would no longer match what the chain emitted.
    if (! cfg.anyModule())
    {
        if (! activeGen.empty())
            flushGeneratedNotes (midi, 0);
        if (! activePass.empty())
            flushPassedNotes (midi, 0);
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

    // Transport stop: everything the engine generated is released. Passed-
    // through host notes are not flushed — the host still owes their note-offs
    // (a live key is released independently of the transport).
    if (! isPlaying && wasPlaying)
        flushGeneratedNotes (midi, 0);
    wasPlaying = isPlaying;

    // While the arp is running it consumes the host notes (they are its input),
    // so they don't also pass straight through. When stopped, host notes pass
    // through so live playing stays audible.
    const bool swallowHostNotes = cfg.hasArp && isPlaying;

    // --- Host events: input filter, held tracking, pass-through -------------
    for (const auto meta : incoming)
    {
        const auto m = meta.getMessage();
        const int  s = meta.samplePosition;

        if (m.isNoteOn())
        {
            // The channel filter is the graph's front door: a rejected note-on
            // doesn't reach the arp either.
            if (! inputAccepts (cfg.inChannelMask, m.getChannel()))
                continue;

            held[(size_t) m.getNoteNumber()] = true;
            if (! swallowHostNotes)
            {
                const int p = mapPitch (m.getNoteNumber(), root, scaleIndex,
                                        globalQuantize, cfg);
                forEachOutChannel (cfg.outChannelMask, m.getChannel(), [&] (int ch)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (ch, p, (juce::uint8) m.getVelocity()), s);
                    activePass.push_back ({ m.getNoteNumber(), m.getChannel(), p, ch });
                });
            }
        }
        else if (m.isNoteOff())
        {
            // Note-offs bypass the input filter and any swallowing: whatever was
            // actually emitted for this key (recorded in activePass) is released
            // exactly as it sounded, even if the config changed since the on.
            held[(size_t) m.getNoteNumber()] = false;
            for (auto it = activePass.begin(); it != activePass.end();)
            {
                if (it->inNote == m.getNoteNumber() && it->inChannel == m.getChannel())
                {
                    midi.addEvent (juce::MidiMessage::noteOff (it->outChannel, it->outNote), s);
                    it = activePass.erase (it);
                }
                else
                    ++it;
            }
        }
        else if (m.getChannel() > 0)
        {
            // CC / pitch-bend / aftertouch: same channel filter and Output
            // stamping as notes, so a controller follows its notes to the synth.
            if (! inputAccepts (cfg.inChannelMask, m.getChannel()))
                continue;
            forEachOutChannel (cfg.outChannelMask, m.getChannel(), [&] (int ch)
            {
                auto copy = m;
                copy.setChannel (ch);
                midi.addEvent (copy, s);
            });
        }
        else
        {
            midi.addEvent (m, s);   // non-channel messages (clock, sysex) pass untouched
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
                forEachOutChannel (cfg.outChannelMask, 1, [&] (int ch)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (ch, p, (juce::uint8) 100), s);
                    activeGen.push_back ({ p, ch, s + gateSamples });
                });
            }

            if (cfg.hasRandom)
            {
                const int cand = 48 + rng.nextInt (25);              // MIDI 48..72
                const int raw  = ScaleTables::snapToScale (cand, root, scaleIndex);
                const int p    = mapPitch (raw, root, scaleIndex, globalQuantize, cfg);
                forEachOutChannel (cfg.outChannelMask, 1, [&] (int ch)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (ch, p, (juce::uint8) 100), s);
                    activeGen.push_back ({ p, ch, s + gateSamples });
                });
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
