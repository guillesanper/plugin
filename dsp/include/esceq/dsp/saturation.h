// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/band_filter.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/realtime.h"

#include <array>
#include <cstdint>

namespace esceq::dsp {

/// Curva de la saturación (PLAN.md 5.8, ADR 0026).
enum class SaturationMode : std::uint8_t
{
    Off,  ///< La etapa no se ejecuta.
    Soft, ///< tanh(d·x): simétrica, solo armónicos impares.
    Warm, ///< tanh(d·x + b) − tanh(b): asimétrica, añade los pares.
};

/// Filtros del sobremuestreo.
enum class OversamplingFilter : std::uint8_t
{
    Iir,         ///< Polifásicos IIR pasa todo: sin latencia y con fase no lineal (el modo de fase IIR).
    LinearPhase, ///< FIR de fase lineal, con latencia (los modos lineales).
};

/// Sobremuestreo por 2, 4 u 8 en etapas ×2 de media banda (docs/dsp/saturacion.md).
///
/// Cada etapa es un filtro de media banda, el mismo al subir y al bajar:
///  - IIR: dos ramas de secciones pasa todo de primer orden en z⁻²
///    (Valenzuela y Constantinides, 1983), con coeficientes elípticos. La
///    primera etapa, 8 coeficientes: plana hasta 0,41·fs y −103 dB desde
///    0,59·fs. Las siguientes, 6: plana hasta 0,59·fs y −105 dB desde 1,41·fs
///    en la segunda (fs, la frecuencia de muestreo de fuera); la tercera usa
///    los mismos, con las bandas al doble, que bastan (docs/dsp/saturacion.md).
///    Sin latencia: el retardo de grupo, de unas pocas muestras, depende de la
///    frecuencia.
///  - Fase lineal: FIR de media banda de 71 coeficientes (−106 dB) y de 31
///    (−110 dB) en las siguientes, con las mismas bandas. Subir y bajar
///    retrasan latency() muestras exactas.
/// Con la frecuencia de muestreo no cambia nada: las bandas son proporcionales a fs.
///
/// Garantías:
///  - No asigna memoria: todo es segura en tiempo real.
///  - El resultado no depende de cómo se trocee el audio en bloques.
///  - Cada canal (hasta kMaxChannels) lleva su memoria; no es segura entre hilos.
class Oversampler
{
public:
    static constexpr int kMaxStages = 3;
    static constexpr int kMaxFactor = 1 << kMaxStages;
    static constexpr int kIirFirstCoefficients = 8;
    static constexpr int kIirCoefficients = 6;
    static constexpr int kFirFirstHalfLength = 18; ///< Coeficientes distintos de la rama impar: (71 + 1) / 4.
    static constexpr int kFirHalfLength = 8;       ///< (31 + 1) / 4.

    /// Latencia de subir y bajar, en muestras de fuera: 0 en IIR; en fase
    /// lineal, 35 con una etapa, 43 con dos y 47 con tres (el retardo de los
    /// FIR, redondeado hacia arriba con un retardo de una fracción de muestra).
    static int latencySamples(OversamplingFilter filter, int stages) noexcept;

    /// Filtro y etapas (1 a kMaxStages; fuera de rango se limita), y empieza de cero. Segura en tiempo real.
    void setup(OversamplingFilter filter, int stages) noexcept ESCEQ_NONBLOCKING;
    /// Borra la memoria. Segura en tiempo real.
    void reset() noexcept ESCEQ_NONBLOCKING;

    OversamplingFilter filter() const noexcept { return filter_; }
    int stages() const noexcept { return stages_; }
    int factor() const noexcept { return 1 << stages_; }
    int latency() const noexcept { return latencySamples(filter_, stages_); }

