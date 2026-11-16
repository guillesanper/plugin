// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"

#include <array>
#include <complex>

/// Diseño de filtros de ecualización: de parámetros de usuario a coeficientes,
/// y de coeficientes a respuesta en frecuencia.
///
/// Derivaciones y referencias en docs/dsp/filtros.md.
namespace esceq::dsp {

/// CONTRATO: los valores numéricos se guardan en archivos de referencia y,
/// más adelante, en el estado. Los tipos nuevos se añaden al final.
enum class FilterType
{
    Bell,      ///< Campana: realza o atenúa alrededor de la frecuencia.
    LowShelf,  ///< Realza o atenúa por debajo de la frecuencia.
    HighShelf, ///< Realza o atenúa por encima de la frecuencia.
    LowCut,    ///< Paso alto: elimina graves. Usa `slopeDbPerOct`.
    HighCut,   ///< Paso bajo: elimina agudos. Usa `slopeDbPerOct`.
    Notch,     ///< Elimina una banda estrecha.
    BandPass,  ///< Deja pasar solo alrededor de la frecuencia (0 dB en el centro).
    AllPass,   ///< No cambia la magnitud; gira la fase 360° alrededor de la frecuencia.
    Tilt,      ///< Inclina el espectro: -gainDb en graves, +gainDb en agudos.
};

/// Cómo se pasa del prototipo analógico de cada tipo al filtro digital.
/// Ver docs/dsp/filtros.md y docs/decisiones/0013-correccion-en-agudos.md.
///
/// CONTRATO: los valores numéricos se guardan en el estado. Los algoritmos
/// nuevos se añaden al final, y los que hay no cambian de sonido.
enum class FilterAlgorithm
{
    /// Transformada bilineal con la frecuencia predeformada: el de la 0.1.
    /// Cerca de Nyquist estrecha campanas y shelves y hunde los cortes.
    Classic,
    /// Magnitud ajustada a la del prototipo analógico (M. Vicanek): polos
    /// por invariancia al impulso y magnitud igual a la analógica en
    /// continua, en Nyquist y en la frecuencia de cada sección.
    Matched,
};

/// Número de algoritmos de FilterAlgorithm.
inline constexpr int kNumFilterAlgorithms = 2;

inline constexpr double kButterworthQ = 0.70710678118654752440;
inline constexpr double kMinFrequencyHz = 1.0;
inline constexpr double kMaxFrequencyRatio = 0.49; ///< Fracción máxima de la frecuencia de muestreo.
inline constexpr double kMinQ = 0.025;
inline constexpr double kMaxQ = 40.0;
inline constexpr double kMaxGainDb = 60.0;
inline constexpr int kMinSlopeDbPerOct = 6;
inline constexpr int kMaxSlopeDbPerOct = 96;

/// Pendiente válida más cercana: entre 6 y 96 dB/oct, en múltiplos de 6.
constexpr int sanitizeSlope(int slopeDbPerOct) noexcept
{
    const int clamped = slopeDbPerOct < kMinSlopeDbPerOct   ? kMinSlopeDbPerOct
                        : slopeDbPerOct > kMaxSlopeDbPerOct ? kMaxSlopeDbPerOct
                                                            : slopeDbPerOct;
    return ((clamped + 3) / 6) * 6;
}

/// true si `gainDb` influye en el sonido para ese tipo: campana, shelves y tilt.
constexpr bool filterUsesGain(FilterType type) noexcept
{
    return type == FilterType::Bell || type == FilterType::LowShelf || type == FilterType::HighShelf ||
           type == FilterType::Tilt;
}

/// Parámetros de una banda tal y como los entiende el usuario.
/// Los valores fuera de rango se ajustan al diseñar; nunca producen
/// coeficientes inestables.
struct FilterParams
{
    FilterType type = FilterType::Bell;
    double frequencyHz = 1000.0;
    double gainDb = 0.0;      ///< Bell, shelves y Tilt. Ignorada en el resto.
    double q = kButterworthQ; ///< En LowCut/HighCut controla la resonancia en el corte.
    int slopeDbPerOct = 12;   ///< Solo LowCut/HighCut: 6, 12, ..., 96.
    FilterAlgorithm algorithm = FilterAlgorithm::Classic;

