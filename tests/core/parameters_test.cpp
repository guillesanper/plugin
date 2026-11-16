// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/parameters.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace esceq::core;

namespace {

// CONTRATO PÚBLICO: tests/fixtures/parameters/v1.txt
//
// Una línea por parámetro: id tipo escala min max opciones. El archivo solo
// puede CRECER por el final. Si este test falla, un cambio rompería
// proyectos o automatizaciones guardadas: no edites las líneas existentes;
// lee docs/decisiones/0004-parametros-congelados.md.
//
// Para añadir al final los parámetros nuevos (y SOLO eso):
//   ESCEQ_APPEND_PARAMETERS=1 ./esceq_tests "Parametros: contrato*"
const std::string kContractPath = std::string(ESCEQ_FIXTURES_DIR) + "/parameters/v1.txt";

std::string_view kindName(ParameterKind kind)
{
    switch (kind)
    {
    case ParameterKind::Continuous: return "continuous";
    case ParameterKind::Toggle: return "toggle";
    case ParameterKind::Choice: return "choice";
    }
    return "?";
}

std::string contractLine(const ParameterSpec& spec)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << spec.id << ' ' << kindName(spec.kind) << ' '
        << (spec.scale == ParameterScale::Logarithmic ? "log" : "linear") << ' ' << spec.min << ' ' << spec.max << ' '
        << spec.choices.size();
    return out.str();
}

std::vector<std::string> readContract()
{
    std::vector<std::string> lines;
    std::ifstream in(kContractPath, std::ios::binary);
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty() && line[0] != '#')
            lines.push_back(line);
    }
    return lines;
}

} // namespace

TEST_CASE("Parametros: contrato congelado (id, tipo, escala, rango y posicion)")
{
    const auto& specs = parameters();
    auto contract = readContract();

    if (std::getenv("ESCEQ_APPEND_PARAMETERS") != nullptr && contract.size() < specs.size())
    {
        std::ofstream out(kContractPath, std::ios::binary | std::ios::app);
        REQUIRE(out.is_open());
        if (contract.empty())
            out << "# id tipo escala min max opciones\n";
        for (std::size_t i = contract.size(); i < specs.size(); ++i)
            out << contractLine(specs[i]) << '\n';
        contract = readContract();
    }

    REQUIRE(specs.size() >= contract.size());
    for (std::size_t i = 0; i < contract.size(); ++i)
    {
        INFO("posicion " << i);
        CHECK(contractLine(specs[i]) == contract[i]);
    }

    INFO("Hay parametros nuevos que no estan en el contrato: anadelos con ESCEQ_APPEND_PARAMETERS=1");
    CHECK(specs.size() == contract.size());
}

TEST_CASE("Parametros: la tabla es coherente")
{
    std::set<std::string_view> ids;
    for (const auto& spec : parameters())
    {
        INFO("parametro " << spec.id);
        CHECK_FALSE(spec.id.empty());
        CHECK(spec.id.find_first_of(" \t\r\n") == std::string_view::npos); // el formato de estado separa por espacios
        CHECK(ids.insert(spec.id).second);
        CHECK(spec.min < spec.max);
        CHECK(spec.defaultValue >= spec.min);
        CHECK(spec.defaultValue <= spec.max);
        CHECK(sanitize(spec, spec.defaultValue) == spec.defaultValue);
        if (spec.kind == ParameterKind::Choice)
            CHECK(spec.max == static_cast<float>(spec.choices.size() - 1));
        if (spec.scale == ParameterScale::Logarithmic)
            CHECK(spec.min > 0.0f);
    }
}

TEST_CASE("Parametros: indices de banda")
{
    CHECK(kNumParameters == 2 + 24 * 6 + 24 * 7 + 24 + 2 + 24);
    CHECK(parameters()[bandParameter(0, BandParam::Enabled)].id == "b01_on");
    CHECK(parameters()[bandParameter(0, BandParam::Slope)].id == "b01_slope");
    CHECK(parameters()[bandParameter(9, BandParam::Frequency)].id == "b10_freq");
    CHECK(parameters()[bandParameter(23, BandParam::Q)].id == "b24_q");
}

TEST_CASE("Parametros: indices de dinamica, despues de todas las bandas")
{
    CHECK(dynamicParameter(0, DynParam::Enabled) == bandParameter(23, BandParam::Slope) + 1);
    CHECK(parameters()[dynamicParameter(0, DynParam::Enabled)].id == "b01_dyn");
    CHECK(parameters()[dynamicParameter(0, DynParam::Source)].id == "b01_sc");
    CHECK(parameters()[dynamicParameter(11, DynParam::Threshold)].id == "b12_thr");
    CHECK(parameters()[dynamicParameter(23, DynParam::Knee)].id == "b24_knee");
    CHECK(dynamicParameter(23, DynParam::Source) == channelParameter(0) - 1);
}

