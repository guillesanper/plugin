// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/control_logic.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace esceq;
using namespace esceq::ui;

namespace {

const core::ParameterSpec& frequency()
{
    return core::parameters()[core::bandParameter(0, core::BandParam::Frequency)];
}
const core::ParameterSpec& gain()
{
    return core::parameters()[core::bandParameter(0, core::BandParam::Gain)];
}
const core::ParameterSpec& threshold()
{
    return core::parameters()[core::dynamicParameter(0, core::DynParam::Threshold)];
}
const core::ParameterSpec& type()
{
    return core::parameters()[core::bandParameter(0, core::BandParam::Type)];
}
const core::ParameterSpec& enabled()
{
    return core::parameters()[core::bandParameter(0, core::BandParam::Enabled)];
}

} // namespace

TEST_CASE("Controles: el arrastre recorre el rango en kDragPixelsForFullRange pixeles")
{
    CHECK_THAT(dragNormalized(0.0f, kDragPixelsForFullRange, false), WithinAbs(1.0, 1e-6));
    CHECK_THAT(dragNormalized(0.5f, 50.0f, false), WithinAbs(0.75, 1e-6));
    CHECK_THAT(dragNormalized(0.5f, -50.0f, false), WithinAbs(0.25, 1e-6));

    // No se sale de 0..1.
    CHECK(dragNormalized(0.9f, 500.0f, false) == 1.0f);
    CHECK(dragNormalized(0.1f, -500.0f, false) == 0.0f);
}

TEST_CASE("Controles: con ajuste fino el arrastre avanza diez veces menos")
{
    const float coarse = dragNormalized(0.5f, 40.0f, false) - 0.5f;
    const float fine = dragNormalized(0.5f, 40.0f, true) - 0.5f;
    CHECK_THAT(fine, WithinRel(coarse * kFineFactor, 1e-4f));
    CHECK(fine > 0.0f);
}

TEST_CASE("Controles: un arrastre lento se acumula y no se pierde")
{
    // 200 movimientos de 1 pixel = un movimiento de 200 pixeles.
    float position = 0.0f;
    for (int i = 0; i < 200; ++i)
        position = dragNormalized(position, 1.0f, false);
    CHECK_THAT(position, WithinAbs(1.0, 1e-4));

    // Lo mismo con el ajuste fino: 2000 pixeles.
    position = 0.0f;
    for (int i = 0; i < 2000; ++i)
        position = dragNormalized(position, 1.0f, true);
    CHECK_THAT(position, WithinAbs(1.0, 1e-3));
}

TEST_CASE("Controles: entradas no finitas en el arrastre no se propagan")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(dragNormalized(0.3f, nan, false) == 0.3f);
    CHECK(dragNormalized(nan, 10.0f, false) == 0.0f);
    CHECK(dragNormalized(0.3f, std::numeric_limits<float>::infinity(), false) == 0.3f);
}

TEST_CASE("Controles: la rueda mueve los parametros continuos en su escala")
{
    // Frecuencia (logaritmica): el mismo movimiento multiplica igual en graves que en agudos.
    const float low = wheelValue(frequency(), 100.0f, 0.1f, false) / 100.0f;
    const float high = wheelValue(frequency(), 5000.0f, 0.1f, false) / 5000.0f;
    CHECK(low > 1.0f);
    CHECK_THAT(low, WithinRel(high, 1e-3f));

    // Ganancia (lineal): suma lo mismo en cualquier punto.
    const float a = wheelValue(gain(), 0.0f, 0.1f, false) - 0.0f;
    const float b = wheelValue(gain(), -12.0f, 0.1f, false) - -12.0f;
    CHECK_THAT(a, WithinAbs(60.0f * 0.1f * kWheelRangePerUnit, 1e-3));
    CHECK_THAT(a, WithinAbs(b, 1e-3));

    // Hacia abajo, y con ajuste fino.
    CHECK(wheelValue(gain(), 0.0f, -0.1f, false) < 0.0f);
    CHECK_THAT(wheelValue(gain(), 0.0f, 0.1f, true), WithinAbs(a * kFineFactor, 1e-3));

    // Sin movimiento, o con un movimiento no finito, el valor no cambia.
    CHECK(wheelValue(gain(), 3.0f, 0.0f, false) == 3.0f);
    CHECK(wheelValue(gain(), 3.0f, std::numeric_limits<float>::quiet_NaN(), false) == 3.0f);

    // No se sale del rango.
    CHECK(wheelValue(gain(), 29.0f, 5.0f, false) == 30.0f);
}

