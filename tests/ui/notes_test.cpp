// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/notes.h"

#include "esceq/core/state.h"
#include "esceq/ui/control_logic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using namespace esceq;
using namespace esceq::ui;
using Catch::Approx;

namespace {

constexpr double kA4 = 440.0;

/// Frecuencia a `cents` de la nota `midi`.
double offset(int midi, double cents, double a4 = kA4)
{
    return frequencyForNote(static_cast<double>(midi) + cents / 100.0, a4);
}

const core::ParameterSpec& frequencySpec()
{
    return core::parameters()[core::bandParameter(0, core::BandParam::Frequency)];
}

} // namespace

TEST_CASE("Notas: referencias de afinacion")
{
    // 440 Hz es el la central (A4, MIDI 69) exacto.
    const auto a4 = noteForFrequency(440.0, kA4);
    CHECK(a4.midi == 69);
    CHECK(a4.cents == 0.0);
    CHECK(noteName(a4.midi) == "A4");

    // 261,626 Hz es el do central (C4, MIDI 60) con menos de 0,1 ct de error.
    const auto c4 = noteForFrequency(261.626, kA4);
    CHECK(c4.midi == 60);
    CHECK(std::abs(c4.cents) < 0.1);
    CHECK(noteName(c4.midi) == "C4");

    // Con A4 = 432 Hz, 432 Hz es A4.
    const auto a432 = noteForFrequency(432.0, 432.0);
    CHECK(a432.midi == 69);
    CHECK(std::abs(a432.cents) < 1e-9);
    CHECK(noteForFrequency(440.0, 432.0).cents == Approx(31.77).margin(0.01));

    // Los extremos de la franja de teclas (PLAN.md 5.1): E0 y D#10.
    CHECK(noteName(noteForFrequency(20.6, kA4).midi) == "E0");
    CHECK(frequencyForNote(16, kA4) == Approx(20.60).margin(0.01));
    CHECK(noteName(noteForFrequency(19912.0, kA4).midi) == "D#10");
    CHECK(frequencyForNote(135, kA4) == Approx(19912.13).margin(0.01));
}

TEST_CASE("Notas: nombres en ingles con sostenido y octava")
{
    CHECK(noteName(0) == "C-1");
    CHECK(noteName(1) == "C#-1");
    CHECK(noteName(57) == "A3");
    CHECK(noteName(61) == "C#4");
    CHECK(noteName(70) == "A#4");
    CHECK(noteName(71) == "B4");
    CHECK(noteName(72) == "C5");
    CHECK(noteName(120) == "C9");
    CHECK(noteName(132) == "C10");
    CHECK(isBlackKey(61));
    CHECK_FALSE(isBlackKey(64)); // E
    CHECK_FALSE(isBlackKey(65)); // F
    CHECK(isBlackKey(70));
}

TEST_CASE("Notas: la desviacion va de -50 a +50 ct y el redondeo es estable")
{
    for (const double a4 : {kA4, 432.0, 400.0, 480.0, 443.7})
    {
        INFO("A4 = " << a4);
        for (int midi = 16; midi <= 135; ++midi)
        {
            INFO("MIDI " << midi);
            // Justo dentro de la nota, por los dos lados.
            const auto below = noteForFrequency(offset(midi, 49.99, a4), a4);
            CHECK(below.midi == midi);
            CHECK(below.cents == Approx(49.99).margin(1e-6));
            const auto above = noteForFrequency(offset(midi, -49.99, a4), a4);
            CHECK(above.midi == midi);
            CHECK(above.cents == Approx(-49.99).margin(1e-6));

            // Justo en la mitad entre dos notas, siempre la de arriba, a -50 ct.
            const auto half = noteForFrequency(offset(midi, 50.0, a4), a4);
            CHECK(half.midi == midi + 1);
            CHECK(half.cents == Approx(-50.0).margin(1e-6));
            CHECK(half.cents >= -50.0);
            CHECK(half.cents < 50.0);
        }
    }

    // El texto: "A3 +0 ct", con la desviacion redondeada y su signo.
    CHECK(noteText(220.0, kA4) == "A3 +0 ct");
    CHECK(noteText(offset(57, -14.2), kA4) == "A3 -14 ct");
    CHECK(noteText(offset(57, 7.6), kA4) == "A3 +8 ct");
    CHECK(noteText(offset(57, -0.4), kA4) == "A3 +0 ct");
    CHECK(noteText(offset(57, 50.0), kA4) == "A#3 -50 ct");
    CHECK(noteText(offset(57, 49.7), kA4) == "A3 +50 ct");
}