    /// Sube `numSamples` muestras (como mucho kMaxBlock) de cada uno de
    /// `numChannels` canales (como mucho kMaxChannels) de `input` a
    /// numSamples·factor() en `output`.
    void upsample(const float* const* input, float* const* output, int numChannels,
                  int numSamples) noexcept ESCEQ_NONBLOCKING;
    /// Baja numSamples·factor() muestras de cada canal de `input` a
    /// `numSamples` (como mucho kMaxBlock) en `output`. `input` se usa de
    /// memoria de trabajo.
    void downsample(float* const* input, float* const* output, int numChannels,
                    int numSamples) noexcept ESCEQ_NONBLOCKING;

    /// Muestras de fuera por llamada.
    static constexpr int kMaxBlock = 64;

    /// false si la memoria de algún filtro tiene un NaN o un infinito.
    bool isFinite() const noexcept;

private:
    /// Memoria de una rama IIR: la entrada anterior y la salida anterior de cada sección.
    using IirBranch = std::array<float, kIirFirstCoefficients / 2 + 1>;
    struct IirStage
    {
        IirBranch even{};
        IirBranch odd{};
        float oddOutput = 0.0f; ///< Al bajar: la salida de la rama impar de la muestra anterior.
    };
    /// Coeficientes de la rama par del FIR más largo (2K).
    static constexpr int kMaxFirTaps = 2 * kFirFirstHalfLength;
    /// Memoria de un FIR, la muestra más antigua primero: 2K − 1 muestras
    /// (al subir y las pares al bajar) o K (las impares al bajar).
    struct FirStage
    {
        std::array<float, kMaxFirTaps> up{};
        std::array<float, kMaxFirTaps> downEven{};
        std::array<float, kMaxFirTaps> downOdd{};
    };
    /// Retardo que redondea la latencia de la fase lineal, a la frecuencia de dentro.
    static constexpr int kMaxPad = kMaxFactor;
    struct Channel
    {
        std::array<IirStage, kMaxStages> iirUp{};
        std::array<IirStage, kMaxStages> iirDown{};
        std::array<FirStage, kMaxStages> fir{};
        std::array<float, kMaxPad> pad{};
        int padPosition = 0;
    };
    /// Muestras de cada canal que entran como mucho en una etapa (o en cada rama al bajar).
    static constexpr int kMaxStageInput = kMaxBlock * kMaxFactor / 2;

    template <int NumChannels>
    void iirUp(int stage, const float* const* input, float* const* output, int numSamples) noexcept;
    template <int NumChannels>
    void iirDown(int stage, const float* const* input, float* const* output, int numSamples) noexcept;
    void firUp(int channel, int stage, const float* input, float* output, int numSamples) noexcept;
    void firDown(int channel, int stage, const float* input, float* output, int numSamples) noexcept;
    /// Retardo de los FIR al subir y al bajar con `stages` etapas, en muestras de dentro.
    static int firDelay(int stages) noexcept;
    int padLength() const noexcept;

