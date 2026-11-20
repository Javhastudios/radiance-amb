#include "PluginEditor.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }
String fmtInt (double v, const char* unit) { return String (roundToInt (v)) + unit; }
const char kKeyChars[] = "awsedftgyhujkolp;";   // como COMPUTER_KEY_MAP de la web

// Contenedor de botones en fila (.row con display:flex)
class ButtonRow : public Component
{
public:
    void resized() override
    {
        const int n = getNumChildComponents();
        if (n == 0) return;
        const int gap = 8, w = (getWidth() - gap * (n - 1)) / n;
        for (int i = 0; i < n; ++i) getChildComponent (i)->setBounds (i * (w + gap), 0, i == n - 1 ? getWidth() - i * (w + gap) : w, getHeight());
    }
};

// Fila con la ganancia de muestra (no es un parámetro: va con cada material)
class GainRow : public Component
{
public:
    explicit GainRow (RadianceProcessor& p) : proc (p)
    {
        slider.setSliderStyle (Slider::LinearHorizontal);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setRange (-24, 24, 0.5);
        slider.setDoubleClickReturnValue (true, 0);
        slider.setWantsKeyboardFocus (false);
        slider.onValueChange = [this] { if (! updating) proc.setSampleGainDb (slider.getValue()); repaint(); };
        addAndMakeVisible (slider);
        setSize (100, ParamRow::kHeight);
    }
    void resized() override { slider.setBounds (0, 20, getWidth(), 18); }
    void paint (Graphics& g) override
    {
        g.setFont (theme::raleway (13.5f, 500));
        g.setColour (theme::textDim);
        g.drawText ("Ganancia de muestra", 0, 0, getWidth(), 18, Justification::centredLeft, true);
        const double db = slider.getValue();
        g.setFont (theme::mono (12.5f, true));
        g.setColour (theme::brass);
        g.drawText ((db >= 0 ? "+" : "") + String (db, 1) + " dB", 0, 0, getWidth(), 18, Justification::centredRight, false);
    }
    void refresh()
    {
        const auto* m = proc.selectedMaterial();
        const double db = m ? m->gainDb : 0;
        if (std::abs (db - slider.getValue()) > 1e-6 && ! slider.isMouseButtonDown())
        {
            updating = true; slider.setValue (db, dontSendNotification); updating = false; repaint();
        }
    }
    Slider slider;

private:
    RadianceProcessor& proc;
    bool updating = false;
};
} // namespace

//==============================================================================
class Panel : public Component, public FileDragAndDropTarget
{
public:
    static constexpr int kWidth = 1180, kMargin = 26, kGap = 14, kHeader = 150;

