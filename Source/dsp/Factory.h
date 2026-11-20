// Radiance-AMB — materiales de fábrica (traducción de los generadores del prototipo).
// Cada uno devuelve 5 s de audio mono a la frecuencia indicada. Usan el generador
// pseudoaleatorio que se les pasa (en las pruebas, el mismo que sustituye a Math.random).
#pragma once

#include "WebAudio.h"
#include <string>

namespace ra
{
using wa::kPi;
using wa::Lcg;

constexpr double kFactoryDur = 5.0;

inline void normalizeInPlace (std::vector<float>& d, double target = 0.92)
{
    double peak = 0;
    for (float v : d) peak = std::max (peak, (double) std::abs (v));
    if (peak > 0.0001)
    {
        const double g = target / peak;
        for (auto& v : d) v = (float) (v * g);
    }
}

inline std::vector<float> genPureSine (double sr)
{
    const int n = (int) std::floor (kFactoryDur * sr);
    std::vector<float> d ((size_t) n);
    for (int i = 0; i < n; ++i) d[(size_t) i] = (float) (std::sin (2 * kPi * 110 * i / sr) * 0.85);
    return d;
}

inline std::vector<float> genAdditiveDrift (double sr, Lcg& rng)
{
    const int n = (int) std::floor (kFactoryDur * sr);
    std::vector<float> d ((size_t) n);
    constexpr int numH = 22;
    const double f0 = 110;
    struct Drift { double rate, phase, depth; } drifts[numH];
    for (auto& dr : drifts)
    {
        dr.rate = 0.05 + rng.next() * 0.25;
        dr.phase = rng.next() * kPi * 2;
        dr.depth = 0.002 + rng.next() * 0.006;
    }
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double richness = 1 - (t / kFactoryDur) * 0.85;
        double s = 0;
        for (int h = 1; h <= numH; ++h)
        {
            const auto& dr = drifts[h - 1];
            const double detune = 1 + dr.depth * std::sin (2 * kPi * dr.rate * t + dr.phase);
            const double amp = (1.0 / h) * std::pow (richness, h * 0.35);
            if (amp < 0.002) continue;
            s += amp * std::sin (2 * kPi * f0 * h * detune * t);
        }
        d[(size_t) i] = (float) s;
    }
    normalizeInPlace (d);
    return d;
}

inline std::vector<float> genDirtyToSmooth (double sr, Lcg& rng)
{
    const int n = (int) std::floor (kFactoryDur * sr);
    std::vector<float> d ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double dirt = 1 - (t / kFactoryDur);
        double s = 0;
        for (int k = 0; k < 15; ++k)
        {
            const int h = 2 * k + 1;
            s += (1.0 / h) * (0.25 + 0.75 * dirt) * std::sin (2 * kPi * 110 * h * t);
        }
        s += (rng.next() * 2 - 1) * 0.06 * dirt;
        d[(size_t) i] = (float) s;
    }
    normalizeInPlace (d);
    return d;
}

inline std::vector<float> genRoughGrain (double sr, Lcg& rng)
{
    const int n = (int) std::floor (kFactoryDur * sr);
    std::vector<float> d ((size_t) n);
    double phase = 0, ampNoise = 0, freqNoise = 0;
    for (int i = 0; i < n; ++i)
    {
        ampNoise += (rng.next() * 2 - 1) * 0.002; ampNoise = std::max (-0.4, std::min (0.4, ampNoise * 0.999));
        freqNoise += (rng.next() * 2 - 1) * 0.15; freqNoise = std::max (-8.0, std::min (8.0, freqNoise * 0.995));
        const double freq = 110 + freqNoise;
        phase += 2 * kPi * freq / sr;
        double s = std::sin (phase) * 0.6 + std::sin (phase * 2.01) * 0.25 + std::sin (phase * 3.98) * 0.15;
        s *= (1 + ampNoise);
        s += (rng.next() * 2 - 1) * 0.05;
        d[(size_t) i] = (float) s;
    }
    normalizeInPlace (d);
    return d;
}

inline std::vector<float> genFMBell (double sr)
{
    const int n = (int) std::floor (kFactoryDur * sr);
    std::vector<float> d ((size_t) n);
    const double carrier = 220, modRatio = 3.43, indexBase = 6;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double env = std::exp (-t * 0.6) * 0.6 + 0.4 * std::exp (-t * 0.05);
        const double idx = indexBase * std::exp (-t * 0.8);
        const double mod = std::sin (2 * kPi * carrier * modRatio * t) * idx;
        d[(size_t) i] = (float) (std::sin (2 * kPi * carrier * t + mod) * env);
    }
    normalizeInPlace (d);
    return d;
}

inline std::vector<float> genFilteredNoiseSweep (double sr, Lcg& rng)
{
    const int n = (int) std::floor (kFactoryDur * sr);
    std::vector<float> d ((size_t) n);
    double lp1 = 0, lp2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double cutoffHz = 200 + (std::sin (2 * kPi * 0.07 * t) * 0.5 + 0.5) * 3200;
        const double a = std::min (0.5, cutoffHz / sr * 3);
        const double white = rng.next() * 2 - 1;
        lp1 += a * (white - lp1);
        lp2 += a * (lp1 - lp2);
        d[(size_t) i] = (float) (lp2 * 2.2);
    }
    normalizeInPlace (d);
    return d;
}

struct FactoryDef { const char* id; const char* name; };
inline const FactoryDef kFactory[6] = {
    { "f_sine", "Seno puro (base neutra)" },
    { "f_add", "Aditivo — denso → puro" },
    { "f_dirty", "Sucia → suave" },
    { "f_rough", "Grano áspero (deriva aleatoria)" },
    { "f_bell", "Campana FM" },
    { "f_noise", "Ruido filtrado en movimiento" },
};

inline std::vector<float> generateFactory (int index, double sr, Lcg& rng)
{
    switch (index)
    {
        case 1: return genAdditiveDrift (sr, rng);
        case 2: return genDirtyToSmooth (sr, rng);
        case 3: return genRoughGrain (sr, rng);
        case 4: return genFMBell (sr);
        case 5: return genFilteredNoiseSweep (sr, rng);
        default: return genPureSine (sr);
    }
}
} // namespace ra
