// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"

#include <optional>
#include <string>
#include <string_view>

/// Comportamiento de los controles (potenciómetros, barras), sin dependencias
/// gráficas: cómo se convierte un gesto en un valor, qué arco se dibuja y
/// cómo se edita el valor como texto. ui/src solo pinta y reenvía los gestos.
namespace esceq::ui {

/// Píxeles de arrastre que recorren todo el rango de un control.
inline constexpr float kDragPixelsForFullRange = 200.0f;
/// Con Mayús, el arrastre, la rueda y las teclas avanzan esta fracción.
inline constexpr float kFineFactor = 0.1f;
/// Un paso de rueda (delta 1.0 de JUCE son ~10 muescas) y de tecla, en fracción del rango.
inline constexpr float kWheelRangePerUnit = 0.5f;
inline constexpr float kKeyStep = 0.01f;

/// Nueva posición 0..1 tras arrastrar `deltaPixels` (positivo = hacia arriba
/// o hacia la derecha) desde `normalized`. Se acumula en coma flotante y no
/// se redondea: así un arrastre lento no se pierde.
float dragNormalized(float normalized, float deltaPixels, bool fine) noexcept;

/// Valor tras un movimiento de rueda `wheelDelta` (el de JUCE: positivo = subir).
/// Los parámetros continuos se mueven en su escala (logarítmica o lineal); las
/// opciones e interruptores avanzan una posición por movimiento.
float wheelValue(const core::ParameterSpec& spec, float value, float wheelDelta, bool fine) noexcept;

/// Valor tras pulsar `steps` veces una tecla de flecha (negativo = bajar).
float keyStepValue(const core::ParameterSpec& spec, float value, int steps, bool fine) noexcept;

/// true si el parámetro tiene un cero central con sentido (ganancia, rango):
/// continuo, lineal y con el 0 dentro del rango, sin estar en un extremo.
bool isBipolar(const core::ParameterSpec& spec) noexcept;

/// Ángulos del potenciómetro, en radianes, medidos desde las 12 en sentido
/// horario: el recorrido va de -kArcHalfSpan a +kArcHalfSpan.
inline constexpr float kArcHalfSpan = 2.35619449f; // 135°

struct ValueArc
{
    float from = 0.0f;      ///< Inicio del arco de valor.
    float to = 0.0f;        ///< Fin del arco de valor (puede ser menor que `from`).
    float indicator = 0.0f; ///< Ángulo del indicador: la posición del valor.
};

/// Arco de valor: desde el mínimo en los parámetros normales y desde el cero
/// en los bipolares, hasta la posición del valor.
ValueArc valueArc(const core::ParameterSpec& spec, float value) noexcept;

/// Fracciones 0..1 equivalentes para una barra horizontal (`from` y `to` ordenadas).
struct ValueSpan
{
    float from = 0.0f;
    float to = 0.0f;
    float position = 0.0f;
};
ValueSpan valueSpan(const core::ParameterSpec& spec, float value) noexcept;

/// Texto con el que se abre el campo de edición de un valor.
std::string editText(const core::ParameterSpec& spec, float value);

/// Valor resultante de confirmar `text` en el campo de edición, o nullopt si
/// no se entiende (el control conserva su valor). Usa core::parseValue.
std::optional<float> commitText(const core::ParameterSpec& spec, std::string_view text);

/// Texto de ayuda del selector del modo de fase, con la latencia que el modo
/// que suena añade: "Phase mode · latency 26.7 ms (1280 samples)", o
/// "Phase mode · no latency".
std::string phaseModeTooltip(int latencySamples, double sampleRate);

} // namespace esceq::ui
