// Colores, fuentes y aspecto copiados de la hoja de estilos del prototipo (variables :root).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
using juce::Colour;
inline const Colour bg { 0xff0d0c12 };
inline const Colour bgGlow { 0xff1e1a2a };
inline const Colour headerBg { 0xff100e16 };
inline const Colour panel { 0xff17151e };
inline const Colour panel2 { 0xff201d29 };
inline const Colour line { 0xff332e40 };
inline const Colour lineSoft { 0xff221f2b };
inline const Colour text { 0xfff0eef5 };
inline const Colour textDim { 0xffb3b0bd };
inline const Colour textFaint { 0xff847f8d };
inline const Colour brass { 0xff9d7bff };
inline const Colour brassDim { 0xff6d4fd6 };
inline const Colour teal { 0xffc9c6d1 };
inline const Colour warn { 0xffff6b57 };
inline const Colour headerText { 0xfff2f0f7 };
inline const Colour headerTextDim { 0xff9a96a5 };
inline const Colour accentText { 0xff100e16 };
inline const Colour scopeBg { 0xff08090a };

juce::Font raleway (float size, int weight = 500);   // 500, 600 o 700
juce::Font mono (float size, bool semibold = false);
juce::Font audiowide (float size);
float textWidth (const juce::Font& f, const juce::String& s);
juce::Image logoJavha();
juce::Image logoMark();
} // namespace theme

class RadianceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RadianceLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 7; }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::mono (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return theme::mono (13.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::raleway (14.0f, 600); }
    juce::Font getAlertWindowMessageFont() override { return theme::raleway (14.0f); }
    juce::Font getAlertWindowTitleFont() override { return theme::raleway (16.0f, 700); }
};
