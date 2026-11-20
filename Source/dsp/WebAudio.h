// Piezas de Web Audio reescritas en C++ siguiendo la especificación W3C
// (y la implementación de referencia web-audio-api-rs donde la especificación deja margen).
// No depende de JUCE. Nada reserva memoria después de construirse, salvo los métodos
// que lo dicen expresamente (setImpulse...).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace wa
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kQuantum = 128;   // tamaño del "render quantum" de Web Audio

// Generador pseudoaleatorio determinista (el mismo que usan las pruebas en JavaScript)
struct Lcg
{
    uint32_t s = 1;
    double next() { s = s * 1664525u + 1013904223u; return s / 4294967296.0; }
};

//==============================================================================
// AudioParam con la automatización que usa el instrumento:
// setValueAtTime, linearRampToValueAtTime, setTargetAtTime y cancelScheduledValues.
class Param
{
public:
    Param() = default;
    explicit Param (double v) : base (v) {}

    // equivale a "param.value = v" en el momento de crear el grafo
    void init (double v) { base = v; n = 0; }

    void setValueAtTime (double v, double t) { insert ({ Type::Set, t, v, 0, 0, 0 }); }

    void linearRampToValueAtTime (double v, double t, double now)
    {
        Ev e { Type::Linear, t, v, 0, now, base };
        // la rampa empieza en el evento anterior (o ahora, con el valor actual)
        int prev = -1;
        for (int i = 0; i < n; ++i) if (ev[(size_t) i].time <= t) prev = i;
        if (prev >= 0) { e.startTime = ev[(size_t) prev].time; e.startValue = endValue (prev); }
        else e.startValue = evalAt (now);
        insert (e);
    }

    void setTargetAtTime (double v, double t, double tau)
    {
        Ev e { Type::Target, t, v, tau, t, evalAt (t) };
        insert (e);
    }

    // Quita los eventos desde t. Si se corta una rampa lineal a medias, el valor se queda
    // donde estaba en t (como web-audio-api-rs), en lugar de saltar al evento anterior.
    void cancelScheduledValues (double t)
    {
        const double held = evalAt (t);
        bool cutRamp = false;
        int k = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& e = ev[(size_t) i];
            if (e.time < t) ev[(size_t) k++] = e;
            else if (e.type == Type::Linear && e.startTime < t) cutRamp = true;
        }
        n = k;
        if (cutRamp) insert ({ Type::Set, t, held, 0, 0, 0 });
    }

    // Valor en el instante t (avanza: descarta eventos ya superados)
    double valueAt (double t)
    {
        // se conserva un margen para poder consultar el valor de hace un "quantum"
        while (n >= 2 && ev[1].time <= t - 2.0 * keepBack)
        {
            base = endValueAtOrBefore (0, ev[1].time);
            for (int i = 1; i < n; ++i) ev[(size_t) i - 1] = ev[(size_t) i];
            --n;
        }
        return evalAt (t);
    }

    double evalAt (double t) const
    {
        if (n == 0) return base;
        // último evento con time <= t
        int i = -1;
        for (int k = 0; k < n; ++k) if (ev[(size_t) k].time <= t) i = k;
        // ¿una rampa lineal en curso hacia el siguiente evento?
        const int next = i + 1;
        if (next < n && ev[(size_t) next].type == Type::Linear)
        {
            const auto& r = ev[(size_t) next];
            if (t >= r.startTime)
            {
                const double span = r.time - r.startTime;
                return span > 0 ? r.startValue + (r.value - r.startValue) * (t - r.startTime) / span : r.value;
            }
        }
        if (i < 0) return base;
        return valueOfEvent (i, t);
    }

    bool isConstant (double t) const
    {
        if (n == 0) return true;
        const auto& last = ev[(size_t) n - 1];
        if (t < last.time) return false;
        if (last.type == Type::Target) return std::abs (valueOfEvent (n - 1, t) - last.value) < 1e-9;
        return true;
    }

    void setKeepBack (double seconds) { keepBack = seconds; }

