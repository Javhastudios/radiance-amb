#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Factory.h"
#include "dsp/RadianceEngine.h"

// Identificadores de parámetros: los mismos id que los controles del prototipo web
// (así los patches .json de la web se cargan tal cual).
namespace pid
{
inline constexpr const char* posKnob = "posKnob";
inline constexpr const char* posRate = "posRate";
inline constexpr const char* posTempo = "posTempo";
inline constexpr const char* posDepth = "posDepth";
inline constexpr const char* grainSize = "grainSize";
inline constexpr const char* grainDensity = "grainDensity";
inline constexpr const char* grainAttack = "grainAttack";
inline constexpr const char* grainRelease = "grainRelease";
inline constexpr const char* grainJitter = "grainJitter";
inline constexpr const char* pitchTranspose = "pitchTranspose";
inline constexpr const char* pitchFine = "pitchFine";
inline constexpr const char* pitchSpray = "pitchSpray";
inline constexpr const char* filterCutoff = "filterCutoff";
inline constexpr const char* filterQ = "filterQ";
inline constexpr const char* filterSpray = "filterSpray";
inline constexpr const char* stereoSpread = "stereoSpread";
inline constexpr const char* reverbSpace = "reverbSpace";
inline constexpr const char* moveRate = "moveRate";
inline constexpr const char* moveDepth = "moveDepth";
inline constexpr const char* dryWet = "dryWet";
inline constexpr const char* reverbPreDelay = "reverbPreDelay";
inline constexpr const char* shimmerTone = "shimmerTone";
inline constexpr const char* shimmerAmount = "shimmerAmount";
inline constexpr const char* noteAttack = "noteAttack";
inline constexpr const char* noteDecay = "noteDecay";
inline constexpr const char* noteSustain = "noteSustain";
inline constexpr const char* noteRelease = "noteRelease";
inline constexpr const char* masterVol = "masterVol";
inline constexpr const char* pitchBendRange = "pitchBendRange";
inline constexpr const char* posMode = "posMode";
inline constexpr const char* posRateMode = "posRateMode";
inline constexpr const char* posSyncDiv = "posSyncDiv";
inline constexpr const char* grainShape = "grainShape";
inline constexpr const char* filterType = "filterType";
inline constexpr const char* play = "play";
} // namespace pid

// Convolución de la reverb con la de JUCE (latencia cero, mucho más rápida)
class JuceConvolver : public wa::ConvolverBase
{
public:
    void prepare (double sr, int maxBlock) { conv.prepare ({ sr, (juce::uint32) juce::jmax (1, maxBlock), 2 }); }
    void setImpulse (const std::vector<float>& L, const std::vector<float>& R, double sr) override
    {
        juce::AudioBuffer<float> b (2, (int) L.size());
        b.copyFrom (0, 0, L.data(), (int) L.size());
        b.copyFrom (1, 0, R.data(), (int) R.size());
        conv.loadImpulseResponse (std::move (b), sr, juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }
    void process (float* L, float* R, int n) override
    {
        float* ch[2] = { L, R };
        juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    void reset() override { conv.reset(); }

private:
    juce::dsp::Convolution conv;
};

// Un material de la librería (de fábrica o tuyo)
struct Material
{
    juce::String id;          // "f_sine"… o "user:<archivo>" o "embedded"
    juce::String name;
    bool factory = true;
    int factoryIndex = 0;
    std::vector<float> original;   // audio tal cual se cargó (mono)
    double originalRate = 0;       // 0 = "a la frecuencia del motor" (PCM de la web)
    double gainDb = 0;
    juce::File file;               // .wav en la carpeta de la librería (los tuyos)
    std::shared_ptr<std::vector<float>> data;   // a la frecuencia actual
};

class RadianceProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    RadianceProcessor();
    ~RadianceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Radiance-AMB"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // teclado en pantalla y teclado del ordenador

    float param (const char* id) const;
    void setParam (const char* id, float plainValue);
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

    // ── librería (hilo de mensajes) ──
    static juce::File userFolder();          // Documentos/Radiance-AMB
    static juce::File libraryFolder() { return userFolder().getChildFile ("Libreria"); }
    static juce::File patchesFolder() { return userFolder().getChildFile ("Patches"); }
    const std::vector<Material>& library() const { return materials; }
    int selectedIndex() const { return selected; }
    const Material* selectedMaterial() const { return selected >= 0 && selected < (int) materials.size() ? &materials[(size_t) selected] : nullptr; }
    void selectMaterial (int index);
    juce::String addUserSamples (const juce::Array<juce::File>& files);   // devuelve errores (vacío si todo bien)
    void deleteUserMaterial (int index);
    void setSampleGainDb (double db);
    void reloadLibraryFolder();
    int libraryVersion() const { return libVersion; }

    // ── patches (mismo .json que exporta la web) ──
    juce::String patchToJson (const juce::String& name) const;
    juce::String applyPatchJson (const juce::String& json, juce::String& nameOut);   // devuelve error o vacío
    void randomize();

    // ── grabación a WAV ──
    bool startRecording (const juce::File& f);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    double recordedSeconds() const { return (double) recordedSamples.load() / sampleRate; }

    // ── para la interfaz ──
    double engineRate() const { return sampleRate; }
    float displayPosition() const { return dispPos.load(); }
    bool droneOn() const { return param (pid::play) > 0.5f; }
    bool isNoteLit (int midi) const;
    int popGrainMarks (float* dest, int max);
    int octaveShift = 0;

private:
    void timerCallback() override;
    void applyAllParams();
    void applyChangedParams();
    void applyParam (int index, float v);
    void handleMidi (const juce::MidiMessage& m);
    void setLit (int midi, bool on);
    void buildData (Material& m) const;
    void pushSourceToEngine();
    void loadUserMaterialsFromFolder();
    juce::var gainsJson() const;
    void saveGains() const;

    ra::RadianceEngine engine;
    JuceConvolver convolver;
    juce::SpinLock lock;
    double sampleRate = 48000;
    int maxBlock = 512;
    bool prepared = false;

    std::vector<std::atomic<float>*> watched;
    std::vector<float> lastValues;

    std::vector<Material> materials;
    int selected = 0;
    int libVersion = 0;
    std::shared_ptr<std::vector<float>> engineData;                 // lo que suena ahora
    std::vector<std::pair<std::shared_ptr<std::vector<float>>, juce::uint32>> retired;   // lo anterior, hasta que acaben sus granos
    juce::AudioFormatManager formats;

    std::atomic<uint64_t> lit[2] {};
    std::atomic<float> dispPos { 0.3f };
    juce::AbstractFifo grainFifo { 512 };
    std::array<float, 512> grainBuf {};
    int lastGrainLog = 0;

    juce::TimeSliceThread writerThread { "Radiance-AMB rec" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recWriter;
    juce::CriticalSection recLock;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> recordedSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RadianceProcessor)
};