TEST_CASE("Parametros: indices de canal, despues de la dinamica")
{
    CHECK(parameters()[channelParameter(0)].id == "b01_chan");
    CHECK(parameters()[channelParameter(23)].id == "b24_chan");
    CHECK(channelParameter(23) + 1 == kPhaseMode);
    CHECK(parameters()[channelParameter(0)].defaultValue == 0.0f); // Stereo: lo que hacian los estados anteriores
    CHECK(formatValue(parameters()[channelParameter(3)], 3.0f) == "Mid");
    CHECK(parseValue(parameters()[channelParameter(3)], "side") == 4.0f);
}

TEST_CASE("Parametros: modo de fase, global, tras los canales")
{
    const auto& spec = parameters()[kPhaseMode];
    CHECK(kPhaseMode + 1 == kAutoGain);
    CHECK(spec.id == "phase_mode");
    CHECK(spec.kind == ParameterKind::Choice);
    CHECK(spec.choices.size() == 4);
    CHECK(spec.defaultValue == 0.0f); // IIR: latencia cero, lo que hacian los estados anteriores
    CHECK(formatValue(spec, 0.0f) == "IIR");
    CHECK(formatValue(spec, 1.0f) == "Linear Low");
    CHECK(formatValue(spec, 2.0f) == "Linear Medium");
    CHECK(formatValue(spec, 3.0f) == "Linear High");
    CHECK(parseValue(spec, "linear high") == 3.0f);
    CHECK(findParameter("phase_mode") == kPhaseMode);
}

TEST_CASE("Parametros: compensacion automatica de ganancia, global, tras el modo de fase")
{
    const auto& spec = parameters()[kAutoGain];
    CHECK(kAutoGain + 1 == autoTimingParameter(0));
    CHECK(spec.id == "auto_gain");
    CHECK(spec.name == "Auto Gain");
    CHECK(spec.kind == ParameterKind::Toggle);
    CHECK(spec.defaultValue == 0.0f); // apagada: lo que hacian los estados anteriores
    CHECK(findParameter("auto_gain") == kAutoGain);
}

TEST_CASE("Parametros: findParameter")
{
    CHECK(findParameter("output_gain") == kOutputGain);
    CHECK(findParameter("bypass") == kBypass);
    CHECK(findParameter("b07_gain") == bandParameter(6, BandParam::Gain));
    CHECK_FALSE(findParameter("no_existe").has_value());
}

TEST_CASE("Parametros: sanitize")
{
    const auto& gain = parameters()[kOutputGain];
    CHECK(sanitize(gain, 3.0f) == 3.0f);
    CHECK(sanitize(gain, 100.0f) == 24.0f);
    CHECK(sanitize(gain, -100.0f) == -24.0f);
    CHECK(sanitize(gain, std::numeric_limits<float>::quiet_NaN()) == gain.defaultValue);
    CHECK(sanitize(gain, std::numeric_limits<float>::infinity()) == gain.defaultValue);

    const auto& bypass = parameters()[kBypass];
    CHECK(sanitize(bypass, 0.7f) == 1.0f);
    CHECK(sanitize(bypass, 0.2f) == 0.0f);
    CHECK(sanitize(bypass, 5.0f) == 1.0f);

    const auto& type = parameters()[bandParameter(0, BandParam::Type)];
    CHECK(sanitize(type, 2.4f) == 2.0f);
    CHECK(sanitize(type, 2.6f) == 3.0f);
    CHECK(sanitize(type, -3.0f) == 0.0f);
    CHECK(sanitize(type, 99.0f) == 8.0f);
}

TEST_CASE("Parametros: normalizacion lineal, logaritmica y de opciones")
{
    const auto& gain = parameters()[bandParameter(0, BandParam::Gain)];
    CHECK(toNormalized(gain, 0.0f) == 0.5f);
    CHECK(fromNormalized(gain, 0.25f) == -15.0f);

    // Frecuencia logarítmica: 20 Hz - 20 kHz son 3 décadas; 632 Hz es la mitad.
    const auto& freq = parameters()[bandParameter(0, BandParam::Frequency)];
    CHECK(toNormalized(freq, 20.0f) == 0.0f);
    CHECK(toNormalized(freq, 20000.0f) == 1.0f);
    CHECK_THAT(fromNormalized(freq, 0.5f), WithinRel(632.4555f, 1e-4f));
    CHECK_THAT(toNormalized(freq, 200.0f), WithinAbs(1.0 / 3.0, 1e-6));

    const auto& slope = parameters()[bandParameter(0, BandParam::Slope)];
    CHECK(toNormalized(slope, 0.0f) == 0.0f);
    CHECK(toNormalized(slope, 15.0f) == 1.0f);
    CHECK(fromNormalized(slope, 0.5f) == 8.0f); // 7,5 redondea a 8

    // Ida y vuelta en todos los parámetros.
    for (const auto& spec : parameters())
        for (float n : {0.0f, 0.1f, 0.37f, 0.5f, 0.93f, 1.0f})
        {
            INFO(spec.id << " n " << n);
            const float value = fromNormalized(spec, n);
            // Error de redondeo de float: como mucho una millonésima del rango.
            CHECK_THAT(fromNormalized(spec, toNormalized(spec, value)), WithinAbs(value, (spec.max - spec.min) * 1e-6));
        }
}

