// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/notes.h"

#include "esceq/core/state.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

namespace esceq::ui {

namespace {

constexpr int kA4Midi = 69;
/// Tolerancia del redondeo a la nota, en semitonos (1e-7 ct): absorbe el
/// error de coma flotante del logaritmo sin mover ninguna nota de verdad.
constexpr double kRoundingTolerance = 1.0e-9;
/// Una frecuencia a menos de esto de una nota, en semitonos, es esa nota al
/// avanzar un semitono (0,1 ct: más que el error de guardarla en float).
constexpr double kOnNoteTolerance = 1.0e-3;
constexpr int kMinOctave = -1;
constexpr int kMaxOctave = 10;

/// Posición de `hz` en semitonos, en la escala MIDI.
double semitones(double hz, double a4Hz) noexcept
{
    return static_cast<double>(kA4Midi) + 12.0 * std::log2(std::max(hz, 1.0e-9) / a4Hz);
}

/// Primera y última nota entre `minHz` y `maxHz`.
std::pair<int, int> notesWithin(double a4Hz, double minHz, double maxHz) noexcept
{
    return {static_cast<int>(std::ceil(semitones(minHz, a4Hz) - kRoundingTolerance)),
            static_cast<int>(std::floor(semitones(maxHz, a4Hz) + kRoundingTolerance))};
}

double clampToNotes(int midi, double a4Hz, double minHz, double maxHz) noexcept
{
    const auto [low, high] = notesWithin(a4Hz, minHz, maxHz);
    if (low > high) // no cabe ninguna nota
        return std::clamp(frequencyForNote(midi, a4Hz), minHz, maxHz);
    return frequencyForNote(std::clamp(midi, low, high), a4Hz);
}

int nearestMidi(double hz, double a4Hz) noexcept
{
    return static_cast<int>(std::floor(semitones(hz, a4Hz) + 0.5 + kRoundingTolerance));
}

int steppedMidi(double hz, double a4Hz, int steps) noexcept
{
    const double position = semitones(hz, a4Hz);
    if (steps == 0)
        return nearestMidi(hz, a4Hz);
    // El primer paso lleva a la nota siguiente en ese sentido (o, si ya está
    // en una, a la de al lado); los demás, un semitono cada uno.
    const int first = steps > 0 ? static_cast<int>(std::floor(position + kOnNoteTolerance)) + 1
                                : static_cast<int>(std::ceil(position - kOnNoteTolerance)) - 1;
    return first + (steps > 0 ? steps - 1 : steps + 1);
}

/// Posición de una nota (o fracción) en el eje x.
float xForMidi(const CurveMapping& mapping, double midi, double a4Hz) noexcept
{
    return mapping.xForFrequency(frequencyForNote(midi, a4Hz));
}

} // namespace

Note noteForFrequency(double hz, double a4Hz) noexcept
{
    const double position = semitones(hz, a4Hz);
    const int midi = static_cast<int>(std::floor(position + 0.5 + kRoundingTolerance));
    const double cents = (position - static_cast<double>(midi)) * 100.0;
    return {midi, std::clamp(cents, -50.0, std::nextafter(50.0, 0.0))};
}

double frequencyForNote(double midi, double a4Hz) noexcept
{
    return a4Hz * std::exp2((midi - static_cast<double>(kA4Midi)) / 12.0);
}

double snapToSemitone(double hz, double a4Hz) noexcept
{
    return frequencyForNote(nearestMidi(hz, a4Hz), a4Hz);
}

double snapToSemitone(double hz, double a4Hz, double minHz, double maxHz) noexcept
{
    return clampToNotes(nearestMidi(hz, a4Hz), a4Hz, minHz, maxHz);
}

double stepSemitones(double hz, double a4Hz, int steps) noexcept
{
    return frequencyForNote(steppedMidi(hz, a4Hz, steps), a4Hz);
}

double stepSemitones(double hz, double a4Hz, int steps, double minHz, double maxHz) noexcept
{
    return clampToNotes(steppedMidi(hz, a4Hz, steps), a4Hz, minHz, maxHz);
}

bool isBlackKey(int midi) noexcept
{
    const int pitchClass = ((midi % 12) + 12) % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

std::string noteName(int midi)
{
    static constexpr std::array<const char*, 12> kNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                        "F#", "G",  "G#", "A",  "A#", "B"};
    const int pitchClass = ((midi % 12) + 12) % 12;
    const int octave = (midi - pitchClass) / 12 - 1;
    return std::string(kNames[static_cast<std::size_t>(pitchClass)]) + std::to_string(octave);
}

std::string noteText(double hz, double a4Hz)
{
    const auto note = noteForFrequency(hz, a4Hz);
    // Lo que redondea a 0 se escribe "+0": la nota está afinada.
    const long cents = std::lround(note.cents);
    char text[16];
    std::snprintf(text, sizeof text, "%s%ld ct", cents < 0 ? "" : "+", cents);
    return noteName(note.midi) + " " + text;
}

std::optional<int> parseNote(std::string_view text) noexcept
{
    const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    if (text.empty())
        return std::nullopt;

    // Semitonos de cada letra respecto a C.
    static constexpr std::array<int, 7> kLetters{9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())));
    if (letter < 'A' || letter > 'G')
        return std::nullopt;
    int pitch = kLetters[static_cast<std::size_t>(letter - 'A')];
    text.remove_prefix(1);

    constexpr std::string_view kSharpSign = "\xE2\x99\xAF"; // ♯
    constexpr std::string_view kFlatSign = "\xE2\x99\xAD";  // ♭
    if (text.starts_with('#') || text.starts_with('b'))
    {
        pitch += text.front() == '#' ? 1 : -1;
        text.remove_prefix(1);
    }
    else if (text.starts_with(kSharpSign) || text.starts_with(kFlatSign))
    {
        pitch += text.starts_with(kSharpSign) ? 1 : -1;
        text.remove_prefix(kSharpSign.size());
    }

    // Octava: un signo menos opcional y una o dos cifras, y nada más.
    const bool negative = text.starts_with('-');
    if (negative)
        text.remove_prefix(1);
    if (text.empty() || text.size() > 2 ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    int octave = 0;
    for (const char c : text)
        octave = 10 * octave + (c - '0');
    if (negative)
        octave = -octave;
    if (octave < kMinOctave || octave > kMaxOctave)
        return std::nullopt;

    const int midi = 12 * (octave + 1) + pitch;
    if (midi < 0)
        return std::nullopt;
    return midi;
}

