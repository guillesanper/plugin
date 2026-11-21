// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"
#include "esceq/dsp/band_filter.h"
#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/fft.h"

#include <array>
#include <complex>
#include <memory>
#include <span>
#include <vector>

namespace esceq::dsp {

/// Dinámica espectral (PLAN.md 5.7, ADR 0024): dentro de la zona de cada
/// banda, baja o sube solo las frecuencias que pasan del umbral, con los
/// controles de la dinámica de la banda. Derivación, decisiones y medidas en
/// docs/dsp/dinamica-espectral.md.
///
/// Una sola STFT para todas las bandas: N = 2048 puntos hasta 50 kHz (×2 hasta
/// 100 kHz, ×4 hasta 200 kHz: sampleRateFactor()), salto de N/4 y ventana raíz
/// de Hann en el análisis y en la síntesis. Por salto, una FFT compleja de la
/// señal (L en la parte real y R en la imaginaria), una del detector interno y
/// otra del sidechain (solo si alguna banda las usa) y una inversa, sean
/// cuantas sean las bandas.
///
/// Por cada banda activa (ver isActive()):
///  - Detector: el nivel de cada bin es la energía de la fuente (la entrada,
///    `detectorInput` o el sidechain) en 1/6 de octava a su alrededor (3 bins
///    como mínimo), con ventana de Hann y calibrada para que un seno lea su
///    nivel de pico, en el canal de la banda (Stereo: el máximo de L y R).
///  - Envolvente por bin, por tramas, con el ataque y la relajación de la banda
///    (o los tiempos automáticos de la frecuencia de cada bin, ADR 0016), con
///    un salto como mínimo, y la curva de ganancia de la sección 4.2 (la de
///    dynamicGainDb(), calculada en float).
///  - La ganancia de cada bin se multiplica por la magnitud del filtro del
///    detector de la banda (designDetector()), normalizada a 1 en su máximo.
/// Las ganancias del mismo dominio (L/R: Stereo, Left y Right; M/S: Mid y
/// Side) se suman en dB y se aplican: primero L/R y luego M/S.
///
/// Latencia: N muestras (latencySamples()). Por debajo del umbral, o sin
/// bandas activas, la salida es la entrada retrasada N muestras (con el error
/// de redondeo de la FFT, por debajo de −120 dB).
///
/// Aquí no hay fundidos: un cambio de ajustes actúa en la trama siguiente y
/// el solapamiento de las tramas lo funde en N muestras. Al activarse una
/// banda, sus envolventes empiezan en cero.
///
/// Garantías:
///  - prepare() asigna memoria y no es segura en tiempo real; el resto sí.
///  - Con los mismos ajustes, el resultado no depende de cómo se trocee el
///    audio en bloques.
///  - Un NaN o un infinito en la señal anula las cuatro tramas que lo
///    contienen (la salida baja a su alrededor, menos de 2N muestras) y no
///    estropea nada; en una fuente del detector, se escucha silencio en esas
///    tramas y las envolventes de las bandas que la escuchan vuelven a cero.
///  - No es segura entre hilos: todas las llamadas deben venir del mismo hilo.
class SpectralDynamics
{
public:
    static constexpr int kBaseSize = 2048;
    static constexpr int kHopDivisor = 4;
    /// Ancho del detector de cada bin, en octavas.
    static constexpr double kDetectorOctaves = 1.0 / 6.0;

    /// Tamaño de la STFT a esa frecuencia de muestreo.
    static int fftSize(double sampleRate) noexcept;
    /// Latencia, en muestras: el tamaño de la STFT.
    static int latencySamples(double sampleRate) noexcept { return fftSize(sampleRate); }

    SpectralDynamics();
    ~SpectralDynamics();
    SpectralDynamics(const SpectralDynamics&) = delete;
    SpectralDynamics& operator=(const SpectralDynamics&) = delete;

    /// Reserva la memoria para esa frecuencia de muestreo y empieza de cero
    /// (como reset()), con los ajustes de las bandas que ya tuviera. Asigna
    /// memoria: no es segura en tiempo real.
    void prepare(double sampleRate);

    /// Nuevos ajustes para una banda (se sanean con sanitizeBand()). La banda
    /// actúa si está encendida, con la dinámica activada y en un tipo que la
    /// admite (supportsDynamics()); el canal decide qué escucha su detector y
    /// dónde se aplica, como en Equalizer. Quien la use decide qué bandas son
    /// espectrales: aquí todas las que actúan lo son. Índices fuera de rango
    /// se ignoran. Segura en tiempo real.
    void setBand(int index, const BandSettings& settings) noexcept ESCEQ_NONBLOCKING;

    /// Borra la memoria (la entrada, la salida y las envolventes). Segura en tiempo real.
    void reset() noexcept ESCEQ_NONBLOCKING;

    /// Procesa en el sitio, con un retardo de latencySamples(). Como mucho
    /// kMaxChannels canales; el resto no se toca. En mono, Stereo, Left y Mid
    /// procesan el único canal; Right y Side no hacen nada.
    ///
    /// `sidechain` (opcional, `numSidechainChannels` canales) es lo que escuchan
    /// las bandas con detección externa; sin él, no reciben señal. Una fuente
    /// mono se escucha tal cual, sea cual sea el canal de la banda.
    /// `detectorInput` (opcional, `numChannels` canales) es lo que escuchan los
    /// detectores internos en lugar de `channels` (la entrada del ecualizador).
    /// Antes de la primera llamada hay que llamar a prepare().
    void process(float* const* channels, int numChannels, int numSamples,
                 const float* const* sidechain = nullptr, int numSidechainChannels = 0,
                 const float* const* detectorInput = nullptr) noexcept ESCEQ_NONBLOCKING;

