// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/dsp/auto_gain.h"
#include "esceq/dsp/phase_equalizer.h"

#include <chrono>
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
    /// hace falta el resultado enseguida: al preparar el audio y al
    /// renderizar sin tiempo real. Bloquea (espera al trabajo en
    /// curso del hilo, si lo hay): nunca desde el hilo de audio en tiempo
    /// real. Se puede llamar desde cualquier hilo.
    void flush();

private:
    void run();
    /// Diseña y calcula hasta que no quede nada, y avisa si ha cambiado la
    /// latencia. Con serviceMutex_ tomado.
    void serviceAll();

    dsp::PhaseEqualizer& equalizer_;
    dsp::AutoGain& autoGain_;
    LatencyCallback onLatencyChange_;
    int seenLatency_;         ///< La última latencia avisada. Con serviceMutex_.
    std::mutex serviceMutex_; ///< serviceAll() solo desde un hilo a la vez.
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool stopping_ = false; ///< Con wakeMutex_.
    std::thread thread_;
};

} // namespace esceq::core
