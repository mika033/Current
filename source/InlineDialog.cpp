#include "InlineDialog.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"   // kUiFontSize — match the dialog body to UI controls

InlineDialog::InlineDialog (const juce::String& title, const juce::String& message)
    : titleText (title), messageText (message)
{
    // Modal-ish behaviour without a desktop window — we sit on top of every
    // sibling inside the editor and grab all input until dismissed.
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);
    setInterceptsMouseClicks (true, true);
}

InlineDialog::~InlineDialog() = default;

void InlineDialog::addTextEditor (const juce::String& name,
                                  const juce::String& initialText,
                                  const juce::String& label)
{
    auto* entry = new TextFieldEntry();
    entry->name  = name;
    entry->label = label;

    entry->labelComp = std::make_unique<juce::Label> ("", label);
    entry->labelComp->setColour (juce::Label::textColourId,
                                 CurrentTheme::active().text.withAlpha (0.85f));
    addAndMakeVisible (entry->labelComp.get());

    entry->editor = std::make_unique<juce::TextEditor>();
    entry->editor->setText (initialText);
    entry->editor->setColour (juce::TextEditor::backgroundColourId,
                              CurrentTheme::active().canvasBg);
    entry->editor->setColour (juce::TextEditor::textColourId,
                              CurrentTheme::active().text);
    entry->editor->setColour (juce::TextEditor::outlineColourId,
                              CurrentTheme::active().widgetOutline);
    entry->editor->setColour (juce::TextEditor::focusedOutlineColourId,
                              CurrentTheme::active().widgetOutline);
    entry->editor->setColour (juce::CaretComponent::caretColourId,
                              CurrentTheme::active().text);

    // A single-line TextEditor swallows Return before it bubbles to keyPressed,
    // so the editor commits the dialog itself: trigger the first (affirmative)
    // button.
    entry->editor->onReturnKey = [this]()
    {
        if (! buttons.isEmpty() && onResult)
            onResult (buttons.getFirst()->returnValue, this);
    };

    addAndMakeVisible (entry->editor.get());
    textFields.add (entry);

    if (getParentComponent())
        resized();
}

juce::String InlineDialog::getTextEditorContents (const juce::String& name) const
{
    for (auto* entry : textFields)
        if (entry->name == name)
            return entry->editor->getText();
    return {};
}

void InlineDialog::addComboBox (const juce::String& name,
                                const juce::StringArray& items,
                                int selectedIndex,
                                const juce::String& label)
{
    auto* entry = new ComboEntry();
    entry->name = name;

    entry->labelComp = std::make_unique<juce::Label> ("", label);
    entry->labelComp->setColour (juce::Label::textColourId,
                                 CurrentTheme::active().text.withAlpha (0.85f));
    addAndMakeVisible (entry->labelComp.get());

    // Colours come from the editor's LookAndFeel (same as the menu-bar combos),
    // so no per-widget colour setup is needed here.
    entry->combo = std::make_unique<juce::ComboBox>();
    entry->combo->addItemList (items, 1);   // ids are 1-based
    entry->combo->setSelectedItemIndex (juce::jlimit (0, items.size() - 1, selectedIndex),
                                        juce::dontSendNotification);
    addAndMakeVisible (entry->combo.get());
    combos.add (entry);

    if (getParentComponent())
        resized();
}

int InlineDialog::getComboBoxSelectedIndex (const juce::String& name) const
{
    for (auto* entry : combos)
        if (entry->name == name)
            return entry->combo->getSelectedItemIndex();
    return -1;
}

void InlineDialog::addButton (const juce::String& text, int returnValue)
{
    auto* entry = new ButtonEntry();
    entry->returnValue = returnValue;
    entry->button = std::make_unique<juce::TextButton> (text);

    entry->button->onClick = [this, returnValue]()
    {
        if (onResult)
            onResult (returnValue, this);
    };

    addAndMakeVisible (entry->button.get());
    buttons.add (entry);

    if (getParentComponent())
        resized();
}

juce::TextLayout InlineDialog::layoutMessage (int width) const
{
    const auto& s = CurrentTheme::active();
    juce::AttributedString attr;
    attr.setText (messageText);
    attr.setFont (juce::Font (juce::FontOptions (CurrentLookAndFeel::kUiFontSize)));
    attr.setColour (s.text.withAlpha (0.85f));
    attr.setJustification (juce::Justification::topLeft);
    attr.setLineSpacing (4.0f);

    juce::TextLayout layout;
    layout.createLayout (attr, (float) width);
    return layout;
}

