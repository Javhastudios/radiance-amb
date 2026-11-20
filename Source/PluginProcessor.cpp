#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
String u8 (const char* t) { return String::fromUTF8 (t); }

struct FloatDef { const char* id; const char* name; float min, max, step, def; const char* label; };

// Rangos y valores por defecto copiados de los <input type="range"> del prototipo,
// en el mismo orden que PATCH_RANGE_IDS de la web.
const FloatDef floatDefs[] = {
    { pid::posKnob, "Posición", 0, 1, 0.001f, 0.3f, "" },
    { pid::posRate, "Velocidad de posición", 0.01f, 2, 0.01f, 0.05f, "Hz" },
    { pid::posTempo, "Tempo", 40, 240, 1, 120, "BPM" },
    { pid::posDepth, "Profundidad de posición", 0, 100, 1, 35, "%" },
    { pid::grainSize, "Tamaño de grano", 10, 500, 1, 120, "ms" },
    { pid::grainDensity, "Densidad", 2, 60, 1, 18, "/s" },
    { pid::grainAttack, "Ataque de grano", 0, 400, 1, 3, "ms" },
    { pid::grainRelease, "Release de grano", 0, 400, 1, 25, "ms" },
    { pid::grainJitter, "Dispersión de posición", 0, 100, 1, 8, "%" },
    { pid::pitchTranspose, "Transposición", -24, 24, 1, 0, "st" },
    { pid::pitchFine, "Fino", -50, 50, 1, 0, "ct" },
    { pid::pitchSpray, "Deriva de afinación", 0, 100, 1, 12, "ct" },
    { pid::filterCutoff, "Corte", 0, 100, 0.1f, 72, "" },
    { pid::filterQ, "Resonancia", 0.1f, 12, 0.1f, 0.8f, "" },
    { pid::filterSpray, "Textura", 0, 100, 1, 20, "%" },
    { pid::stereoSpread, "Dispersión estéreo", 0, 100, 1, 70, "%" },
    { pid::reverbSpace, "Space", 0, 40, 1, 0, "ms" },
    { pid::moveRate, "Velocidad de movimiento", 0.01f, 1, 0.01f, 0.08f, "Hz" },
    { pid::moveDepth, "Profundidad de movimiento", 0, 100, 1, 25, "%" },
    { pid::dryWet, "Dry / Wet", 0, 100, 1, 35, "%" },
    { pid::reverbPreDelay, "Pre-delay", 0, 120, 1, 0, "ms" },
    { pid::shimmerTone, "Shimmer: tono", -12, 12, 0.1f, 12, "st" },
    { pid::shimmerAmount, "Shimmer: cantidad", 0, 100, 1, 0, "%" },
    { pid::noteAttack, "Ataque de nota", 0, 4000, 10, 400, "ms" },
    { pid::noteDecay, "Decay de nota", 0, 4000, 10, 300, "ms" },
    { pid::noteSustain, "Sustain de nota", 0, 100, 1, 70, "%" },
    { pid::noteRelease, "Release de nota", 0, 8000, 10, 1200, "ms" },
    { pid::masterVol, "Volumen", 0, 100, 1, 70, "%" },
    { pid::pitchBendRange, "Rango de pitch bend", 1, 24, 1, 2, "st" },
};
constexpr int kNumFloats = (int) (sizeof (floatDefs) / sizeof (floatDefs[0]));