    explicit Panel (RadianceProcessor& p)
        : proc (p),
          patches ("Patches"), library (u8 ("Librería"), u8 ("F · U")), scopePanel (""), keyboardPanel ("Teclado"),
          grain ("Grano"), source ("Fuente"), note ("Nota (ADSR)"), tone ("Tono"),
          timbre ("Timbre"), space ("Espacio"), reverb ("Reverb"), output ("Salida"),
          libList (p), scope (p), keyboard (p),
          saveBtn (u8 ("Guardar patch"), Btn::Style::Primary, true), importBtn (u8 ("Cargar archivo"), Btn::Style::Normal, true),
          folderBtn (u8 ("Abrir carpeta"), Btn::Style::Normal, true),
          uploadBtn (u8 ("+ Cargar audio"), Btn::Style::Upload),
          playBtn (u8 ("▶ Reproducir"), Btn::Style::Primary), recBtn (u8 ("● Grabar")), randomBtn (u8 ("Aleatorizar")),
          octDown (u8 ("◀ Octava"), Btn::Style::Normal, true), octUp (u8 ("Octava ▶"), Btn::Style::Normal, true),
          gainRow (p),
          midiHint (""), noteHint (u8 ("Se dispara al tocar una tecla del teclado de abajo. El dron (▶ Reproducir) no usa esta envolvente — suena siempre a volumen pleno.")),
          keysHint (u8 ("También puedes tocar con el teclado del ordenador: A W S E D F T G Y H U J = una octava blanca/negra tipo piano, K O L P ; continúa a la octava siguiente.")),
          libHint (u8 ("Arrastra aquí archivos de audio (WAV, AIFF, FLAC, MP3) o usa el botón. Se guardan en Documentos/Radiance-AMB/Libreria, compartida por la app y el plugin."))
    {
        midiHint.setText (proc.isStandalone()
            ? u8 ("MIDI: elige tu controlador en Options (arriba a la izquierda) → Audio/MIDI Settings. Los patches son los mismos .json que guarda la versión web.")
            : u8 ("MIDI: llega por la pista del DAW (notas, pitch bend, rueda de modulación y pedal de sustain). Los patches son los mismos .json que guarda la versión web."));

        // ── Patches ──
        nameEditor.setTextToShowWhenEmpty (u8 ("Nombre del patch…"), theme::textFaint);
        nameEditor.setFont (theme::raleway (13.5f));
        nameEditor.setIndents (8, 6);
        nameEditor.onReturnKey = [this] { savePatch(); };
        saveBtn.onClick = [this] { savePatch(); };
        importBtn.onClick = [this] { importPatch(); };
        folderBtn.onClick = [] { RadianceProcessor::patchesFolder().createDirectory(); RadianceProcessor::userFolder().startAsProcess(); };
        patchList.onLoad = [this] (const File& f) { loadPatchFile (f, false); };
        patchList.onExport = [this] (const File& f) { exportPatch (f); };
        patchList.onDelete = [this] (const File& f) { f.moveToTrash(); patchList.refreshFiles(); };
        patchView.setViewedComponent (&patchList, false);
        patchView.setScrollBarsShown (true, false);
        patchView.setScrollBarThickness (8);
        for (auto* c : std::initializer_list<Component*> { &nameEditor, &saveBtn, &importBtn, &folderBtn, &patchView, &midiHint }) patches.addAndMakeVisible (c);

        // ── Librería ──
        libList.onDelete = [this] (int i) { proc.deleteUserMaterial (i); };
        libView.setViewedComponent (&libList, false);
        libView.setScrollBarsShown (true, false);
        libView.setScrollBarThickness (8);
        uploadBtn.onClick = [this] { chooseAudio(); };
        for (auto* c : std::initializer_list<Component*> { &libView, &uploadBtn, &gainRow, &libHint }) library.addAndMakeVisible (c);
        scopePanel.addAndMakeVisible (scope);
        scopeHead.setInterceptsMouseClicks (false, false);
        scopeHead.fn = [this] (Graphics& g)
        {
            g.setFont (theme::raleway (14.0f, 600));
            g.setColour (theme::text);
            g.drawText (scopeName, scopeHead.getLocalBounds(), Justification::centredLeft, true);
            g.setFont (theme::mono (12.0f));
            g.setColour (theme::textFaint);
            g.drawText (scopeMeta, scopeHead.getLocalBounds(), Justification::centredRight, false);
        };
        scopePanel.addAndMakeVisible (scopeHead);

        // ── Módulos (mismo orden y textos que la web) ──
        addRow (grain, pid::grainSize, u8 ("Tamaño"), [] (double v) { return fmtInt (v, " ms"); });
        addRow (grain, pid::grainDensity, "Densidad", [] (double v) { return fmtInt (v, "/s"); });
        addChoice (grain, pid::grainShape, "Forma");
        addRow (grain, pid::grainAttack, "Ataque", [] (double v) { return fmtInt (v, " ms"); });
        addRow (grain, pid::grainRelease, "Release", [] (double v) { return fmtInt (v, " ms"); });
        addRow (grain, pid::grainJitter, u8 ("Dispersión de posición"), [] (double v) { return fmtInt (v, "%"); });

        addRow (source, pid::posKnob, u8 ("Posición"), [] (double v) { return String (v, 2); });
        addChoice (source, pid::posMode, u8 ("Modo de posición"));
        posRateModeRow = addChoice (source, pid::posRateMode, "Velocidad: modo");
        posRateRow = addRow (source, pid::posRate, "Velocidad", [] (double v) { return String (v, 2) + " Hz"; });
        posSyncDivRow = addChoice (source, pid::posSyncDiv, "Figura");
        posTempoRow = addRow (source, pid::posTempo, "Tempo", [] (double v) { return fmtInt (v, " BPM"); });
        posDepthRow = addRow (source, pid::posDepth, "Profundidad", [] (double v) { return fmtInt (v, "%"); });

        addRow (note, pid::noteAttack, "Ataque", [] (double v) { return fmtInt (v, " ms"); });
        addRow (note, pid::noteDecay, "Decay", [] (double v) { return fmtInt (v, " ms"); });
        addRow (note, pid::noteSustain, "Sustain", [] (double v) { return fmtInt (v, "%"); });
        addRow (note, pid::noteRelease, "Release", [] (double v) { return fmtInt (v, " ms"); });
        note.addAndMakeVisible (noteHint);

        addRow (tone, pid::pitchTranspose, u8 ("Transposición"), [] (double v) { return String (roundToInt (v)) + " st"; });
        addRow (tone, pid::pitchFine, "Fino", [] (double v) { return String (roundToInt (v)) + " ct"; });
        addRow (tone, pid::pitchSpray, u8 ("Deriva de afinación"), [] (double v) { return fmtInt (v, " ct"); });

        addChoice (timbre, pid::filterType, "Tipo");
        addRow (timbre, pid::filterCutoff, "Corte", [] (double v) { return String (roundToInt (ra::cutoffHzFromPct (v))) + " Hz"; });
        addRow (timbre, pid::filterQ, "Resonancia", [] (double v) { return String (v, 1); });
        addRow (timbre, pid::filterSpray, "Textura", [] (double v) { return fmtInt (v, "%"); });

        addRow (space, pid::stereoSpread, u8 ("Dispersión estéreo"), [] (double v) { return fmtInt (v, "%"); });
        addRow (space, pid::reverbSpace, "Space (retardo canal derecho)", [] (double v) { return fmtInt (v, " ms"); });
        addRow (space, pid::moveRate, "Velocidad de movimiento", [] (double v) { return String (v, 2) + " Hz"; });
        addRow (space, pid::moveDepth, "Profundidad de movimiento", [] (double v) { return fmtInt (v, "%"); });

        addRow (reverb, pid::dryWet, "Dry / Wet", [] (double v) { return fmtInt (v, "% wet"); });
        addRow (reverb, pid::reverbPreDelay, "Pre-delay", [] (double v) { return fmtInt (v, " ms"); });
        addRow (reverb, pid::shimmerTone, "Shimmer: tono", [] (double v) { return (v >= 0 ? "+" : "") + String (v, 1) + " st"; });
        addRow (reverb, pid::shimmerAmount, "Shimmer: cantidad", [] (double v) { return fmtInt (v, "%"); });

        playBtn.onClick = [this] { proc.setParam (pid::play, proc.droneOn() ? 0.0f : 1.0f); };
        recBtn.onClick = [this] { toggleRecording(); };
        randomBtn.onClick = [this] { proc.randomize(); };
        randomBtn.setTextColour (theme::teal);
        transport.addAndMakeVisible (playBtn);
        transport.addAndMakeVisible (recBtn);
        transport.setSize (100, 46);
        randomBtn.setSize (100, 40);
        output.addAndMakeVisible (transport);
        output.addAndMakeVisible (randomBtn);
        addRow (output, pid::masterVol, "Volumen", [] (double v) { return fmtInt (v, "%"); });
        addRow (output, pid::pitchBendRange, "Rango de pitch bend", [] (double v) { return u8 ("±") + String (roundToInt (v)) + " st"; });
        recStatus = std::make_unique<Hint> ("", 13.0f);
        output.addChildComponent (*recStatus);

        // ── Teclado ──
        octDown.onClick = [this] { proc.octaveShift = jmax (-3, proc.octaveShift - 1); };
        octUp.onClick = [this] { proc.octaveShift = jmin (3, proc.octaveShift + 1); };
        for (auto* c : std::initializer_list<Component*> { &keyboard, &octDown, &octUp, &keysHint }) keyboardPanel.addAndMakeVisible (c);

        for (auto* m : modules()) addAndMakeVisible (m);
        for (auto* c : std::initializer_list<Component*> { &patches, &library, &scopePanel, &keyboardPanel }) addAndMakeVisible (c);
        setWantsKeyboardFocusRecursively (this);
        nameEditor.setWantsKeyboardFocus (true);

        patchList.refreshFiles();
        layout();
        refresh();
    }