int InlineDialog::messageTextHeight() const
{
    if (messageText.isEmpty())
        return 0;

    return juce::jmax (fieldLabelHeight,
                       (int) layoutMessage (panelWidth - padding * 2).getHeight() + 1);
}

int InlineDialog::calculatePanelHeight() const
{
    int h = padding;
    h += titleHeight;

    if (messageText.isNotEmpty())
        h += messageTextHeight() + fieldSpacing;

    // Acknowledgement-style dialog (message + buttons, no field): breathing
    // room so buttons don't crowd the last line of text.
    if (messageText.isNotEmpty() && textFields.isEmpty() && combos.isEmpty())
        h += messageButtonGap;

    for (int i = 0; i < textFields.size() + combos.size(); ++i)
        h += fieldLabelHeight + fieldHeight + fieldSpacing;

    h += buttonHeight + padding;
    return h;
}

void InlineDialog::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();

    // Faint backdrop dim so the dialog reads as modal over the canvas.
    g.fillAll (juce::Colours::black.withAlpha (0.25f));

    g.setColour (s.panelBg);
    g.fillRoundedRectangle (panelBounds.toFloat(), 8.0f);
    g.setColour (s.panelBorder);
    g.drawRoundedRectangle (panelBounds.toFloat(), 8.0f, 1.5f);

    g.setColour (s.text);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (titleText,
                panelBounds.getX() + padding,
                panelBounds.getY() + padding,
                panelBounds.getWidth() - padding * 2,
                titleHeight,
                juce::Justification::centredLeft);

    if (messageText.isNotEmpty())
    {
        auto layout = layoutMessage (panelBounds.getWidth() - padding * 2);
        layout.draw (g, juce::Rectangle<float> (
                        (float) (panelBounds.getX() + padding),
                        (float) (panelBounds.getY() + padding + titleHeight),
                        (float) (panelBounds.getWidth() - padding * 2),
                        (float) messageTextHeight()));
    }
}

void InlineDialog::resized()
{
    const int panelH  = calculatePanelHeight();
    const int centreX = getLocalBounds().getCentreX();
    const int panelY  = (getHeight() - panelH) / 2;

    panelBounds = juce::Rectangle<int> (centreX - panelWidth / 2, panelY,
                                        panelWidth, panelH);

    int y = panelBounds.getY() + padding + titleHeight;
    const int contentX = panelBounds.getX() + padding;
    const int contentW = panelBounds.getWidth() - padding * 2;

    if (messageText.isNotEmpty())
        y += messageTextHeight() + fieldSpacing;

    for (auto* entry : textFields)
    {
        entry->labelComp->setBounds (contentX, y, contentW, fieldLabelHeight);
        y += fieldLabelHeight;
        entry->editor->setBounds (contentX, y, contentW, fieldHeight);
        y += fieldHeight + fieldSpacing;
    }

    for (auto* entry : combos)
    {
        entry->labelComp->setBounds (contentX, y, contentW, fieldLabelHeight);
        y += fieldLabelHeight;
        entry->combo->setBounds (contentX, y, contentW, fieldHeight);
        y += fieldHeight + fieldSpacing;
    }

    const int btnW = 80;
    int totalButtonWidth = btnW * buttons.size()
                           + buttonSpacing * juce::jmax (0, buttons.size() - 1);
    int bx = (buttons.size() == 1)
                 ? panelBounds.getCentreX() - totalButtonWidth / 2
                 : panelBounds.getRight() - padding - totalButtonWidth;
    const int by = panelBounds.getBottom() - padding - buttonHeight;

    for (auto* entry : buttons)
    {
        entry->button->setBounds (bx, by, btnW, buttonHeight);
        bx += btnW + buttonSpacing;
    }

    if (! textFields.isEmpty())
        textFields.getFirst()->editor->grabKeyboardFocus();
}

void InlineDialog::mouseDown (const juce::MouseEvent& e)
{
    // Click outside the panel cancels (result 0, like Cancel).
    if (! panelBounds.contains (e.getPosition()) && onResult)
        onResult (0, this);
}

bool InlineDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onResult) onResult (0, this);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        if (! buttons.isEmpty() && onResult)
        {
            onResult (buttons.getFirst()->returnValue, this);
            return true;
        }
    }
    return false;
}

void InlineDialog::parentHierarchyChanged()
{
    // Always cover the whole editor so the dim + click-catch fills it after any
    // layout change (window resize, scale flip).
    if (auto* parent = getParentComponent())
        setBounds (parent->getLocalBounds());
}
