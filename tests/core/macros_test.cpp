// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/macros.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace esceq::core;
using Catch::Approx;

namespace {

const std::size_t kGain1 = bandParameter(0, BandParam::Gain);
const std::size_t kGain2 = bandParameter(1, BandParam::Gain);
const std::size_t kFreq1 = bandParameter(0, BandParam::Frequency);
const std::size_t kFreq2 = bandParameter(1, BandParam::Frequency);

ParameterValues withMacro(int macro, float percent)
{
    auto values = defaultValues();
    values[macroParameter(macro)] = percent;
    return values;
}

} // namespace

TEST_CASE("Macros: ocho parametros al final de la tabla")
{
    // PLAN.md 5.9: macro_1 a macro_8, de 0 a 100 %, 0 por defecto (índices 391 a 398).
    CHECK(kNumMacros == 8);
    CHECK(macroParameter(0) == 391);
    CHECK(macroParameter(7) == kNumParameters - 1);
    CHECK(kFirstMacroParameter == kSaturationMix + 1);
    for (int macro = 0; macro < kNumMacros; ++macro)
    {
        const auto& spec = parameters()[macroParameter(macro)];
        INFO(spec.id);
        CHECK(spec.id == "macro_" + std::to_string(macro + 1));
        CHECK(spec.name == "Macro " + std::to_string(macro + 1));
        CHECK(spec.unit == "%");
        CHECK(spec.kind == ParameterKind::Continuous);
        CHECK(spec.scale == ParameterScale::Linear);
        CHECK(spec.min == 0.0f);
        CHECK(spec.max == 100.0f);
        CHECK(spec.defaultValue == 0.0f);
        CHECK(findParameter(spec.id) == macroParameter(macro));
        CHECK_FALSE(isMacroTarget(macroParameter(macro))); // una macro no mueve otra
    }
}

TEST_CASE("Macros: los destinos validos son los continuos que no cambian estructura ni latencia")
{
    // La lista de PLAN.md 5.9.
    std::size_t count = 0;
    for (std::size_t i = 0; i < kNumParameters; ++i)
        if (isMacroTarget(i))
        {
            ++count;
            CHECK(parameters()[i].kind == ParameterKind::Continuous);
        }
    CHECK(count == 24 * 3 + 24 * 5 + 3);

    CHECK(isMacroTarget(kOutputGain));
    CHECK(isMacroTarget(kSaturationDrive));
    CHECK(isMacroTarget(kSaturationMix));
    for (int band : {0, 11, 23})
    {
        INFO("banda " << band);
        for (auto p : {BandParam::Frequency, BandParam::Gain, BandParam::Q})
            CHECK(isMacroTarget(bandParameter(band, p)));
        for (auto p : {BandParam::Enabled, BandParam::Type, BandParam::Slope})
            CHECK_FALSE(isMacroTarget(bandParameter(band, p)));
        for (auto p : {DynParam::Threshold, DynParam::Range, DynParam::Attack, DynParam::Release, DynParam::Knee})
            CHECK(isMacroTarget(dynamicParameter(band, p)));
        for (auto p : {DynParam::Enabled, DynParam::Source})
            CHECK_FALSE(isMacroTarget(dynamicParameter(band, p)));
        CHECK_FALSE(isMacroTarget(channelParameter(band)));
        CHECK_FALSE(isMacroTarget(autoTimingParameter(band)));
        CHECK_FALSE(isMacroTarget(spectralParameter(band)));
    }
    for (auto index : {kBypass, kPhaseMode, kAutoGain, kSaturationMode})
        CHECK_FALSE(isMacroTarget(index));
    CHECK_FALSE(isMacroTarget(kNumParameters));
}