    OversamplingFilter filter_ = OversamplingFilter::Iir;
    int stages_ = 3;
    std::array<Channel, kMaxChannels> channels_{};
    std::array<std::array<float, kMaxBlock * kMaxFactor>, kMaxChannels> scratch_{};
    std::array<float, kMaxFirTaps + kMaxStageInput> work_{};    ///< La memoria y la entrada de un FIR, seguidas.
    std::array<float, kMaxFirTaps + kMaxStageInput> workOdd_{}; ///< Las impares al bajar.
    std::array<float, kMaxStageInput> sums_{};
};

/// Saturación global (PLAN.md 5.8, ADR 0026): una curva suave con
/// sobremuestreo, compensación del drive y mezcla con la señal seca.
/// Derivación, decisiones y medidas en docs/dsp/saturacion.md.
///
/// Sobremuestreo hasta unos 384 kHz (stagesFor()): ×8 hasta 50 kHz, ×4 hasta
/// 100 kHz y ×2 por encima. Por cada muestra de dentro:
///   húmeda = bloqueador(c · (tanh(d·x + b) − tanh(b)))
///   salida = x + mezcla · (húmeda − x)
/// con d el drive en lineal, b = 0 en Soft y kWarmBias en Warm, c la
/// compensación del drive y un bloqueador de continua de un polo a
/// kDcBlockerHz (en los dos modos: en Soft no quita nada a una entrada sin
/// continua). La señal seca pasa por los mismos filtros de sobremuestreo, así
/// que mezclar no hace un filtro de peine en agudos: con la mezcla a 0, la
/// salida es la entrada por esos filtros.
///
/// Compensación: c = 1 / g, con g la ganancia que la curva da a un seno de
/// kCalibrationDbfs (su valor eficaz, sin la continua), calculada con
/// kCalibrationPoints puntos de un periodo al cambiar el drive o la curva
/// (compensationGain()). Así ese seno sale al nivel al que entra, con
/// cualquier drive.
///
/// Cambios: el drive, la compensación, la mezcla y la polarización (Soft ↔
/// Warm) van con una rampa lineal de kRampSeconds (el drive y la
/// compensación, en dB, para que el nivel no se pase durante la rampa). Encender o apagar (desde
/// Off o hacia Off) es un fundido de kRampSeconds con la entrada tal cual; con
/// Off, terminado el fundido, process() no toca nada. Cambiar el filtro
/// (setFilter()) borra la memoria y cambia la latencia de golpe: quien lo use
/// lo hace en silencio, como el cambio de modo de fase. Por la misma razón, en
/// fase lineal, encender y apagar cambian la latencia que hay que comunicar
/// (0 con Off) y el fundido con la entrada sin retrasar solo vale bajo ese silencio.
///
/// Garantías:
///  - No asigna memoria: todo es seguro en tiempo real.
///  - Con Off (y el fundido terminado), process() no toca las muestras.
///  - Con los mismos ajustes, el resultado no depende de cómo se trocee el
///    audio en bloques.
///  - Un NaN o un infinito en la entrada silencia ese tramo (como mucho
///    Oversampler::kMaxBlock muestras) y borra la memoria: no se queda en los filtros.
///  - No es segura entre hilos: todas las llamadas deben venir del mismo hilo.
class Saturation
{
public:
    static constexpr double kMaxDriveDb = 24.0;
    static constexpr double kDefaultDriveDb = 6.0;
    static constexpr double kWarmBias = 0.25;
    static constexpr double kDcBlockerHz = 5.0;
    static constexpr double kRampSeconds = 0.02;
    static constexpr double kCalibrationDbfs = -18.0;
    static constexpr int kCalibrationPoints = 256;

    /// Etapas ×2 del sobremuestreo a esa frecuencia de muestreo: 3 hasta
    /// 50 kHz, 2 hasta 100 kHz y 1 por encima (3 − log2(sampleRateFactor()), 1 como mínimo).
    static int stagesFor(double sampleRate) noexcept;

    /// Latencia con ese filtro, en muestras: 0 en IIR; en fase lineal, 47
    /// hasta 50 kHz (0,98 ms a 48 kHz), 43 hasta 100 kHz y 35 por encima.
    static int latencySamples(OversamplingFilter filter, double sampleRate) noexcept
    {
        return Oversampler::latencySamples(filter, stagesFor(sampleRate));
    }

    /// Ganancia lineal que la curva, con ese drive (en dB, de 0 a kMaxDriveDb),
    /// da a un seno de kCalibrationDbfs, en valor eficaz y sin la continua. 1 con Off.
    static double compensationGain(SaturationMode mode, double driveDb) noexcept ESCEQ_NONBLOCKING;

    Saturation() noexcept;

    /// Frecuencia de muestreo: el sobremuestreo, las rampas y el bloqueador de continua. Empieza
    /// de cero (como reset()) y deja los ajustes sin rampa. No asigna memoria.
    void prepare(double sampleRate) noexcept;

