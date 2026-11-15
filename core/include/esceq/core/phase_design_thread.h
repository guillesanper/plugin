// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/dsp/phase_equalizer.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

/// El hilo de diseño de un dsp::PhaseEqualizer: llama a serviceDesigns()
/// cada kPeriod, fuera del hilo de audio, para que los FIR de fase lineal
/// se diseñen y se entreguen sin bloquearlo. Ver docs/dsp/fase-lineal.md y
/// docs/decisiones/0012-modos-de-fase.md.
namespace esceq::core
{

class PhaseDesignThread
{
public:
    /// Cada cuánto mira el hilo si hay algo que diseñar. Mirar cuesta una
    /// lectura atómica; con un ajuste moviéndose, el FIR nuevo empieza a
    /// diseñarse como mucho kPeriod después.
    static constexpr std::chrono::milliseconds kPeriod{10};

    enum class Threading
    {
        Background, ///< Un hilo propio (lo normal).
        Manual,     ///< Sin hilo: solo flush() diseña (tests).
    };

    /// `equalizer` debe vivir más que este objeto.
    explicit PhaseDesignThread(dsp::PhaseEqualizer& equalizer, Threading threading = Threading::Background);
    /// Para el hilo y espera a que termine (como mucho, lo que tarde un diseño).
    ~PhaseDesignThread();

    PhaseDesignThread(const PhaseDesignThread&) = delete;
    PhaseDesignThread& operator=(const PhaseDesignThread&) = delete;

    /// Diseña ya, en el hilo que llama, todo lo pendiente, y vuelve cuando
    /// está entregado. Para cuando hace falta el resultado enseguida: al
    /// preparar el audio y al renderizar sin tiempo real. Bloquea (espera al
    /// diseño en curso del hilo, si lo hay): nunca desde el hilo de audio en
    /// tiempo real. Se puede llamar desde cualquier hilo.
    void flush();

private:
    void run();
    /// Llama a serviceDesigns() hasta que no quede nada. Con serviceMutex_ tomado.
    void serviceAll();

    dsp::PhaseEqualizer& equalizer_;
    std::mutex serviceMutex_; ///< serviceDesigns() solo desde un hilo a la vez.
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool stopping_ = false; ///< Con wakeMutex_.
    std::thread thread_;
};

} // namespace esceq::core