TEST_CASE("Macros: asignar, cambiar la profundidad y quitar")
{
    MacroAssignments a;
    CHECK(a.empty());
    CHECK(a.assign(0, kGain1, 50.0f));
    CHECK_FALSE(a.empty());
    CHECK(a.depth(0, kGain1) == 50.0f);
    CHECK_FALSE(a.depth(1, kGain1).has_value());

    // El mismo destino en la misma macro: cambia la profundidad, no ocupa otro hueco.
    CHECK(a.assign(0, kGain1, -25.0f));
    CHECK(a.depth(0, kGain1) == -25.0f);
    CHECK(a.targets(0)[0] == MacroTarget{kGain1, -25.0f});
    CHECK_FALSE(a.targets(0)[1].used());

    // Limitada a ±100 %; lo que no es un número, no se asigna.
    CHECK(a.assign(0, kGain2, 250.0f));
    CHECK(a.depth(0, kGain2) == 100.0f);
    CHECK(a.assign(0, kFreq1, -1000.0f));
    CHECK(a.depth(0, kFreq1) == -100.0f);
    CHECK_FALSE(a.assign(0, kFreq2, std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(a.assign(0, kFreq2, std::numeric_limits<float>::infinity()));

    // Ni destinos que no valen ni macros que no existen.
    CHECK_FALSE(a.assign(0, kBypass, 10.0f));
    CHECK_FALSE(a.assign(0, macroParameter(1), 10.0f));
    CHECK_FALSE(a.assign(-1, kFreq2, 10.0f));
    CHECK_FALSE(a.assign(kNumMacros, kFreq2, 10.0f));

    // Quitar deja los demás huecos en su sitio, y el libre se vuelve a usar.
    CHECK(a.remove(0, kGain2));
    CHECK_FALSE(a.remove(0, kGain2));
    CHECK(a.targets(0)[2] == MacroTarget{kFreq1, -100.0f});
    CHECK(a.assign(0, kFreq2, 10.0f));
    CHECK(a.targets(0)[1] == MacroTarget{kFreq2, 10.0f});

    a.clear();
    CHECK(a.empty());
    CHECK(a == MacroAssignments{});
}

TEST_CASE("Macros: como mucho ocho destinos por macro")
{
    MacroAssignments a;
    for (int band = 0; band < kMaxMacroTargets; ++band)
        CHECK(a.assign(3, bandParameter(band, BandParam::Gain), 10.0f));
    CHECK_FALSE(a.assign(3, bandParameter(8, BandParam::Gain), 10.0f));
    // Cambiar la profundidad de uno que ya está sí se puede.
    CHECK(a.assign(3, bandParameter(2, BandParam::Gain), 20.0f));
    // Y las demás macros no se ven afectadas.
    CHECK(a.assign(4, bandParameter(8, BandParam::Gain), 10.0f));
}

TEST_CASE("Macros: poner un hueco como al leer el estado")
{
    MacroAssignments a;
    CHECK(a.setSlot(1, 5, kGain1, 30.0f));
    CHECK(a.targets(1)[5] == MacroTarget{kGain1, 30.0f});
    CHECK_FALSE(a.targets(1)[0].used());
    // El mismo destino en otro hueco de la misma macro, no; en el mismo hueco, sí.
    CHECK_FALSE(a.setSlot(1, 2, kGain1, 10.0f));
    CHECK(a.setSlot(1, 5, kGain1, -40.0f));
    CHECK(a.depth(1, kGain1) == -40.0f);
    // En otra macro, sí.
    CHECK(a.setSlot(2, 0, kGain1, 10.0f));
    for (int slot : {-1, kMaxMacroTargets})
        CHECK_FALSE(a.setSlot(1, slot, kGain2, 10.0f));
    CHECK_FALSE(a.setSlot(kNumMacros, 0, kGain2, 10.0f));
    CHECK_FALSE(a.setSlot(1, 0, kBypass, 10.0f));
    CHECK_FALSE(a.setSlot(1, 0, kGain2, std::numeric_limits<float>::quiet_NaN()));
    CHECK(a.setSlot(1, 0, kGain2, 101.0f));
    CHECK(a.depth(1, kGain2) == 100.0f);
}

TEST_CASE("Macros: al 100 % con +20 % sobre una ganancia en -12 dB la lleva a 0 dB")
{
    // Criterio de PLAN.md 5.9 (corregido con el usuario en el bloque 7.2: la
    // ganancia va de -30 a +30 dB, y +20 % de su recorrido son 12 dB). El
    // valor del host (el de la tabla de parámetros) no se toca: applyMacros
    // trabaja sobre una copia.
    MacroAssignments a;
    REQUIRE(a.assign(0, kGain1, 20.0f));
    auto host = withMacro(0, 100.0f);
    host[kGain1] = -12.0f;
    auto sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == 0.0f);
    CHECK(host[kGain1] == -12.0f);

    // Con +50 %, medio potenciómetro: de -30 a 0 dB.
    REQUIRE(a.assign(0, kGain1, 50.0f));
    host[kGain1] = -30.0f;
    sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == 0.0f);

    // Al 50 %, la mitad.
    host[macroParameter(0)] = 50.0f;
    sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == -15.0f);

    // Lo demás, como estaba.
    for (std::size_t i = 0; i < kNumParameters; ++i)
        if (i != kGain1)
            CHECK(sounding[i] == host[i]);
}