std::optional<double> parseTuning(std::string_view text)
{
    std::istringstream in{std::string{text}};
    in.imbue(std::locale::classic());
    double hz = 0.0;
    if (!(in >> hz))
        return std::nullopt;
    std::string unit;
    in >> unit;
    std::string rest;
    in >> rest;
    std::transform(unit.begin(), unit.end(), unit.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    if ((!unit.empty() && unit != "hz") || !rest.empty())
        return std::nullopt;
    return core::validA4(hz);
}

std::string tuningText(double a4Hz)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.1f Hz", a4Hz);
    return text;
}

std::vector<PianoKey> pianoKeys(const CurveMapping& mapping, double a4Hz)
{
    const auto [first, last] = notesWithin(a4Hz, kMinDisplayHz, kMaxDisplayHz);
    const float areaLeft = mapping.area().x;
    const float areaRight = mapping.area().x + mapping.area().width;

    std::vector<PianoKey> keys;
    keys.reserve(static_cast<std::size_t>(std::max(0, last - first + 1)));
    for (int midi = first; midi <= last; ++midi)
    {
        const bool black = isBlackKey(midi);
        // Una blanca se extiende bajo la mitad de cada negra vecina.
        const double below = black || !isBlackKey(midi - 1) ? 0.5 : 1.0;
        const double above = black || !isBlackKey(midi + 1) ? 0.5 : 1.0;
        const auto m = static_cast<double>(midi);
        keys.push_back({midi, black, std::max(areaLeft, xForMidi(mapping, m - below, a4Hz)),
                        std::min(areaRight, xForMidi(mapping, m + above, a4Hz))});
    }
    return keys;
}

std::optional<int> pianoKeyAt(std::span<const PianoKey> keys, Rect strip, Point p) noexcept
{
    if (p.y < strip.y || p.y >= strip.y + strip.height)
        return std::nullopt;
    const bool upper = p.y < strip.y + kBlackKeyFraction * strip.height;
    const auto contains = [&](const PianoKey& key) { return p.x >= key.left && p.x < key.right; };
    if (upper)
        for (const auto& key : keys)
            if (key.black && contains(key))
                return key.midi;
    for (const auto& key : keys)
        if (!key.black && contains(key))
            return key.midi;
    return std::nullopt;
}

} // namespace esceq::ui