// Menús: valor de la web y texto en pantalla
struct ChoiceDef { const char* id; const char* name; std::vector<const char*> values, labels; int def; };
const ChoiceDef& choiceDef (int i)
{
    static const ChoiceDef defs[] = {
        { pid::posMode, "Modo de posición", { "static", "lfo", "drift" }, { "Fijo", "LFO", "Deriva aleatoria" }, 1 },
        { pid::posRateMode, "Velocidad: modo", { "hz", "sync" }, { "Hz", "Sync (nota)" }, 0 },
        { pid::posSyncDiv, "Figura", { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4D", "1/8D", "1/16D", "1/4T", "1/8T", "1/16T" },
          { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 con puntillo", "1/8 con puntillo", "1/16 con puntillo", "1/4 tresillo", "1/8 tresillo", "1/16 tresillo" }, 2 },
        { pid::grainShape, "Forma", { "sym", "perc", "rev" }, { "Simétrico", "Percusivo", "Swell inverso" }, 0 },
        { pid::filterType, "Tipo de filtro", { "lowpass", "highpass", "bandpass" }, { "Paso bajo", "Paso alto", "Paso banda" }, 0 },
    };
    return defs[i];
}
constexpr int kNumChoices = 5;

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    for (auto& d : floatDefs)
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { d.id, 1 }, u8 (d.name), NormalisableRange<float> (d.min, d.max, d.step), d.def,
                                                            AudioParameterFloatAttributes().withLabel (d.label)));
    for (int i = 0; i < kNumChoices; ++i)
    {
        const auto& c = choiceDef (i);
        StringArray labels;
        for (auto* l : c.labels) labels.add (u8 (l));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { c.id, 1 }, u8 (c.name), labels, c.def));
    }
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { pid::play, 1 }, u8 ("Reproducir (dron)"), false));
    return { p.begin(), p.end() };
}

// PCM de 16 bits en base64, como floatToPCM16Base64 / base64ToFloat de la web
String floatToPcm16Base64 (const std::vector<float>& d)
{
    MemoryBlock mb (d.size() * 2);
    auto* bytes = static_cast<uint8*> (mb.getData());
    for (size_t i = 0; i < d.size(); ++i)
    {
        double s = jlimit (-1.0, 1.0, (double) d[i]);
        s = s < 0 ? s * 0x8000 : s * 0x7FFF;
        const auto v = (int16) (int) s;   // DataView.setInt16 trunca
        bytes[i * 2] = (uint8) (v & 0xff);
        bytes[i * 2 + 1] = (uint8) ((v >> 8) & 0xff);
    }
    return Base64::toBase64 (mb.getData(), mb.getSize());
}

std::vector<float> base64ToFloat (const String& b64)
{
    MemoryOutputStream out;
    if (! Base64::convertFromBase64 (out, b64)) return {};
    const auto* bytes = static_cast<const uint8*> (out.getData());
    const size_t n = out.getDataSize() / 2;
    std::vector<float> d (n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto s = (int16) (bytes[i * 2] | (bytes[i * 2 + 1] << 8));
        d[i] = s < 0 ? s / 32768.0f : s / 32767.0f;
    }
    return d;
}

uint32 factorySeed (int i) { return 0x5eed0000u + (uint32) i * 7919u; }
} // namespace

//==============================================================================
RadianceProcessor::RadianceProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();
    for (auto& d : floatDefs) watched.push_back (apvts.getRawParameterValue (d.id));
    for (int i = 0; i < kNumChoices; ++i) watched.push_back (apvts.getRawParameterValue (choiceDef (i).id));
    watched.push_back (apvts.getRawParameterValue (pid::play));
    lastValues.assign (watched.size(), std::numeric_limits<float>::quiet_NaN());

    for (int i = 0; i < 6; ++i)
    {
        Material m;
        m.id = ra::kFactory[i].id; m.name = u8 (ra::kFactory[i].name); m.factory = true; m.factoryIndex = i;
        materials.push_back (std::move (m));
    }
    loadUserMaterialsFromFolder();
    selected = 0;
    writerThread.startThread();
    startTimerHz (10);
}

RadianceProcessor::~RadianceProcessor()
{
    stopTimer();
    stopRecording();
    writerThread.stopThread (2000);
}

File RadianceProcessor::userFolder()
{
    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Radiance-AMB");
}

