// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/handoff.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/realtime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace esceq::dsp
{

/// El ecualizador completo con sus cuatro modos de fase (ver docs/dsp/fase-lineal.md).
///
///  - Natural: el Equalizer IIR tal cual. Latencia 0; suena bit a bit igual
///    que un Equalizer con los mismos ajustes.
///  - LinearLow, LinearMedium y LinearHigh: la parte estática de las bandas
///    va en un FIR de fase lineal (convolución particionada) y la dinámica,
///    en el Equalizer en modo solo dinámica, que no hace nada mientras la
///    dinámica no actúa. Latencia: latencySamples(mode, sampleRate).
///
/// Dos hilos:
///  - El hilo de audio llama a todo lo que está marcado como seguro en tiempo
///    real (setBand, process...). No asigna memoria ni bloquea.
///  - Un hilo de diseño, que no es de tiempo real, llama a serviceDesigns()
///    cada pocos milisegundos (y cuando haga falta el resultado ya, como al
///    renderizar sin tiempo real). Diseña los FIR y construye lo que cada
///    modo necesita, y se lo entrega al hilo de audio sin bloqueos
///    (TripleBuffer y Handoff).
///
/// Cambios:
///  - Ajustes de las bandas en fase lineal: cada FIR nuevo entra con un
///    fundido de un bloque de la convolución (de 5 a 21 ms). Mientras se
///    diseña, suena el anterior.
///  - Modo de fase: mientras se prepara el modo nuevo suena el anterior.
///    Después, la salida baja a silencio en kTransitionSeconds, se cambia de
///    modo, se espera a que el modo nuevo tenga su latencia de audio dentro,
///    y la salida vuelve a subir en kTransitionSeconds. latencySamples()
///    cambia en el momento del cambio.
///  - Bypass en fase lineal: fundido con la señal seca retrasada, para que
///    la latencia no cambie.
///
/// Garantías (además de las de Equalizer en Natural):
///  - Con parámetros constantes, el resultado no depende de cómo se trocee el
///    audio en bloques.
///  - En fase lineal, con todas las bandas desactivadas (o sin efecto) y con
///    el bypass completo, la salida es la entrada retrasada, bit a bit.
///  - Las muestras no finitas de la entrada y del sidechain cuentan como
///    silencio en fase lineal (no se quedan en la memoria del FIR).
///  - No es segura entre hilos salvo lo dicho: un hilo de audio y un hilo de diseño.
class PhaseEqualizer
{
public:
    static constexpr double kTransitionSeconds = 0.01;

    PhaseEqualizer();
    ~PhaseEqualizer();
    PhaseEqualizer(const PhaseEqualizer&) = delete;
    PhaseEqualizer& operator=(const PhaseEqualizer&) = delete;

    // --- Hilo de audio ---

    /// Fija la frecuencia de muestreo y los canales (1 o 2) y salta sin
    /// transición a los ajustes actuales. No es de tiempo real (libera lo que
    /// usaba el modo anterior). En fase lineal, hasta que llegue lo que diseña
    /// serviceDesigns() suena Natural: para empezar ya en el modo pedido,
    /// llamar a serviceDesigns() y después a update().
    void prepare(double sampleRate, int numChannels = 2);

    /// Nuevos ajustes objetivo para una banda. Índices fuera de rango se ignoran.
    void setBand(int index, const BandSettings& settings) noexcept ESCEQ_NONBLOCKING;
    void setBypassed(bool bypassed) noexcept ESCEQ_NONBLOCKING;
    void setPhaseMode(PhaseMode mode) noexcept ESCEQ_NONBLOCKING;

    /// Borra la memoria de los filtros, termina cualquier transición y salta
    /// sin transición a los ajustes objetivo.
    void reset() noexcept ESCEQ_NONBLOCKING;

    /// Recoge lo que haya entregado el hilo de diseño. process() lo hace al
    /// empezar; tras prepare() o reset(), y antes de procesar audio, lo
    /// recogido se instala sin transición.
    void update() noexcept ESCEQ_NONBLOCKING;

    /// Procesa en el sitio, como Equalizer::process(). Como mucho
    /// kMaxChannels canales; el resto no se toca.
    void process(float* const* channels, int numChannels, int numSamples, const float* const* sidechain = nullptr,
                 int numSidechainChannels = 0) noexcept ESCEQ_NONBLOCKING;

    /// Modo pedido con setPhaseMode().
    PhaseMode phaseMode() const noexcept { return requestedMode_; }
    /// Modo que suena ahora mismo (puede ir por detrás del pedido).
    PhaseMode activePhaseMode() const noexcept { return activeMode_; }

    /// Latencia en muestras del modo que suena. Se puede leer desde cualquier hilo.
    int latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }

    /// Ajustes objetivo de la banda (los últimos recibidos, saneados).
    const BandSettings& target(int index) const noexcept;

    /// Ganancia dinámica que se aplica ahora mismo a la banda, en dB.
    double dynamicGainDb(int index) const noexcept { return iir_.dynamicGainDb(index); }

    /// true si suena el modo pedido, con el FIR de los últimos ajustes, sin
    /// fundidos ni suavizados en curso.
    bool isSettled() const noexcept;

    // --- Hilo de diseño (no es de tiempo real; un solo hilo a la vez) ---

    /// Diseña y entrega lo que haga falta para los últimos ajustes pedidos, y
    /// destruye lo que el hilo de audio ya no usa. Asigna memoria y puede
    /// tardar decenas de milisegundos. true si ha entregado algo.
    bool serviceDesigns();

private:
    struct Kernel;
    struct LinearPath;

    /// Lo que el hilo de audio pide diseñar.
    struct DesignRequest
    {
        std::uint64_t generation = 0; ///< Cambia con todo lo que afecta al FIR.
        std::uint64_t epoch = 0;      ///< Cambia con el modo y la frecuencia de muestreo.
        PhaseMode mode = PhaseMode::Natural;
        double sampleRate = 48000.0;
        int numChannels = 2;
        std::array<BandSettings, kMaxBands> bands{};
    };

    enum class Stage
    {
        Steady,    ///< Suena el modo activo.
        FadingOut, ///< Bajando hacia el silencio para cambiar de modo; en 0, espera a que el modo nuevo esté listo.
        Priming,   ///< En silencio mientras el modo nuevo llena su latencia.
        FadingIn,  ///< Subiendo desde el silencio.
    };

    void publishRequest() noexcept;
    bool targetReady() const noexcept;
    void beginTransitionIfNeeded() noexcept;
    /// Pasa al modo pedido: retira lo anterior e instala lo nuevo.
    void switchMode() noexcept;
    void configureIir() noexcept;
    void takeKernels() noexcept;
    void takePaths() noexcept;
    /// Devuelve un camino que no va a sonar.
    void discardPath(std::unique_ptr<LinearPath> path) noexcept;
    void renderLinear(float* const* io, int numChannels, int length, const float* const* sidechain,
                      int numSidechainChannels) noexcept;
    /// Muestras que se pueden procesar seguidas sin cambiar de etapa ni de bloque.
    int segmentLength(int available) const noexcept;
    void applyTransition(float* const* io, int numChannels, int length) noexcept;

    // --- Estado del hilo de audio ---
    Equalizer iir_;
    std::array<BandSettings, kMaxBands> bands_{};
    PhaseMode requestedMode_ = PhaseMode::Natural;
    PhaseMode activeMode_ = PhaseMode::Natural;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    bool bypassed_ = false;
    bool fresh_ = true; ///< No ha pasado audio desde prepare() o reset().
    std::uint64_t generation_ = 1;
    std::uint64_t epoch_ = 1;
    std::unique_ptr<LinearPath> path_;     ///< El de activeMode_ (nullptr en Natural).
    std::unique_ptr<LinearPath> incoming_; ///< El del modo pedido, esperando a sonar.
    Stage stage_ = Stage::Steady;
    int stageRemaining_ = 0;
    float transitionGain_ = 1.0f;
    int fadeLength_ = 480;
    std::atomic<int> latency_{0};

    // --- Entre hilos ---
    TripleBuffer<DesignRequest> requests_;
    Handoff<LinearPath> paths_;
    Handoff<Kernel> kernels_;

    // --- Estado del hilo de diseño ---
    LinearPhaseDesigner designer_;
    DesignRequest latest_{};
    std::uint64_t builtEpoch_ = 0;
    std::uint64_t builtGeneration_ = 0;
};

} // namespace esceq::dsp
