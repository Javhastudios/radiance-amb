// Prueba de humo del plugin completo (procesador + interfaz), sin DAW.
// Dron, notas por MIDI (ADSR, pedal, pitch bend), todos los materiales de fábrica, audio propio
// a otra frecuencia, patches (incluido uno con el formato de la web), estado con el audio
// dentro, grabación y capturas de la interfaz.  Uso: ra_smoke <carpeta-salida>
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

using namespace juce;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::cout << "  ok   " << msg << "\n"; else { std::cout << "  FALLO " << msg << "\n"; ++failures; } } while (0)

struct Stats { double rms = 0, peak = 0; bool finite = true; };

static Stats run (RadianceProcessor& p, int blocks, std::vector<MidiMessage> events = {}, int bs = 512)
{
    AudioBuffer<float> buf (2, bs);
    Stats s;
    double e = 0;
    int64 n = 0;
    for (int k = 0; k < blocks; ++k)
    {
        buf.clear();
        MidiBuffer midi;
        if (k == 0) for (auto& m : events) midi.addEvent (m, 5);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) s.finite = false;
                e += v * v; ++n;
                s.peak = jmax (s.peak, (double) std::abs (v));
            }
    }
    s.rms = std::sqrt (e / (double) n);
    return s;
}

static void snapshot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
}

