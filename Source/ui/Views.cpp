#include "Views.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }
const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
String noteName (int midi) { return String (kNoteNames[((midi % 12) + 12) % 12]) + String (midi / 12 - 1); }
} // namespace

//==============================================================================
Btn::Btn (const String& buttonText, Style s, bool sm) : Button (buttonText), style (s), small (sm)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

int Btn::idealWidth() const
{
    return (int) std::ceil (theme::textWidth (theme::raleway (small ? 12.0f : 14.0f, 600), getButtonText())) + (small ? 22 : 34);
}

void Btn::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    Colour fill = theme::panel2, border = over ? theme::brassDim : theme::line, txt = theme::text;
    switch (style)
    {
        case Style::Primary: fill = theme::brass.brighter (over ? 0.08f : 0.0f); border = theme::brass; txt = theme::accentText; break;
        case Style::Active: fill = theme::teal; border = theme::teal; txt = theme::accentText; break;
        case Style::Danger: fill = theme::warn; border = theme::warn; txt = theme::accentText; break;
        case Style::Upload: fill = Colours::transparentBlack; txt = over ? theme::text : theme::textDim; break;
        case Style::Normal: break;
    }
    if (hasCustomText && style == Style::Normal) txt = customText;
    if (! isEnabled()) { fill = fill.withMultipliedAlpha (0.4f); txt = txt.withMultipliedAlpha (0.4f); border = border.withMultipliedAlpha (0.4f); }
    if (down) r = r.translated (0, 1);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 3);
    g.setColour (border);
    if (style == Style::Upload)
    {
        Path p; p.addRoundedRectangle (r, 3);
        Path dashed; const float dashes[] = { 4.0f, 3.0f };
        PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
    }
    else g.drawRoundedRectangle (r, 3, 1);
    g.setColour (txt);
    g.setFont (style == Style::Upload ? theme::raleway (13.0f, 500) : theme::raleway (small ? 12.0f : 14.0f, 600));
    g.drawText (getButtonText(), r.reduced (4, 0), Justification::centred, false);
}

//==============================================================================
ParamRow::ParamRow (RadianceProcessor& p, const char* paramId, String l, std::function<String (double)> fmt)
    : label (std::move (l)), format (std::move (fmt))
{
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setWantsKeyboardFocus (false);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, slider);
    if (auto* prm = p.apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    refresh();
}

void ParamRow::resized() { slider.setBounds (0, 20, getWidth(), 18); }

void ParamRow::refresh()
{
    const auto t = format (slider.getValue());
    if (t != valueText) { valueText = t; repaint(); }
}

void ParamRow::paint (Graphics& g)
{
    g.setFont (theme::raleway (13.5f, 500));
    g.setColour (theme::textDim);
    g.drawText (label, 0, 0, getWidth(), 18, Justification::centredLeft, true);
    g.setFont (theme::mono (12.5f, true));
    g.setColour (theme::brass);
    g.drawText (valueText, 0, 0, getWidth(), 18, Justification::centredRight, false);
}

//==============================================================================
ChoiceRow::ChoiceRow (RadianceProcessor& p, const char* paramId, String l) : label (std::move (l))
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (p.apvts.getParameter (paramId)))
        box.addItemList (c->choices, 1);
    box.setWantsKeyboardFocus (false);
    box.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (box);
    attachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, paramId, box);
}

void ChoiceRow::resized() { box.setBounds (0, 21, getWidth(), 31); }

void ChoiceRow::paint (Graphics& g)
{
    g.setFont (theme::raleway (13.5f, 500));
    g.setColour (theme::textDim);
    g.drawText (label, 0, 0, getWidth(), 18, Justification::centredLeft, true);
    if (valueText.isNotEmpty())
    {
        g.setFont (theme::mono (12.5f, true));
        g.setColour (theme::brass);
        g.drawText (valueText, 0, 0, getWidth(), 18, Justification::centredRight, false);
    }
}

//==============================================================================
void Module::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 8);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r, 8, 1);
    g.setColour (theme::textFaint);
    g.setFont (theme::raleway (12.5f, 700).withExtraKerningFactor (0.08f));
    g.drawText (title.toUpperCase(), kPad, kPad - 2, getWidth() - 2 * kPad, 16, Justification::centredLeft, false);
    if (right.isNotEmpty())
    {
        g.setFont (theme::mono (12.0f).withExtraKerningFactor (0.08f));
        g.drawText (right.toUpperCase(), kPad, kPad - 2, getWidth() - 2 * kPad, 16, Justification::centredRight, false);
    }
}