float RadianceProcessor::param (const char* id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void RadianceProcessor::setParam (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

bool RadianceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

bool RadianceProcessor::isNoteLit (int midi) const
{
    if (midi < 0 || midi > 127) return false;
    return (lit[midi >> 6].load() >> (midi & 63)) & 1u;
}

void RadianceProcessor::setLit (int midi, bool on)
{
    if (midi < 0 || midi > 127) return;
    const uint64_t bit = uint64_t (1) << (midi & 63);
    if (on) lit[midi >> 6].fetch_or (bit); else lit[midi >> 6].fetch_and (~bit);
}

int RadianceProcessor::popGrainMarks (float* dest, int max)
{
    const auto scope = grainFifo.read (jmin (max, grainFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = grainBuf[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = grainBuf[(size_t) (scope.startIndex2 + i)];
    return n;
}

//==============================================================================
// Parámetros → motor (sin reservar memoria: se usa el índice)
void RadianceProcessor::applyParam (int i, float v)
{
    auto& p = engine.p;
    if (i < kNumFloats)
    {
        const char* id = floatDefs[i].id;
        auto is = [id] (const char* x) { return std::strcmp (id, x) == 0; };
        if (is (pid::posKnob)) p.position = v;
        else if (is (pid::posRate)) p.posRate = v;
        else if (is (pid::posTempo)) p.posTempo = v;
        else if (is (pid::posDepth)) p.posDepth = v / 100.0;
        else if (is (pid::grainSize)) p.grainSizeMs = v;
        else if (is (pid::grainDensity)) p.density = v;
        else if (is (pid::grainAttack)) p.grainAttackMs = v;
        else if (is (pid::grainRelease)) p.grainReleaseMs = v;
        else if (is (pid::grainJitter)) p.jitter = v / 100.0;
        else if (is (pid::pitchTranspose)) p.transpose = v;
        else if (is (pid::pitchFine)) p.fine = v;
        else if (is (pid::pitchSpray)) p.pitchSpray = v;
        else if (is (pid::filterCutoff)) p.filterCutoffPct = v;
        else if (is (pid::filterQ)) p.filterQ = v;
        else if (is (pid::filterSpray)) p.filterSpray = v / 100.0;
        else if (is (pid::stereoSpread)) p.stereoSpread = v / 100.0;
        else if (is (pid::reverbSpace)) engine.setSpace (v);
        else if (is (pid::moveRate)) p.moveRate = v;
        else if (is (pid::moveDepth)) p.moveDepth = v / 100.0;
        else if (is (pid::dryWet)) engine.setDryWet (v);
        else if (is (pid::reverbPreDelay)) engine.setPreDelay (v);
        else if (is (pid::shimmerTone)) p.shimmerSemitones = v;
        else if (is (pid::shimmerAmount)) p.shimmerAmount = v / 100.0;
        else if (is (pid::noteAttack)) p.noteAttackMs = v;
        else if (is (pid::noteDecay)) p.noteDecayMs = v;
        else if (is (pid::noteSustain)) p.noteSustain = v / 100.0;
        else if (is (pid::noteRelease)) p.noteReleaseMs = v;
        else if (is (pid::masterVol)) engine.setMaster (v);
        else if (is (pid::pitchBendRange)) p.pitchBendRange = v;
        return;
    }
    const int c = i - kNumFloats;
    const int k = (int) std::lround (v);
    switch (c)
    {
        case 0: p.posMode = (ra::PosMode) jlimit (0, 2, k); break;
        case 1: p.posSync = k == 1; break;
        case 2: p.posSyncDiv = jlimit (0, 11, k); break;
        case 3: p.shape = (ra::Shape) jlimit (0, 2, k); break;
        case 4: p.filterType = (ra::FilterType) jlimit (0, 2, k); break;
        case 5: engine.setDrone (v > 0.5f); break;
        default: break;
    }
}

void RadianceProcessor::applyAllParams()
{
    for (size_t i = 0; i < watched.size(); ++i)
    {
        lastValues[i] = watched[i]->load();
        applyParam ((int) i, lastValues[i]);
    }
}

void RadianceProcessor::applyChangedParams()
{
    for (size_t i = 0; i < watched.size(); ++i)
    {
        const float v = watched[i]->load();
        if (v == lastValues[i]) continue;
        lastValues[i] = v;
        applyParam ((int) i, v);
    }
}

//==============================================================================
// Librería
void RadianceProcessor::buildData (Material& m) const
{
    if (m.factory)
    {
        wa::Lcg rng; rng.s = factorySeed (m.factoryIndex);
        m.data = std::make_shared<std::vector<float>> (ra::generateFactory (m.factoryIndex, sampleRate, rng));
        return;
    }
    if (m.originalRate <= 0 || std::abs (m.originalRate - sampleRate) < 0.5)
    {
        m.data = std::make_shared<std::vector<float>> (m.original);
        return;
    }
    // conversión de frecuencia (como decodeAudioData, que entrega el audio a la frecuencia del contexto)
    const double ratio = m.originalRate / sampleRate;
    const int outLen = (int) std::ceil ((double) m.original.size() / ratio);
    WindowedSincInterpolator interp;
    const int skip = (int) std::lround (interp.getBaseLatency() / ratio);
    std::vector<float> padded (m.original);
    padded.resize (padded.size() + 256, 0.0f);
    std::vector<float> out ((size_t) (outLen + skip + 4), 0.0f);
    interp.process (ratio, padded.data(), out.data(), (int) out.size(), (int) padded.size(), 0);
    auto d = std::make_shared<std::vector<float>> (out.begin() + skip, out.begin() + skip + outLen);
    m.data = d;
}

void RadianceProcessor::pushSourceToEngine()
{
    auto* m = selectedMaterial();
    if (m == nullptr || ! m->data || m->data->empty()) return;
    auto newData = m->data;
    const double gain = std::pow (10.0, m->gainDb / 20.0);
    std::shared_ptr<std::vector<float>> old;
    {
        const SpinLock::ScopedLockType sl (lock);
        old = engineData;
        engineData = newData;
        engine.setSource (engineData->data(), (int) engineData->size(), gain);
    }
    if (old && old != newData) retired.push_back ({ old, Time::getMillisecondCounter() });
}

void RadianceProcessor::selectMaterial (int index)
{
    if (index < 0 || index >= (int) materials.size()) return;
    selected = index;
    auto& m = materials[(size_t) index];
    if (! m.data && prepared) buildData (m);
    if (prepared) pushSourceToEngine();
    ++libVersion;
}

void RadianceProcessor::setSampleGainDb (double db)
{
    auto* m = selected >= 0 && selected < (int) materials.size() ? &materials[(size_t) selected] : nullptr;
    if (m == nullptr) return;
    m->gainDb = db;
    {
        const SpinLock::ScopedLockType sl (lock);
        engine.setPresetGain (std::pow (10.0, db / 20.0));
    }
    if (! m->factory) saveGains();
    ++libVersion;
}

var RadianceProcessor::gainsJson() const
{
    auto* o = new DynamicObject();
    for (auto& m : materials)
        if (! m.factory && m.file != File()) o->setProperty (m.file.getFileName(), m.gainDb);
    return var (o);
}

void RadianceProcessor::saveGains() const
{
    libraryFolder().createDirectory();
    libraryFolder().getChildFile ("ganancias.json").replaceWithText (JSON::toString (gainsJson()));
}

void RadianceProcessor::loadUserMaterialsFromFolder()
{
    auto folder = libraryFolder();
    if (! folder.isDirectory()) return;
    const var gains = JSON::parse (folder.getChildFile ("ganancias.json"));
    Array<File> files = folder.findChildFiles (File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    files.sort();
    for (auto& f : files)
    {
        std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0 || r->lengthInSamples > (int64) 48000 * 60 * 10) continue;
        const int n = (int) r->lengthInSamples;
        AudioBuffer<float> b ((int) jmax (1u, r->numChannels), n);
        r->read (&b, 0, n, 0, true, true);
        Material m;
        m.factory = false; m.file = f; m.id = "user:" + f.getFileName(); m.name = f.getFileNameWithoutExtension();
        m.originalRate = r->sampleRate;
        m.original.resize ((size_t) n);
        if (b.getNumChannels() > 1)   // como la web: media de los dos primeros canales
            for (int i = 0; i < n; ++i) m.original[(size_t) i] = (b.getSample (0, i) + b.getSample (1, i)) * 0.5f;
        else
            FloatVectorOperations::copy (m.original.data(), b.getReadPointer (0), n);
        m.gainDb = (double) gains.getProperty (f.getFileName(), 0.0);
        materials.push_back (std::move (m));
    }
}

void RadianceProcessor::reloadLibraryFolder()
{
    const String selId = selectedMaterial() ? selectedMaterial()->id : String();
    std::vector<Material> keep;
    for (auto& m : materials) if (m.factory || m.file == File()) keep.push_back (std::move (m));
    materials = std::move (keep);
    loadUserMaterialsFromFolder();
    selected = 0;
    for (int i = 0; i < (int) materials.size(); ++i) if (materials[(size_t) i].id == selId) selected = i;
    selectMaterial (selected);
}

String RadianceProcessor::addUserSamples (const Array<File>& files)
{
    StringArray errors;
    int lastAdded = -1;
    libraryFolder().createDirectory();
    for (auto& f : files)
    {
        std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0) { errors.add (u8 ("No se pudo leer \"") + f.getFileName() + u8 ("\" como audio.")); continue; }
        // se copia a la carpeta de la librería (la comparten la app y el plugin)
        File dest = libraryFolder().getChildFile (f.getFileName());
        if (dest != f)
        {
            if (dest.existsAsFile()) dest = dest.getNonexistentSibling (false);
            if (! f.copyFileTo (dest)) { errors.add (u8 ("No se pudo copiar \"") + f.getFileName() + u8 ("\" a la librería.")); continue; }
        }
        r.reset();
        const auto before = materials.size();
        // carga solo este archivo
        std::unique_ptr<AudioFormatReader> rr (formats.createReaderFor (dest));
        if (rr == nullptr) continue;
        const int n = (int) rr->lengthInSamples;
        AudioBuffer<float> b ((int) jmax (1u, rr->numChannels), n);
        rr->read (&b, 0, n, 0, true, true);
        Material m;
        m.factory = false; m.file = dest; m.id = "user:" + dest.getFileName(); m.name = dest.getFileNameWithoutExtension();
        m.originalRate = rr->sampleRate;
        m.original.resize ((size_t) n);
        if (b.getNumChannels() > 1)
            for (int i = 0; i < n; ++i) m.original[(size_t) i] = (b.getSample (0, i) + b.getSample (1, i)) * 0.5f;
        else
            FloatVectorOperations::copy (m.original.data(), b.getReadPointer (0), n);
        materials.push_back (std::move (m));
        lastAdded = (int) before;
    }
    if (lastAdded >= 0) selectMaterial (lastAdded);
    ++libVersion;
    return errors.joinIntoString ("\n");
}

void RadianceProcessor::deleteUserMaterial (int index)
{
    if (index < 0 || index >= (int) materials.size() || materials[(size_t) index].factory) return;
    auto& m = materials[(size_t) index];
    if (m.file.existsAsFile()) m.file.moveToTrash();
    const bool wasSelected = index == selected;
    if (wasSelected)
    {
        // lo que suena se queda en "retirados" hasta que acaben sus granos
        selected = 0;
    }
    materials.erase (materials.begin() + index);
    if (selected > index) --selected;
    saveGains();
    if (wasSelected) selectMaterial (0);
    ++libVersion;
}

//==============================================================================
void RadianceProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sr - sampleRate) > 0.5 || ! prepared;
    sampleRate = sr;
    maxBlock = jmax (samplesPerBlock, 512);
    convolver.prepare (sr, maxBlock);
    if (rateChanged)
        for (auto& m : materials) m.data.reset();
    if (auto* m = selected >= 0 && selected < (int) materials.size() ? &materials[(size_t) selected] : nullptr)
        if (! m->data) buildData (*m);
    {
        const SpinLock::ScopedLockType sl (lock);
        engine.webCompat = false;
        engine.prepare (sr, &convolver, (uint32) Time::getMillisecondCounter() | 1u);
        engineData.reset();
        applyAllParams();
        lit[0] = 0; lit[1] = 0;
        prepared = true;
    }
    retired.clear();
    pushSourceToEngine();
    {
        const SpinLock::ScopedLockType sl (lock);
        engine.setDrone (param (pid::play) > 0.5f);   // ya hay material
    }
}

void RadianceProcessor::timerCallback()
{
    // libera el material anterior cuando ya no puede quedar ningún grano leyéndolo
    const auto now = Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& r) { return now - r.second > 3000; }), retired.end());
}