TEST_CASE("Macros: dos destinos y un automation clip se suman y se limitan al rango")
{
    // Criterio de PLAN.md 5.9: la macro suma lo mismo a sus dos destinos; el
    // automation clip mueve el valor de base de uno de ellos, y la suma se
    // limita al rango del parámetro.
    MacroAssignments a;
    REQUIRE(a.assign(2, kGain1, 25.0f));
    REQUIRE(a.assign(2, kGain2, 25.0f));
    auto host = withMacro(2, 100.0f); // las dos ganancias en 0 dB
    auto sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == 15.0f);
    CHECK(sounding[kGain2] == 15.0f);

    // El clip lleva la base del primero a +6 dB: 21 dB.
    host[kGain1] = 6.0f;
    sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == 21.0f);
    CHECK(sounding[kGain2] == 15.0f);

    // A +20 dB: 35 dB no cabe, se queda en +30.
    host[kGain1] = 20.0f;
    sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == 30.0f);

    // Y hacia abajo, en -30.
    REQUIRE(a.assign(2, kGain1, -100.0f));
    sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == -30.0f);
}

TEST_CASE("Macros: varias macros sobre un destino se suman")
{
    MacroAssignments a;
    REQUIRE(a.assign(0, kGain1, 10.0f));
    REQUIRE(a.assign(5, kGain1, 20.0f));
    auto host = withMacro(0, 100.0f);
    host[macroParameter(5)] = 50.0f;
    auto sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding[kGain1] == 12.0f); // (0,1 + 0,1) × 60 dB

    // Si se anulan, el destino queda bit a bit como estaba.
    REQUIRE(a.assign(5, kGain1, -20.0f));
    host[kGain1] = 1.2345678f;
    sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding == host);
}

TEST_CASE("Macros: en la frecuencia, las mismas octavas en graves que en agudos")
{
    // Regla de PLAN.md 5.9: la suma va en la posición del potenciómetro, que
    // en la frecuencia es logarítmica (20 Hz a 20 kHz, casi 10 octavas).
    MacroAssignments a;
    REQUIRE(a.assign(0, kFreq1, 10.0f));
    REQUIRE(a.assign(0, kFreq2, 10.0f));
    auto host = withMacro(0, 100.0f);
    host[kFreq1] = 100.0f;
    host[kFreq2] = 2000.0f;
    auto sounding = host;
    applyMacros(a, sounding);
    const double octaves = std::log2(1000.0) / 10.0;
    CHECK(std::log2(sounding[kFreq1] / 100.0) == Approx(octaves).epsilon(1e-3));
    CHECK(std::log2(sounding[kFreq2] / 2000.0) == Approx(octaves).epsilon(1e-3));
}

TEST_CASE("Macros: sin asignaciones o con las macros en 0 no cambia nada, bit a bit")
{
    auto host = defaultValues();
    host[kGain1] = 3.3333333f;
    host[kFreq1] = 1234.5678f;
    host[kOutputGain] = -0.1f;
    for (int macro = 0; macro < kNumMacros; ++macro)
        host[macroParameter(macro)] = 77.0f;

    // Sin asignaciones, aunque las macros estén movidas.
    auto sounding = host;
    applyMacros(MacroAssignments{}, sounding);
    CHECK(sounding == host);

    // Con asignaciones y las macros en 0: valores que no son "redondos" (que
    // no dan la vuelta exactos por la posición) tampoco cambian.
    MacroAssignments a;
    REQUIRE(a.assign(0, kGain1, 40.0f));
    REQUIRE(a.assign(1, kFreq1, -40.0f));
    REQUIRE(a.assign(2, kOutputGain, 100.0f));
    for (int macro = 0; macro < kNumMacros; ++macro)
        host[macroParameter(macro)] = 0.0f;
    sounding = host;
    applyMacros(a, sounding);
    CHECK(sounding == host);

    // Con profundidad 0, igual.
    MacroAssignments zero;
    REQUIRE(zero.assign(0, kGain1, 0.0f));
    host[macroParameter(0)] = 100.0f;
    sounding = host;
    applyMacros(zero, sounding);
    CHECK(sounding == host);
}

TEST_CASE("Macros: la modulacion solo depende de los valores y las asignaciones")
{
    // Lo que suena con unos valores y unas asignaciones es siempre lo mismo:
    // no hay estado entre llamadas (el render no depende del tamaño de bloque).
    MacroAssignments a;
    for (int macro = 0; macro < kNumMacros; ++macro)
        for (int slot = 0; slot < kMaxMacroTargets; ++slot)
            REQUIRE(a.setSlot(macro, slot, bandParameter((macro * 3 + slot) % kNumBands, BandParam::Gain),
                              static_cast<float>(slot * 13 - 50)));
    auto host = defaultValues();
    for (int macro = 0; macro < kNumMacros; ++macro)
        host[macroParameter(macro)] = static_cast<float>(macro * 12);
    auto first = host;
    applyMacros(a, first);
    auto second = host;
    applyMacros(a, second);
    CHECK(first == second);
    CHECK_FALSE(first == host);
    for (std::size_t i = 0; i < kNumParameters; ++i)
        CHECK(first[i] == sanitize(parameters()[i], first[i]));
}