TEST_CASE("Parametros: formatValue")
{
    const auto& freq = parameters()[bandParameter(0, BandParam::Frequency)];
    CHECK(formatValue(freq, 50.0f) == "50.0 Hz");
    CHECK(formatValue(freq, 350.0f) == "350 Hz");
    CHECK(formatValue(freq, 1200.0f) == "1.20 kHz");
    CHECK(formatValue(freq, 15000.0f) == "15.0 kHz");

    const auto& gain = parameters()[bandParameter(0, BandParam::Gain)];
    CHECK(formatValue(gain, 3.0f) == "+3.0 dB");
    CHECK(formatValue(gain, -4.56f) == "-4.6 dB");
    CHECK(formatValue(gain, -0.01f) == "0.0 dB");

    CHECK(formatValue(parameters()[bandParameter(0, BandParam::Type)], 3.0f) == "Low Cut");
    CHECK(formatValue(parameters()[bandParameter(0, BandParam::Slope)], 3.0f) == "24 dB/oct");
    CHECK(formatValue(parameters()[kBypass], 1.0f) == "On");
    CHECK(formatValue(parameters()[bandParameter(0, BandParam::Q)], 0.7071f) == "0.71");

    const auto& attack = parameters()[dynamicParameter(0, DynParam::Attack)];
    CHECK(formatValue(attack, 0.5f) == "0.50 ms");
    CHECK(formatValue(attack, 12.0f) == "12.0 ms");
    CHECK(formatValue(attack, 150.0f) == "150 ms");
    CHECK(formatValue(parameters()[dynamicParameter(0, DynParam::Release)], 1500.0f) == "1.50 s");
    CHECK(formatValue(parameters()[dynamicParameter(0, DynParam::Source)], 1.0f) == "External");
}

TEST_CASE("Parametros: parseValue")
{
    const auto& freq = parameters()[bandParameter(0, BandParam::Frequency)];
    CHECK(parseValue(freq, "1.2k") == 1200.0f);
    CHECK(parseValue(freq, " 1.2 kHz ") == 1200.0f);
    CHECK(parseValue(freq, "350 Hz") == 350.0f);
    CHECK(parseValue(freq, "5") == 20.0f); // se ajusta al rango
    CHECK_FALSE(parseValue(freq, "abc").has_value());
    CHECK_FALSE(parseValue(freq, "").has_value());

    const auto& type = parameters()[bandParameter(0, BandParam::Type)];
    CHECK(parseValue(type, "low cut") == 3.0f);
    CHECK(parseValue(type, "Tilt") == 8.0f);
    CHECK(parseValue(type, "2") == 2.0f);

    CHECK(parseValue(parameters()[kBypass], "on") == 1.0f);
    CHECK(parseValue(parameters()[bandParameter(0, BandParam::Gain)], "-3.5 dB") == -3.5f);

    const auto& release = parameters()[dynamicParameter(0, DynParam::Release)];
    CHECK(parseValue(release, "250 ms") == 250.0f);
    CHECK(parseValue(release, "1.5 s") == 1500.0f);
    CHECK(parseValue(release, "2s") == 2000.0f);
    CHECK(parseValue(parameters()[dynamicParameter(0, DynParam::Source)], "external") == 1.0f);

    // El texto que se muestra siempre se puede volver a leer.
    for (const auto& spec : parameters())
        for (float n : {0.0f, 0.3f, 0.77f, 1.0f})
        {
            const float value = fromNormalized(spec, n);
            const auto parsed = parseValue(spec, formatValue(spec, value));
            INFO(spec.id << " " << formatValue(spec, value));
            REQUIRE(parsed.has_value());
            CHECK_THAT(*parsed, WithinRel(value, 0.01f) || WithinAbs(value, 0.051));
        }
}

