// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/handoff.h"
#include "esceq/dsp/realtime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

/// Compensación automática de ganancia: la ganancia que deja igual la
/// sonoridad de ruido rosa al pasar por la curva estática de las bandas.
/// Derivación, rejilla y medidas en docs/dsp/compensacion-de-ganancia.md;
/// decisión en docs/decisiones/0015-compensacion-automatica-de-ganancia.md.
namespace esceq::dsp {

/// La compensación nunca pasa de ±kAutoGainLimitDb.
inline constexpr double kAutoGainLimitDb = 12.0;
/// Banda de frecuencias que se integra (el límite superior baja a
/// kMaxFrequencyRatio·fs si la frecuencia de muestreo no llega).
inline constexpr double kAutoGainLowHz = 20.0;
inline constexpr double kAutoGainHighHz = 20000.0;

/// |K(f)|²: la ponderación K de ITU-R BS.1770 (el filtro de shelf más el
/// paso alto "RLB", con los coeficientes que da la norma a 48 kHz), en
/// potencia, en `frequencyHz` (de 0 a 24 kHz). No depende de la frecuencia de
/// muestreo del audio. Segura en tiempo real y entre hilos.
double kWeightingPower(double frequencyHz) noexcept ESCEQ_NONBLOCKING;

/// Compensación, en dB, que deja igual la sonoridad de ruido rosa al pasar
/// por la parte estática de `bands` (en serie, como en el ecualizador):
///
///     G = −10·log10( ∫ |K|²·P  /  ∫ |K|² )   en log(f), de 20 Hz a 20 kHz
///
/// P es la potencia de la curva: |H|² en mono y con bandas Stereo; con
/// bandas en L, R, M o S, la media de la potencia de los dos canales de
/// salida para dos canales de entrada incorrelados de la misma potencia.
/// Solo cuenta la ganancia estática (la de la banda, sin la dinámica), y no
/// cuentan las bandas desactivadas ni las que no cambian la magnitud (pasa
/// todo, y campana, shelves y tilt a 0 dB). Sin ninguna banda que cuente, el
/// resultado es exactamente 0. Se limita a ±kAutoGainLimitDb.
///
/// `numChannels`: 1 o 2 (en mono cuentan las bandas Stereo, Left y Mid,
/// como en el ecualizador). Los ajustes se sanean como en el ecualizador.
///
/// No asigna memoria ni bloquea, pero tarda de 0,02 a 5 ms según las bandas
/// (docs/dsp/compensacion-de-ganancia.md): no llamar desde el hilo de audio
/// en tiempo real. Segura entre hilos (no tiene estado).
double autoGainDb(std::span<const BandSettings> bands, double sampleRate, int numChannels) noexcept;

/// La curva estática de `bands` que integra autoGainDb(), en dB, en cada
/// frecuencia de `frequenciesHz`: 10·log10 P con la misma regla de canales y
/// las mismas bandas que cuentan. Sin ninguna que cuente, exactamente 0 en
/// todos los puntos. Escribe min(tamaños) puntos de `outDb`. La usa la
/// igualación (curve_match.h) para ajustar lo que falta sobre las bandas
/// actuales. No asigna memoria; segura entre hilos. No es de tiempo real.
void staticCurveDb(std::span<const BandSettings> bands, double sampleRate, int numChannels,
                   std::span<const double> frequenciesHz, std::span<double> outDb) noexcept;

/// Entrega de la compensación entre el hilo de audio, que pide calcularla, y
/// un hilo de control, que la calcula con autoGainDb() y la publica.
///
///  - Hilo de audio: request() cuando cambian las bandas y latestDb() para
///    leer el último resultado. restart() descarta lo calculado hasta ahora.
///  - Hilo de control: service() calcula la última petición, si hay una nueva.
///
/// Un solo hilo de cada lado. Sin bloqueos ni memoria dinámica.
class AutoGain
{
public:
    AutoGain() = default;
    AutoGain(const AutoGain&) = delete;
    AutoGain& operator=(const AutoGain&) = delete;

    // --- Hilo de audio ---

    /// Pide la compensación de `bands` (como mucho kMaxBands). Copia los
    /// ajustes: unos 3 kB.
    void request(std::span<const BandSettings> bands, double sampleRate, int numChannels) noexcept ESCEQ_NONBLOCKING;

    /// Lo calculado antes de esta llamada ya no vale: latestDb() devuelve
    /// nullopt hasta que se calcule una petición posterior. Para empezar de
    /// cero al encender la compensación.
    void restart() noexcept ESCEQ_NONBLOCKING;

    /// Resultado de la petición más reciente ya calculada, posterior al
    /// último restart(), o nullopt si no hay ninguna. No espera a que se
    /// calcule la última: mientras tanto, devuelve la anterior.
    std::optional<float> latestDb() const noexcept ESCEQ_NONBLOCKING;

    // --- Hilo de control ---

    /// Calcula la última petición si no se había calculado ya. Devuelve true
    /// si ha calculado algo. Tarda lo que autoGainDb().
    bool service() noexcept;

private:
    struct Request
    {
        std::array<BandSettings, kMaxBands> bands{};
        int numBands = 0;
        double sampleRate = 48000.0;
        int numChannels = 2;
        std::uint32_t sequence = 0;
    };

    TripleBuffer<Request> requests_;
    std::uint32_t nextSequence_ = 1; ///< Solo el hilo de audio.
    std::uint32_t validFrom_ = 1;    ///< Solo el hilo de audio.
    /// Número de la petición (32 bits altos) y resultado (los bits del float).
    /// 0: nada calculado.
    std::atomic<std::uint64_t> result_{0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
};

} // namespace esceq::dsp
