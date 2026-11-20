#include "Theme.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
Typeface::Ptr load (const char* data, int size) { return Typeface::createSystemTypefaceFor (data, (size_t) size); }
Typeface::Ptr r500() { static auto t = load (BinaryData::Raleway500_ttf, BinaryData::Raleway500_ttfSize); return t; }
Typeface::Ptr r600() { static auto t = load (BinaryData::Raleway600_ttf, BinaryData::Raleway600_ttfSize); return t; }
Typeface::Ptr r700() { static auto t = load (BinaryData::Raleway700_ttf, BinaryData::Raleway700_ttfSize); return t; }
Typeface::Ptr m400() { static auto t = load (BinaryData::JetBrainsMono400_ttf, BinaryData::JetBrainsMono400_ttfSize); return t; }
Typeface::Ptr m600() { static auto t = load (BinaryData::JetBrainsMono600_ttf, BinaryData::JetBrainsMono600_ttfSize); return t; }
Typeface::Ptr aw() { static auto t = load (BinaryData::Audiowide_ttf, BinaryData::Audiowide_ttfSize); return t; }
} // namespace

Font theme::raleway (float size, int weight)
{
    auto tf = weight >= 700 ? r700() : (weight >= 600 ? r600() : r500());
    return Font (FontOptions (tf).withHeight (size));
}
Font theme::mono (float size, bool semibold) { return Font (FontOptions (semibold ? m600() : m400()).withHeight (size)); }
Font theme::audiowide (float size) { return Font (FontOptions (aw()).withHeight (size)); }
float theme::textWidth (const Font& f, const String& s) { return GlyphArrangement::getStringWidth (f, s); }

Image theme::logoJavha() { static Image i = ImageCache::getFromMemory (BinaryData::logo_javha_png, BinaryData::logo_javha_pngSize); return i; }
Image theme::logoMark() { static Image i = ImageCache::getFromMemory (BinaryData::logo_mark_png, BinaryData::logo_mark_pngSize); return i; }

RadianceLookAndFeel::RadianceLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, theme::bg);
    setColour (ComboBox::textColourId, theme::text);
    setColour (ComboBox::arrowColourId, theme::textDim);
    setColour (PopupMenu::backgroundColourId, theme::panel2);
    setColour (PopupMenu::textColourId, theme::text);
    setColour (PopupMenu::highlightedBackgroundColourId, theme::brassDim);
    setColour (PopupMenu::highlightedTextColourId, theme::text);
    setColour (TextEditor::backgroundColourId, theme::panel2);
    setColour (TextEditor::textColourId, theme::text);
    setColour (TextEditor::outlineColourId, theme::line);
    setColour (TextEditor::focusedOutlineColourId, theme::brassDim);
    setColour (TextEditor::highlightColourId, theme::brass);
    setColour (TextEditor::highlightedTextColourId, theme::accentText);
    setColour (CaretComponent::caretColourId, theme::text);
    setColour (TooltipWindow::backgroundColourId, theme::panel2);
    setColour (TooltipWindow::textColourId, theme::text);
    setColour (TooltipWindow::outlineColourId, theme::line);
    setColour (AlertWindow::backgroundColourId, theme::panel);
    setColour (AlertWindow::textColourId, theme::text);
    setColour (AlertWindow::outlineColourId, theme::line);
    setColour (TextButton::buttonColourId, theme::panel2);
    setColour (TextButton::textColourOffId, theme::text);
    setColour (ListBox::backgroundColourId, theme::panel);
    setColour (ScrollBar::thumbColourId, theme::line);
}

Typeface::Ptr RadianceLookAndFeel::getTypefaceForFont (const Font& f)
{
    if (f.getTypefaceName() == Font::getDefaultSansSerifFontName()) return r500();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

// input[type=range]: pista de 4 px color --line y bola morada con borde del fondo
void RadianceLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const float cy = y + h * 0.5f;
    g.setColour (theme::line);
    g.fillRoundedRectangle ((float) x, cy - 2, (float) w, 4, 2);
    auto thumb = Rectangle<float> (14, 14).withCentre ({ pos, cy });
    g.setColour (theme::bg);
    g.fillEllipse (thumb.expanded (1));
    g.setColour (s.isEnabled() ? theme::brass : theme::textFaint);
    g.fillEllipse (thumb.reduced (2));
}

void RadianceLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (theme::panel2);
    g.fillRoundedRectangle (r, 3);
    g.setColour (box.isMouseOver (true) ? theme::brassDim : theme::line);
    g.drawRoundedRectangle (r, 3, 1);
    const float ax = (float) w - 16, ay = h * 0.5f;
    Path p;
    p.startNewSubPath (ax - 4, ay - 2);
    p.lineTo (ax, ay + 2);
    p.lineTo (ax + 4, ay - 2);
    g.setColour (theme::text);
    g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void RadianceLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (2, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void RadianceLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (theme::panel2);
    g.setColour (theme::line);
    g.drawRect (0, 0, w, h);
}

void RadianceLookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool, bool isHighlighted, bool isTicked,
                                             bool, const String& text, const String&, const Drawable*, const Colour*)
{
    if (isSeparator) return;
    if (isHighlighted) { g.setColour (theme::brassDim); g.fillRect (area); }
    g.setColour (isTicked ? theme::brass.brighter (0.3f) : theme::text);
    if (isHighlighted) g.setColour (theme::text);
    g.setFont (theme::mono (13.0f, isTicked));
    g.drawText (text, area.reduced (10, 0), Justification::centredLeft, true);
}

void RadianceLookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool)
{
    auto thumb = vertical ? Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize) : Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (over ? theme::brassDim : theme::line);
    g.fillRoundedRectangle (thumb.toFloat(), 3);
}
