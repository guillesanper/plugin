// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/realtime.h"

#include <array>
#include <cstddef>
#include <optional>

/// Macros (PLAN.md 5.9, ADR 0028): ocho parámetros, `macro_1` a `macro_8`,
/// que mueven cada uno hasta kMaxMacroTargets destinos sin escribir en ellos.
///
/// El valor que suena de un destino es el de su parámetro más la modulación,
/// sumada en el espacio normalizado del host (la posición 0..1 del
/// potenciómetro) y limitada a 0..1: una macro al 100 % con una profundidad
/// del +50 % recorre medio potenciómetro, y en la frecuencia, las mismas
/// octavas en graves que en agudos. El valor del parámetro en el host no
/// cambia: una macro y un automation clip del mismo destino se suman.
///
/// Las asignaciones (qué destino mueve cada macro y con qué profundidad) no
/// son parámetros: van en el estado (versión 4, líneas `macro.N.M`) y en los
/// presets de usuario.
namespace esceq::core {

inline constexpr int kNumMacros = 8;
inline constexpr int kMaxMacroTargets = 8;
static_assert(kNumParameters == kFirstMacroParameter + static_cast<std::size_t>(kNumMacros));

/// Profundidad máxima de una asignación, en %: de −kMaxMacroDepth a +kMaxMacroDepth.
inline constexpr float kMaxMacroDepth = 100.0f;

/// true si el parámetro `index` se puede modular con una macro: los continuos
/// que no cambian la estructura ni la latencia (frecuencia, ganancia y Q de
/// cada banda; umbral, rango, ataque, relajación y rodilla de su dinámica;
/// drive y mezcla de la saturación; ganancia de salida). Los interruptores,
/// las listas y las propias macros, no. CONTRATO: un destino válido no deja
/// de serlo (hay estados que lo llevan).
bool isMacroTarget(std::size_t index) noexcept;

/// Un destino de una macro.
struct MacroTarget
{
    /// Índice en parameters(), o kNumParameters si el hueco está libre.
    std::size_t parameter = kNumParameters;
    /// Profundidad, en %, de −kMaxMacroDepth a +kMaxMacroDepth: lo que se
    /// mueve el destino con la macro al 100 %, en % de su recorrido.
    float depth = 0.0f;

    bool used() const noexcept { return parameter < kNumParameters; }

    friend bool operator==(const MacroTarget&, const MacroTarget&) = default;
};

/// Las asignaciones de las ocho macros: kMaxMacroTargets huecos por macro.
/// Un hueco libre deja los demás en su sitio (las líneas del estado se
/// numeran por hueco). Un destino aparece como mucho una vez en cada macro;
/// en varias macros, sí, y sus modulaciones se suman.
///
/// Tamaño fijo y sin memoria dinámica: se copia sin asignar (así llega al
/// hilo de audio).
class MacroAssignments
{
public:
    using Targets = std::array<MacroTarget, kMaxMacroTargets>;

    /// Los huecos de la macro `macro` (0..kNumMacros-1).
    const Targets& targets(int macro) const noexcept;

    /// Profundidad de `parameter` en la macro `macro`, o nullopt si no lo mueve.
    std::optional<float> depth(int macro, std::size_t parameter) const noexcept;

    /// Asigna `parameter` a la macro `macro` con la profundidad `depth`
    /// (limitada al rango), o le cambia la profundidad si ya lo movía. Si no,
    /// ocupa el primer hueco libre. false (y no cambia nada) si la macro no
    /// existe, el parámetro no es un destino válido, la profundidad no es un
    /// número finito o la macro ya tiene kMaxMacroTargets destinos.
    bool assign(int macro, std::size_t parameter, float depth) noexcept;

    /// Quita `parameter` de la macro `macro`; los demás huecos no se mueven.
    /// false si no lo movía.
    bool remove(int macro, std::size_t parameter) noexcept;

    /// Pone el hueco `slot` (0..kMaxMacroTargets-1) de la macro `macro`, como
    /// al leer el estado. false (y no cambia nada) si la macro o el hueco no
    /// existen, el parámetro no es un destino válido, la profundidad no es
    /// finita o el destino ya está en otro hueco de esa macro. La profundidad
    /// se limita al rango.
    bool setSlot(int macro, int slot, std::size_t parameter, float depth) noexcept;

    /// Deja libre el hueco `slot` de la macro `macro` (si existen).
    void clearSlot(int macro, int slot) noexcept;

    /// true si ninguna macro tiene destinos.
    bool empty() const noexcept;
    void clear() noexcept;

    // Igualdad exacta a propósito: las profundidades se guardan y se leen tal cual.
    // NOLINTNEXTLINE(clang-diagnostic-float-equal)
    friend bool operator==(const MacroAssignments&, const MacroAssignments&) = default;

private:
    std::array<Targets, kNumMacros> macros_{};
};

/// Lo que suena: aplica a `values` (los de los parámetros del host) la
/// modulación de las macros. Para cada destino, la modulación es la suma, en
/// orden de macro y de hueco, de valor de la macro (0..1) × profundidad
/// (−1..+1); si no es 0, el destino pasa a fromNormalized(toNormalized(valor)
/// + modulación, limitado a 0..1). Un destino con la modulación en 0 (sus
/// macros en 0, o profundidades que se anulan) se queda bit a bit como estaba.
/// Las macros no son destinos, así que su valor es el del host.
///
/// Segura en tiempo real. Sin asignaciones no hace nada.
void applyMacros(const MacroAssignments& assignments, ParameterValues& values) noexcept ESCEQ_NONBLOCKING;

} // namespace esceq::core