private:
    enum class Type { Set, Linear, Target };
    struct Ev { Type type; double time, value, tau, startTime, startValue; };

    void insert (const Ev& e)
    {
        if (n >= (int) ev.size())   // sin sitio: se descarta el más antiguo
        {
            base = endValueAtOrBefore (0, ev[1].time);
            for (int i = 1; i < n; ++i) ev[(size_t) i - 1] = ev[(size_t) i];
            --n;
        }
        int pos = n;
        while (pos > 0 && ev[(size_t) pos - 1].time > e.time) --pos;
        for (int i = n; i > pos; --i) ev[(size_t) i] = ev[(size_t) i - 1];
        ev[(size_t) pos] = e;
        ++n;
    }

    double valueOfEvent (int i, double t) const
    {
        const auto& e = ev[(size_t) i];
        if (e.type == Type::Target)
            return e.value + (e.startValue - e.value) * std::exp (-(t - e.time) / e.tau);
        return e.value;
    }

    double endValue (int i) const { return ev[(size_t) i].type == Type::Target ? ev[(size_t) i].startValue : ev[(size_t) i].value; }
    double endValueAtOrBefore (int i, double t) const { return valueOfEvent (i, t); }

    std::array<Ev, 16> ev {};
    int n = 0;
    double base = 0;
    double keepBack = 0.01;
};

//==============================================================================
// BiquadFilterNode (fórmulas de la especificación: Q en dB para paso bajo/alto)
class Biquad
{
public:
    enum class Type { Lowpass, Highpass, Bandpass };

    void setup (Type t, double freq, double q, double sampleRate)
    {
        type = t; Q = q; sr = sampleRate; curFreq = -1;
        setFrequency (freq);
    }

    void setFrequency (double f)
    {
        if (f == curFreq) return;
        curFreq = f;
        const double nyq = sr / 2;
        const double nf = std::clamp (f, 0.0, nyq) / nyq;
        double b0, b1, b2, a0, a1, a2;
        if (type == Type::Bandpass)
        {
            if (nf > 0 && nf < 1)
            {
                const double w0 = kPi * nf, alpha = std::sin (w0) / (2 * Q), c = std::cos (w0);
                b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            }
            else { b0 = b1 = b2 = 0; a0 = 1; a1 = a2 = 0; }
        }
        else if (type == Type::Lowpass)
        {
            if (nf >= 1) { b0 = 1; b1 = b2 = 0; a0 = 1; a1 = a2 = 0; }
            else
            {
                const double w0 = kPi * nf, alpha = std::sin (w0) / (2 * std::pow (10.0, Q / 20)), c = std::cos (w0);
                const double beta = (1 - c) / 2;
                b0 = beta; b1 = 2 * beta; b2 = beta; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            }
        }
        else
        {
            if (nf >= 1) { b0 = b1 = b2 = 0; a0 = 1; a1 = a2 = 0; }
            else if (nf <= 0) { b0 = 1; b1 = b2 = 0; a0 = 1; a1 = a2 = 0; }
            else
            {
                const double w0 = kPi * nf, alpha = std::sin (w0) / (2 * std::pow (10.0, Q / 20)), c = std::cos (w0);
                const double beta = (1 + c) / 2;
                b0 = beta; b1 = -2 * beta; b2 = beta; a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            }
        }
        B0 = b0 / a0; B1 = b1 / a0; B2 = b2 / a0; A1 = a1 / a0; A2 = a2 / a0;
    }

    double process (double x)
    {
        const double y = B0 * x + B1 * x1 + B2 * x2 - A1 * y1 - A2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        if (std::abs (y1) < 1e-30) y1 = 0;   // sin desnormales
        return y;
    }

    void reset() { x1 = x2 = y1 = y2 = 0; }

private:
    Type type = Type::Lowpass;
    double Q = 1, sr = 48000, curFreq = -1;
    double B0 = 1, B1 = 0, B2 = 0, A1 = 0, A2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

//==============================================================================
// WaveShaperNode con curva de 1024 puntos (interpolación lineal, como la especificación)
struct WaveShaper
{
    std::array<float, 1024> curve {};