//==============================================================================
void RadianceProcessor::handleMidi (const MidiMessage& msg)
{
    if (msg.isNoteOn())
    {
        engine.midiNoteOn (msg.getNoteNumber(), msg.getVelocity());
        setLit (msg.getNoteNumber(), true);
    }
    else if (msg.isNoteOff())
    {
        engine.midiNoteOff (msg.getNoteNumber());
        setLit (msg.getNoteNumber(), false);
    }
    else if (msg.isPitchWheel()) engine.midiPitchBend (msg.getPitchWheelValue());
    else if (msg.isController() && msg.getControllerNumber() == 1) engine.midiModWheel (msg.getControllerValue());
    else if (msg.isSustainPedalOn()) engine.midiSustain (true);
    else if (msg.isSustainPedalOff()) engine.midiSustain (false);
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        engine.allNotesOff();
        lit[0] = 0; lit[1] = 0;
    }
}

void RadianceProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    if (buffer.getNumChannels() < 2 || ! prepared) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    {
        const SpinLock::ScopedLockType sl (lock);
        applyChangedParams();
        int pos = 0;
        for (const auto meta : midi)
        {
            const int s = jlimit (0, n, meta.samplePosition);
            if (s > pos) { engine.process (L + pos, R + pos, s - pos); pos = s; }
            handleMidi (meta.getMessage());
        }
        if (pos < n) engine.process (L + pos, R + pos, n - pos);

        dispPos = (float) engine.displayPosition();
        // granos nuevos para los puntos del dibujo
        while (lastGrainLog != engine.grainLogPos)
        {
            const float v = (float) engine.grainLog[(size_t) lastGrainLog].pos;
            lastGrainLog = (lastGrainLog + 1) % ra::RadianceEngine::kLog;
            const auto w = grainFifo.write (1);
            if (w.blockSize1 > 0) grainBuf[(size_t) w.startIndex1] = v;
        }
    }

    if (recording.load())
    {
        const ScopedTryLock tl (recLock);
        if (tl.isLocked() && recWriter != nullptr)
        {
            const float* chans[2] = { L, R };
            recWriter->write (chans, n);
            recordedSamples += n;
        }
    }
}