int Module::layoutColumn (int width)
{
    int y = kPad + kTitle;
    const int w = width - 2 * kPad;
    for (auto* c : getChildren())
    {
        if (! c->isVisible()) continue;
        int h = c->getHeight();
        if (dynamic_cast<ParamRow*> (c) != nullptr) h = ParamRow::kHeight;
        else if (dynamic_cast<ChoiceRow*> (c) != nullptr) h = ChoiceRow::kHeight;
        else if (auto* hint = dynamic_cast<Hint*> (c)) h = hint->heightFor (w);
        c->setBounds (kPad, y, w, h);
        y += h + kGap;
    }
    return y - kGap + kPad;
}

//==============================================================================
int Hint::heightFor (int width) const
{
    AttributedString a;
    a.append (text, theme::raleway (fontSize, 500), theme::textFaint);
    a.setLineSpacing (3.0f);
    TextLayout tl;
    tl.createLayout (a, (float) jmax (10, width));
    return (int) std::ceil (tl.getHeight()) + 2;
}

void Hint::paint (Graphics& g)
{
    AttributedString a;
    a.append (text, theme::raleway (fontSize, 500), theme::textFaint);
    a.setLineSpacing (3.0f);
    a.draw (g, getLocalBounds().toFloat());
}

//==============================================================================
void LibraryList::paint (Graphics& g)
{
    const auto& lib = proc.library();
    const double sr = proc.engineRate();
    for (int i = 0; i < (int) lib.size(); ++i)
    {
        const auto& m = lib[(size_t) i];
        auto r = Rectangle<float> (0, (float) (i * kRow), (float) getWidth(), (float) kRow - 4).reduced (0.5f);
        const bool sel = i == proc.selectedIndex();
        if (sel || i == hover) { g.setColour (theme::panel2); g.fillRoundedRectangle (r, 3); }
        if (sel) { g.setColour (theme::brassDim); g.drawRoundedRectangle (r, 3, 1); }
        // insignia F / U
        auto badge = Rectangle<float> (8, r.getCentreY() - 8, 16, 16);
        g.setColour (m.factory ? theme::brass.withAlpha (0.18f) : theme::teal.withAlpha (0.16f));
        g.fillRoundedRectangle (badge, 3);
        g.setColour (m.factory ? theme::brass : theme::teal);
        g.setFont (theme::mono (9.0f, true));
        g.drawText (m.factory ? "F" : "U", badge, Justification::centred, false);
        // duración
        double secs = ra::kFactoryDur;
        if (! m.factory)
            secs = m.data ? m.data->size() / sr : (m.originalRate > 0 ? m.original.size() / m.originalRate : m.original.size() / sr);
        const String dur = String (secs, 1) + "s";
        g.setFont (theme::mono (11.5f));
        g.setColour (theme::textFaint);
        const int durRight = m.factory ? getWidth() - 8 : getWidth() - 32;
        g.drawText (dur, durRight - 50, (int) r.getY(), 50, (int) r.getHeight(), Justification::centredRight, false);
        g.setFont (theme::raleway (13.5f, 500));
        g.setColour (theme::text);
        g.drawText (m.name, 32, (int) r.getY(), durRight - 50 - 36, (int) r.getHeight(), Justification::centredLeft, true);
        if (! m.factory)
        {
            auto d = deleteRect (i).toFloat();
            if (i == hover) { g.setColour (theme::warn.withAlpha (0.18f)); g.fillRoundedRectangle (d, 3); }
            g.setColour (i == hover ? theme::warn : theme::textFaint);
            g.setFont (theme::raleway (14.0f, 600));
            g.drawText (u8 ("×"), d, Justification::centred, false);
        }
    }
}

void LibraryList::mouseDown (const MouseEvent& e)
{
    const int r = rowAt (e.y);
    if (r < 0) return;
    if (! proc.library()[(size_t) r].factory && deleteRect (r).contains (e.getPosition()))
    {
        if (onDelete) onDelete (r);
        return;
    }
    proc.selectMaterial (r);
    repaint();
}

//==============================================================================
void Scope::rebuildWave()
{
    const auto* m = proc.selectedMaterial();
    const int w = jmax (1, getWidth()), h = jmax (1, getHeight());
    wave = Image (Image::ARGB, w, h, true);
    cachedFor = m && m->data ? m->data.get() : nullptr;
    cachedSize = cachedFor ? cachedFor->size() : 0;
    if (cachedFor == nullptr) return;
    Graphics g (wave);
    const auto& data = *cachedFor;
    const float mid = h / 2.0f;
    // como drawScope: una línea vertical mín–máx por columna
    const size_t step = jmax ((size_t) 1, data.size() / (size_t) w);
    g.setColour (Colour (0x8c8b6bff));
    for (int x = 0; x < w; ++x)
    {
        float mn = 1, mx = -1;
        const size_t start = (size_t) x * step;
        for (size_t i = 0; i < step && start + i < data.size(); ++i) { mn = jmin (mn, data[start + i]); mx = jmax (mx, data[start + i]); }
        if (start >= data.size()) mn = mx = 0;
        g.drawVerticalLine (x, mid - mx * mid * 0.9f, jmax (mid - mn * mid * 0.9f, mid - mx * mid * 0.9f + 1));
    }
}

