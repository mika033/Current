#include "SettingsView.h"
#include "PluginProcessor.h"
#include "AuditionSynth.h"
#include "Theme.h"

SettingsView::SettingsView (CurrentAudioProcessor& processor,
                            std::function<void()> onThemeChanged)
    : state (processor.apvts()), themeChanged (std::move (onThemeChanged))
{
    themeLabel.setText ("Theme", juce::dontSendNotification);
    themeLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    themeLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (themeLabel);

    addAndMakeVisible (themeButton);
    refreshThemeButtonText();
    // Click cycles the choice param (two values => toggle). We drive the param
    // directly the way a ComboBoxAttachment would, so state saves / automation
    // see a normal edit, then apply the skin and refresh the label right here
    // (we're on the message thread) rather than waiting on the async listener.
    themeButton.onClick = [this]()
    {
        if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (
                state.getParameter (ParamIDs::theme)))
        {
            const int n = pc->choices.size();
            if (n <= 0) return;
            *pc = (pc->getIndex() + 1) % n;
            if (themeChanged) themeChanged();
            refreshThemeButtonText();
            if (onFeedback)
                onFeedback ("Theme: " + pc->getCurrentChoiceName(), "theme");
        }
    };
    // Observe external writes (state restore, automation) so the button label
    // and applied skin stay in sync when we didn't drive the edit. Registered
    // here (not the menu bar) since this component now owns the theme control;
    // it exists for the editor's whole life even while hidden, so the sync
    // works with the settings space closed.
    state.addParameterListener (ParamIDs::theme, this);

    showSynth = processor.isAuditionSynthSupported();
    if (showSynth)
    {
        addAndMakeVisible (synthEnableToggle);
        synthEnableAtt = std::make_unique<ButtonAttachment> (
            state, AuditionSynth::enabledId, synthEnableToggle);
        // Help-bar report, gated on the pointer being over the toggle so
        // attachment-driven flips (state restore, automation) stay silent —
        // the canonical continuous-control filter (messaging-area spec §1.6).
        synthEnableToggle.onClick = [this]()
        {
            if (synthEnableToggle.isMouseOverOrDragging() && onFeedback)
                onFeedback (juce::String ("Audition Synth: ")
                                + (synthEnableToggle.getToggleState() ? "On" : "Off"),
                            "synth.enabled");
        };

        // Signal-flow order: voice tone (Character, Cutoff, Env Amt, Decay),
        // then the insert FX (Space).
        auto pct = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
        setupDial (dials[0], AuditionSynth::characterId, "Character", "synth.character", pct);
        setupDial (dials[1], AuditionSynth::cutoffId, "Cutoff", "synth.cutoff", [] (double v)
        {
            return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz"
                               : juce::String ((int) std::round (v)) + " Hz";
        });
        setupDial (dials[2], AuditionSynth::envAmtId, "Env Amt", "synth.envamt", pct);
        setupDial (dials[3], AuditionSynth::decayId, "Decay", "synth.decay",
                   [] (double v) { return juce::String (v, 2) + " s"; });
        setupDial (dials[4], AuditionSynth::spaceId, "Space", "synth.space", pct);
    }
}

SettingsView::~SettingsView()
{
    state.removeParameterListener (ParamIDs::theme, this);
}

void SettingsView::setupDial (SynthDial& d, const juce::String& paramID,
                              const juce::String& baseName,
                              const juce::String& helpKey,
                              std::function<juce::String (double)> format)
{
    d.baseName = baseName;
    d.helpKey  = helpKey;
    d.format   = std::move (format);

    d.label.setFont (juce::Font (juce::FontOptions (12.0f)));
    d.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (d.label);

    // Same rotary setup as ModuleWindow's dials — a gap at the bottom of the
    // sweep so min/max read as distinct end-stops.
    d.dial.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    d.dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    d.dial.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (d.dial);

    // Live readout: fold the value into the label on every move. The
    // attachment (below) both seeds the initial value and echoes external
    // writes back through onValueChange, so the label stays true in all cases.
    // The help-bar report is gated on an actual drag/hover (messaging-area
    // spec §1.6) so those programmatic echoes stay silent.
    auto* cell = &d;
    d.dial.onValueChange = [this, cell]()
    {
        refreshDialLabel (*cell);
        if ((cell->dial.isMouseOverOrDragging() || cell->dial.isMouseButtonDown())
            && onFeedback)
            onFeedback (cell->label.getText(), cell->helpKey);
    };

    d.attachment = std::make_unique<SliderAttachment> (state, paramID, d.dial);
    refreshDialLabel (d);
}

void SettingsView::refreshDialLabel (SynthDial& d)
{
    d.label.setText (d.baseName + ": " + d.format (d.dial.getValue()),
                     juce::dontSendNotification);
}

void SettingsView::refreshThemeButtonText()
{
    if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (
            state.getParameter (ParamIDs::theme)))
        themeButton.setButtonText (pc->getCurrentChoiceName());
}

void SettingsView::parameterChanged (const juce::String&, float)
{
    // parameterChanged can arrive off the message thread; marshal the UI work.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SettingsView> (this)]()
    {
        if (safe == nullptr) return;
        if (safe->themeChanged) safe->themeChanged();
        safe->refreshThemeButtonText();
    });
}

void SettingsView::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();

    // Each panel is a menu-bar-style card: panelBg fill, panelBorder outline,
    // with the section title on the name column's label line (shrunk to fit).
    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        if (r.isEmpty()) return;
        auto b = r.toFloat();
        g.setColour (s.panelBg);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (s.panelBorder);
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (s.text);
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawFittedText (title,
                          r.getX() + kPanelInset, r.getY() + kPanelInset,
                          kSlotW, kLabelH + kLabelGap,
                          juce::Justification::centredLeft, 1);
    };

    panel (globalPanel, "Global");
    panel (synthPanel,  "Audition Synth");
}

void SettingsView::resized()
{
    auto area = getLocalBounds();

    globalPanel = area.removeFromTop (kPanelH);
    {
        auto inner = globalPanel.reduced (kPanelInset);
        inner.removeFromLeft (kSlotW + kColGap);   // section-title column (painted)

        auto col = inner.removeFromLeft (kSlotW);
        themeLabel.setBounds (col.removeFromTop (kLabelH));
        col.removeFromTop (kLabelGap);
        themeButton.setBounds (col.removeFromTop (kComboH));
    }

    if (showSynth)
    {
        area.removeFromTop (kPanelGap);
        synthPanel = area.removeFromTop (kPanelH);

        auto inner = synthPanel.reduced (kPanelInset);
        // Name column: the painted title sits on the label line, the enable
        // toggle below it in the control slot — reads as "Audition Synth [x]".
        auto nameCol = inner.removeFromLeft (kSlotW);
        nameCol.removeFromTop (kLabelH + kLabelGap);
        synthEnableToggle.setBounds (nameCol.removeFromTop (kToggleH).withWidth (kToggleW));
        inner.removeFromLeft (kColGap);

        for (auto& d : dials)
        {
            auto col = inner.removeFromLeft (kSlotW);
            d.label.setBounds (col.removeFromTop (kLabelH));
            col.removeFromTop (kLabelGap);
            d.dial.setBounds (col.withSizeKeepingCentre (kDialSize, kDialSize));
            inner.removeFromLeft (kColGap);
        }
    }
    else
        synthPanel = {};
}
