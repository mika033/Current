#include "ModuleWindow.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"   // kUiFontSize — dial/title sizing

ModuleWindow::ModuleWindow (const juce::String& title)
    : titleText (title)
{
    // Modal-ish behaviour without a desktop window — we sit on top of every
    // sibling inside the editor and grab all input until dismissed (same
    // contract as InlineDialog).
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);
    setInterceptsMouseClicks (true, true);
}

ModuleWindow::~ModuleWindow() = default;

juce::Label& ModuleWindow::makeLabel (std::unique_ptr<juce::Label>& holder,
                                      const juce::String& text,
                                      juce::Justification just)
{
    holder = std::make_unique<juce::Label> ("", text);
    holder->setJustificationType (just);
    // Text colour comes from the shared LookAndFeel (Label::textColourId), so a
    // theme swap recolours every label; a slight alpha dip matches InlineDialog.
    holder->setColour (juce::Label::textColourId,
                       CurrentTheme::active().text.withAlpha (0.85f));
    addAndMakeVisible (holder.get());
    return *holder;
}

void ModuleWindow::configureDial (juce::Slider& dial)
{
    dial.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    // Same sweep as LAM's dials — a gap at the bottom so min/max read as
    // distinct end-stops rather than meeting.
    dial.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                              juce::MathConstants<float>::pi * 2.8f, true);
}

void ModuleWindow::setMenuCombo (int slot,
                                 const juce::String& name,
                                 const juce::StringArray& items,
                                 int selectedIndex,
                                 const juce::String& label)
{
    jassert (juce::isPositiveAndBelow (slot, kMenuSlots));
    auto& s = menuSlots[(size_t) slot];
    s.name = name;
    makeLabel (s.label, label, juce::Justification::centredRight);

    s.combo = std::make_unique<juce::ComboBox>();
    s.combo->addItemList (items, 1);   // ids are 1-based
    s.combo->setSelectedItemIndex (juce::jlimit (0, items.size() - 1, selectedIndex),
                                   juce::dontSendNotification);
    addAndMakeVisible (s.combo.get());

    if (getParentComponent())
        resized();
}

void ModuleWindow::setGridCombo (int slot,
                                 const juce::String& name,
                                 const juce::StringArray& items,
                                 int selectedIndex,
                                 const juce::String& label)
{
    jassert (juce::isPositiveAndBelow (slot, kGridSlots));
    auto& c = gridCells[(size_t) slot];
    c.name = name;
    makeLabel (c.label, label, juce::Justification::centred);

    c.combo = std::make_unique<juce::ComboBox>();
    c.combo->addItemList (items, 1);
    c.combo->setSelectedItemIndex (juce::jlimit (0, items.size() - 1, selectedIndex),
                                   juce::dontSendNotification);
    addAndMakeVisible (c.combo.get());

    if (getParentComponent())
        resized();
}

void ModuleWindow::setGridDial (int slot,
                                const juce::String& name,
                                double minValue, double maxValue, double interval,
                                double value,
                                const juce::String& label,
                                std::function<juce::String (double)> valueText)
{
    jassert (juce::isPositiveAndBelow (slot, kGridSlots));
    auto& c = gridCells[(size_t) slot];
    c.name       = name;
    c.baseLabel  = label;
    c.dialFormat = std::move (valueText);
    makeLabel (c.label, label, juce::Justification::centred);

    c.dial = std::make_unique<juce::Slider>();
    configureDial (*c.dial);
    c.dial->setRange (minValue, maxValue, interval);
    c.dial->setValue (value, juce::dontSendNotification);
    // Live readout: fold the value into the label ("From: C2") on every move,
    // then fire any installed reaction (Mirror's Low/High push).
    auto* cell = &c;
    c.dial->onValueChange = [this, cell]()
    {
        refreshDialLabel (*cell);
        if (cell->dialChangeCb)
            cell->dialChangeCb();
    };
    addAndMakeVisible (c.dial.get());
    refreshDialLabel (c);   // initial text (setValue above was silent)

    if (getParentComponent())
        resized();
}