    double process (double x) const
    {
        constexpr int N = 1024;
        const double v = (N - 1) / 2.0 * (x + 1);
        if (v <= 0) return curve[0];
        if (v >= N - 1) return curve[N - 1];
        const double k = std::floor (v), f = v - k;
        return (1 - f) * curve[(size_t) k] + f * curve[(size_t) k + 1];
    }
};

//==============================================================================
// Línea de retardo con lectura fraccionaria lineal (DelayNode)
class DelayLine
{
public:
    void setup (double maxSeconds, double sampleRate)
    {
        size = (int) std::ceil (maxSeconds * sampleRate) + 4;
        buf.assign ((size_t) size, 0.0);
        w = 0;
    }
    void write (double x) { buf[(size_t) w] = x; w = w + 1 == size ? 0 : w + 1; }
    // Leer ANTES de escribir la muestra actual: devuelve x[n - delaySamples]
    double read (double delaySamples) const
    {
        delaySamples = std::clamp (delaySamples, 1.0, (double) size - 3);
        double pos = (double) w - delaySamples;
        while (pos < 0) pos += size;
        const int i0 = (int) pos;
        const double f = pos - i0;
        const int i1 = i0 + 1 == size ? 0 : i0 + 1;
        return buf[(size_t) i0] * (1 - f) + buf[(size_t) i1] * f;
    }
    // Leer DESPUÉS de escribir la muestra actual: devuelve x[n - delaySamples] (admite 0)
    double readAfterWrite (double delaySamples) const
    {
        delaySamples = std::clamp (delaySamples, 0.0, (double) size - 3);
        double pos = (double) w - 1 - delaySamples;
        while (pos < 0) pos += size;
        const int i0 = (int) pos;
        const double f = pos - i0;
        const int i1 = i0 + 1 == size ? 0 : i0 + 1;
        return buf[(size_t) i0] * (1 - f) + buf[(size_t) i1] * f;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); }

private:
    std::vector<double> buf;
    int size = 1, w = 0;
};

//==============================================================================
// OscillatorNode "sawtooth": PolyBLEP normalizado (el mismo que web-audio-api-rs,
// con la normalización que iguala el volumen al de los navegadores)
struct SawOsc
{
    double phase = 0;
    // Chrome normaliza la onda limitada en banda a pico 1, lo que la deja ~1,4 dB por debajo
    // de un diente de sierra ideal. node-web-audio-api 2.x (la referencia de las pruebas) no
    // aplica ese ajuste, así que las pruebas lo ponen a 1.
    static inline double kNorm = 1.0 / 1.178979744472167;

    static double unroll (double p) { return p >= 1 ? p - 1 : (p < 0 ? p + 1 : p); }
    static double polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    double next (double freq, double sampleRate)
    {
        const double inc = freq / sampleRate;
        double out = 0;
        if (std::abs (freq) < sampleRate / 2)
        {
            const double p = unroll (phase + 0.5);
            out = (2.0 * p - 1.0 - polyBlep (p, inc)) * kNorm;
            phase = unroll (phase + inc);
        }
        else phase = phase + inc - std::floor (phase + inc);
        return out;
    }
};

//==============================================================================
// Convolución (ConvolverNode). La clase base permite que el plugin use la de JUCE,
// mucho más rápida; las pruebas usan esta, exacta y sin dependencias.
class ConvolverBase
{
public:
    virtual ~ConvolverBase() = default;
    // IR estéreo ya normalizada. Puede reservar memoria (no llamar desde el hilo de audio).
    virtual void setImpulse (const std::vector<float>& L, const std::vector<float>& R, double sampleRate) = 0;
    // procesa en el sitio (cualquier tamaño)
    virtual void process (float* L, float* R, int n) = 0;
    virtual void reset() = 0;
};

