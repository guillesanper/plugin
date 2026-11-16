// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"
#include "esceq/dsp/filter_design.h"

#include <algorithm>

/// Piezas de la dinámica por banda: curva de ganancia, seguidor de
/// envolvente y filtro del detector. El ecualizador (equalizer.h) las
/// combina. Derivación y decisiones en docs/dsp/dinamica.md.
namespace esceq::dsp {

inline constexpr double kMinAttackMs = 0.05;
inline constexpr double kMinReleaseMs = 1.0;

// Tiempos automáticos (ADR 0016): ligados al periodo de la frecuencia de la banda.
inline constexpr double kAutoAttackPeriods = 2.0;
inline constexpr double kAutoMinAttackMs = 0.5;
inline constexpr double kAutoMaxAttackMs = 50.0;
inline constexpr double kAutoReleasePeriods = 8.0;
inline constexpr double kAutoMinReleaseMs = 20.0;
inline constexpr double kAutoSlowReleaseFactor = 10.0;
inline constexpr double kAutoMaxSlowReleaseMs = 1000.0;

/// Ajustes de la dinámica de una banda.
///
/// La ganancia dinámica se suma a la ganancia estática de la banda. Con
/// `rangeDb` negativo la banda comprime (baja cuando el nivel supera el
/// umbral); con `rangeDb` positivo expande hacia arriba (sube).
struct DynamicSettings
{
    bool enabled = false;
    double thresholdDb = -24.0; ///< Nivel de pico del detector, en dBFS.
    double rangeDb = -6.0;      ///< Máxima ganancia dinámica, con signo.
    double kneeDb = 6.0;        ///< Anchura de la rodilla suave (0 = rodilla dura).
    double attackMs = 5.0;
    double releaseMs = 100.0;
    bool external = false; ///< true: el detector escucha el sidechain externo.
    /// true: ataque y relajación salen de la frecuencia de la banda
    /// (autoEnvelopeTimes()) y `attackMs` y `releaseMs` no se usan.
    bool autoTiming = false;

    friend bool operator==(const DynamicSettings&, const DynamicSettings&) = default;
};

/// Tiempos de un EnvelopeFollower, en milisegundos.
struct EnvelopeTimes
{
    double attackMs = 5.0;
    double releaseMs = 100.0;
    /// Relajación de la etapa lenta; 0 (o menos) la quita. Ver EnvelopeFollower.
    double slowReleaseMs = 0.0;

    friend bool operator==(const EnvelopeTimes&, const EnvelopeTimes&) = default;
};

/// Tiempos automáticos para una banda de frecuencia `frequencyHz` (ADR 0016):
///
///     ataque          = 2 periodos, de 0,5 a 50 ms
///     relajación      = 8 periodos, 20 ms como mínimo
///     relajación lenta = 10 veces la rápida, 1 s como máximo (nunca menos que la rápida)
///
/// Una frecuencia no finita o no positiva cuenta como 1 kHz.
/// Segura en tiempo real (no asigna; calcula unas divisiones).
EnvelopeTimes autoEnvelopeTimes(double frequencyHz) noexcept ESCEQ_NONBLOCKING;

/// true si la dinámica tiene efecto con ese tipo de filtro: solo los tipos
/// que tienen ganancia, filterUsesGain() (campana, shelves y tilt; ADR 0006).
bool supportsDynamics(FilterType type) noexcept;

/// Curva de ganancia: ganancia dinámica en dB para un nivel de detector.
///
/// El exceso sobre el umbral, redondeado por la rodilla, se traslada 1:1 a
/// la ganancia hasta agotar el rango:
///
///     exceso = nivel − umbral
///     y = 0                          si exceso ≤ −rodilla/2
///     y = (exceso + rodilla/2)² / (2·rodilla)   dentro de la rodilla
///     y = exceso                     si exceso ≥ rodilla/2
///     ganancia = signo(rango) · min(y, |rango|)
///
/// Segura en tiempo real.
double dynamicGainDb(double levelDb, double thresholdDb, double rangeDb, double kneeDb) noexcept ESCEQ_NONBLOCKING;

/// Filtro que alimenta al detector de una banda (ver docs/dsp/dinamica.md):
///  - Campana: paso banda con la frecuencia y el Q de la banda.
///  - Shelf de graves: paso bajo de 12 dB/oct en la frecuencia de la banda.
///  - Shelf de agudos: paso alto de 12 dB/oct.
///  - Tilt y el resto: sin filtro (banda completa).
/// Con el algoritmo de filtro de la banda (`params.algorithm`).
/// Segura en tiempo real.
BandDesign designDetector(const FilterParams& params, double sampleRate) noexcept ESCEQ_NONBLOCKING;

/// Seguidor de envolvente de pico con ataque y relajación separados
/// ("branching peak detector", Giannoulis, Massberg y Reiss, 2012).
///
/// Trabaja en amplitud lineal. Un escalón de nivel se sigue con una
/// constante de tiempo de `attackMs` al subir y de `releaseMs` al bajar.
///
/// Etapa lenta (opcional, `slowReleaseMs` > 0; tiempos automáticos, ADR
/// 0016): un filtro de un polo, con esa constante de tiempo al subir y al
/// bajar, promedia la envolvente rápida, y la salida es la mayor de las
/// dos. Un transitorio apenas la carga y se suelta con la relajación rápida;
/// una señal sostenida la carga y se suelta despacio (no bombea). Sin la
/// etapa lenta, el cálculo es exactamente el de arriba.
///
/// Garantías: process() es segura en tiempo real y no depende de cómo se
/// trocee el audio. No es segura entre hilos.
class EnvelopeFollower
{
public:
    EnvelopeFollower() noexcept { prepare(48000.0); }

    void prepare(double sampleRate) noexcept;

    /// Cambia los tiempos sin que el nivel salte: al poner la etapa lenta,
    /// empieza en el nivel actual; al quitarla, la envolvente sigue desde el
    /// nivel combinado. Recalcula coeficientes solo si los tiempos cambian.
    /// Segura en tiempo real.
    void setTimes(const EnvelopeTimes& times) noexcept ESCEQ_NONBLOCKING;

    void reset() noexcept
    {
        envelope_ = 0.0;
        slowEnvelope_ = 0.0;
    }

    /// Avanza una muestra con el nivel (valor absoluto) de entrada y
    /// devuelve la envolvente.
    double process(double level) noexcept ESCEQ_NONBLOCKING
    {
        const double coeff = level > envelope_ ? attack_ : release_;
        envelope_ = level + coeff * (envelope_ - level);
        if (!slowStage_)
            return envelope_;
        slowEnvelope_ = envelope_ + slowRelease_ * (slowEnvelope_ - envelope_);
        return std::max(envelope_, slowEnvelope_);
    }

    double value() const noexcept { return slowStage_ ? std::max(envelope_, slowEnvelope_) : envelope_; }

private:
    double coefficient(double ms) const noexcept;

    double sampleRate_ = 48000.0;
    EnvelopeTimes times_{};
    double attack_ = 0.0;
    double release_ = 0.0;
    double slowRelease_ = 0.0;
    bool slowStage_ = false;
    double envelope_ = 0.0;
    double slowEnvelope_ = 0.0;
};

} // namespace esceq::dsp