    int contentHeight() const { return totalH; }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::bg);
        // brillo radial de fondo (radial-gradient en 20% -10%)
        g.setGradientFill (ColourGradient (theme::bgGlow, getWidth() * 0.2f, -60.0f, theme::bg.withAlpha (0.0f), getWidth() * 0.2f + 720, 300, true));
        g.fillRect (0, 0, getWidth(), 700);
        // cabecera
        g.setColour (theme::headerBg);
        g.fillRect (0, 0, getWidth(), kHeader);
        g.setColour (theme::lineSoft);
        g.fillRect (0, kHeader - 1, getWidth(), 1);
        // título con degradado
        const String title = "RADIANCE_AMB";
        const auto tf = theme::audiowide (38.0f).withExtraKerningFactor (0.03f);
        const float tw = theme::textWidth (tf, title);
        const float tx = getWidth() * 0.5f - tw * 0.5f;
        g.setGradientFill (ColourGradient::horizontal (theme::headerText, tx, Colour (0xffc4b5fd), tx + tw));
        g.setFont (tf);
        g.drawText (title, Rectangle<float> (tx, 18, tw + 4, 46), Justification::centredLeft, false);
        g.setColour (theme::headerTextDim);
        g.setFont (theme::mono (11.5f).withExtraKerningFactor (0.18f));
        g.drawText (u8 ("MOTOR GRANULAR CONTINUO · MATERIAL SINTÉTICO"), 0, 66, getWidth(), 16, Justification::centred, false);
        // logo (máscara PNG teñida del color brass)
        drawTinted (g, theme::logoJavha(), Rectangle<float> (getWidth() * 0.5f - 65, 90, 130, 49), theme::brass);
        // pie
        g.setColour (theme::textFaint);
        g.setFont (theme::raleway (12.0f, 600).withExtraKerningFactor (0.05f));
        g.drawText (u8 ("© 2026 JAVHASTUDIOS · ALL RIGHTS RESERVED"), 0, totalH - 40, getWidth(), 18, Justification::centred, false);
    }

    void paintOverChildren (Graphics& g) override
    {
        // logo de la marca bajo el scope (opacidad 0,35)
        auto mark = scopePanel.getBounds().toFloat();
        drawTinted (g, theme::logoMark(), Rectangle<float> (120, 120).withCentre ({ mark.getCentreX(), scope.getBottom() + scopePanel.getY() + 14 + 60.0f }),
                    theme::textFaint.withAlpha (0.35f));
        if (dragOver)
        {
            g.setColour (theme::brass.withAlpha (0.12f));
            g.fillRoundedRectangle (library.getBounds().toFloat(), 8);
            g.setColour (theme::brass);
            g.drawRoundedRectangle (library.getBounds().toFloat().reduced (1), 8, 2);
        }
    }

    static void drawTinted (Graphics& g, const Image& mask, Rectangle<float> r, Colour c)
    {
        if (! mask.isValid()) return;
        Graphics::ScopedSaveState ss (g);
        g.setColour (c);
        g.drawImage (mask, r, RectanglePlacement::centred, true);   // fillAlphaChannelWithCurrentBrush
    }

    void resized() override { layout(); }

    void layout()
    {
        const int x0 = jmax (kMargin, (getWidth() - (kWidth - 2 * kMargin)) / 2);
        const int W = kWidth - 2 * kMargin;
        int y = kHeader + 20;

        // Patches
        {
            const int h = 150;
            patches.setBounds (x0, y, W, h);
            const int left = 380, pad = Module::kPad, top = pad + Module::kTitle;
            nameEditor.setBounds (pad, top, left, 32);
            int bx = pad;
            for (auto* b : { &saveBtn, &importBtn, &folderBtn })
            {
                b->setBounds (bx, top + 42, b->idealWidth(), 30);
                bx += b->getWidth() + 8;
            }
            midiHint.setBounds (pad, top + 80, left, midiHint.heightFor (left));
            const int lx = pad + left + 24;
            patchView.setBounds (lx, top, W - lx - pad, h - top - pad);
            patchList.setSize (patchView.getWidth() - (patchList.contentHeight() > patchView.getHeight() ? 10 : 0), patchList.contentHeight());
            y += h + kGap;
        }

        // Librería + scope
        {
            const int h = 360;
            library.setBounds (x0, y, 300, h);
            const int pad = Module::kPad, top = pad + Module::kTitle, iw = 300 - 2 * pad;
            const int listH = jmin (220, jmax (libList.contentHeight(), LibraryList::kRow));
            libView.setBounds (pad, top, iw, listH);
            libList.setSize (iw - (libList.contentHeight() > listH ? 10 : 0), libList.contentHeight());
            uploadBtn.setBounds (pad, top + listH + 6, iw, 34);
            gainRow.setBounds (pad, uploadBtn.getBottom() + 12, iw, ParamRow::kHeight);
            libHint.setBounds (pad, gainRow.getBottom() + 4, iw, jmax (0, h - pad - gainRow.getBottom() - 4));
            libHint.setVisible (libHint.heightFor (iw) <= libHint.getHeight());
            scopePanel.setBounds (x0 + 300 + kGap, y, W - 300 - kGap, h);
            scope.setBounds (pad, pad + 26, scopePanel.getWidth() - 2 * pad, 180);
            scopeHead.setBounds (pad, pad - 2, scopePanel.getWidth() - 2 * pad, 20);
            y += h + kGap;
        }

        // Dos filas de 4 módulos
        const int mw = (W - 3 * kGap) / 4;
        for (int row = 0; row < 2; ++row)
        {
            Module* ms[4] = { row == 0 ? &grain : &timbre, row == 0 ? &source : &space, row == 0 ? &note : &reverb, row == 0 ? &tone : &output };
            int rowH = 0;
            for (int i = 0; i < 4; ++i)
            {
                ms[i]->setSize (mw, 10);
                rowH = jmax (rowH, ms[i]->layoutColumn (mw));
            }
            for (int i = 0; i < 4; ++i) ms[i]->setBounds (x0 + i * (mw + kGap), y, i == 3 ? W - 3 * (mw + kGap) : mw, rowH);
            y += rowH + kGap;
        }

        // Teclado
        {
            const int pad = Module::kPad, top = pad + Module::kTitle;
            const int hintW = W - 2 * pad - 180;
            const int hintH = keysHint.heightFor (hintW);
            const int h = top + 110 + 10 + jmax (28, hintH) + pad;
            keyboardPanel.setBounds (x0, y, W, h);
            keyboard.setBounds (pad, top, W - 2 * pad, 110);
            octDown.setBounds (pad, top + 120, octDown.idealWidth(), 28);
            octUp.setBounds (octDown.getRight() + 8, top + 120, octUp.idealWidth(), 28);
            keysHint.setBounds (octUp.getRight() + 12, top + 120 + jmax (0, (28 - hintH) / 2), W - 2 * pad - (octUp.getRight() + 12 - pad), hintH);
            y += h;
        }
        totalH = y + 60;
    }

    void refresh()
    {
        for (auto* r : rows) r->refresh();
        gainRow.refresh();

        // filas de la fuente que dependen del modo (updatePosRowsVisibility)
        const int mode = (int) proc.param (pid::posMode);
        const bool isLfo = mode == 1, isDrift = mode == 2, showDepth = isLfo || isDrift;
        const bool syncActive = isLfo && (int) proc.param (pid::posRateMode) == 1;
        bool changed = false;
        auto vis = [&changed] (Component* c, bool v) { if (c->isVisible() != v) { c->setVisible (v); changed = true; } };
        vis (posRateModeRow, isLfo);
        vis (posRateRow, showDepth && ! syncActive);
        vis (posSyncDivRow, syncActive);
        vis (posTempoRow, syncActive);
        vis (posDepthRow, showDepth);
        const int div = jlimit (0, 11, (int) proc.param (pid::posSyncDiv));
        posSyncDivRow->setValueText (String (ra::kSyncNames[div]) + u8 (" · ")
                                     + String (jmax (0.001, (proc.param (pid::posTempo) / 60.0) / ra::kSyncBeats[div]), 2) + " Hz");

        // transporte
        const bool on = proc.droneOn();
        playBtn.setButtonText (on ? u8 ("■ Detener") : u8 ("▶ Reproducir"));
        playBtn.setStyle (on ? Btn::Style::Active : Btn::Style::Primary);
        const bool rec = proc.isRecording();
        recBtn.setButtonText (rec ? u8 ("■ Detener") : u8 ("● Grabar"));
        recBtn.setStyle (rec ? Btn::Style::Danger : Btn::Style::Normal);
        vis (recStatus.get(), rec);
        if (rec)
        {
            const int s = (int) proc.recordedSeconds();
            recStatus->setText (u8 ("Grabando  ") + String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2));
        }

        // librería
        if (libVersion != proc.libraryVersion() || libCount != proc.library().size())
        {
            libVersion = proc.libraryVersion();
            libCount = proc.library().size();
            changed = true;
            libList.repaint();
        }
        if (const auto* m = proc.selectedMaterial())
        {
            const double secs = m->data ? m->data->size() / proc.engineRate() : ra::kFactoryDur;
            scopeName = m->name;
            scopeMeta = String (m->factory ? u8 ("Fábrica") : u8 ("Usuario")) + u8 (" · ") + String (secs, 1) + u8 ("s · ") + String (roundToInt (proc.engineRate())) + "Hz";
            if (scopeName != lastScopeName || scopeMeta != lastScopeMeta) { lastScopeName = scopeName; lastScopeMeta = scopeMeta; scopeHead.repaint(); scopePanel.repaint(); }
        }
        const String oct = "octava base: C" + String (4 + proc.octaveShift);
        keyboardPanel.setRightText (oct);
        scope.tick();
        keyboard.refresh();
        if (changed) layout();
    }

    // scope: nombre y datos encima (los pinta el panel)
    struct Painter : Component
    {
        std::function<void (Graphics&)> fn;
        void paint (Graphics& g) override { if (fn) fn (g); }
    } scopeHead;
    String scopeName, scopeMeta, lastScopeName, lastScopeMeta;

    // FileDragAndDropTarget: soltar audio sobre la ventana lo añade a la librería
    bool isInterestedInFileDrag (const StringArray& files) override
    {
        for (auto& f : files) if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;json")) return true;
        return false;
    }
    void fileDragEnter (const StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const StringArray& files, int, int) override
    {
        dragOver = false;
        Array<File> audio;
        for (auto& f : files)
        {
            File file (f);
            if (file.hasFileExtension ("json")) loadPatchFile (file, true);
            else audio.add (file);
        }
        if (! audio.isEmpty()) addAudio (audio);
        repaint();
    }

    std::vector<Module*> modules() { return { &grain, &source, &note, &tone, &timbre, &space, &reverb, &output }; }