    double sampleRate() const noexcept { return sampleRate_; }
    int size() const noexcept { return size_; }
    int hopSize() const noexcept { return size_ / kHopDivisor; }
    int latency() const noexcept { return size_; }
    /// Bins de la mitad positiva del espectro: size()/2 + 1.
    int numBins() const noexcept { return size_ / 2 + 1; }
    double binFrequencyHz(int bin) const noexcept { return sampleRate_ * bin / size_; }

    /// true si la banda actúa (ver setBand()).
    bool isActive(int index) const noexcept;
    /// true si alguna banda actúa.
    bool anyActive() const noexcept;

    /// Ganancia de la banda en el bin que más cambia en la última trama, en dB
    /// y con signo (0 si la banda no actúa): el medidor de la banda.
    double gainDb(int index) const noexcept;
    /// Ganancia de cada bin de la banda en la última trama, en dB (numBins()
    /// valores; vacía si la banda no actúa o fuera de rango): la forma de la máscara.
    std::span<const float> binGainsDb(int index) const noexcept;

    /// Tramas calculadas desde prepare() o reset(). Para los tests.
    long long framesProcessed() const noexcept { return frames_; }

private:
    using Complex = std::complex<float>;

    /// Dónde se escucha y dónde se aplica: los dos canales, cada uno, la suma o la diferencia.
    enum Domain
    {
        kLeft,
        kRight,
        kMid,
        kSide,
        kNumDomains,
        kStereo = kNumDomains, ///< Solo para el detector: el máximo de L y R.
        kNumLevels,
    };
    enum Source
    {
        kInternal,
        kExternal,
        kNumSources,
    };

    struct Band
    {
        BandSettings settings{};
        bool active = false;
        bool running = false; ///< Sus envolventes llevan la cuenta (false: empiezan de cero).
        FilterParams weightFor{};
        bool weightValid = false;
        float attack = 0.0f;  ///< Coeficientes por trama de los tiempos fijos.
        float release = 0.0f;
        float kneeStart = 0.0f; ///< Nivel lineal donde empieza la rodilla: por debajo, ganancia 0.
        double gainDb = 0.0;
        std::vector<float> envelope;
        std::vector<float> slowEnvelope;
        std::vector<float> gains;
        std::vector<float> weight;
    };

    void updateBandConstants(Band& band) noexcept;
    void pushSamples(float* const* channels, int numChannels, int offset, int length, const float* const* sidechain,
                     int numSidechainChannels, const float* const* detectorInput) noexcept;
    void processFrame(int numChannels) noexcept;
    void analyseSource(Source source, int numSourceChannels, const std::array<bool, kNumLevels>& needed) noexcept;
    void runBand(Band& band, int numChannels) noexcept;
    void updateWeight(Band& band) noexcept;
    void applyMasks(int numChannels) noexcept;
    /// Dominio en el que se aplica la banda (kNumDomains: no hace nada).
    static int applyDomain(ChannelMode mode, int numChannels) noexcept;
    /// Nivel que escucha la banda en una fuente con ese número de canales.
    static int levelFor(ChannelMode mode, int numSourceChannels) noexcept;

    double sampleRate_ = 48000.0;
    int size_ = 0;
    int bins_ = 0;
    std::unique_ptr<Fft> fft_;

    std::vector<float> analysisWindow_; ///< Raíz de Hann (análisis y síntesis).
    std::vector<float> detectorWindow_; ///< Hann.
    std::vector<int> detectorLow_;      ///< Primer y último bin del detector de cada bin.
    std::vector<int> detectorHigh_;
    std::vector<double> tangent_;       ///< responseTangent() de cada bin.
    std::vector<float> autoAttack_;     ///< Coeficientes por trama de los tiempos automáticos de cada bin.
    std::vector<float> autoRelease_;
    std::vector<float> autoSlow_;

    // Señal, fuentes del detector y salida: anillos de size_ muestras.
    std::array<std::vector<float>, kMaxChannels> input_;
    std::array<std::array<std::vector<float>, kMaxChannels>, kNumSources> sources_;
    std::array<int, kNumSources> sourceChannels_{};
    std::array<std::vector<float>, kMaxChannels> output_;
    int position_ = 0; ///< Posición en los anillos.
    int hopPhase_ = 0; ///< Muestras desde la última trama.
    long long frames_ = 0;

    std::vector<Complex> spectrum_;  ///< La señal de la trama.
    std::vector<Complex> detector_;  ///< Una fuente del detector de la trama.
    std::vector<double> prefix_;     ///< Sumas acumuladas de la energía de un nivel.
    std::array<std::vector<float>, kNumLevels> power_;
    std::array<std::array<std::vector<float>, kNumLevels>, kNumSources> levels_;
    std::array<bool, kNumSources> sourceReset_{}; ///< La fuente trajo un no finito en esta trama.
    std::array<std::vector<float>, kNumDomains> masks_; ///< dB, y luego lineal.
    std::vector<float> effective_; ///< La envolvente de la banda en la trama (con la etapa lenta).
    std::array<bool, kNumDomains> maskUsed_{};

    std::array<Band, kMaxBands> bands_{};
};

} // namespace esceq::dsp