//==============================================================================
bool RadianceProcessor::startRecording (const File& f)
{
    stopRecording();
    f.deleteFile();
    std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (f);
    if (! static_cast<FileOutputStream*> (stream.get())->openedOk()) return false;
    WavAudioFormat wav;
    auto w = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (16));
    if (w == nullptr) return false;
    {
        const ScopedLock sl (recLock);
        recWriter = std::make_unique<AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 1 << 17);
        recordedSamples = 0;
    }
    recording = true;
    return true;
}

void RadianceProcessor::stopRecording()
{
    recording = false;
    std::unique_ptr<AudioFormatWriter::ThreadedWriter> old;
    {
        const ScopedLock sl (recLock);
        old = std::move (recWriter);
    }
    old.reset();   // termina de escribir y cierra el archivo
}

//==============================================================================
// Patches: el mismo JSON que "Descargar como .json" de la web
String RadianceProcessor::patchToJson (const String& name) const
{
    auto* values = new DynamicObject();
    for (auto& d : floatDefs)
    {
        const float v = param (d.id);
        values->setProperty (d.id, String (v, d.step < 0.01f ? 3 : (d.step < 1 ? (d.step < 0.1f ? 2 : 1) : 0)));
    }
    for (int i = 0; i < kNumChoices; ++i)
    {
        const auto& c = choiceDef (i);
        values->setProperty (c.id, c.values[(size_t) jlimit (0, (int) c.values.size() - 1, (int) std::lround (param (c.id)))]);
    }
    auto* o = new DynamicObject();
    o->setProperty ("app", "Radiance-AMB");
    o->setProperty ("formatVersion", 1);
    o->setProperty ("name", name);
    o->setProperty ("createdAt", Time::currentTimeMillis());
    o->setProperty ("values", var (values));
    const Material* m = selectedMaterial();
    o->setProperty ("presetId", m ? var (m->factory ? m->id : String ("deriva_preset:") + m->id.fromFirstOccurrenceOf (":", false, false)) : var());
    o->setProperty ("presetName", m ? var (m->name) : var());
    o->setProperty ("presetSource", m ? var (m->factory ? "factory" : "user") : var());
    if (m != nullptr && ! m->factory && m->data)
    {
        auto* a = new DynamicObject();
        a->setProperty ("pcm", floatToPcm16Base64 (*m->data));
        a->setProperty ("gainDb", m->gainDb);
        o->setProperty ("presetAudio", var (a));
    }
    else o->setProperty ("presetAudio", var());
    return JSON::toString (var (o), true);
}