    /// Curva. Desde Off o hacia Off, con fundido; entre Soft y Warm, con rampa.
    void setMode(SaturationMode mode) noexcept ESCEQ_NONBLOCKING;
    /// Drive en dB (se limita a [0, kMaxDriveDb]; un no finito no cambia nada).
    /// Calcula la compensación si cambia (unos microsegundos).
    void setDriveDb(double driveDb) noexcept ESCEQ_NONBLOCKING;
    /// Mezcla con la señal seca, de 0 a 1 (se limita; un no finito no cambia nada).
    void setMix(double mix) noexcept ESCEQ_NONBLOCKING;
    /// Filtros del sobremuestreo. Si cambia, borra la memoria y cambia latency().
    void setFilter(OversamplingFilter filter) noexcept ESCEQ_NONBLOCKING;

    /// Borra la memoria de los filtros y termina las rampas y los fundidos. Segura en tiempo real.
    void reset() noexcept ESCEQ_NONBLOCKING;

    /// Procesa en el sitio. Como mucho kMaxChannels canales; el resto no se toca.
    void process(float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING;

    SaturationMode mode() const noexcept { return mode_; }
    OversamplingFilter filter() const noexcept { return oversampler_.filter(); }
    /// false con Off y el fundido terminado: process() no hace nada.
    bool isActive() const noexcept { return mode_ != SaturationMode::Off || fade_.value > 0.0; }
    /// Latencia de los filtros con el filtro actual (la de latencySamples()), se use o no.
    int latency() const noexcept { return oversampler_.latency(); }

private:
    /// Un valor que va hacia su objetivo en línea recta en rampSamples_ muestras.
    struct Ramp
    {
        double value = 0.0;
        double target = 0.0;
        double step = 0.0;
        int remaining = 0;

        void jump(double v) noexcept
        {
            value = target = v;
            step = 0.0;
            remaining = 0;
        }
        void moveTo(double v, int samples) noexcept;
        void advance() noexcept
        {
            if (remaining > 0 && --remaining == 0)
                value = target;
            else if (remaining > 0)
                value += step;
        }
    };

    void updateCompensation() noexcept;
    void processChunk(float* const* channels, int numChannels, int offset, int length) noexcept;
    void clearMemory() noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960;
    double dcCoefficient_ = 0.0;

    SaturationMode mode_ = SaturationMode::Off;
    SaturationMode curve_ = SaturationMode::Soft; ///< La última curva que sonó (con Off, la del fundido).
    double driveDb_ = kDefaultDriveDb;
    Ramp drive_;        ///< En dB: con la compensación en dB, su producto va de un extremo al otro sin pasarse.
    Ramp compensation_; ///< En dB.
    Ramp mix_;
    Ramp bias_;
    Ramp fade_; ///< 1: suena la saturación; 0: la entrada tal cual.
    /// Los valores lineales de las rampas, calculados solo cuando cambian.
    struct Cache
    {
        double bias = 0.0;
        float biasTanh = 0.0f;
        double driveDb = -1.0;
        float drive = 0.0f;
        double compensationDb = 1.0e9;
        float compensation = 0.0f;
    };
    Cache cache_;

    Oversampler oversampler_;
    struct DcBlocker
    {
        double input = 0.0;
        double output = 0.0;
    };
    std::array<DcBlocker, kMaxChannels> dcBlockers_{};

    /// Por muestra de fuera del tramo: drive, compensación, mezcla, polarización, su tanh y fundido.
    struct Frame
    {
        float drive, compensation, mix, bias, biasTanh, fade;
    };
    std::array<Frame, Oversampler::kMaxBlock> frames_{};
    using Inner = std::array<float, Oversampler::kMaxBlock * Oversampler::kMaxFactor>;
    std::array<Inner, kMaxChannels> oversampled_{};
    std::array<Inner, kMaxChannels> wet_{};
    std::array<std::array<float, Oversampler::kMaxBlock>, kMaxChannels> dry_{};
};

} // namespace esceq::dsp
