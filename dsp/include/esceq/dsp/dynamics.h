// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/filter_design.h"

/// Piezas de la dinámica por banda: curva de ganancia, seguidor de
/// envolvente y filtro del detector. El ecualizador (equalizer.h) las
/// combina. Derivación y decisiones en docs/dsp/dinamica.md.
namespace esceq::dsp {

inline constexpr double kMinAttackMs = 0.05;
inline constexpr double kMinReleaseMs = 1.0;

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

    friend bool operator==(const DynamicSettings&, const DynamicSettings&) = default;
};

/// true si la dinámica tiene efecto con ese tipo de filtro: solo los tipos
/// que tienen ganancia (campana, shelves y tilt).
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
double dynamicGainDb(double levelDb, double thresholdDb, double rangeDb, double kneeDb) noexcept;

/// Filtro que alimenta al detector de una banda (ver docs/dsp/dinamica.md):
///  - Campana: paso banda con la frecuencia y el Q de la banda.
///  - Shelf de graves: paso bajo de 12 dB/oct en la frecuencia de la banda.
///  - Shelf de agudos: paso alto de 12 dB/oct.
///  - Tilt y el resto: sin filtro (banda completa).
/// Segura en tiempo real.
BandDesign designDetector(const FilterParams& params, double sampleRate) noexcept;

/// Seguidor de envolvente de pico con ataque y relajación separados
/// ("branching peak detector", Giannoulis, Massberg y Reiss, 2012).
///
/// Trabaja en amplitud lineal. Un escalón de nivel se sigue con una
/// constante de tiempo de `attackMs` al subir y de `releaseMs` al bajar.
///
/// Garantías: process() es segura en tiempo real y no depende de cómo se
/// trocee el audio. No es segura entre hilos.
class EnvelopeFollower
{
public:
    EnvelopeFollower() noexcept { prepare(48000.0); }

    void prepare(double sampleRate) noexcept;

    /// Cambia los tiempos sin alterar el nivel actual (no produce saltos).
    void setTimes(double attackMs, double releaseMs) noexcept;

    void reset() noexcept { envelope_ = 0.0; }

    /// Avanza una muestra con el nivel (valor absoluto) de entrada.
    double process(double level) noexcept
    {
        const double coeff = level > envelope_ ? attack_ : release_;
        envelope_ = level + coeff * (envelope_ - level);
        return envelope_;
    }

    double value() const noexcept { return envelope_; }

private:
    double coefficient(double ms) const noexcept;

    double sampleRate_ = 48000.0;
    double attackMs_ = 5.0;
    double releaseMs_ = 100.0;
    double attack_ = 0.0;
    double release_ = 0.0;
    double envelope_ = 0.0;
};

} // namespace esceq::dsp