TEST_CASE("Notas: de la nota a la frecuencia y vuelta")
{
    for (const double a4 : {kA4, 432.0, 415.3})
        for (int midi = 0; midi <= 140; ++midi)
        {
            INFO(a4 << " Hz, MIDI " << midi);
            const double hz = frequencyForNote(midi, a4);
            const auto note = noteForFrequency(hz, a4);
            CHECK(note.midi == midi);
            CHECK(std::abs(note.cents) < 1e-6);
            // Tambien con la frecuencia en float, como la guarda el parametro.
            const auto stored = noteForFrequency(static_cast<double>(static_cast<float>(hz)), a4);
            CHECK(stored.midi == midi);
            CHECK(std::abs(stored.cents) < 0.01);
        }

    // Cualquier frecuencia: frequencyForNote(noteForFrequency(f)) es la nota temperada mas cercana.
    for (double hz = 20.0; hz <= 20000.0; hz *= 1.0137)
    {
        const auto note = noteForFrequency(hz, kA4);
        const double tempered = frequencyForNote(note.midi, kA4);
        CHECK(std::abs(1200.0 * std::log2(hz / tempered)) <= 50.0 + 1e-9);
        CHECK(noteForFrequency(tempered, kA4).midi == note.midi);
    }
}

TEST_CASE("Notas: ajustar a semitonos")
{
    CHECK(snapToSemitone(offset(57, 30.0), kA4) == Approx(220.0).epsilon(1e-12));
    CHECK(snapToSemitone(offset(57, -49.0), kA4) == Approx(220.0).epsilon(1e-12));
    CHECK(snapToSemitone(offset(57, 50.0), kA4) == Approx(frequencyForNote(58, kA4)).epsilon(1e-12));
    CHECK(snapToSemitone(440.0, 432.0) == 432.0);                                              // +32 ct: A4
    CHECK(snapToSemitone(450.0, 432.0) == Approx(frequencyForNote(70, 432.0)).epsilon(1e-12)); // +71 ct: A#4

    // Dentro de los limites de la frecuencia de banda: la nota mas cercana que quepa.
    CHECK(snapToSemitone(20.05, kA4, 20.0, 20000.0) == Approx(frequencyForNote(16, kA4)));    // no D#0 (19,4 Hz)
    CHECK(snapToSemitone(19999.0, kA4, 20.0, 20000.0) == Approx(frequencyForNote(135, kA4))); // no E10 (21,1 kHz)
    CHECK(snapToSemitone(1000.0, kA4, 20.0, 20000.0) == Approx(snapToSemitone(1000.0, kA4)));
}

TEST_CASE("Notas: avanzar un semitono")
{
    // Desde una nota, a la siguiente o la anterior.
    CHECK(stepSemitones(220.0, kA4, 1) == Approx(frequencyForNote(58, kA4)).epsilon(1e-12));
    CHECK(stepSemitones(220.0, kA4, -1) == Approx(frequencyForNote(56, kA4)).epsilon(1e-12));
    CHECK(stepSemitones(220.0, kA4, 12) == Approx(440.0).epsilon(1e-12));
    // Una frecuencia guardada en float sigue contando como esa nota.
    CHECK(stepSemitones(static_cast<double>(261.626f), kA4, 1) == Approx(frequencyForNote(61, kA4)).epsilon(1e-12));

    // Entre dos notas, a la mas cercana en esa direccion.
    CHECK(stepSemitones(offset(57, 30.0), kA4, 1) == Approx(frequencyForNote(58, kA4)).epsilon(1e-12));
    CHECK(stepSemitones(offset(57, 30.0), kA4, -1) == Approx(220.0).epsilon(1e-12));
    CHECK(stepSemitones(offset(57, -30.0), kA4, 1) == Approx(220.0).epsilon(1e-12));
    CHECK(stepSemitones(offset(57, -30.0), kA4, -1) == Approx(frequencyForNote(56, kA4)).epsilon(1e-12));

    // En los limites no se sale: se queda en la ultima nota que cabe.
    CHECK(stepSemitones(frequencyForNote(135, kA4), kA4, 1, 20.0, 20000.0) == Approx(frequencyForNote(135, kA4)));
    CHECK(stepSemitones(20.0, kA4, -1, 20.0, 20000.0) == Approx(frequencyForNote(16, kA4)));
    CHECK(stepSemitones(20000.0, kA4, 1, 20.0, 20000.0) == Approx(frequencyForNote(135, kA4)));
}

