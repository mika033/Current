#include "StepperControl.h"
#include "Theme.h"

StepperControl::StepperControl()
{
    setOpaque (false);

    incrButton.setButtonText ("+");
    incrButton.addMouseListener (this, false);
    addAndMakeVisible (incrButton);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    valueLabel.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    valueLabel.setBorderSize (juce::BorderSize<int> (0));
    valueLabel.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    valueLabel.addMouseListener (this, false);
    addAndMakeVisible (valueLabel);

    decrButton.setButtonText ("-");
    decrButton.addMouseListener (this, false);
    addAndMakeVisible (decrButton);

    updateDisplay();
}

StepperControl::~StepperControl()
{
    incrButton.removeMouseListener (this);
    decrButton.removeMouseListener (this);
    valueLabel.removeMouseListener (this);
}

void StepperControl::paint (juce::Graphics& g)
{
    // The value box sits between the two buttons; paint it from the active
    // scheme so a theme swap lands on the next repaint.
    auto valueBounds = getLocalBounds().reduced (kButtonW, 0).toFloat().reduced (0.5f);
    const auto& s = CurrentTheme::active();
    g.setColour (s.widgetBg);
    g.fillRoundedRectangle (valueBounds, 6.0f);

    g.setColour (s.widgetOutline);
    g.drawRoundedRectangle (valueBounds, 6.0f, 1.0f);
}

void StepperControl::resized()
{
    auto bounds = getLocalBounds();

    // Minus on the left, plus on the right, value in the middle (matches LAM).
    decrButton.setBounds (bounds.removeFromLeft (kButtonW));
    incrButton.setBounds (bounds.removeFromRight (kButtonW));
    valueLabel.setBounds (bounds);
}

void StepperControl::setRange (double minValue, double maxValue, double stepSize)
{
    minVal = minValue;
    maxVal = maxValue;
    step   = stepSize;
    setValue (value, juce::dontSendNotification);
}

void StepperControl::setValue (double newValue, juce::NotificationType notification)
{
    newValue = juce::jlimit (minVal, maxVal, newValue);

    if (newValue != value)
    {
        value = newValue;
        updateDisplay();

        if (notification != juce::dontSendNotification && onValueChange)
            onValueChange (value);
    }
}

void StepperControl::updateDisplay()
{
    char buffer[32];
    snprintf (buffer, sizeof (buffer), format.toRawUTF8(), value);
    valueLabel.setText (buffer, juce::dontSendNotification);
}

void StepperControl::mouseDown (const juce::MouseEvent& event)
{
    if (event.eventComponent == &decrButton || event.eventComponent == &incrButton)
    {
        repeatDirection = (event.eventComponent == &incrButton) ? 1 : -1;
        setValue (value + repeatDirection * step);
        repeatIntervalMs = repeatInitialDelayMs;
        startTimer (repeatIntervalMs);
    }
    else if (event.eventComponent == &valueLabel)
    {
        dragStartValue = value;
    }
}

void StepperControl::mouseDrag (const juce::MouseEvent& event)
{
    if (event.eventComponent == &valueLabel)
    {
        // Drag up to raise, down to lower; halve the pixel-to-step rate so the
        // scrub isn't twitchy.
        double rawNew = dragStartValue - event.getDistanceFromDragStartY() * step / 2.0;
        double snapped = std::round (rawNew / step) * step;
        setValue (snapped);
    }
}

void StepperControl::mouseUp (const juce::MouseEvent& event)
{
    if (event.eventComponent == &decrButton || event.eventComponent == &incrButton)
    {
        stopTimer();
        repeatDirection = 0;
    }
}

void StepperControl::timerCallback()
{
    // Auto-repeat while a button is held, accelerating toward repeatMinIntervalMs.
    setValue (value + repeatDirection * step);
    repeatIntervalMs = std::max (repeatMinIntervalMs, (int) (repeatIntervalMs * 0.7));
    startTimer (repeatIntervalMs);
}
