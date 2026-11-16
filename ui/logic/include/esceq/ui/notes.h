// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/ui/curve_mapping.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Escala de notas (PLAN.md 5.1, ADR 0017), sin dependencias gráficas: de
/// frecuencia a nota temperada y vuelta, ajuste a semitonos, notas escritas
/// como texto y el teclado de piano a lo largo del eje de frecuencias.
///
/// Notas en numeración MIDI: 69 es el la central (A4), que suena a `a4Hz`;
/// 60 es el do central (C4). La frecuencia de una banda se sigue guardando
/// como número real: la nota solo se calcula para mostrarla o al ajustar.
/// Todas las funciones son puras y se pueden llamar desde cualquier hilo;
/// las que devuelven texto asignan memoria. `a4Hz` es la afinación, que la
/// interfaz guarda saneada entre core::kMinA4Hz y core::kMaxA4Hz.
namespace esceq::ui {

/// Nota temperada más cercana a una frecuencia y su desviación.
struct Note
{
    int midi = 69;
    /// Desviación de la frecuencia respecto a la nota, en cents: de -50
    /// (incluido) a +50 (excluido). Justo en la mitad entre dos notas, la
    /// frecuencia es la de arriba a -50 ct.
    double cents = 0.0;
};

/// Nota más cercana a `hz` (> 0) con la afinación `a4Hz`. Las frecuencias
/// que quedan a menos de 1e-7 ct de la mitad entre dos notas cuentan como la
/// mitad: así el redondeo no depende del error de coma flotante.
Note noteForFrequency(double hz, double a4Hz) noexcept;

/// Frecuencia, en Hz, de la nota `midi` (puede tener parte fraccionaria:
/// 60,5 es el cuarto de tono entre C4 y C#4).
double frequencyForNote(double midi, double a4Hz) noexcept;

/// La frecuencia de la nota temperada más cercana a `hz`.
double snapToSemitone(double hz, double a4Hz) noexcept;

/// Como la anterior, pero solo con notas entre `minHz` y `maxHz` (los
/// límites de la frecuencia de una banda): si la más cercana se sale, la
/// más cercana que quepa.
double snapToSemitone(double hz, double a4Hz, double minHz, double maxHz) noexcept;

/// Frecuencia `steps` semitonos por encima (o por debajo, si es negativo)
/// de `hz`. Desde una nota (a menos de 0,1 ct), a la siguiente o la
/// anterior; desde fuera de ellas, el primer paso llega a la nota más
/// cercana en ese sentido. Con límites, se queda en la última nota que cabe.
double stepSemitones(double hz, double a4Hz, int steps) noexcept;
double stepSemitones(double hz, double a4Hz, int steps, double minHz, double maxHz) noexcept;

/// Nombre de una nota, en inglés y con sostenido: "A4", "C#4", "C-1".
std::string noteName(int midi);

/// true en las teclas negras (C#, D#, F#, G#, A#).
bool isBlackKey(int midi) noexcept;

/// Nota de una frecuencia como texto, con la desviación redondeada a cents
/// enteros y su signo: "A3 +0 ct", "C#4 -14 ct".
std::string noteText(double hz, double a4Hz);

/// Nota escrita en inglés: letra (A a G, mayúscula o minúscula), alteración
/// opcional (`#` o `♯` sostenido, `b` o `♭` bemol) y octava de -1 a 10:
/// "A3", "C#2", "Eb4". Devuelve su número MIDI, o nullopt si no es una nota
/// o queda por debajo de C-1. No acepta números: "220" no es una nota.
std::optional<int> parseNote(std::string_view text) noexcept;

/// Afinación escrita por el usuario: un número de Hz, con "Hz" detrás o sin
/// él ("432", "438.5 Hz"), redondeado a 0,1 Hz. nullopt si no se entiende o
/// queda fuera de core::kMinA4Hz..core::kMaxA4Hz: la afinación no cambia.
std::optional<double> parseTuning(std::string_view text);

/// Afinación como texto, con una cifra decimal: "440.0 Hz".
std::string tuningText(double a4Hz);

// ---------------------------------------------------------------------------
// Teclado de piano bajo la curva

/// Alto de la franja del teclado, en píxeles lógicos.
inline constexpr float kPianoKeyboardHeight = 14.0f;
/// Las teclas negras ocupan esta fracción del alto, desde arriba.
inline constexpr float kBlackKeyFraction = 0.6f;

/// Una tecla, en el eje x de la curva.
struct PianoKey
{
    int midi = 0;
    bool black = false;
    /// Bordes en píxeles, recortados al área de la curva. Una negra ocupa su
    /// semitono; una blanca, además, la mitad de cada negra vecina (por
    /// debajo de ella), como en un piano.
    float left = 0.0f;
    float right = 0.0f;
};

/// Las teclas cuyo centro (la frecuencia de su nota) cae entre
/// kMinDisplayHz y kMaxDisplayHz, de la más grave a la más aguda. Con
/// A4 = 440 Hz, de E0 (20,6 Hz) a D#10 (19,9 kHz).
std::vector<PianoKey> pianoKeys(const CurveMapping& mapping, double a4Hz);

/// Nota de la tecla que hay en `p`, dentro de la franja `strip` (que tiene el
/// mismo eje x que las teclas). Arriba, las negras tapan a las blancas.
std::optional<int> pianoKeyAt(std::span<const PianoKey> keys, Rect strip, Point p) noexcept;

} // namespace esceq::ui
