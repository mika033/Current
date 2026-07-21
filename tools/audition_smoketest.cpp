// Headless end-to-end check of the audition path: full CurrentAudioProcessor
// (graph engine + audition synth), no plugin wrapper. Reproduces the manual
// repro "Random wired into Output, transport playing, synth enabled" and
// reports two independent facts — MIDI note-ons leaving processBlock, and
// audio magnitude in the buffer — so an engine failure and a synth failure
// read differently. Built only with -DCURRENT_BUILD_TESTS=ON.
#include "PluginProcessor.h"
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    CurrentAudioProcessor proc;

    // A non-Standalone processor boots with the wired MIDI In -> Output pair;
    // reuse its Output and wire a Random generator into it alongside.
    const int randomId = proc.addModule (ModuleType::Random, 200.0f, 100.0f);
    int outputId = -1;
    for (const auto& m : proc.modules())
        if (m.type == ModuleType::Output)
            outputId = m.id;
    const bool wired = outputId >= 0 && proc.addConnection (randomId, outputId);

    if (auto* p = proc.apvts().getParameter (AuditionSynth::enabledId))
        p->setValueNotifyingHost (1.0f);

    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // With no playhead the processor free-runs its internal transport at 120
    // BPM, so ~4 s covers several beats of the Random's default rate.
    int   noteOns = 0;
    float peak    = 0.0f;
    for (int b = 0; b < 400; ++b)
    {
        midi.clear();
        proc.processBlock (buffer, midi);
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                ++noteOns;
        peak = std::max (peak, buffer.getMagnitude (0, 512));
    }

    std::cout << "wired=" << (wired ? "yes" : "no")
              << " noteOns=" << noteOns
              << " peakMagnitude=" << peak << "\n";

    const bool pass = wired && noteOns > 0 && peak > 0.01f;
    std::cout << (pass ? "AUDITION TEST PASS" : "AUDITION TEST FAIL") << "\n";
    return pass ? 0 : 1;
}