    friend bool operator==(const FilterParams&, const FilterParams&) = default;
};

/// Una sección de primer o segundo orden de un filtro de variable de estado
/// con integradores trapezoidales (TPT/SVF).
///
/// Salida de orden 2: m0·x + m1·v1 + m2·v2  (v1 = paso banda, v2 = paso bajo).
/// Salida de orden 1: m0·x + m2·lp.
struct SectionCoeffs
{
    int order = 2;
    double g = 1.0; ///< tan(π·f/fs), ya corregida por la ganancia en los shelves.
    double k = 1.0; ///< Amortiguamiento, 1/Q. No se usa en orden 1.
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

/// 96 dB/oct = orden 16 = 8 secciones de segundo orden.
inline constexpr int kMaxSections = 8;

/// Coeficientes completos de una banda: una cascada de secciones.
struct BandDesign
{
    std::array<SectionCoeffs, kMaxSections> sections{};
    int numSections = 0;
};

/// Calcula los coeficientes de una banda con el algoritmo de `params`.
/// Segura en tiempo real (no asigna memoria). Classic usa tan() y pow(), unos
/// 40 ns; Matched añade exp(), expm1(), sin() y sqrt() (coste medido en
/// docs/dsp/filtros.md). El ecualizador la llama una vez por tramo de 32
/// muestras, y en las bandas dinámicas hasta una vez por muestra (con
/// designBandPrepared()).
BandDesign designBand(const FilterParams& params, double sampleRate) noexcept ESCEQ_NONBLOCKING;

/// Lo que solo depende de la frecuencia de una banda y de la de muestreo,
/// con la frecuencia ajustada al mismo rango que en designBand().
struct PreparedFrequency
{
    double g = 0.0;     ///< tan(π·f/fs): Classic.
    double omega = 0.0; ///< 2π·f/fs, en rad/muestra: Matched.
};

PreparedFrequency prepareFrequency(double frequencyHz, double sampleRate) noexcept ESCEQ_NONBLOCKING;

/// Como designBand(), pero con `frequency` ya calculada con prepareFrequency()
/// para la frecuencia de `params`. El resultado es idéntico bit a bit; sirve
/// para no recalcular la tangente cuando solo cambia la ganancia (la dinámica).
BandDesign designBandPrepared(const FilterParams& params,
                              const PreparedFrequency& frequency) noexcept ESCEQ_NONBLOCKING;

/// Respuesta compleja exacta del filtro digital en `frequencyHz`, con
/// cualquier algoritmo. Coincide con lo que mide BandFilter, incluida la
/// deformación cerca de Nyquist de Classic. Segura en tiempo real.
std::complex<double> response(const BandDesign& design, double frequencyHz,
                              double sampleRate) noexcept ESCEQ_NONBLOCKING;

/// Magnitud de response() en decibelios.
double magnitudeDb(const BandDesign& design, double frequencyHz, double sampleRate) noexcept ESCEQ_NONBLOCKING;

/// tan(ω/2) para evaluar la respuesta en `frequencyHz` (ajustada a [0, 0,4999·fs]).
/// Solo depende de la frecuencia y de la frecuencia de muestreo: al evaluar
/// muchas bandas en las mismas frecuencias (la curva de la interfaz) se
/// calcula una vez por frecuencia y no una por banda.
double responseTangent(double frequencyHz, double sampleRate) noexcept ESCEQ_NONBLOCKING;

/// Como response() y magnitudeDb(), con `tangent` calculada por
/// responseTangent(). El resultado es idéntico bit a bit.
std::complex<double> responseAtTangent(const BandDesign& design, double tangent) noexcept ESCEQ_NONBLOCKING;
double magnitudeDbAtTangent(const BandDesign& design, double tangent) noexcept ESCEQ_NONBLOCKING;

} // namespace esceq::dsp