static MidiMessage on (int n, int v = 100) { return MidiMessage::noteOn (1, n, (uint8) v); }
static MidiMessage off (int n) { return MidiMessage::noteOff (1, n); }
static void pump (int ms = 50) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static int blocksFor (double secs) { return (int) std::ceil (secs * 48000 / 512); }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File out (argc > 1 ? String (argv[1]) : File::getCurrentWorkingDirectory().getChildFile ("smoke-out").getFullPathName());
    out.createDirectory();
    std::cout << "carpeta de usuario: " << RadianceProcessor::userFolder().getFullPathName() << "\n";

    auto proc = std::make_unique<RadianceProcessor>();
    proc->setRateAndBufferSizeDetails (48000, 512);
    proc->prepareToPlay (48000, 512);
    CHECK (proc->selectedMaterial() != nullptr && proc->selectedMaterial()->data && proc->selectedMaterial()->data->size() == 240000,
           "el seno de fábrica se genera a 48 kHz (5 s)");

    std::cout << "dron\n";
    CHECK (run (*proc, 20).peak < 1e-6, "silencio al empezar");
    proc->setParam (pid::play, 1);
    auto drone = run (*proc, blocksFor (2));
    CHECK (drone.finite && drone.rms > 0.02 && drone.peak < 1.5, "Reproducir: el dron suena (rms " << drone.rms << ", pico " << drone.peak << ")");
    for (int i = 1; i < 6; ++i)
    {
        proc->selectMaterial (i);
        auto s = run (*proc, blocksFor (1));
        CHECK (s.finite && s.rms > 0.01, "material de fábrica " << i << " (" << proc->selectedMaterial()->name << ") suena, rms " << s.rms);
    }
    proc->setParam (pid::posMode, 2);   // deriva
    proc->setParam (pid::shimmerAmount, 60);
    proc->setParam (pid::filterType, 1);
    proc->setParam (pid::posRateMode, 1);
    auto drift = run (*proc, blocksFor (1.5));
    CHECK (drift.finite && drift.rms > 0.005, "deriva + shimmer + paso alto (rms " << drift.rms << ")");
    proc->setParam (pid::grainDensity, 60);
    proc->setParam (pid::grainSize, 500);
    auto dense = run (*proc, blocksFor (1.5));
    CHECK (dense.finite && dense.peak < 8, "máxima densidad y tamaño sin problemas (pico " << dense.peak << ")");
    proc->setParam (pid::play, 0);
    run (*proc, blocksFor (5));
    CHECK (run (*proc, 20).peak < 1e-4, "al detener se apaga (granos + cola de reverb)");

    std::cout << "notas\n";
    for (auto id : { pid::posMode, pid::shimmerAmount, pid::filterType, pid::posRateMode, pid::grainDensity, pid::grainSize })
        if (auto* prm = proc->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->getDefaultValue());
    proc->selectMaterial (1);
    auto note = run (*proc, blocksFor (1), { on (60) });
    CHECK (note.rms > 0.01 && proc->isNoteLit (60), "una nota MIDI suena y se ilumina (rms " << note.rms << ")");
    run (*proc, 2, { MidiMessage::pitchWheel (1, 16383), MidiMessage::controllerEvent (1, 1, 127) });
    CHECK (run (*proc, 20).finite, "pitch bend y rueda de modulación");
    run (*proc, 2, { off (60) });
    CHECK (! proc->isNoteLit (60), "al soltar se apaga la tecla");
    run (*proc, blocksFor (6));
    CHECK (run (*proc, 20).peak < 1e-4, "tras el release vuelve el silencio");
    run (*proc, 2, { MidiMessage::controllerEvent (1, 64, 127), on (64, 80), off (64) });
    CHECK (run (*proc, blocksFor (1)).rms > 0.005, "el pedal de sustain mantiene la nota");
    run (*proc, 2, { MidiMessage::controllerEvent (1, 64, 0) });
    run (*proc, blocksFor (6));
    CHECK (run (*proc, 20).peak < 1e-4, "al soltar el pedal se apaga");
    {
        // bloques irregulares (como algunos DAW)
        auto s1 = run (*proc, 300, { on (55) }, 37);
        auto s2 = run (*proc, 40, { off (55) }, 1024);
        CHECK (s1.finite && s2.finite && s1.rms > 0.005, "bloques de 37 y 1024 muestras");
        run (*proc, blocksFor (6));
    }

    std::cout << "audio propio\n";
    const File tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("ra_smoke_material.wav");
    {
        tmp.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os = std::make_unique<FileOutputStream> (tmp);
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (44100).withNumChannels (2).withBitsPerSample (16));
        AudioBuffer<float> b (2, 44100 * 2);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = 0.5f * (float) std::sin (2 * MathConstants<double>::pi * 220 * i / 44100.0);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    const auto before = proc->library().size();
    const String err = proc->addUserSamples ({ tmp });
    CHECK (err.isEmpty() && proc->library().size() == before + 1, "se añade un WAV a la librería");
    const auto* um = proc->selectedMaterial();
    CHECK (um != nullptr && ! um->factory && um->data && std::abs ((double) um->data->size() - 96000.0) < 4,
           "el WAV a 44,1 kHz se convierte a 48 kHz (" << (um && um->data ? (int) um->data->size() : 0) << " muestras)");
    CHECK (um != nullptr && um->file.existsAsFile() && um->file.getParentDirectory() == RadianceProcessor::libraryFolder(), "copiado a la carpeta de la librería");
    proc->setSampleGainDb (-6);
    proc->setParam (pid::play, 1);
    CHECK (run (*proc, blocksFor (1)).rms > 0.005, "el audio propio suena");
    proc->setParam (pid::play, 0);
    run (*proc, blocksFor (5));

    std::cout << "patches\n";
    proc->setParam (pid::grainSize, 333);
    proc->setParam (pid::posMode, 2);
    proc->setParam (pid::posSyncDiv, 9);
    const String json = proc->patchToJson ("Prueba");
    auto parsed = JSON::parse (json);
    CHECK (parsed.getProperty ("app", "").toString() == "Radiance-AMB" && parsed["values"].getProperty ("grainSize", "").toString() == "333"
               && parsed["values"].getProperty ("posMode", "").toString() == "drift" && parsed["values"].getProperty ("posSyncDiv", "").toString() == "1/4T"
               && parsed["presetAudio"].isObject(),
           "el patch tiene el formato de la web (con el audio dentro)");
    proc->setParam (pid::grainSize, 120);
    proc->setParam (pid::posMode, 1);
    proc->selectMaterial (0);
    String name;
    const auto libBefore = proc->library().size();
    proc->applyPatchJson (json, name);
    CHECK (name == "Prueba" && std::abs (proc->param (pid::grainSize) - 333) < 0.5f && (int) proc->param (pid::posMode) == 2
               && proc->library().size() == libBefore + 1 && ! proc->selectedMaterial()->factory,
           "cargar el patch recupera ajustes y da de alta su audio");
    {
        // un patch tal como lo descarga la web (material de fábrica)
        const String web = R"({"app":"Radiance-AMB","formatVersion":1,"name":"Web","createdAt":1,"values":{"posKnob":"0.612","posRate":"0.05","posTempo":"120","posDepth":"35","grainSize":"200","grainDensity":"25","grainAttack":"3","grainRelease":"25","grainJitter":"8","pitchTranspose":"-5","pitchFine":"0","pitchSpray":"12","filterCutoff":"55.5","filterQ":"0.8","filterSpray":"20","stereoSpread":"70","reverbSpace":"12","moveRate":"0.08","moveDepth":"25","dryWet":"60","reverbPreDelay":"30","shimmerTone":"7","shimmerAmount":"40","noteAttack":"400","noteDecay":"300","noteSustain":"70","noteRelease":"1200","masterVol":"70","pitchBendRange":"2","posMode":"lfo","posRateMode":"sync","posSyncDiv":"1/8D","grainShape":"perc","filterType":"bandpass"},"presetId":"f_bell","presetName":"Campana FM","presetSource":"factory","presetAudio":null})";
        String n2;
        const String w = proc->applyPatchJson (web, n2);
        CHECK (w.isEmpty() && std::abs (proc->param (pid::posKnob) - 0.612f) < 1e-3f && std::abs (proc->param (pid::filterCutoff) - 55.5f) < 1e-3f
                   && (int) proc->param (pid::posSyncDiv) == 7 && (int) proc->param (pid::grainShape) == 1 && (int) proc->param (pid::filterType) == 2
                   && proc->selectedMaterial()->id == "f_bell",
               "patch de la web: valores, menús y material");
    }

    std::cout << "estado\n";
    proc->addUserSamples ({ tmp });
    proc->setSampleGainDb (3.5);
    proc->octaveShift = 1;
    proc->setParam (pid::reverbSpace, 17);
    MemoryBlock state;
    proc->getStateInformation (state);
    const File userFile = proc->selectedMaterial()->file;
    userFile.deleteFile();   // en otro ordenador no existiría el archivo
    auto proc2 = std::make_unique<RadianceProcessor>();
    proc2->setStateInformation (state.getData(), (int) state.getSize());
    proc2->setRateAndBufferSizeDetails (44100, 256);
    proc2->prepareToPlay (44100, 256);
    const auto* m2 = proc2->selectedMaterial();
    CHECK (m2 != nullptr && ! m2->factory && m2->data && std::abs ((double) m2->data->size() - 88200.0) < 4 && std::abs (m2->gainDb - 3.5) < 1e-6
               && proc2->octaveShift == 1 && std::abs (proc2->param (pid::reverbSpace) - 17) < 0.5f,
           "estado restaurado en otra instancia, con el audio dentro del proyecto (a 44,1 kHz)");
    {
        AudioBuffer<float> b2 (2, 256);
        double e2 = 0;
        proc2->setParam (pid::play, 1);
        for (int k = 0; k < 200; ++k) { b2.clear(); MidiBuffer m; proc2->processBlock (b2, m); e2 += b2.getRMSLevel (0, 0, 256); }
        CHECK (e2 > 0.5, "suena a 44,1 kHz");
    }

    std::cout << "grabación\n";
    {
        const File wavOut = out.getChildFile ("grabacion.wav");
        proc->setParam (pid::play, 1);
        CHECK (proc->startRecording (wavOut), "empieza a grabar");
        for (int k = 0; k < 20; ++k) { run (*proc, 10); pump (10); }
        proc->stopRecording();
        proc->setParam (pid::play, 0);
        std::unique_ptr<AudioFormatReader> r (WavAudioFormat().createReaderFor (new FileInputStream (wavOut), true));
        CHECK (r != nullptr && r->numChannels == 2 && r->bitsPerSample == 16 && r->lengthInSamples == 200 * 512, "WAV estéreo de 16 bits con todo lo grabado");
    }

    std::cout << "interfaz\n";
    {
        proc->selectMaterial (2);
        proc->setParam (pid::play, 1);
        proc->setParam (pid::posMode, 1);
        proc->setParam (pid::posRateMode, 1);
        run (*proc, 2, { on (60), on (64) });
        std::unique_ptr<AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (1190, 900);
        for (int k = 0; k < 20; ++k) { run (*proc, 3); pump (35); }
        snapshot (*ed, out.getChildFile ("ui_editor.png"));
        if (auto* vp = dynamic_cast<Viewport*> (ed->getChildComponent (0)))
            if (auto* panel = vp->getViewedComponent())
                snapshot (*panel, out.getChildFile ("ui_full.png"));
        CHECK (out.getChildFile ("ui_full.png").getSize() > 10000, "captura de la interfaz");
        proc->setParam (pid::posMode, 2);
        proc->setParam (pid::play, 0);
        for (int k = 0; k < 4; ++k) { run (*proc, 3); pump (35); }
        if (auto* vp = dynamic_cast<Viewport*> (ed->getChildComponent (0)))
            if (auto* panel = vp->getViewedComponent())
                snapshot (*panel, out.getChildFile ("ui_full_deriva.png"));
        run (*proc, 2, { off (60), off (64) });
    }

    // limpieza de lo que la prueba dejó en la librería
    for (auto& m : proc->library()) if (! m.factory && m.file.getFileName().startsWith ("ra_smoke")) m.file.deleteFile();
    for (auto& f : RadianceProcessor::libraryFolder().findChildFiles (File::findFiles, false, "Prueba*")) f.deleteFile();
    tmp.deleteFile();

    std::cout << (failures == 0 ? "TODO OK\n" : String (failures) + " FALLOS\n");
    return failures == 0 ? 0 : 1;
}
