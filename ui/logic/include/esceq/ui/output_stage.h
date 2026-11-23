// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/saturation.h"

#include <string>
#include <string_view>

/// Cajón de la etapa de salida (PLAN.md 5.8, ADR 0027), sin dependencias
/// gráficas: la saturación (modo, drive y mezcla), la ganancia de salida y
/// la compensación automática, en el orden de la señal. Su sitio en la
/// ventana está en esceq/ui/layout.h (outputDrawerLayout).
namespace esceq::ui {

/// Títulos del cajón y de la sección de la saturación.
inline constexpr std::string_view kOutputStageTitle = "Output";
inline constexpr std::string_view kSaturationTitle = "Saturation";

/// Si el drive y la mezcla influyen en el sonido: fuera de Off. Con Off se
/// atenúan, pero se pueden editar (y conservan su valor).
bool saturationAmountActive(dsp::SaturationMode mode) noexcept;

/// Latencia que añade la saturación fuera de Off con el modo de fase
/// `phase`, en milisegundos (dsp::PhaseEqualizer::saturationLatency: 47
/// muestras, 0,98 ms, a 48 kHz en fase lineal; 0 en IIR).
double saturationLatencyMs(dsp::PhaseMode phase, double sampleRate) noexcept;

/// Texto de ayuda del selector Off/Soft/Warm: qué hace y, en fase lineal, la
/// latencia que añade a la frecuencia de muestreo actual ("Saturation · adds
/// 0.98 ms latency · ...") y que encenderla o apagarla deja un hueco. En IIR
/// no dice nada de latencia: no la tiene.
std::string saturationTooltip(dsp::PhaseMode phase, double sampleRate);

/// Textos de ayuda del drive y de la mezcla; con Off, avisan de que no
/// influyen hasta elegir Soft o Warm.
std::string saturationDriveTooltip(dsp::SaturationMode mode);
std::string saturationMixTooltip(dsp::SaturationMode mode);

/// Si algo del cajón cambia ahora el sonido: la saturación fuera de Off, una
/// ganancia de salida distinta de 0 dB o la compensación automática
/// encendida. Plegado, la pestaña lo marca con un punto.
bool outputStageActive(const core::ParameterValues& values) noexcept;

/// Texto de ayuda de la cabecera (abierto) o de la pestaña (plegado). Plegado
/// dice además lo que está cambiando el sonido, porque no se ve: "Show the
/// output stage ... · Saturation Soft, drive 6.0 dB, mix 100% · Output -3.0
/// dB · Auto gain on".
std::string outputStageTooltip(const core::ParameterValues& values, bool open);

} // namespace esceq::ui