void ModuleWindow::setCustomBody (std::unique_ptr<juce::Component> body, int height)
{
    customBody       = std::move (body);
    customBodyHeight = height;
    addAndMakeVisible (customBody.get());

    if (getParentComponent())
        resized();
}

void ModuleWindow::refreshDialLabel (GridCell& cell)
{
    if (cell.label == nullptr || ! cell.dialFormat)
        return;
    cell.label->setText (cell.baseLabel + ": " + cell.dialFormat (cell.dial->getValue()),
                         juce::dontSendNotification);
}

int ModuleWindow::getComboSelectedIndex (const juce::String& name) const
{
    for (const auto& s : menuSlots)
        if (s.filled() && s.name == name)
            return s.combo->getSelectedItemIndex();
    for (const auto& c : gridCells)
        if (c.combo != nullptr && c.name == name)
            return c.combo->getSelectedItemIndex();
    return -1;
}

double ModuleWindow::getDialValue (const juce::String& name) const
{
    for (const auto& c : gridCells)
        if (c.dial != nullptr && c.name == name)
            return c.dial->getValue();
    return 0.0;
}

void ModuleWindow::refreshDial (const juce::String& name)
{
    for (auto& c : gridCells)
        if (c.dial != nullptr && c.name == name)
        {
            refreshDialLabel (c);
            return;
        }
}

void ModuleWindow::setComboChangeCallback (const juce::String& name, std::function<void()> cb)
{
    for (auto& s : menuSlots)
        if (s.filled() && s.name == name) { s.combo->onChange = cb; return; }
    for (auto& c : gridCells)
        if (c.combo != nullptr && c.name == name) { c.combo->onChange = cb; return; }
}

void ModuleWindow::setDialChangeCallback (const juce::String& name, std::function<void()> cb)
{
    for (auto& c : gridCells)
        if (c.dial != nullptr && c.name == name) { c.dialChangeCb = std::move (cb); return; }
}

void ModuleWindow::setDialValue (const juce::String& name, double value)
{
    for (auto& c : gridCells)
        if (c.dial != nullptr && c.name == name)
        {
            // dontSendNotification so a push reaction can't recurse into the
            // dial that triggered it; refresh the label by hand instead.
            c.dial->setValue (value, juce::dontSendNotification);
            refreshDialLabel (c);
            return;
        }
}

void ModuleWindow::addButton (const juce::String& text, int returnValue)
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

int ModuleWindow::calculatePanelHeight() const
{
    int h = padding;
    h += titleHeight;
    h += sectionGap + menuStripH;
    // Body: the custom body's height when one is installed, else the 3x2 grid box.
    h += sectionGap + (customBody != nullptr ? customBodyHeight
                                             : 2 * cellRowH + rowGap + 2 * gridInset);
    h += sectionGap + buttonHeight;
    h += padding;
    return h;
}

void ModuleWindow::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();

    // Faint backdrop dim so the window reads as modal over the canvas.
    g.fillAll (juce::Colours::black.withAlpha (0.25f));

    // Window frame.
    g.setColour (s.panelBg);
    g.fillRoundedRectangle (panelBounds.toFloat(), windowCorner);
    g.setColour (s.panelBorder);
    g.drawRoundedRectangle (panelBounds.toFloat(), windowCorner, 1.5f);

    // Title (module name).
    g.setColour (s.text);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (titleText,
                panelBounds.getX() + padding,
                panelBounds.getY() + padding,
                panelBounds.getWidth() - padding * 2,
                titleHeight,
                juce::Justification::centredLeft);

    // Menu-bar strip and grid box drawn as nested section boxes (canvasBg is a
    // touch offset from panelBg, so they read as recessed panels).
    auto section = [&] (juce::Rectangle<int> r)
    {
        g.setColour (s.canvasBg);
        g.fillRoundedRectangle (r.toFloat(), sectionCorner);
        g.setColour (s.panelBorder);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), sectionCorner, 1.0f);
    };
    section (menuStripBounds);
    section (gridBoxBounds);
}

