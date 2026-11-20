// Radiance-AMB — motor granular continuo (traducción del prototipo web/radiance-amb.html).
//
// Cada grano es lo que en la web era un AudioBufferSourceNode + BiquadFilter + Gain con
// setValueCurveAtTime + StereoPanner. Aquí son objetos de una reserva fija que se suman
// muestra a muestra. Luego: directo, y envío a pre-delay → convolución → "Space" (retardo
// del canal derecho) → retorno; mezcla equal-power y volumen general.
//
// Dos formas de programar los granos:
//  - webCompat = true: imita el planificador de la web (un "tick" cada 11 quantums, con
//    0,2 s de antelación y la posición calculada en el instante del tick). Solo para comparar
//    con el prototipo muestra a muestra.
//  - webCompat = false (plugin): cada grano se lanza justo en su instante, con la posición
//    calculada en ese instante. Sin la latencia de 0,2 s de la web.
//
// No depende de JUCE. No reserva memoria al procesar.
#pragma once

#include "WebAudio.h"
#include <memory>

namespace ra
{
using namespace wa;

enum class Shape { Sym = 0, Perc = 1, Rev = 2 };
enum class PosMode { Static = 0, Lfo = 1, Drift = 2 };
enum class FilterType { Lowpass = 0, Highpass = 1, Bandpass = 2 };

inline constexpr const char* kSyncNames[12] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4D", "1/8D", "1/16D", "1/4T", "1/8T", "1/16T" };
inline constexpr double kSyncBeats[12] = { 4, 2, 1, 0.5, 0.25, 0.125, 1.5, 0.75, 0.375, 2.0 / 3, 1.0 / 3, 1.0 / 6 };

// Los mismos valores iniciales que los <input> del prototipo
struct Settings
{
    double grainSizeMs = 120, density = 18, grainAttackMs = 3, grainReleaseMs = 25, jitter = 0.08;
    Shape shape = Shape::Sym;
    double position = 0.3, posRate = 0.05, posTempo = 120, posDepth = 0.35;
    PosMode posMode = PosMode::Lfo;
    bool posSync = false;
    int posSyncDiv = 2;
    double noteAttackMs = 400, noteDecayMs = 300, noteSustain = 0.7, noteReleaseMs = 1200;
    double transpose = 0, fine = 0, pitchSpray = 12;
    FilterType filterType = FilterType::Lowpass;
    double filterCutoffPct = 72, filterQ = 0.8, filterSpray = 0.2;
    double stereoSpread = 0.7, reverbSpaceMs = 0, moveRate = 0.08, moveDepth = 0.25;
    double dryWet = 0.35, preDelayMs = 0, shimmerSemitones = 12, shimmerAmount = 0;
    double masterVol = 0.7, pitchBendRange = 2;
};

inline double cutoffHzFromPct (double pct) { return 80 * std::pow (12000.0 / 80, pct / 100); }

class RadianceEngine
{
public:
    static constexpr int kEnvPoints = 48;
    static constexpr int kMaxGrains = 1024;
    static constexpr int kMaxVoices = 128;
    static constexpr double kReverbTail = 3.2;

    Settings p;
    bool webCompat = false;

    Lcg& random() { return rng; }

