// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/handoff.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/realtime.h"
#include "esceq/dsp/spectral_dynamics.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace esceq::dsp
{

/// El ecualizador completo con sus cuatro modos de fase (ver docs/dsp/fase-lineal.md).
///
///  - IIR: el Equalizer tal cual. Latencia 0; suena bit a bit igual
///    que un Equalizer con los mismos ajustes.
///  - LinearLow, LinearMedium y LinearHigh: la parte estática de las bandas
///    va en un FIR de fase lineal (convolución particionada) y la dinámica,
///    en el Equalizer en modo solo dinámica, que no hace nada mientras la
///    dinámica no actúa. Latencia: latencySamples(mode, sampleRate).
///
/// Dinámica espectral (PLAN.md 5.7, ADR 0024): mientras alguna banda tenga
/// `dynamics.spectral`, después del IIR o del FIR suena un SpectralDynamics,
/// con el detector escuchando la entrada (y el sidechain) retrasada como el
/// audio. En las bandas espectrales, la máscara sustituye a la parte dinámica
/// del Equalizer; la estática sigue igual. La latencia del modo crece en
/// SpectralDynamics::latencySamples(). Sin ninguna banda espectral, todo es
/// bit a bit como si no existiera.
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
///  - Primera banda espectral encendida, o última apagada: como el cambio de
///    modo (silencio, cambio, la latencia nueva en silencio y subida). Las
///    demás bandas espectrales se encienden y se apagan sin cambiar la
///    latencia: la dinámica normal de la banda se funde (Equalizer) y la
///    máscara entra en la trama siguiente.
///  - Bypass en fase lineal o con dinámica espectral: fundido con la señal
///    seca retrasada, para que la latencia no cambie.
///
/// Garantías (además de las de Equalizer en IIR):
///  - Con parámetros constantes, el resultado no depende de cómo se trocee el
///    audio en bloques.
///  - En fase lineal, con todas las bandas desactivadas (o sin efecto) y sin
///    dinámica espectral, y con el bypass completo en cualquier modo, la
///    salida es la entrada retrasada, bit a bit.
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
    /// serviceDesigns() suena IIR: para empezar ya en el modo pedido,
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

    /// Latencia en muestras de lo que suena (el modo y, si la hay, la dinámica
    /// espectral). Se puede leer desde cualquier hilo.
    int latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }

    /// true si alguna banda pide la dinámica espectral (`dynamics.spectral`).
    bool spectralRequested() const noexcept { return requestedSpectral_; }
    /// true si suena la dinámica espectral (puede ir por detrás de lo pedido).
    bool spectralActive() const noexcept { return activeSpectral_; }
    /// El motor de la dinámica espectral, para leer sus máscaras (en el hilo de audio).
    const SpectralDynamics& spectralDynamics() const noexcept;

    /// Ajustes objetivo de la banda (los últimos recibidos, saneados).
    const BandSettings& target(int index) const noexcept;

    /// Ganancia dinámica que se aplica ahora mismo a la banda, en dB. En una
    /// banda espectral que suena, la del bin que más cambia
    /// (SpectralDynamics::gainDb()).
    double dynamicGainDb(int index) const noexcept;

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
    struct SpectralPath;

    /// Lo que el hilo de audio pide diseñar.
    struct DesignRequest
    {
        std::uint64_t generation = 0; ///< Cambia con todo lo que afecta al FIR.
        std::uint64_t epoch = 0;      ///< Cambia con el modo y la frecuencia de muestreo.
        PhaseMode mode = PhaseMode::Iir;
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
    /// Pasa los ajustes de la banda al Equalizer y a la dinámica espectral,
    /// según suene o no la dinámica espectral.
    void routeBand(int index) noexcept;
    /// Lo que suena no es lo pedido (el modo o la dinámica espectral).
    bool switchPending() const noexcept;
    bool targetReady() const noexcept;
    void beginTransitionIfNeeded() noexcept;
    /// Pasa al modo pedido y a la dinámica espectral pedida: retira lo
    /// anterior e instala lo nuevo.
    void switchMode() noexcept;
    void resetSpectral() noexcept;
    void configureIir() noexcept;
    void takeKernels() noexcept;
    void takePaths() noexcept;
    /// Devuelve un camino que no va a sonar.
    void discardPath(std::unique_ptr<LinearPath> path) noexcept;
    void renderLinear(float* const* io, int numChannels, int length, const float* const* sidechain,
                      int numSidechainChannels) noexcept;
    /// La dinámica espectral sobre la salida del ecualizador, con su bypass.
    void renderSpectral(float* const* io, int numChannels, int length, const float* const* sidechain,
                        int numSidechainChannels, const float* const* detectorInput) noexcept;
    /// Muestras que se pueden procesar seguidas sin cambiar de etapa ni de bloque.
    int segmentLength(int available) const noexcept;
    void applyTransition(float* const* io, int numChannels, int length) noexcept;

    // --- Estado del hilo de audio ---
    Equalizer iir_;
    std::array<BandSettings, kMaxBands> bands_{};
    PhaseMode requestedMode_ = PhaseMode::Iir;
    PhaseMode activeMode_ = PhaseMode::Iir;
    bool requestedSpectral_ = false;
    bool activeSpectral_ = false;
    std::unique_ptr<SpectralPath> spectral_; ///< Siempre existe; su memoria se reserva en prepare().
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    bool bypassed_ = false;
    bool fresh_ = true; ///< No ha pasado audio desde prepare() o reset().
    std::uint64_t generation_ = 1;
    std::uint64_t epoch_ = 1;
    std::unique_ptr<LinearPath> path_;     ///< El de activeMode_ (nullptr en IIR).
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