String RadianceProcessor::applyPatchJson (const String& json, String& nameOut)
{
    var payload;
    if (JSON::parse (json, payload).failed() || ! payload.isObject()) return u8 ("el archivo no es un JSON válido");
    const var values = payload.getProperty ("values", var());
    if (! values.isObject()) return u8 ("el archivo no tiene el formato de un patch de Radiance-AMB");
    nameOut = payload.getProperty ("name", "").toString();

    // primero los menús, luego los sliders (como applyPatchSnapshot)
    for (int i = 0; i < kNumChoices; ++i)
    {
        const auto& c = choiceDef (i);
        if (! values.hasProperty (c.id)) continue;
        const String v = values.getProperty (c.id, "").toString();
        for (size_t k = 0; k < c.values.size(); ++k)
            if (v == c.values[k]) setParam (c.id, (float) k);
    }
    for (auto& d : floatDefs)
        if (values.hasProperty (d.id))
            setParam (d.id, (float) values.getProperty (d.id, d.def).toString().getDoubleValue());

    String warning;
    const var audio = payload.getProperty ("presetAudio", var());
    const String presetId = payload.getProperty ("presetId", "").toString();
    if (audio.isObject() && audio.getProperty ("pcm", "").toString().isNotEmpty())
    {
        // trae su propio audio: se da de alta como material tuyo (archivo .wav en la librería)
        Material m;
        m.factory = false;
        m.name = payload.getProperty ("presetName", "Importado").toString();
        if (m.name.isEmpty()) m.name = "Importado";
        m.original = base64ToFloat (audio.getProperty ("pcm", "").toString());
        m.originalRate = sampleRate;   // la web no guarda la frecuencia: se supone la del motor
        m.gainDb = (double) audio.getProperty ("gainDb", 0.0);
        libraryFolder().createDirectory();
        File dest = libraryFolder().getChildFile (File::createLegalFileName (m.name) + ".wav").getNonexistentSibling (false);
        {
            WavAudioFormat wav;
            std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (dest);
            if (static_cast<FileOutputStream*> (stream.get())->openedOk())
                if (auto w = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (1).withBitsPerSample (24)))
                {
                    const float* ch[1] = { m.original.data() };
                    w->writeFromFloatArrays (ch, 1, (int) m.original.size());
                }
        }
        if (dest.existsAsFile()) { m.file = dest; m.id = "user:" + dest.getFileName(); m.name = dest.getFileNameWithoutExtension(); }
        else m.id = "embedded";
        materials.push_back (std::move (m));
        saveGains();
        selectMaterial ((int) materials.size() - 1);
    }
    else if (presetId.isNotEmpty())
    {
        int found = -1;
        for (int i = 0; i < (int) materials.size(); ++i)
            if (materials[(size_t) i].id == presetId) found = i;
        if (found >= 0) selectMaterial (found);
        else warning = u8 ("Este patch usaba un material de librería que no existe aquí (\"") + presetId + u8 ("\"). El resto de la configuración se ha cargado igualmente.");
    }
    ++libVersion;
    return warning;
}