    // Reserva memoria: llamar fuera del hilo de audio.
    void prepare (double sampleRate, ConvolverBase* convolver, uint32_t seed = 1)
    {
        sr = sampleRate; conv = convolver; rng.s = seed; frame = 0;
        preDelay[0].setup (0.2, sr); preDelay[1].setup (0.2, sr);
        spaceDelay.setup (0.05, sr);
        preDelayTime.init (p.preDelayMs / 1000);
        spaceTime.init (p.reverbSpaceMs / 1000);
        applyDryWet();
        masterGain = (float) p.masterVol;
        dryCur = dryGain; returnCur = returnGain; masterCur = masterGain;
        smoothCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));   // ~10 ms
        makeImpulse();
        killGrains();
        nVoices = 0; running = false; isPlaying = false;
        driftPos = driftTarget = p.position; lastDriftPick = 0;
        bendCents = 0; modDepthCents = 0; sustainDown = false;
        for (auto& s : sustained) s = false;
        const size_t maxChunk = 4096;
        for (auto* b : { &dryL, &dryR, &wetL, &wetR, &convL, &convR }) b->assign (maxChunk, 0.0f);
        grainLog.fill ({ -1, 0 }); grainLogPos = 0;
    }

    double now() const { return (double) frame / sr; }
    double sampleRate() const { return sr; }

    // ── material ──────────────────────────────────────────────────────────────
    // Los granos ya lanzados siguen leyendo el material anterior (como en la web): quien llama
    // debe mantenerlo vivo al menos ~1 s más (ver retiredSources en el procesador).
    void setSource (const float* data, int length, double gainLinear)
    {
        src = data; srcLen = length; presetGain = gainLinear;
    }
    // corta todos los granos (antes de liberar un material que aún se esté leyendo)
    void killGrains()
    {
        for (auto& g : grains) g.active = false;
        for (auto& b : inList) b = false;
        nActive = 0;
    }
    void setPresetGain (double g) { presetGain = g; }
    bool hasSource() const { return src != nullptr && srcLen > 0; }

    // ── controles (equivalen a los manejadores de la web) ─────────────────────
    void setDryWet (double pct) { p.dryWet = pct / 100; applyDryWet(); }
    void setMaster (double pct) { p.masterVol = pct / 100; masterGain = (float) p.masterVol; if (webCompat) masterCur = masterGain; }
    void setPreDelay (double ms) { p.preDelayMs = ms; target (preDelayTime, ms / 1000, 0.015); }
    void setSpace (double ms) { p.reverbSpaceMs = ms; target (spaceTime, ms / 1000, 0.015); }

    // Reproducir / Detener (el dron)
    void setDrone (bool on)
    {
        if (on == isPlaying) return;
        if (on)
        {
            if (! hasSource()) return;
            isPlaying = true;
            ensureSchedulerRunning();
        }
        else isPlaying = false;
    }
    bool droneOn() const { return isPlaying; }

    // ── notas (id = semitono relativo a C4, como 'k'+semitono en la web) ───────
    void noteOn (int semitone, double velocity = 1.0)
    {
        if (! webCompat) processTimers (now());
        const double t = now();
        Voice* v = findVoice (semitone);
        if (v == nullptr)
        {
            if (nVoices >= kMaxVoices) removeVoice (0);
            v = &voices[(size_t) nVoices++];
            v->id = semitone;
        }
        v->semitone = semitone; v->onTime = t; v->releaseStart = -1; v->releaseFrom = 0;
        v->attack = p.noteAttackMs / 1000; v->decay = p.noteDecayMs / 1000; v->sustain = p.noteSustain; v->release = p.noteReleaseMs / 1000;
        v->velocity = velocity;
        v->deleteAt = 1e300;
        ensureSchedulerRunning();
    }

    void noteOff (int semitone)
    {
        if (! webCompat) processTimers (now());
        Voice* v = findVoice (semitone);
        const double t = now();
        if (v == nullptr || v->releaseStart >= 0) return;
        v->releaseFrom = envAt (*v, t);
        v->releaseStart = t;
        v->deleteAt = t + v->release + 0.06;
    }

    // Entrada MIDI (como handleLiveMidiMessage)
    void midiNoteOn (int note, int velocity) { const int st = note - 60; sustained[(size_t) (st + 128)] = false; noteOn (st, velocity / 127.0); }
    void midiNoteOff (int note)
    {
        const int st = note - 60;
        if (sustainDown) sustained[(size_t) (st + 128)] = true;
        else noteOff (st);
    }
    void midiPitchBend (int value14) { bendCents = ((value14 - 8192) / 8192.0) * p.pitchBendRange * 100; }
    void midiModWheel (int value) { modDepthCents = (value / 127.0) * 50; }
    void midiSustain (bool down)
    {
        if (sustainDown && ! down)
        {
            for (int i = 0; i < 256; ++i)
                if (sustained[(size_t) i]) { sustained[(size_t) i] = false; noteOff (i - 128); }
        }
        sustainDown = down;
    }
    void allNotesOff()
    {
        for (int i = 0; i < nVoices; ++i) noteOff (voices[(size_t) i].id);
        for (auto& s : sustained) s = false;
        sustainDown = false;
    }
    bool isNoteHeld (int semitone) const
    {
        for (int i = 0; i < nVoices; ++i)
            if (voices[(size_t) i].id == semitone && voices[(size_t) i].releaseStart < 0) return true;
        return false;
    }

    // ── solo para comparar con la web (webCompat) ──────────────────────────────
    void webTick() { schedulerTick (now()); }                 // un setInterval del planificador
    void webDeleteVoice (int id) { for (int i = 0; i < nVoices; ++i) if (voices[(size_t) i].id == id) { removeVoice (i); return; } }
    bool schedulerRunning() const { return running; }
    void webScopeFrame() { if (isPlaying) currentPosition (now()); }   // drawScope() al elegir material

    // ── para la interfaz ──────────────────────────────────────────────────────
    // posición actual sin alterar el estado (la web llamaba a currentPosition desde el dibujo)
    double displayPosition() const
    {
        if (! isPlaying || p.posMode == PosMode::Static) return clamp01 (p.position);
        if (p.posMode == PosMode::Lfo) return clamp01 (p.position + p.posDepth * std::sin (2 * kPi * effectiveRate() * now()));
        return clamp01 (driftPos);
    }
    struct GrainMark { double pos, born; };
    static constexpr int kLog = 256;
    std::array<GrainMark, kLog> grainLog {};
    int grainLogPos = 0;
    int activeGrains() const { return nActive; }

    // ── proceso ───────────────────────────────────────────────────────────────
    void process (float* outL, float* outR, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int toBoundary = kQuantum - (int) (frame % kQuantum);
            const int m = std::min ({ n - done, toBoundary, 4096 });
            processChunk (outL + done, outR + done, m);
            done += m;
        }
    }

