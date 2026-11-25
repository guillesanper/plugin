// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/dsp/auto_gain.h"
#include "esceq/dsp/phase_equalizer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

/// El hilo de control de una instancia: hace fuera del hilo de audio lo que
/// este pide y no puede hacer él porque tarda. Cada kPeriod diseña los FIR de
/// fase lineal pendientes de un dsp::PhaseEqualizer (serviceDesigns()) y
/// calcula la compensación de ganancia pendiente de un dsp::AutoGain
/// (service()), y los entrega sin bloquear al hilo de audio. También avisa
/// cuando cambia la latencia del ecualizador, para que el plugin se lo diga
/// al host sin mirarla él periódicamente. Ver docs/dsp/fase-lineal.md,
/// docs/dsp/compensacion-de-ganancia.md y las ADR 0012, 0015 y 0021.
namespace esceq::core
{

class ControlThread
{
public:
    /// Cada cuánto mira el hilo si hay algo que hacer. Mirar cuesta dos
    /// lecturas atómicas; con un ajuste moviéndose, el trabajo nuevo empieza
    /// como mucho kPeriod después.
    static constexpr std::chrono::milliseconds kPeriod{10};

    enum class Threading
    {
        Background, ///< Un hilo propio (lo normal).
        Manual,     ///< Sin hilo: solo flush() trabaja (tests).
    };

    /// Recibe la latencia nueva, en muestras.
    using LatencyCallback = std::function<void(int latencySamples)>;

    /// `equalizer` y `autoGain` deben vivir más que este objeto.
    /// `onLatencyChange`, si se da, se llama una vez por cada cambio de
    /// dsp::PhaseEqualizer::latencySamples() respecto a la última vista (al
    /// construir, la de ese momento), desde el hilo propio o desde el que
    /// llame a flush(), nunca desde dos a la vez. Lo ve como mucho kPeriod
    /// después de que el hilo de audio cambie de modo. Sin cambios no se
    /// llama. Debe volver pronto y no llamar a este objeto.
    ControlThread(dsp::PhaseEqualizer& equalizer, dsp::AutoGain& autoGain, Threading threading = Threading::Background,
                  LatencyCallback onLatencyChange = {});
    /// Para el hilo y espera a que termine (como mucho, lo que tarde un diseño).
    ~ControlThread();

    ControlThread(const ControlThread&) = delete;
    ControlThread& operator=(const ControlThread&) = delete;

    /// Hace ya, en el hilo que llama, todo lo pendiente (y el aviso de
    /// latencia, si ha cambiado), y vuelve cuando está entregado. Para cuando
    /// hace falta el resultado enseguida, como al preparar el audio. Bloquea
    /// (espera al trabajo en curso del hilo, si lo hay): nunca desde el hilo
    /// de audio en tiempo real. Se puede llamar desde cualquier hilo. Después,
    /// renderBlock() hace lo pendiente en el bloque siguiente.
    void flush();

    /// Al renderizar sin tiempo real, antes de procesar cada bloque (en el
    /// hilo de audio, con los ajustes del bloque ya entregados al ecualizador):
    /// hace lo que flush(), pero como mucho una vez por periodo de audio,
    /// contado en muestras. El periodo es kPeriod o, si es más largo, el
    /// bloque de la convolución del modo pedido (un FIR nuevo no puede entrar
    /// más a menudo). Si ha pasado un periodo desde lo último que hizo (o es el
    /// primer bloque del render, o el primero tras flush()), lo pendiente se
    /// hace ya; si no, en el primer bloque en que se cumpla. Mientras se
    /// renderiza, el hilo propio no trabaja: el resultado depende solo de los
    /// ajustes de cada bloque y de cómo se trocea el audio, no de lo rápido
    /// que vaya. Bloquea como flush(). Ver docs/dsp/fase-lineal.md y la ADR 0028.
    void renderBlock(int numSamples, double sampleRate);

    /// En tiempo real, antes de procesar cada bloque: el hilo propio vuelve
    /// a trabajar si se estaba renderizando. Seguro en tiempo real.
    void realtimeBlock() noexcept ESCEQ_NONBLOCKING;

    /// Cuántas veces ha hecho algo (diseñar y entregar, o calcular la
    /// compensación), desde cualquier hilo. Para tests y esceq_bench.
    std::uint64_t serviceCount() const noexcept { return serviceCount_.load(std::memory_order_relaxed); }

private:
    void run();
    /// Diseña y calcula hasta que no quede nada, y avisa si ha cambiado la
    /// latencia. Con serviceMutex_ tomado. true si ha hecho algo.
    bool serviceAll();

    dsp::PhaseEqualizer& equalizer_;
    dsp::AutoGain& autoGain_;
    LatencyCallback onLatencyChange_;
    int seenLatency_;         ///< La última latencia avisada. Con serviceMutex_.
    std::mutex serviceMutex_; ///< serviceAll() solo desde un hilo a la vez.
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool stopping_ = false; ///< Con wakeMutex_.
    std::atomic<bool> rendering_{false}; ///< Se renderiza: el hilo propio no trabaja.
    /// Muestras renderizadas desde lo último que hizo renderBlock()
    /// (kDue: lo pendiente se hace en el bloque siguiente).
    std::atomic<std::int64_t> samplesSinceService_{kDue};
    std::atomic<std::uint64_t> serviceCount_{0};
    static constexpr std::int64_t kDue = std::int64_t{1} << 40;
    std::thread thread_;
};

} // namespace esceq::core