// Convolución por particiones uniformes de 128 muestras (FFT de 256), latencia cero
// cuando se procesa en bloques de 128.
class UniformConvolver : public ConvolverBase
{
public:
    void setImpulse (const std::vector<float>& L, const std::vector<float>& R, double) override
    {
        const int len = (int) std::max (L.size(), R.size());
        parts = std::max (1, (len + B - 1) / B);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& ir = ch == 0 ? L : R;
            irRe[ch].assign ((size_t) parts * (B + 1), 0.0);
            irIm[ch].assign ((size_t) parts * (B + 1), 0.0);
            for (int p = 0; p < parts; ++p)
            {
                std::vector<double> re ((size_t) N, 0.0), im ((size_t) N, 0.0);
                for (int i = 0; i < B; ++i)
                {
                    const int k = p * B + i;
                    if (k < (int) ir.size()) re[(size_t) i] = ir[(size_t) k];
                }
                fft (re, im, false);
                for (int k = 0; k <= B; ++k)
                {
                    irRe[ch][(size_t) (p * (B + 1) + k)] = re[(size_t) k];
                    irIm[ch][(size_t) (p * (B + 1) + k)] = im[(size_t) k];
                }
            }
            fdlRe[ch].assign ((size_t) parts * (B + 1), 0.0);
            fdlIm[ch].assign ((size_t) parts * (B + 1), 0.0);
            prevIn[ch].assign ((size_t) B, 0.0);
        }
        head = 0;
    }

    void process (float* L, float* R, int n) override
    {
        for (int off = 0; off < n; off += B)
        {
            const int m = std::min (B, n - off);
            for (int ch = 0; ch < 2; ++ch) block (ch == 0 ? L + off : R + off, ch, m);
            head = head == 0 ? parts - 1 : head - 1;
        }
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (fdlRe[ch].begin(), fdlRe[ch].end(), 0.0);
            std::fill (fdlIm[ch].begin(), fdlIm[ch].end(), 0.0);
            std::fill (prevIn[ch].begin(), prevIn[ch].end(), 0.0);
        }
    }

private:
    static constexpr int B = 128, N = 256;

    void block (float* x, int ch, int m)
    {
        // ventana [bloque anterior | bloque actual] → espectro guardado en la línea
        std::vector<double>& re = tmpRe; std::vector<double>& im = tmpIm;
        re.assign ((size_t) N, 0.0); im.assign ((size_t) N, 0.0);
        for (int i = 0; i < B; ++i) re[(size_t) i] = prevIn[ch][(size_t) i];
        for (int i = 0; i < m; ++i) re[(size_t) (B + i)] = x[i];
        for (int i = 0; i < m; ++i) prevIn[ch][(size_t) i] = x[i];
        for (int i = m; i < B; ++i) prevIn[ch][(size_t) i] = 0;
        fft (re, im, false);
        for (int k = 0; k <= B; ++k)
        {
            fdlRe[ch][(size_t) (head * (B + 1) + k)] = re[(size_t) k];
            fdlIm[ch][(size_t) (head * (B + 1) + k)] = im[(size_t) k];
        }
        std::vector<double> accRe ((size_t) (B + 1), 0.0), accIm ((size_t) (B + 1), 0.0);
        for (int p = 0; p < parts; ++p)
        {
            const int slot = (head + p) % parts;
            const double* fr = &fdlRe[ch][(size_t) (slot * (B + 1))];
            const double* fi = &fdlIm[ch][(size_t) (slot * (B + 1))];
            const double* hr = &irRe[ch][(size_t) (p * (B + 1))];
            const double* hi = &irIm[ch][(size_t) (p * (B + 1))];
            for (int k = 0; k <= B; ++k)
            {
                accRe[(size_t) k] += fr[k] * hr[k] - fi[k] * hi[k];
                accIm[(size_t) k] += fr[k] * hi[k] + fi[k] * hr[k];
            }
        }
        for (int k = 0; k <= B; ++k) { re[(size_t) k] = accRe[(size_t) k]; im[(size_t) k] = accIm[(size_t) k]; }
        for (int k = 1; k < B; ++k) { re[(size_t) (N - k)] = re[(size_t) k]; im[(size_t) (N - k)] = -im[(size_t) k]; }
        fft (re, im, true);
        for (int i = 0; i < m; ++i) x[i] = (float) re[(size_t) (B + i)];
    }

    static void fft (std::vector<double>& re, std::vector<double>& im, bool inverse)
    {
        const int n = (int) re.size();
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = 2 * kPi / len * (inverse ? 1 : -1);
            const double wr = std::cos (ang), wi = std::sin (ang);
            for (int i = 0; i < n; i += len)
            {
                double cr = 1, ci = 0;
                for (int k = 0; k < len / 2; ++k)
                {
                    const size_t a = (size_t) (i + k), b = (size_t) (i + k + len / 2);
                    const double tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                    const double ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
                }
            }
        }
        if (inverse) for (int i = 0; i < n; ++i) { re[(size_t) i] /= n; im[(size_t) i] /= n; }
    }

    int parts = 1, head = 0;
    std::vector<double> irRe[2], irIm[2], fdlRe[2], fdlIm[2], prevIn[2], tmpRe, tmpIm;
};