void RadianceProcessor::randomize()
{
    // las mismas tiradas que el botón Aleatorizar de la web
    auto& r = Random::getSystemRandom();
    auto rnd = [&r] { return (double) r.nextDouble(); };
    auto fixed = [] (double v, int d) { return String (v, d).getDoubleValue(); };
    setParam (pid::posKnob, (float) fixed (rnd() * 0.9 + 0.05, 3));
    setParam (pid::posMode, (float) (int) (rnd() * 3));
    setParam (pid::posRate, (float) fixed (rnd() * 0.4 + 0.02, 2));
    setParam (pid::posDepth, (float) std::floor (rnd() * 60 + 10 + 0.5));
    setParam (pid::grainSize, (float) std::floor (rnd() * 350 + 40 + 0.5));
    setParam (pid::grainDensity, (float) std::floor (rnd() * 35 + 6 + 0.5));
    setParam (pid::grainAttack, (float) std::floor (rnd() * rnd() * 150 + 0.5));
    setParam (pid::grainRelease, (float) std::floor (rnd() * 200 + 10 + 0.5));
    setParam (pid::grainShape, (float) (int) (rnd() * 3));
    setParam (pid::grainJitter, (float) std::floor (rnd() * 40 + 0.5));
    setParam (pid::pitchTranspose, (float) std::floor ((rnd() * 2 - 1) * 12 + 0.5));
    setParam (pid::pitchSpray, (float) std::floor (rnd() * 40 + 0.5));
    setParam (pid::filterCutoff, (float) std::floor (rnd() * 60 + 30 + 0.5));
    setParam (pid::filterQ, (float) fixed (rnd() * 4 + 0.3, 1));
    setParam (pid::filterSpray, (float) std::floor (rnd() * 40 + 0.5));
    setParam (pid::stereoSpread, (float) std::floor (rnd() * 70 + 20 + 0.5));
    setParam (pid::dryWet, (float) std::floor (rnd() * 60 + 15 + 0.5));
    setParam (pid::reverbPreDelay, (float) std::floor (rnd() * rnd() * 80 + 0.5));
    setParam (pid::reverbSpace, (float) std::floor (rnd() * 30 + 0.5));
    setParam (pid::shimmerTone, (float) fixed (rnd() * 24 - 12, 1));
    setParam (pid::shimmerAmount, rnd() < 0.3 ? (float) std::floor (rnd() * 70 + 10 + 0.5) : 0.0f);
    setParam (pid::moveRate, (float) fixed (rnd() * 0.3 + 0.02, 2));
    setParam (pid::moveDepth, (float) std::floor (rnd() * 50 + 5 + 0.5));
}