private:
    ParamRow* addRow (Module& m, const char* id, const String& label, std::function<String (double)> fmt)
    {
        auto r = std::make_unique<ParamRow> (proc, id, label, std::move (fmt));
        m.addAndMakeVisible (*r);
        rows.push_back (r.get());
        owned.push_back (std::move (r));
        return rows.back();
    }
    ChoiceRow* addChoice (Module& m, const char* id, const String& label)
    {
        auto c = std::make_unique<ChoiceRow> (proc, id, label);
        auto* raw = c.get();
        m.addAndMakeVisible (*c);
        owned.push_back (std::move (c));
        return raw;
    }
    static void setWantsKeyboardFocusRecursively (Component* c)
    {
        c->setWantsKeyboardFocus (false);
        for (auto* ch : c->getChildren()) setWantsKeyboardFocusRecursively (ch);
    }

    void message (const String& title, const String& text)
    {
        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::NoIcon).withTitle (title).withMessage (text).withButton ("OK"), nullptr);
    }

    void savePatch()
    {
        const String name = nameEditor.getText().trim();
        if (name.isEmpty()) { message ("Patches", "Ponle un nombre al patch antes de guardarlo."); return; }
        auto folder = RadianceProcessor::patchesFolder();
        folder.createDirectory();
        auto f = folder.getChildFile (File::createLegalFileName (name) + ".json").getNonexistentSibling (false);
        f.replaceWithText (proc.patchToJson (name));
        nameEditor.clear();
        patchList.refreshFiles();
        layout();
    }

    void loadPatchFile (const File& f, bool addToList)
    {
        String name;
        const String warn = proc.applyPatchJson (f.loadFileAsString(), name);
        if (name.isEmpty()) name = f.getFileNameWithoutExtension();
        const bool failed = warn.startsWith (u8 ("el archivo"));
        if (failed) { message ("Patches", u8 ("No se pudo cargar el archivo como patch: ") + warn); return; }
        if (addToList && f.getParentDirectory() != RadianceProcessor::patchesFolder())
        {
            RadianceProcessor::patchesFolder().createDirectory();
            auto dest = RadianceProcessor::patchesFolder().getChildFile (f.getFileName()).getNonexistentSibling (false);
            dest.replaceWithText (proc.patchToJson (name));   // con el material ya dado de alta aquí
            patchList.refreshFiles();
            layout();
            message ("Patches", u8 ("Patch \"") + name + u8 ("\" cargado y añadido a tu lista.") + (warn.isNotEmpty() ? "\n\n" + warn : String()));
        }
        else if (warn.isNotEmpty()) message ("Patches", warn);
    }

    void importPatch()
    {
        chooser = std::make_unique<FileChooser> ("Cargar patch", File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"), "*.json");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) loadPatchFile (f, true);
        });
    }

    void exportPatch (const File& f)
    {
        const auto safe = f.getFileNameWithoutExtension().toLowerCase().replaceCharacters (" ", "_");
        chooser = std::make_unique<FileChooser> ("Descargar patch como .json",
                                                 File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads").getChildFile ("radiance-amb-patch-" + safe + ".json"), "*.json");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [f] (const FileChooser& fc)
        {
            const auto dest = fc.getResult();
            if (dest != File()) f.copyFileTo (dest);
        });
    }

    void chooseAudio()
    {
        chooser = std::make_unique<FileChooser> ("Cargar audio", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc) { addAudio (fc.getResults()); });
    }

    void addAudio (const Array<File>& files)
    {
        if (files.isEmpty()) return;
        const String errors = proc.addUserSamples (files);
        if (errors.isNotEmpty()) message (u8 ("Librería"), errors);
        layout();
    }

    void toggleRecording()
    {
        if (proc.isRecording())
        {
            const double secs = proc.recordedSeconds();
            proc.stopRecording();
            if (secs <= 0) { message ("Grabar", u8 ("No se capturó audio (¿estaba sonando el motor?).")); return; }
            const File f = lastRecording;
            AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::NoIcon).withTitle ("Grabar")
                                        .withMessage (u8 ("Grabación guardada en:\n") + f.getFullPathName())
                                        .withButton ("Mostrar en la carpeta").withButton ("OK"),
                                    [f] (int r) { if (r == 1) f.revealToUser(); });
            return;
        }
        auto folder = RadianceProcessor::userFolder().getChildFile ("Grabaciones");
        folder.createDirectory();
        lastRecording = folder.getChildFile ("radiance-amb-textura-" + Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav");
        if (! proc.startRecording (lastRecording)) message ("Grabar", u8 ("No se pudo crear el archivo de la grabación."));
    }

    RadianceProcessor& proc;
    Module patches, library, scopePanel, keyboardPanel;
    Module grain, source, note, tone, timbre, space, reverb, output;
    LibraryList libList;
    Viewport libView, patchView;
    PatchList patchList;
    Scope scope;
    Keyboard keyboard;
    TextEditor nameEditor;
    Btn saveBtn, importBtn, folderBtn, uploadBtn, playBtn, recBtn, randomBtn, octDown, octUp;
    ButtonRow transport;
    GainRow gainRow;
    Hint midiHint, noteHint, keysHint, libHint;
    std::unique_ptr<Hint> recStatus;
    std::vector<ParamRow*> rows;
    std::vector<std::unique_ptr<Component>> owned;
    ChoiceRow *posRateModeRow = nullptr, *posSyncDivRow = nullptr;
    ParamRow *posRateRow = nullptr, *posTempoRow = nullptr, *posDepthRow = nullptr;
    std::unique_ptr<FileChooser> chooser;
    File lastRecording;
    int libVersion = -1;
    size_t libCount = 0;
    int totalH = 1400;
    bool dragOver = false;

};

