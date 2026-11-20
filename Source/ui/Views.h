// Controles con el aspecto del prototipo: botones .btn, filas de slider (.row), módulos,
// lista de la librería, scope con la posición, lista de patches y teclado.
#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"

// .btn (normal / primary / active / danger-active / small / subida con borde discontinuo)
class Btn : public juce::Button
{
public:
    enum class Style { Normal, Primary, Active, Danger, Upload };
    explicit Btn (const juce::String& text, Style s = Style::Normal, bool small = false);
    void setStyle (Style s) { if (s != style) { style = s; repaint(); } }
    void setTextColour (juce::Colour c) { customText = c; hasCustomText = true; }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    int idealWidth() const;

private:
    Style style;
    bool small;
    juce::Colour customText;
    bool hasCustomText = false;
};

// Una fila: etiqueta a la izquierda, valor en morado a la derecha y el slider debajo
class ParamRow : public juce::Component
{
public:
    ParamRow (RadianceProcessor& p, const char* paramId, juce::String label, std::function<juce::String (double)> fmt);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    static constexpr int kHeight = 40;
    juce::Slider slider;

private:
    juce::String label, valueText;
    std::function<juce::String (double)> format;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Fila con un menú (select.rack-select); el valor puede mostrarse a la derecha
class ChoiceRow : public juce::Component
{
public:
    ChoiceRow (RadianceProcessor& p, const char* paramId, juce::String label);
    void resized() override;
    void paint (juce::Graphics&) override;
    void setValueText (const juce::String& t) { if (t != valueText) { valueText = t; repaint(); } }
    static constexpr int kHeight = 54;
    juce::ComboBox box;

private:
    juce::String label, valueText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Panel con título (.panel / .module)
class Module : public juce::Component
{
public:
    explicit Module (juce::String titleText, juce::String rightText = {}) : title (std::move (titleText)), right (std::move (rightText)) {}
    void paint (juce::Graphics&) override;
    void setRightText (const juce::String& t) { if (t != right) { right = t; repaint(); } }
    // coloca los hijos visibles en columna; devuelve la altura que necesita
    int layoutColumn (int width);
    static constexpr int kPad = 14, kTitle = 26, kGap = 10;

private:
    juce::String title, right;
};

// Texto pequeño gris (.lib-hint)
class Hint : public juce::Component
{
public:
    explicit Hint (juce::String t, float size = 12.0f) : text (std::move (t)), fontSize (size) { setInterceptsMouseClicks (false, false); }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics&) override;
    int heightFor (int width) const;

private:
    juce::String text;
    float fontSize;
};

// Lista de materiales (.lib-list)
class LibraryList : public juce::Component
{
public:
    explicit LibraryList (RadianceProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { setHover (rowAt (e.y)); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    int contentHeight() const { return (int) proc.library().size() * kRow; }
    static constexpr int kRow = 32;
    std::function<void (int)> onDelete;

private:
    int rowAt (int y) const { const int r = y / kRow; return r >= 0 && r < (int) proc.library().size() ? r : -1; }
    void setHover (int r) { if (r != hover) { hover = r; repaint(); } }
    juce::Rectangle<int> deleteRect (int row) const { return { getWidth() - 26, row * kRow + 7, 18, 18 }; }
    RadianceProcessor& proc;
    int hover = -1;
};

// Forma de onda del material, cursor de posición (arrastrable) y puntos de los granos
class Scope : public juce::Component
{
public:
    explicit Scope (RadianceProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
    void paint (juce::Graphics&) override;
    void resized() override { cachedFor = nullptr; }
    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp (const juce::MouseEvent&) override;
    void tick();

private:
    void drag (const juce::MouseEvent&);
    void rebuildWave();
    RadianceProcessor& proc;
    juce::Image wave;
    const std::vector<float>* cachedFor = nullptr;
    size_t cachedSize = 0;
    struct Dot { float pos; double born; };
    std::vector<Dot> dots;
    bool dragging = false;
};

// Lista de patches guardados (archivos .json de la carpeta Patches)
class PatchList : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { setHover (e.y / kRow); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void refreshFiles();
    int contentHeight() const { return juce::jmax (1, files.size()) * kRow; }
    int numFiles() const { return files.size(); }
    static constexpr int kRow = 30;
    std::function<void (const juce::File&)> onLoad, onExport, onDelete;

private:
    void setHover (int r) { if (r != hover) { hover = r; repaint(); } }
    juce::Array<juce::File> files;
    juce::StringArray names;
    int hover = -1;
};

// Teclado en pantalla: 2 octavas desde C3 (más el desplazamiento de octava)
class Keyboard : public juce::Component
{
public:
    explicit Keyboard (RadianceProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    int startNote() const { return 48 + proc.octaveShift * 12; }

private:
    struct Key { int midi; juce::Rectangle<float> r; bool black; };
    std::vector<Key> layout() const;
    int noteAt (juce::Point<float>) const;
    RadianceProcessor& proc;
    int mouseNote = -1;
    uint64_t litCache[2] {};
    int shiftCache = 0;
};
