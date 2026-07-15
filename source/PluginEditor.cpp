#include "PluginEditor.h"
#include "UI/ColourScheme.h"

CurrentAudioProcessorEditor::CurrentAudioProcessorEditor (CurrentAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    setActive (kLight); // Light is the default theme per themes.md §1.1
    applyTheme();

    addAndMakeVisible (content);
    content.setBounds (0, 0, nativeW, nativeH);

    settingsBar.setBounds (0, 0, nativeW, settingsBarH);
    content.addAndMakeVisible (settingsBar);

    canvas.setBounds (0, settingsBarH, nativeW, nativeH - settingsBarH - paletteH);
    canvas.onRequestModuleSettings = [this] (const juce::String& moduleName)
    {
        showInlineDialog (moduleName, "Settings for this module aren't available yet - coming in a later phase.");
    };
    content.addAndMakeVisible (canvas);

    palette.setBounds (0, nativeH - paletteH, nativeW, paletteH);
    content.addAndMakeVisible (palette);

    inlineDialog.setBounds (0, 0, nativeW, nativeH);
    content.addChildComponent (inlineDialog); // stays hidden until showInlineDialog() is called

    // resize-scaling.md §1.3/§1.4: aspect-locked, resizable between 0.75x
    // and 2.00x of the native canvas (desktop only).
    setResizable (true, true);
    setResizeLimits ((int) (nativeW * 0.75), (int) (nativeH * 0.75),
                      (int) (nativeW * 2.0), (int) (nativeH * 2.0));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) nativeW / (double) nativeH);

    setSize (nativeW, nativeH);
}

CurrentAudioProcessorEditor::~CurrentAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void CurrentAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (active().windowBg);
}

void CurrentAudioProcessorEditor::resized()
{
    // resize-scaling.md §1.2: one scale factor, applied to the single
    // top-level "content" child; nothing beneath it computes its own scale.
    const float scale = juce::jmin ((float) getWidth() / (float) nativeW,
                                     (float) getHeight() / (float) nativeH);
    content.setTransform (juce::AffineTransform::scale (scale));
}

void CurrentAudioProcessorEditor::showInlineDialog (const juce::String& title, const juce::String& message)
{
    inlineDialog.toFront (false);
    inlineDialog.show (title, message);
}

// themes.md §1.8 cascade.
void CurrentAudioProcessorEditor::applyTheme()
{
    const auto& cs = active();

    lookAndFeel.setColourScheme (cs.useDarkBase ? juce::LookAndFeel_V4::getDarkColourScheme()
                                                  : juce::LookAndFeel_V4::getLightColourScheme());

    lookAndFeel.setColour (juce::ResizableWindow::backgroundColourId, cs.windowBg);
    lookAndFeel.setColour (juce::Label::textColourId, cs.text);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, cs.widgetBg);
    lookAndFeel.setColour (juce::TextButton::textColourOnId, cs.text);
    lookAndFeel.setColour (juce::TextButton::textColourOffId, cs.text);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, cs.widgetBg);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, cs.widgetOutline);
    lookAndFeel.setColour (juce::ComboBox::textColourId, cs.text);
    lookAndFeel.setColour (juce::PopupMenu::backgroundColourId, cs.sectionBoxBg);
    lookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, cs.accent);
    lookAndFeel.setColour (juce::PopupMenu::highlightedTextColourId,
                            cs.useDarkBase ? juce::Colours::black : juce::Colours::white);

    sendLookAndFeelChange();
}
