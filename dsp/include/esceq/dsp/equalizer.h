// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/band_filter.h"
#include "esceq/dsp/filter_design.h"

#include <array>

namespace esceq::dsp {

inline constexpr int kMaxBands = 24;

/// Ajustes de una banda del ecualizador.
struct BandSettings
{
    bool enabled = false;
    FilterParams filter{};

    friend bool operator==(const BandSettings&, const BandSettings&) = default;
};

/// Ecualizador de hasta kMaxBands bandas en serie, con transiciones sin
/// chasquidos.
///
/// Cómo trata los cambios de parámetros (ver docs/dsp/ecualizador.md):
///  - Frecuencia, ganancia y Q se suavizan (en escala logarítmica o en dB)
///    y los coeficientes se recalculan cada kChunkSize muestras.
///  - Activar o desactivar una banda, o cambiar su tipo o su pendiente, hace
///    un fundido de kCrossfadeSeconds entre el filtro anterior y el nuevo.
///  - El bypass hace un fundido entre la señal seca y la procesada.
///
/// Garantías:
///  - process() y setBand() son seguras en tiempo real.
///  - Con parámetros constantes, el resultado no depende de cómo se trocee
///    el audio en bloques.
///  - Con todas las bandas desactivadas el audio pasa bit a bit.
///  - No es segura entre hilos: todas las llamadas deben venir del mismo hilo.
class Equalizer
{
public:
    static constexpr int kChunkSize = 32;
    static constexpr double kSmoothingSeconds = 0.015;
    static constexpr double kCrossfadeSeconds = 0.01;

    /// Fija la frecuencia de muestreo y salta sin transición a los ajustes actuales.
    void prepare(double sampleRate) noexcept;

    /// Nuevos ajustes objetivo para una banda. Índices fuera de rango se ignoran.
    void setBand(int index, const BandSettings& settings) noexcept;

    void setBypassed(bool bypassed) noexcept;

    /// Borra la memoria de los filtros y salta sin transición a los ajustes objetivo.
    void reset() noexcept;

    /// Procesa en el sitio. Como mucho kMaxChannels canales; el resto no se toca.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    /// Ajustes objetivo (los últimos recibidos, ya saneados).
    const BandSettings& target(int index) const noexcept;

    /// true si no hay ningún suavizado ni fundido en curso.
    bool isSettled() const noexcept;

private:
    struct Band
    {
        BandSettings target{};
        BandSettings current{}; ///< Valores que suenan ahora mismo.
        BandFilter active;
        BandFilter previous;    ///< Configuración anterior durante un fundido.
        int fadeRemaining = 0;
    };

    void processChunk(float* const* channels, int numChannels, int offset, int length) noexcept;
    void processBand(Band& band, float* const* channels, int numChannels, int length) noexcept;
    void smoothBand(Band& band) noexcept;
    void snapBand(Band& band) noexcept;

    std::array<Band, kMaxBands> bands_{};
    double sampleRate_ = 48000.0;
    double smoothingCoeff_ = 0.0;
    int fadeLength_ = 480;
    int chunkPhase_ = 0;

    bool bypassed_ = false;
    float wet_ = 1.0f;

    std::array<std::array<float, kChunkSize>, kMaxChannels> scratch_{};
    std::array<std::array<float, kChunkSize>, kMaxChannels> dry_{};
};

} // namespace esceq::dsp