TEST_CASE("Notas: escribir una nota")
{
    CHECK(parseNote("A4") == 69);
    CHECK(parseNote("a4") == 69);
    CHECK(parseNote(" A3 ") == 57);
    CHECK(parseNote("C#2") == 37);
    CHECK(parseNote("Db2") == 37);
    CHECK(parseNote("Eb4") == 63);
    CHECK(parseNote("D#4") == 63);
    CHECK(parseNote("eb4") == 63);
    CHECK(parseNote("bb3") == 58);
    CHECK(parseNote("B3") == 59);
    CHECK(parseNote("Cb4") == 59);
    CHECK(parseNote("B#3") == 60);
    CHECK(parseNote("C-1") == 0);
    CHECK(parseNote("C10") == 132);
    CHECK(parseNote("D\u266F4") == 63); // D♯4
    CHECK(parseNote("E\u266D4") == 63); // E♭4

    // "Eb4" y "D#4" dan la misma frecuencia.
    CHECK(frequencyForNote(*parseNote("Eb4"), kA4) == frequencyForNote(*parseNote("D#4"), kA4));

    for (const char* invalid : {"", " ", "H4", "A", "Ab", "#4", "A44", "A4x", "A 4", "1e3", "220", "Cb-1", "A-2", "C11",
                                "A##4", "Abb4", "A4.5", "a-"})
    {
        INFO("'" << invalid << "'");
        CHECK_FALSE(parseNote(invalid).has_value());
    }
}

TEST_CASE("Notas: el campo de frecuencia acepta notas")
{
    const auto& spec = frequencySpec();
    CHECK(commitText(spec, "A3", kA4) == Approx(220.0f));
    CHECK(commitText(spec, "A3", 432.0) == Approx(216.0f));
    CHECK(commitText(spec, "Eb4", kA4) == commitText(spec, "D#4", kA4));
    // Los numeros siguen como antes.
    CHECK(commitText(spec, "1.2k", kA4) == 1200.0f);
    CHECK(commitText(spec, "350 Hz", kA4) == 350.0f);
    // Fuera del rango de la banda, se ajusta como un numero.
    CHECK(commitText(spec, "C0", kA4) == 20.0f);
    CHECK(commitText(spec, "B10", kA4) == 20000.0f);
    // Lo que no se entiende se rechaza: el control conserva su valor.
    CHECK_FALSE(commitText(spec, "H4", kA4).has_value());
    CHECK_FALSE(commitText(spec, "A#", kA4).has_value());

    // Solo en los parametros en Hz: en la ganancia, una nota no significa nada.
    const auto& gain = core::parameters()[core::bandParameter(0, core::BandParam::Gain)];
    CHECK_FALSE(commitText(gain, "A3", kA4).has_value());
}

TEST_CASE("Notas: la afinacion se escribe en Hz, de 400 a 480 en pasos de 0,1")
{
    CHECK(parseTuning("440") == 440.0);
    CHECK(parseTuning("432 Hz") == 432.0);
    CHECK(parseTuning("432hz") == 432.0);
    CHECK(parseTuning(" 438.5 ") == 438.5);
    CHECK(parseTuning("400") == 400.0);
    CHECK(parseTuning("480") == 480.0);
    CHECK(parseTuning("441.26") == Approx(441.3));
    for (const char* invalid : {"", "abc", "399.9", "480.1", "1e9", "-440", "nan", "inf", "440 kHz", "A4"})
    {
        INFO("'" << invalid << "'");
        CHECK_FALSE(parseTuning(invalid).has_value());
    }
    CHECK(tuningText(440.0) == "440.0 Hz");
    CHECK(tuningText(438.5) == "438.5 Hz");
}

