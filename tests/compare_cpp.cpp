// Ejecuta el motor en C++ con los registros que deja tests/run_js.mjs (mismas acciones, mismos
// ticks del planificador y en el mismo orden) y guarda lo que suena.
//   compare_cpp <carpeta>
#include "../Source/dsp/Factory.h"
#include "../Source/dsp/RadianceEngine.h"

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

using namespace ra;

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "tests/out";
    constexpr double SR = 48000;

    std::ifstream list (dir + "/scenarios.json");
    std::string json ((std::istreambuf_iterator<char> (list)), std::istreambuf_iterator<char>());
    std::vector<std::string> names;
    for (size_t p = 0; (p = json.find ('"', p)) != std::string::npos;)
    {
        const size_t e = json.find ('"', p + 1);
        names.push_back (json.substr (p + 1, e - p - 1));
        p = e + 1;
    }

    const std::map<char, int> keyMap { { 'a', 0 }, { 'w', 1 }, { 's', 2 }, { 'e', 3 }, { 'd', 4 }, { 'f', 5 }, { 't', 6 }, { 'g', 7 }, { 'y', 8 },
                                       { 'h', 9 }, { 'u', 10 }, { 'j', 11 }, { 'k', 12 }, { 'o', 13 }, { 'l', 14 }, { 'p', 15 }, { ';', 16 } };

    for (const auto& name : names)
    {
        std::ifstream in (dir + "/" + name + ".txt");
        struct Ev { int q; std::vector<std::string> w; };
        std::vector<Ev> evs;
        int endQ = 0;
        for (std::string line; std::getline (in, line);)
        {
            std::istringstream ss (line);
            std::string tok; int q; ss >> tok >> q;
            Ev e { q, {} };
            for (std::string w; ss >> w;) e.w.push_back (w);
            if (! e.w.empty() && e.w[0] == "end") { endQ = q; continue; }
            evs.push_back (e);
        }

        auto eng = std::make_unique<RadianceEngine>();
        UniformConvolver conv;
        eng->webCompat = true;
        eng->prepare (SR, &conv, 1);
        std::vector<float> factory[6];
        double gainDb[6] = {};
        int selected = -1;

        std::vector<float> outL ((size_t) endQ * 128), outR ((size_t) endQ * 128);
        size_t ei = 0;
        for (int q = 0; q < endQ; ++q)
        {
            for (; ei < evs.size() && evs[ei].q == q; ++ei)
            {
                const auto& w = evs[ei].w;
                const std::string& cmd = w[0];
                auto num = [&] (size_t i) { return std::stod (w[i]); };
                if (cmd == "tick") eng->webTick();
                else if (cmd == "del") eng->webDeleteVoice (std::stoi (w[1]));
                else if (cmd == "source")
                {
                    const int i = std::stoi (w[1]);
                    if (factory[i].empty()) factory[i] = generateFactory (i, SR, eng->random());
                    selected = i;
                    eng->setSource (factory[i].data(), (int) factory[i].size(), std::pow (10.0, gainDb[i] / 20));
                    eng->webScopeFrame();
                }
                else if (cmd == "preset") {}   // la carga real llega como "source"
                else if (cmd == "click")
                {
                    if (w[1] == "playBtn") { if (eng->hasSource()) eng->setDrone (! eng->droneOn()); }
                    else if (w[1] != "midiConnectBtn") std::cerr << "click desconocido " << w[1] << "\n";
                }
                else if (cmd == "key") eng->noteOn (keyMap.at (w[1][0]) - 12);
                else if (cmd == "keyup") eng->noteOff (keyMap.at (w[1][0]) - 12);
                else if (cmd == "midi")
                {
                    const int st = std::stoi (w[1]) & 0xF0, d1 = std::stoi (w[2]), d2 = w.size() > 3 ? std::stoi (w[3]) : 0;
                    if (st == 0x90 && d2 > 0) eng->midiNoteOn (d1, d2);
                    else if (st == 0x80 || st == 0x90) eng->midiNoteOff (d1);
                    else if (st == 0xE0) eng->midiPitchBend ((d2 << 7) | d1);
                    else if (st == 0xB0 && d1 == 1) eng->midiModWheel (d2);
                    else if (st == 0xB0 && d1 == 64) eng->midiSustain (d2 >= 64);
                }
                else if (cmd == "select")
                {
                    const std::string& id = w[1]; const std::string& v = w[2];
                    auto& p = eng->p;
                    if (id == "posMode") p.posMode = v == "static" ? PosMode::Static : (v == "drift" ? PosMode::Drift : PosMode::Lfo);
                    else if (id == "posRateMode") p.posSync = v == "sync";
                    else if (id == "posSyncDiv") { for (int k = 0; k < 12; ++k) if (v == kSyncNames[k]) p.posSyncDiv = k; }
                    else if (id == "grainShape") p.shape = v == "perc" ? Shape::Perc : (v == "rev" ? Shape::Rev : Shape::Sym);
                    else if (id == "filterType") p.filterType = v == "highpass" ? FilterType::Highpass : (v == "bandpass" ? FilterType::Bandpass : FilterType::Lowpass);
                    else std::cerr << "menú desconocido " << id << "\n";
                }
                else if (cmd == "slider")
                {
                    const std::string& id = w[1]; const double v = num (2);
                    auto& p = eng->p;
                    if (id == "posKnob") p.position = v;
                    else if (id == "posRate") p.posRate = v;
                    else if (id == "posTempo") p.posTempo = v;
                    else if (id == "posDepth") p.posDepth = v / 100;
                    else if (id == "grainSize") p.grainSizeMs = v;
                    else if (id == "grainDensity") p.density = v;
                    else if (id == "grainAttack") p.grainAttackMs = v;
                    else if (id == "grainRelease") p.grainReleaseMs = v;
                    else if (id == "grainJitter") p.jitter = v / 100;
                    else if (id == "pitchTranspose") p.transpose = v;
                    else if (id == "pitchFine") p.fine = v;
                    else if (id == "pitchSpray") p.pitchSpray = v;
                    else if (id == "filterCutoff") p.filterCutoffPct = v;
                    else if (id == "filterQ") p.filterQ = v;
                    else if (id == "filterSpray") p.filterSpray = v / 100;
                    else if (id == "stereoSpread") p.stereoSpread = v / 100;
                    else if (id == "dryWet") eng->setDryWet (v);
                    else if (id == "reverbPreDelay") eng->setPreDelay (v);
                    else if (id == "reverbSpace") eng->setSpace (v);
                    else if (id == "shimmerTone") p.shimmerSemitones = v;
                    else if (id == "shimmerAmount") p.shimmerAmount = v / 100;
                    else if (id == "moveRate") p.moveRate = v;
                    else if (id == "moveDepth") p.moveDepth = v / 100;
                    else if (id == "masterVol") eng->setMaster (v);
                    else if (id == "pitchBendRange") p.pitchBendRange = v;
                    else if (id == "noteAttack") p.noteAttackMs = v;
                    else if (id == "noteDecay") p.noteDecayMs = v;
                    else if (id == "noteSustain") p.noteSustain = v / 100;
                    else if (id == "noteRelease") p.noteReleaseMs = v;
                    else if (id == "sampleGain") { if (selected >= 0) { gainDb[selected] = v; eng->setPresetGain (std::pow (10.0, v / 20)); } }
                    else std::cerr << "slider desconocido " << id << "\n";
                }
                else std::cerr << "acción desconocida " << cmd << "\n";
            }
            eng->process (outL.data() + q * 128, outR.data() + q * 128, 128);
        }
        std::ofstream out (dir + "/" + name + ".cpp.f32", std::ios::binary);
        out.write ((const char*) outL.data(), (std::streamsize) (outL.size() * 4));
        out.write ((const char*) outR.data(), (std::streamsize) (outR.size() * 4));
    }
    std::cout << "escenarios C++: " << names.size() << "\n";
    return 0;
}
