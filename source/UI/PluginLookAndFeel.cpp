#include "PluginLookAndFeel.h"

PluginLookAndFeel::PluginLookAndFeel()
{
    // getOptionsForComboBoxPopupMenu below already drops the anchor-to-
    // selected-item behaviour; no per-instance state needed here.
}

juce::Font PluginLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (kUiFontSize));
}

juce::Font PluginLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (kUiFontSize));
}

juce::Font PluginLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (kUiFontSize));
}

juce::Font PluginLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (kUiFontSize));
}

juce::Font PluginLookAndFeel::getTabButtonFont (juce::TabBarButton&, float)
{
    return juce::Font (juce::FontOptions (kUiFontSize));
}

// panels-controls.md §1.3 / §1.6: plain rounded rectangle, no chevron, no
// arrow zone; 3 px corner radius, distinct from the 5-6 px section-box
// rounding so combos read as sitting inside the panel.
void PluginLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
}

void PluginLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - 2, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// Load-bearing on iPad (panels-controls.md §1.4): AUv3 sandboxes block
// plugins from creating top-level OS windows, so the default ComboBox popup
// silently never appears unless it's parented into the editor's own view.
juce::Component* PluginLookAndFeel::getParentComponentForMenuOptions (const juce::PopupMenu::Options& options)
{
    if (auto* target = options.getTargetComponent())
        return target->getTopLevelComponent();

    return juce::LookAndFeel_V4::getParentComponentForMenuOptions (options);
}

// panels-controls.md §1.5: drop withItemThatMustBeVisible so the popup
// grows downward from the combo instead of anchoring the selected row to
// the combo's Y position, which collapses tall lists near the top of the
// editor.
juce::PopupMenu::Options PluginLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return juce::PopupMenu::Options()
            .withTargetComponent (&box)
            .withInitiallySelectedItem (box.getSelectedId())
            .withMinimumWidth (box.getWidth())
            .withMaximumNumColumns (1)
            .withStandardItemHeight (label.getHeight());
}