//==============================================================================
// Estado: parámetros + material elegido (y su audio, si es tuyo, para que el proyecto
// suene igual en otro ordenador)
void RadianceProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("octave", octaveShift, nullptr);
    state.removeChild (state.getChildWithName ("MATERIAL"), nullptr);
    if (auto* m = selectedMaterial())
    {
        ValueTree mt ("MATERIAL");
        mt.setProperty ("id", m->id, nullptr);
        mt.setProperty ("name", m->name, nullptr);
        mt.setProperty ("gainDb", m->gainDb, nullptr);
        if (! m->factory)
        {
            mt.setProperty ("rate", m->originalRate, nullptr);
            MemoryBlock mb (m->original.data(), m->original.size() * sizeof (float));
            mt.setProperty ("audio", mb.toBase64Encoding(), nullptr);
        }
        state.appendChild (mt, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void RadianceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;
    octaveShift = jlimit (-3, 3, (int) state.getProperty ("octave", 0));
    auto mt = state.getChildWithName ("MATERIAL");
    state.removeChild (mt, nullptr);
    apvts.replaceState (state);
    if (! mt.isValid()) return;

    const String id = mt.getProperty ("id").toString();
    int found = -1;
    for (int i = 0; i < (int) materials.size(); ++i) if (materials[(size_t) i].id == id) found = i;
    if (found < 0 && mt.hasProperty ("audio"))
    {
        Material m;
        m.factory = false; m.id = id; m.name = mt.getProperty ("name").toString();
        m.originalRate = (double) mt.getProperty ("rate", 0.0);
        MemoryBlock mb;
        if (mb.fromBase64Encoding (mt.getProperty ("audio").toString()))
        {
            m.original.resize (mb.getSize() / sizeof (float));
            std::memcpy (m.original.data(), mb.getData(), m.original.size() * sizeof (float));
        }
        if (! m.original.empty()) { materials.push_back (std::move (m)); found = (int) materials.size() - 1; }
    }
    if (found < 0) found = 0;
    materials[(size_t) found].gainDb = (double) mt.getProperty ("gainDb", 0.0);
    selectMaterial (found);
}

AudioProcessorEditor* RadianceProcessor::createEditor() { return new RadianceEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RadianceProcessor(); }
