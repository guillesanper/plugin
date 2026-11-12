// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"
#include "esceq/dsp/filter_design.h"

#include <array>

namespace esceq::dsp {

inline constexpr int kMaxChannels = 2;

/// Procesa audio con los coeficientes de una banda (BandDesign).
///
/// Garantías:
///  - process() y setDesign() son seguras en tiempo real.
///  - El resultado no depende de cómo se trocee el audio en bloques.
///  - Los coeficientes pueden cambiarse en cualquier momento, incluso cada
///    muestra, sin que el filtro se vuelva inestable (topología TPT/SVF).
///    El cambio es inmediato: suavizarlo es responsabilidad de quien llama.
///  - Una campana con 0 dB de ganancia deja pasar el audio bit a bit.
///  - Los canales son independientes entre sí.
///  - No es segura entre hilos: todas las llamadas deben venir del mismo hilo.
class BandFilter
{
public:
    /// Sustituye los coeficientes conservando el estado interno. Si cambia
    /// el número de secciones, el estado de las secciones nuevas se limpia.
    void setDesign(const BandDesign& design) noexcept ESCEQ_NONBLOCKING;

    /// Pone a cero el estado interno (silencio previo).
    void reset() noexcept ESCEQ_NONBLOCKING;

    /// Procesa en el sitio. `numChannels` debe ser <= kMaxChannels; los
    /// canales que excedan ese límite se dejan sin tocar.
    void process(float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING;

    const BandDesign& design() const noexcept { return design_; }

private:
    struct SectionState
    {
        double ic1 = 0.0;
        double ic2 = 0.0;
    };

    BandDesign design_{};
    std::array<std::array<SectionState, kMaxSections>, kMaxChannels> state_{};
};

} // namespace esceq::dsp
