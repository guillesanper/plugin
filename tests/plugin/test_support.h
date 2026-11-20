// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace esceq::test {

inline std::unique_ptr<PluginProcessor> makeProcessor()
{
    return std::unique_ptr<PluginProcessor>(static_cast<PluginProcessor*>(createPluginFilter()));
}

/// Deja correr el hilo de mensajes: temporizadores y avisos pendientes.
inline void pump(int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

/// Deja correr el hilo de mensajes hasta que `done()` o pasen `limitMs`
/// milisegundos. true si `done()`. El límite solo detecta un fallo: para algo
/// que ya está en la cola de mensajes, `done()` se cumple en la primera vuelta.
template <typename Done> bool pumpUntil(Done done, int limitMs = 10000)
{
    const auto limit = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(limitMs);
    while (!done())
    {
        if (juce::Time::getMillisecondCounter() > limit)
            return false;
        pump(1);
    }
    return true;
}

/// Tiempo suficiente para que el historial registre una acción.
inline void settle()
{
    pump(EditHistory::kCoalesceMs + 150);
}

inline float valueOf(PluginProcessor& p, std::size_t index)
{
    return p.currentValues()[index];
}

/// Lo que hace la interfaz al mover un control: un gesto con varios valores.
inline void drag(PluginProcessor& p, std::size_t index, std::initializer_list<float> values)
{
    auto& parameter = p.parameter(index);
    parameter.beginChangeGesture();
    for (const float value : values)
        parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
    parameter.endChangeGesture();
}

} // namespace esceq::test