TEST_CASE("Controles: la rueda y las teclas avanzan las opciones de una en una")
{
    CHECK(wheelValue(type(), 2.0f, 0.01f, false) == 3.0f);
    CHECK(wheelValue(type(), 2.0f, -3.0f, false) == 1.0f);
    CHECK(wheelValue(type(), 8.0f, 1.0f, false) == 8.0f); // ultima opcion
    CHECK(wheelValue(type(), 0.0f, -1.0f, false) == 0.0f);
    CHECK(keyStepValue(type(), 2.0f, 2, false) == 4.0f);
    CHECK(keyStepValue(type(), 2.0f, -1, true) == 1.0f); // el ajuste fino no cambia el paso
    CHECK(keyStepValue(enabled(), 0.0f, 1, false) == 1.0f);
    CHECK(keyStepValue(enabled(), 1.0f, 1, false) == 1.0f);
    CHECK(keyStepValue(enabled(), 1.0f, -1, false) == 0.0f);
}

TEST_CASE("Controles: cada tecla mueve un 1 % del rango, o un 0,1 % con ajuste fino")
{
    CHECK_THAT(keyStepValue(gain(), 0.0f, 1, false), WithinAbs(0.6, 1e-4));
    CHECK_THAT(keyStepValue(gain(), 0.0f, -5, false), WithinAbs(-3.0, 1e-4));
    CHECK_THAT(keyStepValue(gain(), 0.0f, 1, true), WithinAbs(0.06, 1e-4));
    CHECK(keyStepValue(gain(), 30.0f, 1, false) == 30.0f);
}

TEST_CASE("Controles: que parametros son bipolares")
{
    CHECK(isBipolar(gain()));
    CHECK(isBipolar(core::parameters()[core::dynamicParameter(0, core::DynParam::Range)]));
    CHECK(isBipolar(core::parameters()[core::kOutputGain]));

    CHECK_FALSE(isBipolar(frequency()));
    CHECK_FALSE(isBipolar(threshold())); // de -60 a 0: el cero es un extremo
    CHECK_FALSE(isBipolar(core::parameters()[core::bandParameter(0, core::BandParam::Q)]));
    CHECK_FALSE(isBipolar(type()));
    CHECK_FALSE(isBipolar(enabled()));
}

TEST_CASE("Controles: el arco de valor sale del centro en los bipolares y del minimo en el resto")
{
    // Bipolar: en 0 dB no hay arco, y el indicador apunta a las 12.
    auto arc = valueArc(gain(), 0.0f);
    CHECK_THAT(arc.from, WithinAbs(0.0, 1e-6));
    CHECK_THAT(arc.to, WithinAbs(0.0, 1e-6));
    CHECK_THAT(arc.indicator, WithinAbs(0.0, 1e-6));

    arc = valueArc(gain(), 30.0f);
    CHECK_THAT(arc.from, WithinAbs(0.0, 1e-6));
    CHECK_THAT(arc.to, WithinAbs(kArcHalfSpan, 1e-6));

    arc = valueArc(gain(), -15.0f);
    CHECK_THAT(arc.from, WithinAbs(0.0, 1e-6));
    CHECK_THAT(arc.to, WithinAbs(-0.5 * kArcHalfSpan, 1e-5));
    CHECK(arc.indicator == arc.to);

    // No bipolar: desde el minimo.
    arc = valueArc(threshold(), -60.0f);
    CHECK_THAT(arc.from, WithinAbs(-kArcHalfSpan, 1e-6));
    CHECK_THAT(arc.to, WithinAbs(-kArcHalfSpan, 1e-6));

    arc = valueArc(threshold(), 0.0f);
    CHECK_THAT(arc.from, WithinAbs(-kArcHalfSpan, 1e-6));
    CHECK_THAT(arc.to, WithinAbs(kArcHalfSpan, 1e-6));

    // Logaritmico: 632 Hz es el centro geometrico de 20 Hz a 20 kHz.
    arc = valueArc(frequency(), 632.4555f);
    CHECK_THAT(arc.from, WithinAbs(-kArcHalfSpan, 1e-6));
    CHECK_THAT(arc.to, WithinAbs(0.0, 1e-4));

    // Valores fuera de rango se recortan.
    CHECK_THAT(valueArc(gain(), 500.0f).to, WithinAbs(kArcHalfSpan, 1e-6));
}