void Scope::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::scopeBg);
    g.fillRoundedRectangle (r, 3);
    const auto* m = proc.selectedMaterial();
    if (m == nullptr || ! m->data)
    {
        g.setColour (Colours::white.withAlpha (0.25f));
        g.setFont (theme::mono (13.0f));
        g.drawText (m ? u8 ("generando material…") : u8 ("Selecciona un preset de la librería"), r.reduced (16, 0), Justification::centredLeft, false);
    }
    else
    {
        if (cachedFor != m->data.get() || cachedSize != m->data->size() || wave.getWidth() != getWidth() || wave.getHeight() != getHeight()) rebuildWave();
        g.drawImageAt (wave, 0, 0);
        const float w = (float) getWidth(), h = (float) getHeight(), mid = h / 2;
        const float cx = proc.displayPosition() * w;
        g.setColour (Colour (0xd95cf0d0));
        g.fillRect (cx - 1, 0.0f, 2.0f, h);
        const double now = Time::getMillisecondCounterHiRes();
        for (auto& d : dots)
        {
            const double age = now - d.born;
            if (age > 900) continue;
            const float alpha = (float) (1 - age / 900);
            const float gx = d.pos * w;
            const float gy = mid + (float) std::sin (d.pos * 997 + d.born * 0.001) * h * 0.32f;
            g.setColour (Colour (0xff5cf0d0).withAlpha (alpha * 0.7f));
            g.fillEllipse (gx - 2.4f, gy - 2.4f, 4.8f, 4.8f);
        }
    }
    g.setColour (Colour (0xff2a2a35));
    g.drawRoundedRectangle (r.reduced (0.5f), 3, 1);
}

void Scope::tick()
{
    float marks[256];
    const int n = proc.popGrainMarks (marks, 256);
    const double now = Time::getMillisecondCounterHiRes();
    for (int i = 0; i < n; ++i) dots.push_back ({ marks[i], now + i * 0.01 });
    dots.erase (std::remove_if (dots.begin(), dots.end(), [now] (const Dot& d) { return now - d.born > 900; }), dots.end());
    if (dots.size() > 200) dots.erase (dots.begin(), dots.begin() + (long) (dots.size() - 200));
    repaint();
}