void ModuleWindow::resized()
{
    const int panelH  = calculatePanelHeight();
    const int centreX = getLocalBounds().getCentreX();
    const int panelY  = juce::jmax (0, (getHeight() - panelH) / 2);

    panelBounds = juce::Rectangle<int> (centreX - panelWidth / 2, panelY,
                                        panelWidth, panelH);

    const int contentX = panelBounds.getX() + padding;
    const int contentW = panelBounds.getWidth() - padding * 2;

    int y = panelBounds.getY() + padding + titleHeight + sectionGap;

    // ----- Menu-bar strip: Root / Scale / Rate(or Length), label-left -----
    menuStripBounds = { contentX, y, contentW, menuStripH };
    {
        int mx = menuStripBounds.getX() + menuStripInsetX;
        const int my = menuStripBounds.getY() + menuStripInsetY;
        for (auto& slot : menuSlots)
        {
            if (slot.filled())
            {
                slot.label->setBounds (mx, my, menuLabelW, menuRowH);
                slot.combo->setBounds (mx + menuLabelW + menuLabelGap, my, menuComboW, menuRowH);
            }
            // Advance by a full group whether filled or not, so an unused slot
            // leaves reserved empty space and the filled slots keep their
            // column positions.
            mx += menuLabelW + menuLabelGap + menuComboW + menuGroupGap;
        }
    }
    y += menuStripH + sectionGap;

    // ----- Body: a custom body component, or the default 3x2 grid box -----
    const int gridBoxH = customBody != nullptr ? customBodyHeight
                                               : 2 * cellRowH + rowGap + 2 * gridInset;
    gridBoxBounds = { contentX, y, contentW, gridBoxH };
    if (customBody != nullptr)
    {
        // The body draws itself inside the recessed section box paint() lays
        // down; give it the full box so it owns the whole area.
        customBody->setBounds (gridBoxBounds);
    }
    else
    {
        const int innerX = gridBoxBounds.getX() + gridInset;
        const int innerY = gridBoxBounds.getY() + gridInset;
        const int innerW = gridBoxBounds.getWidth() - 2 * gridInset;
        const int cellW  = (innerW - 2 * colGap) / 3;

        for (int slot = 0; slot < kGridSlots; ++slot)
        {
            auto& cell = gridCells[(size_t) slot];
            if (! cell.filled())
                continue;

            const int col = slot % 3;
            const int row = slot / 3;
            const int cellX = innerX + col * (cellW + colGap);
            const int cellY = innerY + row * (cellRowH + rowGap);

            cell.label->setBounds (cellX, cellY, cellW, cellLabelH);
            const int ctrlY = cellY + cellLabelH + cellLabelGap;

            if (cell.dial != nullptr)
            {
                cell.dial->setBounds (juce::Rectangle<int> (cellX, ctrlY, cellW, controlAreaH)
                                          .withSizeKeepingCentre (dialSize, dialSize));
            }
            else if (cell.combo != nullptr)
            {
                cell.combo->setBounds (juce::Rectangle<int> (cellX, ctrlY, cellW, controlAreaH)
                                           .withSizeKeepingCentre (cellW, comboH));
            }
        }
    }
    y += gridBoxH + sectionGap;

    // ----- Action buttons, bottom-right -----
    const int count = buttons.size();
    const int totalW = buttonWidth * count + buttonSpacing * juce::jmax (0, count - 1);
    int bx = panelBounds.getRight() - padding - totalW;
    const int by = panelBounds.getBottom() - padding - buttonHeight;
    for (auto* entry : buttons)
    {
        entry->button->setBounds (bx, by, buttonWidth, buttonHeight);
        bx += buttonWidth + buttonSpacing;
    }
}

void ModuleWindow::mouseDown (const juce::MouseEvent& e)
{
    // Click outside the panel cancels (result 0, like Cancel).
    if (! panelBounds.contains (e.getPosition()) && onResult)
        onResult (0, this);
}

bool ModuleWindow::keyPressed (const juce::KeyPress& key)
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

void ModuleWindow::parentHierarchyChanged()
{
    // Always cover the whole editor so the dim + click-catch fills it after any
    // layout change (window resize, scale flip).
    if (auto* parent = getParentComponent())
        setBounds (parent->getLocalBounds());
}