TEST_CASE("Controles: tramo de valor de una barra horizontal")
{
    auto span = valueSpan(gain(), -15.0f);
    CHECK_THAT(span.from, WithinAbs(0.25, 1e-6));
    CHECK_THAT(span.to, WithinAbs(0.5, 1e-6));
    CHECK_THAT(span.position, WithinAbs(0.25, 1e-6));

    span = valueSpan(gain(), 15.0f);
    CHECK_THAT(span.from, WithinAbs(0.5, 1e-6));
    CHECK_THAT(span.to, WithinAbs(0.75, 1e-6));

    span = valueSpan(threshold(), -30.0f);
    CHECK_THAT(span.from, WithinAbs(0.0, 1e-6));
    CHECK_THAT(span.to, WithinAbs(0.5, 1e-6));
}

TEST_CASE("Controles: el texto de edicion se vuelve a leer como el mismo valor")
{
    // Para todos los parametros y varios puntos del rango: lo que se muestra
    // al abrir el campo, confirmado sin tocar, no mueve el valor mas de lo
    // que se pierde al redondear para mostrarlo.
    for (const auto& spec : core::parameters())
    {
        for (float n : {0.0f, 0.13f, 0.5f, 0.77f, 1.0f})
        {
            const float value = core::fromNormalized(spec, n);
            const auto text = editText(spec, value);
            const auto back = commitText(spec, text);
            INFO(spec.id << " = " << value << " -> \"" << text << "\"");
            REQUIRE(back.has_value());
            CHECK_THAT(core::toNormalized(spec, *back), WithinAbs(core::toNormalized(spec, value), 0.004));
            // Y confirmarlo otra vez ya no lo mueve nada.
            CHECK(editText(spec, *back) == text);
        }
    }
}

TEST_CASE("Controles: confirmar texto escrito a mano")
{
    CHECK(commitText(frequency(), "1.2k") == 1200.0f);
    CHECK(commitText(frequency(), "440") == 440.0f);
    CHECK(commitText(frequency(), " 80 hz ") == 80.0f);
    CHECK(commitText(gain(), "-4.5") == -4.5f);
    CHECK(commitText(gain(), "+3 dB") == 3.0f);
    CHECK(commitText(gain(), "99") == 30.0f); // se ajusta al rango
    CHECK(commitText(type(), "low cut") == 3.0f);

    // Lo que no se entiende no cambia el valor.
    CHECK_FALSE(commitText(gain(), "").has_value());
    CHECK_FALSE(commitText(gain(), "abc").has_value());
    CHECK_FALSE(commitText(frequency(), "   ").has_value());
}

TEST_CASE("Controles: la ayuda del modo de fase dice la latencia")
{
    CHECK(phaseModeTooltip(0, 48000.0) == "Phase mode \xC2\xB7 no latency");
    CHECK(phaseModeTooltip(1280, 48000.0) == "Phase mode \xC2\xB7 latency 26.7 ms (1280 samples)");
    CHECK(phaseModeTooltip(17408, 48000.0) == "Phase mode \xC2\xB7 latency 362.7 ms (17408 samples)");
    CHECK(phaseModeTooltip(4608, 0.0) == "Phase mode \xC2\xB7 no latency");
}

TEST_CASE("Controles: ataque y relajacion con los tiempos automaticos")
{
    CHECK(dynamicTimeValueText(true) == "Auto");
    CHECK(dynamicTimeValueText(false).empty());

    CHECK(autoTimingTooltip(1000.0) ==
          "Auto timing \xC2\xB7 attack 2.00 ms, release 20.0 ms (200 ms after long notes)");
    CHECK(autoTimingTooltip(80.0) == "Auto timing \xC2\xB7 attack 25.0 ms, release 100 ms (1.00 s after long notes)");
}

