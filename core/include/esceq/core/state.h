// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/filter_design.h"

#include <optional>
#include <string>
#include <string_view>

namespace esceq::core {

/// Versión del formato de estado que escribe este código.
/// Subirla exige añadir una migración y un fixture nuevo en
/// tests/fixtures/state/. Ver docs/decisiones/0003-formato-de-estado.md y
/// docs/decisiones/0009-preferencias-deshacer-y-presets.md.
///
///  - 1: solo parámetros.
///  - 2: además, preferencias de la interfaz (líneas `ui.*`).
///  - 3: además, el algoritmo de filtro de la instancia (línea
///    `filter_algorithm`). Ver docs/decisiones/0014-estado-v3-algoritmo-de-filtro.md.
inline constexpr int kStateVersion = 3;

/// Algoritmo de filtro de una instancia nueva.
inline constexpr dsp::FilterAlgorithm kNewInstanceFilterAlgorithm = dsp::FilterAlgorithm::Matched;

/// Algoritmo de los estados escritos antes de la versión 3 (todos los de la
/// 0.1): el único que había. Con él suenan bit a bit igual que entonces.
inline constexpr dsp::FilterAlgorithm kLegacyFilterAlgorithm = dsp::FilterAlgorithm::Classic;

/// Preferencias de la interfaz que se guardan con el proyecto. No son
/// parámetros: el host no las automatiza ni las ve.
struct UiSettings
{
    /// Modo del analizador. CONTRATO: 0 = apagado, 1 = post, 2 = pre + post.
    int analyzerMode = 1;
    /// Rango de la curva, en dB (± este valor): 6, 12, 18 o 30.
    int displayRangeDb = 18;
    /// Tamaño de la ventana en píxeles lógicos; 0 = el tamaño por defecto.
    int windowWidth = 0;
    int windowHeight = 0;
    /// Nombre del preset cargado, o vacío. Solo informativo.
    std::string presetName{};
    /// Escala de notas (PLAN.md 5.1, ADR 0017): si se muestra el teclado de
    /// piano bajo la curva. Línea `ui.notes` (0 o 1); solo se escribe encendida.
    bool pianoKeyboard = false;
    /// Afinación de la escala de notas: frecuencia del la central (A4), en Hz,
    /// de kMinA4Hz a kMaxA4Hz en pasos de 0,1. Línea `ui.a4`; solo se escribe
    /// si no es kDefaultA4Hz. Solo cambia cómo se leen y se escriben las
    /// frecuencias como notas, nunca el sonido.
    double a4Hz = 440.0;
    /// Analizador ampliado (PLAN.md 5.2, ADR 0018). Solo cambian cómo se ve el
    /// espectro, nunca el sonido, y cada una se escribe solo si no tiene su
    /// valor por defecto, que es el del analizador de antes de existir:
    ///  - puntos de la FFT, uno de kAnalyzerFftSizes (línea `ui.analyzer_fft`);
    int analyzerFftSize = 4096;
    ///  - constante de tiempo de bajada, en ms, una de kAnalyzerFallTimesMs
    ///    (línea `ui.analyzer_fall`);
    int analyzerFallMs = 350;
    ///  - inclinación en dB por octava, una de kAnalyzerTilts (`ui.analyzer_tilt`);
    double analyzerTiltDbPerOctave = 4.5;
    ///  - mantener los picos (línea `ui.analyzer_peaks`, 0 o 1).
    bool analyzerPeakHold = false;

    /// Igualdad exacta a propósito: a4Hz está redondeada a 0,1 Hz por
    /// sanitize(), y la inclinación es uno de unos pocos valores exactos.
    // NOLINTNEXTLINE(clang-diagnostic-float-equal)
    friend bool operator==(const UiSettings&, const UiSettings&) = default;
};

inline constexpr int kNumAnalyzerModes = 3;
inline constexpr int kMaxPresetNameLength = 64;
inline constexpr double kDefaultA4Hz = 440.0;
inline constexpr double kMinA4Hz = 400.0;
inline constexpr double kMaxA4Hz = 480.0;
/// Valores válidos de los ajustes del analizador (UiSettings), en el orden en
/// que se ofrecen. Cualquier otro se lee como el valor por defecto.
inline constexpr int kAnalyzerFftSizes[] = {4096, 8192, 16384};
inline constexpr int kAnalyzerFallTimesMs[] = {1000, 350, 120}; ///< Lenta, media y rápida.
inline constexpr double kAnalyzerTilts[] = {0.0, 3.0, 4.5, 6.0};

/// La afinación `a4Hz` redondeada a 0,1 Hz, o nullopt si no es un número
/// finito o, redondeada, queda fuera de kMinA4Hz..kMaxA4Hz.
std::optional<double> validA4(double a4Hz) noexcept;

/// Lleva las preferencias a valores válidos: lo que no se entienda toma su
/// valor por defecto. Los límites del tamaño de ventana son amplios a
/// propósito: la interfaz los ajusta a los suyos.
UiSettings sanitize(UiSettings settings);

/// Todo lo que se guarda con el proyecto.
struct PluginState
{
    ParameterValues parameters = defaultValues();
    /// Algoritmo de filtro de todas las bandas. No es un parámetro (el host no
    /// lo ve ni lo automatiza) y los presets no lo cambian.
    dsp::FilterAlgorithm filterAlgorithm = kNewInstanceFilterAlgorithm;
    UiSettings ui{};

    friend bool operator==(const PluginState&, const PluginState&) = default;
};

/// Serializa el estado completo en texto UTF-8:
///
///     esceQ-state 3
///     output_gain 0
///     bypass 0
///     ...
///     filter_algorithm 1
///     ui.analyzer 1
///     ui.range 18
///     ui.width 1180
///     ui.height 700
///     ui.preset Vocal
///     ui.notes 1
///     ui.a4 432
///     ui.analyzer_fft 8192
///     ui.analyzer_fall 1000
///     ui.analyzer_tilt 3
///     ui.analyzer_peaks 1
///
/// Una línea por dato (`clave valor`), con punto decimal siempre,
/// independientemente de la configuración regional. `filter_algorithm` (el
/// valor numérico de dsp::FilterAlgorithm) y las claves `ui.*` no son
/// parámetros. No es segura en tiempo real (asigna memoria).
std::string serializePluginState(const PluginState& state);

/// Lee un estado escrito por cualquier versión de esceQ.
///
///  - Datos ausentes: valor por defecto.
///  - Claves desconocidas (estado de una versión más nueva): se ignoran.
///  - Valores fuera de rango o ilegibles: se ajustan o pasan a por defecto.
///  - Cabecera inválida: devuelve nullopt y el llamador conserva su estado.
///  - Versiones anteriores: se migran paso a paso hasta la actual. Las
///    anteriores a la 3 cargan con kLegacyFilterAlgorithm.
///  - Un algoritmo que esta versión no conoce (de una versión más nueva) o
///    ilegible: kNewInstanceFilterAlgorithm, el más reciente que conoce.
///
/// No es segura en tiempo real.
std::optional<PluginState> deserializePluginState(std::string_view text);

/// Solo los parámetros, sin algoritmo ni preferencias de la interfaz: es lo
/// que guarda un preset. Se lee con cualquiera de las dos funciones de lectura.
std::string serializeState(const ParameterValues& values);

/// Como deserializePluginState(), pero devuelve solo los parámetros.
std::optional<ParameterValues> deserializeState(std::string_view text);

} // namespace esceq::core