void Scope::drag (const MouseEvent& e)
{
    auto* prm = proc.apvts.getParameter (pid::posKnob);
    if (prm == nullptr) return;
    if (! dragging) { prm->beginChangeGesture(); dragging = true; }
    const float v = jlimit (0.0f, 1.0f, (float) e.x / (float) jmax (1, getWidth()));
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void Scope::mouseUp (const MouseEvent&)
{
    if (dragging)
        if (auto* prm = proc.apvts.getParameter (pid::posKnob)) prm->endChangeGesture();
    dragging = false;
}

//==============================================================================
void PatchList::refreshFiles()
{
    files = RadianceProcessor::patchesFolder().findChildFiles (File::findFiles, false, "*.json");
    std::sort (files.begin(), files.end(), [] (const File& a, const File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
    names.clear();
    for (auto& f : files)
    {
        auto v = JSON::parse (f);
        auto n = v.getProperty ("name", "").toString();
        names.add (n.isNotEmpty() ? n : f.getFileNameWithoutExtension());
    }
    repaint();
}

void PatchList::paint (Graphics& g)
{
    if (files.isEmpty())
    {
        g.setFont (theme::raleway (12.0f));
        g.setColour (theme::textFaint);
        g.drawText (u8 ("Aún no hay patches guardados."), getLocalBounds().withHeight (kRow), Justification::centredLeft, false);
        return;
    }
    for (int i = 0; i < files.size(); ++i)
    {
        auto r = Rectangle<float> (0, (float) (i * kRow), (float) getWidth(), (float) kRow - 3);
        if (i == hover) { g.setColour (theme::panel2); g.fillRoundedRectangle (r, 3); }
        g.setFont (theme::mono (11.5f));
        g.setColour (theme::textFaint);
        const String date = files[i].getLastModificationTime().formatted ("%d/%m/%Y");
        g.drawText (date, getWidth() - 140, (int) r.getY(), 84, (int) r.getHeight(), Justification::centredRight, false);
        g.setFont (theme::raleway (13.5f));
        g.setColour (theme::text);
        g.drawText (names[i], 8, (int) r.getY(), getWidth() - 160, (int) r.getHeight(), Justification::centredLeft, true);
        g.setFont (theme::raleway (13.0f, 600));
        g.setColour (i == hover ? theme::text : theme::textFaint);
        g.drawText (u8 ("↓"), getWidth() - 48, (int) r.getY(), 20, (int) r.getHeight(), Justification::centred, false);
        g.setColour (i == hover ? theme::warn : theme::textFaint);
        g.drawText (u8 ("×"), getWidth() - 24, (int) r.getY(), 20, (int) r.getHeight(), Justification::centred, false);
    }
}

void PatchList::mouseDown (const MouseEvent& e)
{
    const int i = e.y / kRow;
    if (i < 0 || i >= files.size()) return;
    const auto f = files[i];
    if (e.x >= getWidth() - 26) { if (onDelete) onDelete (f); }
    else if (e.x >= getWidth() - 50) { if (onExport) onExport (f); }
    else if (onLoad) onLoad (f);
}

//==============================================================================
std::vector<Keyboard::Key> Keyboard::layout() const
{
    std::vector<Key> keys;
    static const int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int blackOffsets[7] = { 1, 3, -1, 6, 8, 10, -1 };
    const int totalWhite = 14;
    const float gap = 2, w = (float) getWidth(), h = (float) getHeight();
    const float ww = (w - gap * (totalWhite - 1)) / totalWhite;
    std::vector<Key> blacks;
    int wi = 0;
    for (int oct = 0; oct < 2; ++oct)
        for (int k = 0; k < 7; ++k, ++wi)
        {
            keys.push_back ({ startNote() + oct * 12 + whiteOffsets[k], { wi * (ww + gap), 0, ww, h }, false });
            if (blackOffsets[k] >= 0)
            {
                const float left = (wi + 1) * w / totalWhite - 17;
                blacks.push_back ({ startNote() + oct * 12 + blackOffsets[k], { left, 0, 34, h * 0.64f }, true });
            }
        }
    keys.insert (keys.end(), blacks.begin(), blacks.end());
    return keys;
}

int Keyboard::noteAt (Point<float> p) const
{
    const auto keys = layout();
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)   // negras primero (están encima)
        if (it->r.contains (p)) return it->midi;
    return -1;
}

void Keyboard::paint (Graphics& g)
{
    for (auto& k : layout())
    {
        const bool on = proc.isNoteLit (k.midi) || k.midi == mouseNote;
        auto r = k.r;
        if (on) g.setGradientFill (ColourGradient (theme::brass, 0, r.getY(), theme::brassDim, 0, r.getBottom(), false));
        else if (k.black) g.setGradientFill (ColourGradient (Colour (0xff26282c), 0, r.getY(), Colour (0xff101113), 0, r.getBottom(), false));
        else g.setGradientFill (ColourGradient (Colour (0xffe9e7e0), 0, r.getY(), Colour (0xffc7c5bd), 0, r.getBottom(), false));
        Path p;
        p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), k.black ? 3.0f : 4.0f, k.black ? 3.0f : 4.0f, false, false, true, true);
        g.fillPath (p);
        g.setColour (k.black ? Colours::black : Colour (0xff0b0c0e));
        g.strokePath (p, PathStrokeType (1.0f));
        if (! k.black)
        {
            g.setFont (theme::mono (10.5f));
            g.setColour (on ? theme::accentText : Colour (0xff5a584f));
            g.drawText (noteName (k.midi), r.withTrimmedBottom (6), Justification::centredBottom, false);
        }
    }
}

void Keyboard::mouseDown (const MouseEvent& e)
{
    mouseNote = noteAt (e.position);
    if (mouseNote >= 0) proc.keyboardState.noteOn (1, mouseNote, 1.0f);
    repaint();
}

void Keyboard::mouseDrag (const MouseEvent&)
{
    // como la web: la nota sigue sonando hasta soltar el botón, aunque el cursor salga de la tecla
}

void Keyboard::mouseUp (const MouseEvent&)
{
    if (mouseNote >= 0) proc.keyboardState.noteOff (1, mouseNote, 0.0f);
    mouseNote = -1;
    repaint();
}

void Keyboard::refresh()
{
    uint64_t now[2] = { 0, 0 };
    for (int m = 0; m < 128; ++m) if (proc.isNoteLit (m)) now[m >> 6] |= uint64_t (1) << (m & 63);
    if (now[0] != litCache[0] || now[1] != litCache[1] || shiftCache != proc.octaveShift)
    {
        litCache[0] = now[0]; litCache[1] = now[1]; shiftCache = proc.octaveShift;
        repaint();
    }
}