TEST_CASE("Parametros: los valores redondos dan la vuelta exactos por la posicion del host")
{
    // El host guarda cada parametro como una posicion 0..1 en float. Al
    // cargar un estado o un preset, cada valor pasa por ahi: si no volviera
    // exacto, un preset recien cargado ya no coincidiria con su archivo.
    for (const auto& spec : parameters())
    {
        INFO(spec.id);
        CHECK(fromNormalized(spec, toNormalized(spec, spec.defaultValue)) == spec.defaultValue);
        CHECK(fromNormalized(spec, toNormalized(spec, spec.min)) == spec.min);
        CHECK(fromNormalized(spec, toNormalized(spec, spec.max)) == spec.max);
    }

    auto roundTrips = [](std::size_t index, std::initializer_list<float> values) {
        const auto& spec = parameters()[index];
        for (const float value : values)
        {
            INFO(spec.id << " = " << value);
            CHECK(fromNormalized(spec, toNormalized(spec, value)) == value);
        }
    };
    roundTrips(bandParameter(0, BandParam::Gain), {6.0f, -6.0f, 3.0f, -4.5f, 2.5f, 0.1f, -0.1f, 12.3f, -29.9f, 1.25f});
    roundTrips(bandParameter(0, BandParam::Frequency), {25.0f, 30.0f, 90.0f, 120.0f, 250.0f, 432.0f, 777.0f, 2500.0f, 6500.0f, 12345.0f});
    roundTrips(bandParameter(0, BandParam::Q), {0.7071f, 1.0f, 1.2f, 1.4f, 2.0f, 4.0f, 5.0f, 0.8f, 12.5f});
    roundTrips(dynamicParameter(0, DynParam::Threshold), {-30.0f, -28.0f, -41.0f, -0.5f, -59.9f});
    roundTrips(dynamicParameter(0, DynParam::Attack), {0.5f, 1.0f, 3.0f, 5.0f, 12.5f, 150.0f});
    roundTrips(dynamicParameter(0, DynParam::Release), {60.0f, 80.0f, 100.0f, 250.0f, 1500.0f});
    roundTrips(kOutputGain, {5.0f, -2.0f, -7.3125f, 3.0f, -4.0f});
}

TEST_CASE("Parametros: redondear el valor no cambia la posicion ni el orden")
{
    // El contrato de automatizacion: una posicion siempre da un valor que se
    // normaliza a esa misma posicion, y posiciones mayores dan valores mayores.
    std::uint32_t rng = 2024u;
    for (const std::size_t index : {static_cast<std::size_t>(kOutputGain), bandParameter(0, BandParam::Frequency),
                                    bandParameter(0, BandParam::Gain), bandParameter(0, BandParam::Q),
                                    dynamicParameter(0, DynParam::Threshold), dynamicParameter(0, DynParam::Attack),
                                    dynamicParameter(0, DynParam::Release), dynamicParameter(0, DynParam::Knee)})
    {
        const auto& spec = parameters()[index];
        INFO(spec.id);
        for (int i = 0; i < 2000; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            const float n = static_cast<float>(rng >> 8) / 16777216.0f;
            const float value = fromNormalized(spec, n);
            // Una segunda vuelta ya no mueve nada: el estado guardado es estable.
            const float again = fromNormalized(spec, toNormalized(spec, value));
            CHECK(again == value);
            // Y el valor esta donde dice la formula, salvo la resolucion del float.
            const double expected = spec.scale == ParameterScale::Logarithmic
                                        ? spec.min * std::pow(static_cast<double>(spec.max) / spec.min, static_cast<double>(n))
                                        : spec.min + static_cast<double>(n) * (spec.max - spec.min);
            CHECK_THAT(static_cast<double>(value), WithinRel(expected, 2e-6) || WithinAbs(expected, 4e-6));
        }

        float previous = fromNormalized(spec, 0.0f);
        for (int i = 1; i <= 4000; ++i)
        {
            const float value = fromNormalized(spec, static_cast<float>(i) / 4000.0f);
            CHECK(value > previous);
            previous = value;
        }
    }
}

TEST_CASE("Parametros: tiempos automaticos, uno por banda, los ultimos")
{
    CHECK(autoTimingParameter(0) == kAutoGain + 1);
    CHECK(autoTimingParameter(23) == kNumParameters - 1);
    CHECK(parameters()[autoTimingParameter(0)].id == "b01_autotime");
    CHECK(parameters()[autoTimingParameter(23)].id == "b24_autotime");
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto& spec = parameters()[autoTimingParameter(band)];
        INFO(spec.id);
        CHECK(spec.name == "Band " + std::to_string(band + 1) + " Auto Timing");
        CHECK(spec.kind == ParameterKind::Toggle);
        CHECK(spec.defaultValue == 0.0f); // apagados: lo que hacian los estados anteriores
        CHECK(findParameter(spec.id) == autoTimingParameter(band));
    }
}