private:
    struct Voice
    {
        int id = 0, semitone = 0;
        double onTime = 0, releaseStart = -1, releaseFrom = 0;
        double attack = 0, decay = 0, sustain = 1, release = 0, velocity = 1, deleteAt = 1e300;
    };

    struct Grain
    {
        bool active = false, started = false, toDry = true, toWet = true;
        const float* src = nullptr;
        int len = 0;
        double start = 0, stop = 0, offset = 0, bufferTime = 0, elapsed = 0, durLimit = 0, rate = 1;
        double curveStart = 0, curveDur = 0;
        float gL = 0, gR = 0;
        std::array<float, kEnvPoints> curve {};
        Biquad filter;
    };

    static double clamp01 (double x) { return std::max (0.0, std::min (1.0, x)); }

    void target (Param& prm, double v, double tau)
    {
        const double t = now();
        prm.cancelScheduledValues (t);
        prm.setTargetAtTime (v, t, tau);
    }

    void applyDryWet()
    {
        const double angle = p.dryWet * (kPi / 2);
        dryGain = (float) std::cos (angle);
        returnGain = (float) std::sin (angle);
        if (webCompat) { dryCur = dryGain; returnCur = returnGain; }
    }

    void makeImpulse()
    {
        const int len = (int) std::floor (sr * kReverbTail);
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i) mono[(size_t) i] = (float) ((rng.next() * 2 - 1) * std::pow (1.0 - (double) i / len, 2.6));
        // normalización del ConvolverNode (L y R idénticos)
        float power = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            float chp = 0;
            for (float v : mono) chp += v * v;
            power += chp;
        }
        power = std::sqrt (power / (float) (2 * len));
        if (! std::isfinite (power) || power < 0.000125f) power = 0.000125f;
        const float scale = (float) (1.0 / power * 0.00125 * (44100.0 / sr));
        for (auto& v : mono) v *= scale;
        if (conv != nullptr) conv->setImpulse (mono, mono, sr);
    }

    double effectiveRate() const
    {
        if (p.posSync) return std::max (0.001, (p.posTempo / 60) / kSyncBeats[std::clamp (p.posSyncDiv, 0, 11)]);
        return p.posRate;
    }

    double currentPosition (double t)
    {
        if (p.posMode == PosMode::Static) return clamp01 (p.position);
        if (p.posMode == PosMode::Lfo) return clamp01 (p.position + p.posDepth * std::sin (2 * kPi * effectiveRate() * t));
        if (t - lastDriftPick > (1 / std::max (0.02, p.posRate)))
        {
            lastDriftPick = t;
            driftTarget = clamp01 (p.position + (rng.next() * 2 - 1) * p.posDepth);
        }
        driftPos += (driftTarget - driftPos) * 0.02;
        return clamp01 (driftPos);
    }

    static double attackCurve (double x, Shape s)
    {
        if (s == Shape::Perc) return std::pow (x, 0.35);
        if (s == Shape::Rev) return std::pow (x, 2.5);
        return 0.5 - 0.5 * std::cos (kPi * x);
    }
    static double releaseCurve (double x, Shape s)
    {
        if (s == Shape::Perc) return std::pow (1 - x, 1.6);
        if (s == Shape::Rev) return 1 - x;
        return 0.5 + 0.5 * std::cos (kPi * x);
    }

    void buildEnvelope (std::array<float, kEnvPoints>& arr, double grainSec) const
    {
        const double attackSec = std::max (0.0, std::min (grainSec, p.grainAttackMs / 1000));
        const double releaseSec = std::max (0.0, std::min (grainSec - attackSec, p.grainReleaseMs / 1000));
        const double sustainSec = std::max (0.0, grainSec - attackSec - releaseSec);
        for (int i = 0; i < kEnvPoints; ++i)
        {
            const double t = ((double) i / (kEnvPoints - 1)) * grainSec;
            double v;
            if (t < attackSec) v = attackSec > 0 ? attackCurve (t / attackSec, p.shape) : 1;
            else if (t < attackSec + sustainSec) v = 1;
            else
            {
                const double rt = releaseSec > 0 ? (t - attackSec - sustainSec) / releaseSec : 1;
                v = releaseCurve (std::min (1.0, rt), p.shape);
            }
            arr[(size_t) i] = (float) v;
        }
        if (attackSec > 0) arr[0] = 0;
        if (releaseSec > 0) arr[kEnvPoints - 1] = 0;
    }

    static double envAt (const Voice& v, double t)
    {
        if (v.releaseStart >= 0)
        {
            const double rel = std::max (0.001, v.release);
            const double x = (t - v.releaseStart) / rel;
            if (x >= 1) return 0;
            if (x <= 0) return v.releaseFrom;
            return v.releaseFrom * (0.5 + 0.5 * std::cos (kPi * x));
        }
        const double elapsed = t - v.onTime;
        if (elapsed < 0) return 0;
        if (elapsed < v.attack)
        {
            const double x = v.attack > 0 ? elapsed / v.attack : 1;
            return 0.5 - 0.5 * std::cos (kPi * x);
        }
        const double after = elapsed - v.attack;
        if (after < v.decay)
        {
            const double x = v.decay > 0 ? after / v.decay : 1;
            return v.sustain + (1 - v.sustain) * (0.5 + 0.5 * std::cos (kPi * x));
        }
        return v.sustain;
    }

    Voice* findVoice (int id)
    {
        for (int i = 0; i < nVoices; ++i) if (voices[(size_t) i].id == id) return &voices[(size_t) i];
        return nullptr;
    }
    void removeVoice (int index)
    {
        for (int i = index; i + 1 < nVoices; ++i) voices[(size_t) i] = voices[(size_t) i + 1];
        --nVoices;
    }
    // los "setTimeout" que borran voces terminadas
    void processTimers (double t)
    {
        for (int i = 0; i < nVoices;)
            if (voices[(size_t) i].deleteAt <= t + 1e-12) removeVoice (i); else ++i;
    }

    void ensureSchedulerRunning()
    {
        if (! hasSource() || running) return;
        running = true;
        nextGrainTime = webCompat ? now() + 0.05 : now();
    }

    // un tick del planificador de la web
    void schedulerTick (double t)
    {
        if (! webCompat) processTimers (t);
        if (! isPlaying && nVoices == 0) { running = false; return; }
        if (nextGrainTime < t - 0.5) nextGrainTime = t + 0.02;
        while (nextGrainTime < t + 0.2) spawnStep (t);
    }

    void spawnStep (double posTime)
    {
        if (isPlaying) spawnGrain (nextGrainTime, nullptr, posTime);
        const int count = nVoices;
        for (int i = 0; i < count && i < nVoices; ++i) spawnGrain (nextGrainTime, &voices[(size_t) i], posTime);
        nextGrainTime += 1.0 / std::max (1.0, p.density);
    }

    void spawnGrain (double startTime, const Voice* voice, double t)
    {
        if (! hasSource()) return;
        double envScalar = 1;
        if (voice != nullptr)
        {
            envScalar = envAt (*voice, startTime) * voice->velocity;
            if (envScalar < 0.003) return;
        }
        const double dur = srcLen / sr;
        const double grainSec = std::max (0.005, p.grainSizeMs / 1000);
        const double basePos = currentPosition (t);
        const double jitter = (rng.next() * 2 - 1) * p.jitter * 0.5;
        const double pos = clamp01 (basePos + jitter);
        double offset = pos * std::max (0.0, dur - grainSec);
        offset = std::max (0.0, std::min (dur - 0.001, offset));

        const double move = std::sin (2 * kPi * p.moveRate * t) * p.moveDepth;
        const double semitone = p.transpose + (voice ? voice->semitone : 0);
        const double bend = voice ? bendCents : 0;
        const double vib = voice ? std::sin (2 * kPi * 5.5 * t) * modDepthCents : 0;
        const double cents = p.fine + (rng.next() * 2 - 1) * p.pitchSpray + move * 15 + bend + vib;
        const double cutoffSpray = 1 + (rng.next() * 2 - 1) * p.filterSpray;
        const double cutoff = std::max (40.0, std::min (18000.0, cutoffHzFromPct (p.filterCutoffPct) * cutoffSpray * (1 + move * 0.4)));

        std::array<float, kEnvPoints> curve;
        buildEnvelope (curve, grainSec);
        const double total = envScalar * presetGain;
        if (total < 0.999 || total > 1.001)
            for (auto& c : curve) c = (float) (c * total);

        const double pan = (rng.next() * 2 - 1) * p.stereoSpread;
        addGrain (startTime, dur, offset, grainSec, semitone, cents, cutoff, curve, pan, true, true);

        if (p.shimmerAmount > 0.002)
        {
            std::array<float, kEnvPoints> sc;
            for (int i = 0; i < kEnvPoints; ++i) sc[(size_t) i] = (float) (curve[(size_t) i] * p.shimmerAmount);
            const double pan2 = (rng.next() * 2 - 1) * p.stereoSpread;
            addGrain (startTime, dur, offset, grainSec, semitone + p.shimmerSemitones, cents, cutoff, sc, pan2, false, true);
        }
        grainLog[(size_t) grainLogPos] = { pos, now() };
        grainLogPos = (grainLogPos + 1) % kLog;
    }

    void addGrain (double startTime, double dur, double offset, double grainSec, double semitone, double cents, double cutoff,
                   const std::array<float, kEnvPoints>& curve, double pan, bool toDry, bool toWet)
    {
        Grain* g = nullptr;
        if (nActive < kMaxGrains)
        {
            // hueco libre: los que no están en la lista activa
            while (grains[(size_t) freeScan].active || inList[(size_t) freeScan]) freeScan = (freeScan + 1) % kMaxGrains;
            g = &grains[(size_t) freeScan];
            inList[(size_t) freeScan] = true;
            activeIdx[(size_t) nActive++] = freeScan;
        }
        else   // reserva llena: se reutiliza el grano que antes termina
        {
            g = &grains[0];
            for (auto& c : grains) if (c.curveStart + c.curveDur < g->curveStart + g->curveDur) g = &c;
        }
        const double rate = (double) (float) std::max (0.05, std::pow (2.0, semitone / 12) * std::pow (2.0, cents / 1200));
        double need = (grainSec + 0.02) * rate;
        need = std::min (need, std::max (0.002, dur - offset - 0.001));
        g->active = true; g->started = false; g->src = src; g->len = srcLen;
        g->start = startTime; g->stop = startTime + grainSec + 0.05;
        g->offset = offset; g->bufferTime = 0; g->elapsed = 0; g->durLimit = need; g->rate = rate;
        g->curveStart = startTime; g->curveDur = grainSec;
        g->curve = curve;
        const auto type = p.filterType == FilterType::Highpass ? Biquad::Type::Highpass
                        : (p.filterType == FilterType::Bandpass ? Biquad::Type::Bandpass : Biquad::Type::Lowpass);
        g->filter.reset();
        g->filter.setup (type, (double) (float) cutoff, (double) (float) p.filterQ, sr);
        const float pv = (float) pan;
        const double x = (std::max (-1.0f, std::min (1.0f, pv)) + 1) * 0.5;
        g->gL = (float) std::cos (x * kPi / 2);
        g->gR = (float) std::sin (x * kPi / 2);
        g->toDry = toDry; g->toWet = toWet;
    }

    // una muestra de un grano en el instante ct (AudioBufferSourceNode, "slow track")
    float grainSample (Grain& g, double ct)
    {
        const double dt = 1.0 / sr;
        double x = 0;
        if (! g.started && std::abs (ct - g.start) < 1e-12) g.start = ct;
        const bool silent = ct < g.start || ct >= g.stop || g.elapsed >= g.durLimit;
        if (! silent)
        {
            const int len = g.len;
            const float* data = g.src;
            const double bufDur = len / sr;
            if (! g.started)
            {
                const double delta = ct - g.start;
                g.offset = std::max (0.0, std::min (bufDur, g.offset + delta * g.rate));
                g.bufferTime = g.offset;
                g.elapsed = std::abs (delta * g.rate);
                g.started = true;
            }
            if (std::abs (g.bufferTime) < 1e-12) g.bufferTime = 0;
            if (g.bufferTime >= 0 && g.bufferTime < bufDur)
            {
                const double playhead = g.bufferTime * sr;
                const double fl = std::floor (playhead);
                const int idx = (int) fl;
                const double k = playhead - fl;
                if (idx < len)
                {
                    const double prev = data[idx];
                    double next;
                    if (idx + 1 < len) next = data[idx + 1];
                    else next = (std::abs (k - 1) < 1e-12 || idx == 0) ? 0.0 : 2 * prev - data[idx - 1];
                    x = (float) ((1 - k) * prev + k * next);
                }
            }
            g.bufferTime += dt * g.rate;
            g.elapsed += std::abs (dt * g.rate);
        }
        const double y = g.filter.process (x);
        // envolvente del grano (setValueCurveAtTime)
        float env;
        const double rel = ct - g.curveStart;
        if (rel < 0) env = 0;
        else if (rel >= g.curveDur) env = g.curve[kEnvPoints - 1];
        else
        {
            const double posC = (kEnvPoints - 1) * rel / g.curveDur;
            const int k = (int) posC;
            const float ph = (float) (posC - std::floor (posC));
            env = (g.curve[(size_t) k + 1] - g.curve[(size_t) k]) * ph + g.curve[(size_t) k];
        }
        const float out = (float) y * env;
        // terminado: la fuente ya paró y el filtro se ha apagado (o la envolvente ya es 0)
        if (ct >= g.stop && rel >= g.curveDur && (env == 0.0f || std::abs (out) < 1e-9f)) g.active = false;
        return out;
    }

    void processChunk (float* outL, float* outR, int m)
    {
        const double dt = 1.0 / sr;
        const double blockTime = (double) frame / sr;

        for (int i = 0; i < m; ++i)
        {
            const double ct = (double) i * dt + blockTime;
            if (! webCompat)
            {
                processTimers (ct);
                // la web movía la deriva también desde el dibujo (~60 veces por segundo)
                if (isPlaying && p.posMode == PosMode::Drift && --driftFrameCountdown <= 0)
                {
                    driftFrameCountdown = (int) (sr / 60);
                    currentPosition (ct);
                }
                if (running)
                {
                    if (! isPlaying && nVoices == 0) running = false;
                    else
                    {
                        int guard = 0;
                        while (nextGrainTime <= ct + 1e-12 && guard++ < 64) spawnStep (nextGrainTime);
                    }
                }
            }
            float dl = 0, dr = 0, wl = 0, wr = 0;
            for (int a = 0; a < nActive; ++a)
                {
                    auto& g = grains[(size_t) activeIdx[(size_t) a]];
                    if (! g.active) continue;
                    const float v = grainSample (g, ct);
                    const float l = v * g.gL, r = v * g.gR;
                    if (g.toDry) { dl += l; dr += r; }
                    if (g.toWet) { wl += l; wr += r; }
                }
            dryL[(size_t) i] = dl; dryR[(size_t) i] = dr;
            // pre-delay (estéreo, sin límite inferior: a 0 ms pasa tal cual)
            const double d = preDelayTime.valueAt (ct) * sr;
            preDelay[0].write (wl); preDelay[1].write (wr);
            convL[(size_t) i] = (float) preDelay[0].readAfterWrite (d);
            convR[(size_t) i] = (float) preDelay[1].readAfterWrite (d);
        }

        // compacta la lista de granos activos
        int k = 0;
        for (int a = 0; a < nActive; ++a)
        {
            const int idx = activeIdx[(size_t) a];
            if (grains[(size_t) idx].active) activeIdx[(size_t) k++] = idx;
            else inList[(size_t) idx] = false;
        }
        nActive = k;

        if (conv != nullptr) conv->process (convL.data(), convR.data(), m);
        else { std::fill (convL.begin(), convL.begin() + m, 0.0f); std::fill (convR.begin(), convR.begin() + m, 0.0f); }

        for (int i = 0; i < m; ++i)
        {
            const double ct = (double) i * dt + blockTime;
            const double sd = spaceTime.valueAt (ct) * sr;
            spaceDelay.write (convR[(size_t) i]);
            const float rr = (float) spaceDelay.readAfterWrite (sd);
            if (! webCompat)   // la web cambia estos volúmenes de golpe; aquí se suavizan para evitar clics
            {
                dryCur += (dryGain - dryCur) * smoothCoef;
                returnCur += (returnGain - returnCur) * smoothCoef;
                masterCur += (masterGain - masterCur) * smoothCoef;
            }
            const float l = dryL[(size_t) i] * dryCur + convL[(size_t) i] * returnCur;
            const float r = dryR[(size_t) i] * dryCur + rr * returnCur;
            outL[i] = l * masterCur;
            outR[i] = r * masterCur;
        }
        frame += m;
    }

    double sr = 48000;
    int64_t frame = 0;
    Lcg rng;
    ConvolverBase* conv = nullptr;

    const float* src = nullptr;
    int srcLen = 0;
    double presetGain = 1;

    std::array<Voice, kMaxVoices> voices {};
    int nVoices = 0;
    std::array<Grain, kMaxGrains> grains {};
    std::array<int, kMaxGrains> activeIdx {};
    std::array<bool, kMaxGrains> inList {};
    int nActive = 0, freeScan = 0;
    bool running = false, isPlaying = false;
    double nextGrainTime = 0;
    int driftFrameCountdown = 0;
    double driftPos = 0.3, driftTarget = 0.3, lastDriftPick = 0;
    double bendCents = 0, modDepthCents = 0;
    bool sustainDown = false;
    std::array<bool, 256> sustained {};

    float dryGain = 1, returnGain = 0, masterGain = 0.7f;
    float dryCur = 1, returnCur = 0, masterCur = 0.7f, smoothCoef = 1;
    Param preDelayTime, spaceTime;
    DelayLine preDelay[2], spaceDelay;
    std::vector<float> dryL, dryR, wetL, wetR, convL, convR;
};
} // namespace ra
