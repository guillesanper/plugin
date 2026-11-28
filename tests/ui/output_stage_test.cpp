// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/output_stage.h"

#include "esceq/core/parameters.h"
#include "esceq/dsp/phase_equalizer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace esceq;
using namespace esceq::ui;

namespace {

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

constexpr dsp::PhaseMode kLinearModes[] = {dsp::PhaseMode::LinearLow, dsp::PhaseMode::LinearMedium,
                                           dsp::PhaseMode::LinearHigh};

} // namespace

TEST_CASE("Etapa de salida: drive y mezcla solo influyen fuera de Off")
{
    CHECK_FALSE(saturationAmountActive(dsp::SaturationMode::Off));
    CHECK(saturationAmountActive(dsp::SaturationMode::Soft));
    CHECK(saturationAmountActive(dsp::SaturationMode::Warm));

    // Con Off, sus ayudas lo dicen; fuera de Off, no.
    for (const auto mode : {dsp::SaturationMode::Off, dsp::SaturationMode::Soft, dsp::SaturationMode::Warm})
    {
        const bool off = mode == dsp::SaturationMode::Off;
        CHECK(contains(saturationDriveTooltip(mode), "saturation is off") == off);
        CHECK(contains(saturationMixTooltip(mode), "saturation is off") == off);
        CHECK(saturationDriveTooltip(mode).rfind("Saturation drive", 0) == 0);
        CHECK(saturationMixTooltip(mode).rfind("Saturation mix", 0) == 0);
    }
}

TEST_CASE("Etapa de salida: la latencia de la saturacion es la del procesador")
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        INFO(rate);
        CHECK(saturationLatencyMs(dsp::PhaseMode::Iir, rate) == 0.0);
        for (const auto mode : kLinearModes)
        {
            const int samples = dsp::PhaseEqualizer::saturationLatency(mode, rate);
            CHECK(samples > 0);
            CHECK(saturationLatencyMs(mode, rate) == Catch::Approx(1000.0 * samples / rate));
        }
    }
    // 47 muestras a 48 kHz; sin frecuencia de muestreo, nada.
    CHECK(saturationLatencyMs(dsp::PhaseMode::LinearMedium, 48000.0) == Catch::Approx(47.0 / 48.0));
    CHECK(saturationLatencyMs(dsp::PhaseMode::LinearMedium, 0.0) == 0.0);
}

TEST_CASE("Etapa de salida: la ayuda de la saturacion dice la latencia en fase lineal y nada en IIR")
{
    for (const auto mode : kLinearModes)
    {
        CHECK(saturationTooltip(mode, 48000.0).rfind("Saturation \xC2\xB7 adds 0.98 ms latency \xC2\xB7 ", 0) == 0);
        CHECK(contains(saturationTooltip(mode, 44100.0), "adds 1.07 ms latency"));
        CHECK(contains(saturationTooltip(mode, 96000.0), "adds 0.45 ms latency"));
        CHECK(contains(saturationTooltip(mode, 192000.0), "adds 0.18 ms latency"));
        CHECK(contains(saturationTooltip(mode, 48000.0), "short gap"));
    }
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        const auto iir = saturationTooltip(dsp::PhaseMode::Iir, rate);
        CHECK(iir.rfind("Saturation \xC2\xB7 after the EQ", 0) == 0);
        CHECK_FALSE(contains(iir, "latency"));
        CHECK_FALSE(contains(iir, "gap"));
    }
    // Sin frecuencia de muestreo (antes de prepareToPlay), como en IIR.
    CHECK_FALSE(contains(saturationTooltip(dsp::PhaseMode::LinearHigh, 0.0), "latency"));
}

TEST_CASE("Etapa de salida: la pestana marca lo que cambia el sonido")
{
    auto values = core::defaultValues();
    CHECK_FALSE(outputStageActive(values));
    CHECK(outputStageTooltip(values, true) == "Hide the output stage (saturation, output gain, Auto gain and macros)");
    CHECK(outputStageTooltip(values, false) == "Show the output stage: saturation, output gain, Auto gain and macros");

    // El drive y la mezcla, con Off, no cuentan.
    values[core::kSaturationDrive] = 18.0f;
    values[core::kSaturationMix] = 50.0f;
    CHECK_FALSE(outputStageActive(values));

    values[core::kSaturationMode] = 1.0f; // Soft
    CHECK(outputStageActive(values));
    CHECK(contains(outputStageTooltip(values, false), "Saturation Soft, drive 18.0 dB, mix 50%"));
    values[core::kSaturationMode] = 2.0f; // Warm
    CHECK(contains(outputStageTooltip(values, false), "Saturation Warm"));
    values[core::kSaturationMode] = 0.0f;

    values[core::kOutputGain] = -3.0f;
    CHECK(outputStageActive(values));
    CHECK(contains(outputStageTooltip(values, false), "Output -3.0 dB"));
    values[core::kOutputGain] = 0.0f;

    values[core::kAutoGain] = 1.0f;
    CHECK(outputStageActive(values));
    CHECK(contains(outputStageTooltip(values, false), "Auto gain on"));

    // Abierto se ve todo: la ayuda no lo repite.
    values[core::kSaturationMode] = 1.0f;
    values[core::kOutputGain] = 6.0f;
    CHECK(outputStageTooltip(values, true) == "Hide the output stage (saturation, output gain, Auto gain and macros)");
}

TEST_CASE("Etapa de salida: plegado, la pestana marca tambien las macros que mueven algo")
{
    auto values = core::defaultValues();
    core::MacroAssignments macros;
    REQUIRE(macros.assign(0, core::bandParameter(2, core::BandParam::Gain), 25.0f));
    // Con destinos pero en 0, no cambian el sonido.
    CHECK_FALSE(outputStageActive(values, macros));
    CHECK_FALSE(contains(outputStageTooltip(values, false, macros), "Macros"));

    values[core::macroParameter(0)] = 60.0f;
    CHECK(outputStageActive(values, macros));
    CHECK(contains(outputStageTooltip(values, false, macros), "Macros: 1 link moving"));
    // Sin las asignaciones (o sin destinos), la macro no cuenta.
    CHECK_FALSE(outputStageActive(values));
    CHECK_FALSE(outputStageActive(values, core::MacroAssignments{}));
    // Abierto se ve: la ayuda no lo repite.
    CHECK(outputStageTooltip(values, true, macros) ==
          "Hide the output stage (saturation, output gain, Auto gain and macros)");
}