//==============================================================================
RadianceEditor::RadianceEditor (RadianceProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    panel = std::make_unique<Panel> (p);
    panel->setSize (Panel::kWidth, panel->contentHeight());
    viewport.setViewedComponent (panel.get(), false);
    viewport.setScrollBarThickness (10);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (720, 480, Panel::kWidth + 400, panel->contentHeight() + 40);
    setSize (Panel::kWidth + 10, jmin (panel->contentHeight(), 900));
    startTimerHz (30);
}

RadianceEditor::~RadianceEditor()
{
    stopTimer();
    for (auto& [k, m] : computerKeysDown) proc.keyboardState.noteOff (1, m, 0.0f);
    setLookAndFeel (nullptr);
}

void RadianceEditor::paint (Graphics& g) { g.fillAll (theme::bg); }

void RadianceEditor::resized()
{
    viewport.setBounds (getLocalBounds());
    panel->setSize (jmax (Panel::kWidth, viewport.getMaximumVisibleWidth()), panel->contentHeight());
}

void RadianceEditor::timerCallback()
{
    const int before = panel->contentHeight();
    panel->refresh();
    if (panel->contentHeight() != before || panel->getHeight() != panel->contentHeight())
        panel->setSize (jmax (Panel::kWidth, viewport.getMaximumVisibleWidth()), panel->contentHeight());
}

bool RadianceEditor::keyPressed (const KeyPress& k)
{
    const auto c = (char) CharacterFunctions::toLowerCase (k.getTextCharacter());
    return c != 0 && std::strchr (kKeyChars, c) != nullptr;
}

bool RadianceEditor::keyStateChanged (bool)
{
    bool handled = false;
    for (int i = 0; kKeyChars[i] != 0; ++i)
    {
        const char c = kKeyChars[i];
        const bool down = KeyPress::isKeyCurrentlyDown (c) || KeyPress::isKeyCurrentlyDown (CharacterFunctions::toUpperCase ((juce_wchar) c));
        const auto it = computerKeysDown.find (c);
        if (down && it == computerKeysDown.end())
        {
            const int midi = jlimit (0, 127, 48 + i + proc.octaveShift * 12);
            computerKeysDown[c] = midi;
            proc.keyboardState.noteOn (1, midi, 1.0f);
            handled = true;
        }
        else if (! down && it != computerKeysDown.end())
        {
            proc.keyboardState.noteOff (1, it->second, 0.0f);
            computerKeysDown.erase (it);
            handled = true;
        }
    }
    return handled;
}