TEST_CASE("Notas: teclas del teclado de piano a lo largo del eje")
{
    const CurveMapping mapping({10.0f, 0.0f, 1096.0f, 300.0f}, 18.0);
    const auto keys = pianoKeys(mapping, kA4);

    // De E0 (20,6 Hz) a D#10 (19,9 kHz): las notas cuyo centro se ve.
    REQUIRE_FALSE(keys.empty());
    CHECK(keys.front().midi == 16);
    CHECK(keys.back().midi == 135);
    CHECK(keys.size() == 120);

    const float areaLeft = mapping.area().x;
    const float areaRight = mapping.area().x + mapping.area().width;
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        const auto& key = keys[i];
        INFO(noteName(key.midi));
        if (i > 0)
            CHECK(key.midi == keys[i - 1].midi + 1);
        CHECK(key.black == isBlackKey(key.midi));
        CHECK(key.left < key.right);
        CHECK(key.left >= areaLeft);
        CHECK(key.right <= areaRight);
        // Cada tecla contiene su frecuencia.
        const float x = mapping.xForFrequency(frequencyForNote(key.midi, kA4));
        CHECK(x >= key.left);
        CHECK(x <= key.right);
        // Una tecla negra ocupa su semitono; una blanca, ademas, la mitad de
        // la negra de cada lado, como en un piano.
        const float semitone = mapping.xForFrequency(frequencyForNote(key.midi + 0.5, kA4)) -
                               mapping.xForFrequency(frequencyForNote(key.midi - 0.5, kA4));
        if (key.black && key.left > areaLeft && key.right < areaRight)
            CHECK(key.right - key.left == Approx(semitone).margin(1e-3));
    }

    // Con otra afinacion las teclas se desplazan.
    const auto shifted = pianoKeys(mapping, 480.0);
    CHECK(shifted.front().midi == 14);
}

TEST_CASE("Notas: tecla bajo el raton")
{
    const CurveMapping mapping({0.0f, 0.0f, 1000.0f, 300.0f}, 18.0);
    const Rect strip{0.0f, 310.0f, 1000.0f, kPianoKeyboardHeight};
    const auto keys = pianoKeys(mapping, kA4);

    const auto xOf = [&](double midi) { return mapping.xForFrequency(frequencyForNote(midi, kA4)); };
    const float blackY = strip.y + 2.0f;                // a la altura de las negras
    const float whiteY = strip.y + strip.height - 2.0f; // por debajo de las negras

    // Sobre una negra (C#4), arriba: la negra. Abajo, la blanca de al lado.
    CHECK(pianoKeyAt(keys, strip, {xOf(61.2), blackY}) == 61);
    CHECK(pianoKeyAt(keys, strip, {xOf(61.2), whiteY}) == 62);
    CHECK(pianoKeyAt(keys, strip, {xOf(60.8), whiteY}) == 60);
    // Sobre una blanca sin negras al lado (E-F).
    CHECK(pianoKeyAt(keys, strip, {xOf(64.0), blackY}) == 64);
    CHECK(pianoKeyAt(keys, strip, {xOf(65.0), blackY}) == 65);
    // Fuera de la franja, ninguna.
    CHECK_FALSE(pianoKeyAt(keys, strip, {xOf(64.0), strip.y - 1.0f}).has_value());
    CHECK_FALSE(pianoKeyAt(keys, strip, {xOf(64.0), strip.y + strip.height + 1.0f}).has_value());
    CHECK_FALSE(pianoKeyAt(keys, strip, {-5.0f, whiteY}).has_value());
}

TEST_CASE("Notas: preferencias por defecto y saneadas")
{
    // Por defecto, la franja oculta y A4 = 440 Hz: los proyectos anteriores se ven igual.
    CHECK_FALSE(core::UiSettings{}.pianoKeyboard);
    CHECK(core::UiSettings{}.a4Hz == 440.0);
    CHECK(core::kDefaultA4Hz == 440.0);
    CHECK(core::kMinA4Hz == 400.0);
    CHECK(core::kMaxA4Hz == 480.0);
}