TEST_CASE("Controles: el interruptor de la dinamica espectral")
{
    dsp::BandSettings band{true, {dsp::FilterType::Bell, 1000.0, 0.0, 1.0}, {true, -20.0, -6.0, 0.0, 5.0, 100.0}};

    // Solo se puede tocar con la dinamica encendida y en un tipo que la admite.
    CHECK(spectralSwitchEnabled(band));
    for (const auto type : {dsp::FilterType::LowShelf, dsp::FilterType::HighShelf, dsp::FilterType::Tilt})
    {
        band.filter.type = type;
        CHECK(spectralSwitchEnabled(band));
    }
    for (const auto type : {dsp::FilterType::LowCut, dsp::FilterType::HighCut, dsp::FilterType::Notch,
                            dsp::FilterType::BandPass})
    {
        band.filter.type = type;
        CHECK_FALSE(spectralSwitchEnabled(band));
    }
    band.filter.type = dsp::FilterType::Bell;
    band.dynamics.enabled = false;
    CHECK_FALSE(spectralSwitchEnabled(band));

    // La latencia, la de dsp::SpectralDynamics (N + N/4): los mismos ms a 48, 96 y 192 kHz.
    CHECK_THAT(spectralLatencyMs(48000.0), WithinAbs(2560.0 / 48.0, 1e-9));
    CHECK_THAT(spectralLatencyMs(96000.0), WithinAbs(2560.0 / 48.0, 1e-9));
    CHECK_THAT(spectralLatencyMs(192000.0), WithinAbs(2560.0 / 48.0, 1e-9));
    CHECK_THAT(spectralLatencyMs(44100.0), WithinAbs(2560.0 / 44.1, 1e-9));
    CHECK(spectralLatencyMs(0.0) == 0.0);
}

TEST_CASE("Controles: la ayuda de la dinamica espectral dice la latencia y el hueco")
{
    dsp::BandSettings band{true, {dsp::FilterType::Bell, 1000.0, 0.0, 1.0}, {true, -20.0, -6.0, 0.0, 5.0, 100.0}};
    const std::string tail = " \xC2\xB7 only the frequencies above the threshold move, not the whole band. Turning on "
                             "the first spectral band, or off the last one, changes the latency and leaves a short "
                             "gap, also when automated";

    CHECK(spectralTooltip(band, 48000.0) == "Spectral \xC2\xB7 adds 53.3 ms latency" + tail);
    CHECK(spectralTooltip(band, 44100.0) == "Spectral \xC2\xB7 adds 58.0 ms latency" + tail);
    CHECK(spectralTooltip(band, 192000.0) == "Spectral \xC2\xB7 adds 53.3 ms latency" + tail);

    // Encendido, pero sin dinamica (o en un tipo sin ella): la latencia sigue y se dice.
    band.dynamics.spectral = true;
    CHECK(spectralTooltip(band, 48000.0) == "Spectral \xC2\xB7 adds 53.3 ms latency" + tail);
    band.dynamics.enabled = false;
    const std::string off = "Spectral \xC2\xB7 adds 53.3 ms latency, also with dynamics off: turn dynamics on to "
                            "switch it off" + tail;
    CHECK(spectralTooltip(band, 48000.0) == off);
    band.dynamics.enabled = true;
    band.filter.type = dsp::FilterType::Notch;
    CHECK(spectralTooltip(band, 48000.0) == off);
    // Apagado y desactivado: nada que avisar.
    band.dynamics.spectral = false;
    CHECK(spectralTooltip(band, 48000.0) == "Spectral \xC2\xB7 adds 53.3 ms latency" + tail);
}

TEST_CASE("Controles: texto del boton de la compensacion automatica")
{
    CHECK(autoGainLabel(false, -2.3) == "Auto");
    CHECK(autoGainLabel(true, -2.34) == "Auto -2.3 dB");
    CHECK(autoGainLabel(true, 1.0) == "Auto +1.0 dB");
    CHECK(autoGainLabel(true, -12.0) == "Auto -12.0 dB");
    // Lo que redondea a 0 no lleva signo.
    CHECK(autoGainLabel(true, 0.0) == "Auto 0.0 dB");
    CHECK(autoGainLabel(true, -0.04) == "Auto 0.0 dB");
    CHECK(autoGainLabel(true, 0.049) == "Auto 0.0 dB");
    CHECK(autoGainLabel(true, -0.06) == "Auto -0.1 dB");

    CHECK(autoGainTooltip(true, -2.3).find("-2.3 dB") != std::string::npos);
    CHECK(autoGainTooltip(false, -2.3).find("off") != std::string::npos);
    CHECK(autoGainTooltip(false, -2.3).find("dB") == std::string::npos);
}