//==============================================================================
// DynamicsCompressorNode (algoritmo de web-audio-api-rs: detector de pico con ramas de
// ataque/relajación, ganancia de compensación y retardo de anticipación de ~6 ms)
class Compressor
{
public:
    void setup (double sampleRate, double thresholdDb, double kneeDb, double ratioV, double attackS, double releaseS)
    {
        sr = sampleRate; threshold = thresholdDb; knee = kneeDb; ratio = ratioV; attack = attackS; release = releaseS;
        const int ringQuanta = (int) std::ceil (sr * 0.006 / kQuantum) + 1;
        delay = (ringQuanta - 1) * kQuantum;
        bufL.assign ((size_t) std::max (1, delay), 0.0f); bufR = bufL;
        pos = 0; prevDetector = 0;
        attackTau = std::exp (-1.0 / (attack * sr));
        releaseTau = std::exp (-1.0 / (release * sr));
        const double th = knee > 0 ? threshold + knee / 2 : threshold;
        const double fullRangeGain = th + (-th / ratio);
        const double fullRangeMakeup = 1.0 / dbToLin (fullRangeGain);
        makeup = linToDb (std::pow (fullRangeMakeup, 0.6));
    }

    int latency() const { return delay; }

    void process (float& l, float& r)
    {
        const double th = knee > 0 ? threshold + knee / 2 : threshold;
        const double halfKnee = knee / 2;
        const double kneePartial = knee > 0 ? (1.0 / ratio - 1.0) / (2.0 * knee) : 0;
        const double mx = std::max (std::abs ((double) l), std::abs ((double) r));
        const double xdb = linToDb (mx);
        double ydb;
        if (xdb <= th - halfKnee) ydb = xdb;
        else if (xdb <= th + halfKnee) ydb = xdb + (xdb - th + halfKnee) * (xdb - th + halfKnee) * kneePartial;
        else ydb = th + (xdb - th) / ratio;
        const double att = xdb - ydb;
        const double det = att > prevDetector ? attackTau * prevDetector + (1 - attackTau) * att
                                              : releaseTau * prevDetector + (1 - releaseTau) * att;
        prevDetector = det;
        const double g = dbToLin (-det + makeup);

        // la ganancia calculada ahora se aplica a la señal retrasada
        const float dl = bufL[(size_t) pos], dr = bufR[(size_t) pos];
        bufL[(size_t) pos] = l; bufR[(size_t) pos] = r;
        pos = pos + 1 == (int) bufL.size() ? 0 : pos + 1;
        l = (float) (dl * g);
        r = (float) (dr * g);
    }

    void reset() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); prevDetector = 0; }

private:
    static double dbToLin (double v) { return std::pow (10.0, v / 20); }
    static double linToDb (double v) { return v == 0 ? -1000.0 : 20 * std::log10 (v); }

    double sr = 48000, threshold = -24, knee = 30, ratio = 12, attack = 0.003, release = 0.25;
    double attackTau = 0, releaseTau = 0, makeup = 0, prevDetector = 0;
    int delay = 0, pos = 0;
    std::vector<float> bufL, bufR;
};

// StereoPannerNode con pan fijo en los extremos (los únicos valores que usa el instrumento)
// Entrada mono: pan −1 → todo a la izquierda; +1 → todo a la derecha.
// Entrada estéreo: pan −1 → L+R a la izquierda; +1 → L+R a la derecha.
} // namespace wa
