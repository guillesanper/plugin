// SPDX-License-Identifier: MIT
#include "esceq/dsp/curve_match.h"

#include "esceq/dsp/auto_gain.h"
#include "esceq/dsp/filter_design.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace esceq::dsp;

namespace {

constexpr double kRate = 48000.0;

/// La rejilla de la sesión de espectros (core::SpectrumSession): 192 puntos
/// logarítmicos de 20 Hz a 20 kHz. dsp no ve core: la rejilla entra como argumento.
std::vector<double> sessionGrid()
{
    std::vector<double> grid(192);
    for (std::size_t i = 0; i < grid.size(); ++i)
        grid[i] = 20.0 * std::pow(1000.0, static_cast<double>(i) / 191.0);
    return grid;
}

FilterParams filter(FilterType type, double frequencyHz, double gainDb, double q)
{
    FilterParams params;
    params.type = type;
    params.frequencyHz = frequencyHz;
    params.gainDb = gainDb;
    params.q = q;
    return params;
}

BandSettings enabled(const FilterParams& params)
{
    BandSettings band;
    band.enabled = true;
    band.filter = params;
    return band;
}

/// Curva de unas bandas, en dB, en `frequencies`.
std::vector<double> curveDb(std::span<const FilterParams> bands, std::span<const double> frequencies,
                            double sampleRate = kRate)
{
    std::vector<double> out(frequencies.size(), 0.0);
    for (const FilterParams& params : bands)
    {
        const BandDesign design = designBand(params, sampleRate);
        for (std::size_t i = 0; i < frequencies.size(); ++i)
            out[i] += magnitudeDb(design, frequencies[i], sampleRate);
    }
    return out;
}

/// Una pista (ruido rosa visto por el analizador: plano a -20 dB) y la
/// referencia: la misma pista por `bands`.
struct Spectra
{
    std::vector<double> frequencies = sessionGrid();
    std::vector<float> track;
    std::vector<float> reference;

    explicit Spectra(std::span<const FilterParams> bands = {})
    {
        const std::vector<double> curve = curveDb(bands, frequencies);
        for (std::size_t i = 0; i < frequencies.size(); ++i)
        {
            track.push_back(-20.0f);
            reference.push_back(static_cast<float>(-20.0 + curve[i]));
        }
    }

    CurveMatchInput input(std::span<const BandSettings> current = {}) const
    {
        CurveMatchInput in;
        in.frequencies = frequencies;
        in.trackDb = track;
        in.referenceDb = reference;
        in.currentBands = current;
        in.sampleRate = kRate;
        return in;
    }
};

CurveMatchSettings fullAmount(int maxBands = 8)
{
    CurveMatchSettings settings;
    settings.amount = 1.0;
    settings.maxBands = maxBands;
    return settings;
}

/// Error RMS (en log f, dentro de [lowHz, highHz]) entre lo que añaden las
/// bandas propuestas y `expectedDb`, con el nivel libre: el nivel no se iguala.
double rmsErrorModuloLevel(const CurveMatchResult& result, std::span<const double> expectedDb, double lowHz,
                           double highHz)
{
    std::vector<double> difference;
    for (std::size_t i = 0; i < result.frequencies.size(); ++i)
        if (result.frequencies[i] >= lowHz && result.frequencies[i] <= highHz)
            difference.push_back(result.proposedDb[i] - result.currentDb[i] - expectedDb[i]);
    double mean = 0.0;
    for (const double d : difference)
        mean += d;
    mean /= static_cast<double>(difference.size());
    double sum = 0.0;
    for (const double d : difference)
        sum += (d - mean) * (d - mean);
    return std::sqrt(sum / static_cast<double>(difference.size()));
}

/// 24 campanas estrechas y separadas, de ±5 a ±9 dB: un objetivo que pide
/// todas las bandas (el mismo que mide esceq_bench --match).
std::vector<FilterParams> twentyFourBells()
{
    std::vector<FilterParams> bands;
    for (int i = 0; i < 24; ++i)
    {
        const double gain = (i % 2 == 0 ? 1.0 : -1.0) * (5.0 + static_cast<double>(i * 7 % 5));
        bands.push_back(filter(FilterType::Bell, 32.0 * std::exp2(0.36 * i), gain, 4.0));
    }
    return bands;
}

void checkLimits(const CurveMatchResult& result, const CurveMatchSettings& settings)
{
    for (const FilterParams& band : result.bands)
    {
        CHECK((band.type == FilterType::Bell || band.type == FilterType::LowShelf ||
               band.type == FilterType::HighShelf));
        CHECK(band.frequencyHz >= settings.lowHz * (1.0 - 1e-12));
        CHECK(band.frequencyHz <= settings.highHz * (1.0 + 1e-12));
        CHECK(std::abs(band.gainDb) <= kMatchMaxGainDb);
        CHECK(std::abs(band.gainDb) >= kMatchMinBandGainDb);
        if (band.type == FilterType::Bell)
        {
            CHECK(band.q >= kMatchMinBellQ * (1.0 - 1e-12));
            CHECK(band.q <= kMatchMaxBellQ * (1.0 + 1e-12));
        }
        else
        {
            CHECK(band.q >= kMatchMinShelfQ * (1.0 - 1e-12));
            CHECK(band.q <= kMatchMaxShelfQ * (1.0 + 1e-12));
        }
        CHECK(band.slopeDbPerOct == FilterParams{}.slopeDbPerOct);
    }
    for (std::size_t k = 1; k < result.bands.size(); ++k)
        CHECK(result.bands[k - 1].frequencyHz <= result.bands[k].frequencyHz);
}

} // namespace

TEST_CASE("qForBandwidth: Q del ancho a -3 dB, en dsp", "[curve_match]")
{
    CHECK_THAT(qForBandwidth(1.0), WithinRel(std::sqrt(2.0), 1e-12));
    CHECK_THAT(qForBandwidth(2.0), WithinRel(2.0 / 3.0, 1e-12));
    CHECK(std::isinf(qForBandwidth(0.0)));
    CHECK(std::isinf(qForBandwidth(-1.0)));
    CHECK(std::isinf(qForBandwidth(std::nan(""))));
}

TEST_CASE("CurveMatch: tres campanas conocidas se recuperan", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::Bell, 200.0, 4.0, 1.0),
                                  filter(FilterType::Bell, 2000.0, -3.0, 2.0),
                                  filter(FilterType::Bell, 8000.0, 5.0, 0.7)};
    const Spectra spectra(known);
    for (const double smoothing : {0.0, 1.0 / 3.0})
    {
        CAPTURE(smoothing);
        CurveMatchSettings settings = fullAmount();
        settings.smoothingOctaves = smoothing;
        const CurveMatchResult result = matchCurve(spectra.input(), settings);

        CHECK(result.finalRmsDb < kMatchStopErrorDb);
        CHECK(result.bands.size() <= 6);
        CHECK(result.initialRmsDb > 1.0);
        checkLimits(result, settings);
        // Tres campanas en las frecuencias de la referencia. Sus ganancias y
        // sus Q se apartan algo de las de la referencia porque el nivel no se
        // iguala (docs/dsp/igualacion.md); la curva, sin el nivel, es la misma.
        REQUIRE(result.bands.size() == 3);
        for (std::size_t k = 0; k < 3; ++k)
        {
            CHECK(result.bands[k].type == FilterType::Bell);
            CHECK_THAT(std::log2(result.bands[k].frequencyHz / known[k].frequencyHz), WithinAbs(0.0, 0.03));
            CHECK_THAT(result.bands[k].gainDb, WithinAbs(known[k].gainDb, 0.5));
        }
        CHECK(rmsErrorModuloLevel(result, curveDb(known, result.frequencies), 30.0, 16000.0) < 0.2);
    }
}

TEST_CASE("CurveMatch: un low shelf se recupera con una banda", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::LowShelf, 100.0, 6.0, kButterworthQ)};
    const Spectra spectra(known);

    // Con el rango en toda la rejilla, nada tira del shelf fuera de él.
    CurveMatchSettings settings = fullAmount();
    settings.smoothingOctaves = 0.0;
    settings.lowHz = 20.0;
    settings.highHz = 20000.0;
    CurveMatchResult result = matchCurve(spectra.input(), settings);
    REQUIRE(result.bands.size() == 1);
    CHECK(result.bands[0].type == FilterType::LowShelf);
    CHECK(result.finalRmsDb < 0.3);
    CHECK(rmsErrorModuloLevel(result, curveDb(known, result.frequencies), 20.0, 20000.0) < 0.3);
    CHECK_THAT(result.bands[0].gainDb, WithinAbs(6.0, 0.1));

    // Con el rango por defecto (30 Hz a 16 kHz), por debajo de 30 Hz se
    // mantiene lo del borde: el shelf puede seguir, y sigue siendo uno.
    // Y suavizado, igual.
    for (const double smoothing : {0.0, 1.0 / 3.0})
    {
        CAPTURE(smoothing);
        settings = fullAmount();
        settings.smoothingOctaves = smoothing;
        result = matchCurve(spectra.input(), settings);
        REQUIRE(result.bands.size() == 1);
        CHECK(result.bands[0].type == FilterType::LowShelf);
        CHECK(result.finalRmsDb < 0.3);
        CHECK(rmsErrorModuloLevel(result, curveDb(known, result.frequencies), 30.0, 16000.0) < 0.3);
    }
}

TEST_CASE("CurveMatch: un objetivo plano o con rizado de 0,3 dB no crea bandas", "[curve_match]")
{
    const Spectra flat;
    CurveMatchResult result = matchCurve(flat.input(), fullAmount());
    CHECK(result.bands.empty());
    CHECK(result.finalRmsDb == 0.0);
    CHECK(result.evaluations == 0);
    for (std::size_t i = 0; i < result.frequencies.size(); ++i)
        CHECK(result.proposedDb[i] == 0.0);

    // Rizado uniforme de ±0,3 dB (generador fijo), sin suavizar y suavizado.
    Spectra ripple;
    std::uint32_t state = 1;
    for (float& level : ripple.reference)
    {
        state = state * 1664525u + 1013904223u;
        level += static_cast<float>(0.6 * (static_cast<double>(state >> 8) / 16777216.0) - 0.3);
    }
    for (const double smoothing : {0.0, 1.0 / 12.0, 1.0 / 3.0})
    {
        CAPTURE(smoothing);
        CurveMatchSettings settings = fullAmount(24);
        settings.smoothingOctaves = smoothing;
        result = matchCurve(ripple.input(), settings);
        CHECK(result.bands.empty());
    }
}

TEST_CASE("CurveMatch: en graves la ventana abarca al menos 8 bins", "[curve_match]")
{
    // Un rizado de ±2 dB por debajo de 200 Hz, como el del ruido medido con
    // pocos bins por ventana (docs/dsp/igualacion.md).
    Spectra spectra;
    for (std::size_t i = 0; i < spectra.frequencies.size(); ++i)
        if (spectra.frequencies[i] < 200.0)
            spectra.reference[i] += static_cast<float>(2.0 * std::sin(1.2 * static_cast<double>(i)));
    // Rizado del objetivo de 30 a 200 Hz, de pico a pico.
    auto ripple = [](const CurveMatchResult& result) {
        double low = 0.0, high = 0.0;
        for (std::size_t i = 0; i < result.frequencies.size(); ++i)
            if (result.frequencies[i] >= 30.0 && result.frequencies[i] <= 200.0)
            {
                low = std::min(low, result.targetDb[i]);
                high = std::max(high, result.targetDb[i]);
            }
        return high - low;
    };
    CurveMatchInput input = spectra.input();
    input.resolutionHz = 48000.0 / 8192.0;
    for (const double smoothing : {0.0, 1.0 / 3.0})
    {
        CAPTURE(smoothing);
        CurveMatchSettings settings = fullAmount();
        settings.smoothingOctaves = smoothing;
        const CurveMatchResult narrow = matchCurve(spectra.input(), settings);
        const CurveMatchResult wide = matchCurve(input, settings);
        CHECK(ripple(wide) < 1.0);
        CHECK(ripple(wide) < ripple(narrow));
        CHECK(wide.bands.empty());
        // Sin suavizar ni ancho mínimo, el rizado entero pide bandas.
        if (smoothing == 0.0)
        {
            CHECK(ripple(narrow) > 3.0);
            CHECK_FALSE(narrow.bands.empty());
        }
    }
}

TEST_CASE("CurveMatch: al 50 % la propuesta es la mitad de la del 100 %", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::LowShelf, 120.0, 5.0, kButterworthQ),
                                  filter(FilterType::Bell, 900.0, -4.0, 1.5),
                                  filter(FilterType::Bell, 3500.0, 3.0, 2.0),
                                  filter(FilterType::HighShelf, 9000.0, 4.0, kButterworthQ)};
    const Spectra spectra(known);
    CurveMatchSettings full = fullAmount();
    CurveMatchSettings half = full;
    half.amount = 0.5;
    const CurveMatchResult a = matchCurve(spectra.input(), full);
    const CurveMatchResult b = matchCurve(spectra.input(), half);
    REQUIRE_FALSE(a.bands.empty());
    REQUIRE_FALSE(b.bands.empty());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.frequencies.size(); ++i)
        if (a.frequencies[i] >= full.lowHz && a.frequencies[i] <= full.highHz)
        {
            worst = std::max(worst, std::abs(b.proposedDb[i] - 0.5 * a.proposedDb[i]));
        }
    CHECK(worst < 0.2);
}

TEST_CASE("CurveMatch: con las bandas actuales iguales a lo que falta, nada que proponer", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::Bell, 200.0, 4.0, 1.0),
                                  filter(FilterType::Bell, 2000.0, -3.0, 2.0),
                                  filter(FilterType::HighShelf, 8000.0, 3.0, kButterworthQ)};
    const Spectra spectra(known);

    // Las bandas de la referencia ya puestas: no queda nada.
    std::vector<BandSettings> current;
    for (const FilterParams& params : known)
        current.push_back(enabled(params));
    CurveMatchResult result = matchCurve(spectra.input(current), fullAmount());
    CHECK(result.bands.empty());
    CHECK(result.initialRmsDb < kMatchStopErrorDb);

    // Aplicar dos veces: lo propuesto, puesto en bandas libres, no pide más.
    // Una banda apagada no cuenta, como una libre.
    current.assign(4, BandSettings{});
    current[1] = enabled(filter(FilterType::Bell, 2000.0, -1.0, 2.0)); // ya había algo
    result = matchCurve(spectra.input(current), fullAmount());
    REQUIRE_FALSE(result.bands.empty());
    for (const FilterParams& params : result.bands)
        current.push_back(enabled(params));
    const CurveMatchResult again = matchCurve(spectra.input(current), fullAmount());
    CHECK(again.bands.empty());
    CHECK(again.initialRmsDb < kMatchStopErrorDb);
}

TEST_CASE("CurveMatch: la curva actual cuenta con la regla de canales de la compensacion", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::Bell, 1000.0, 6.0, 1.0)};
    const Spectra spectra(known);
    BandSettings left = enabled(known[0]);
    left.channel = ChannelMode::Left;
    const BandSettings current[] = {left};
    const CurveMatchResult result = matchCurve(spectra.input(current), fullAmount());
    std::vector<double> expected(result.frequencies.size());
    staticCurveDb(current, kRate, 2, result.frequencies, expected);
    for (std::size_t i = 0; i < expected.size(); ++i)
        CHECK(result.currentDb[i] == expected[i]);
    // Una campana en un solo canal hace la mitad de su potencia: falta otra.
    REQUIRE_FALSE(result.bands.empty());
    CHECK(result.bands.front().gainDb > 0.0);
}

TEST_CASE("CurveMatch: sin contenido en una de las dos, no cuenta", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::Bell, 1000.0, 5.0, 1.0)};
    Spectra spectra(known);
    // La referencia no tiene nada por encima de 10 kHz.
    for (std::size_t i = 0; i < spectra.frequencies.size(); ++i)
        if (spectra.frequencies[i] > 10000.0)
            spectra.reference[i] = -120.0f;
    CurveMatchSettings settings = fullAmount();
    const CurveMatchResult result = matchCurve(spectra.input(), settings);
    REQUIRE_FALSE(result.bands.empty());
    for (const FilterParams& band : result.bands)
    {
        CHECK(band.frequencyHz < 10000.0);
        CHECK(band.type == FilterType::Bell);
    }
    for (std::size_t i = 0; i < result.frequencies.size(); ++i)
        if (result.frequencies[i] > 10000.0 && result.frequencies[i] <= settings.highHz)
        {
            CHECK(result.weights[i] == 0.0);
            CHECK(result.targetDb[i] == 0.0);
        }

    // Ningún punto con las dos presentes: nada que igualar.
    Spectra silent(known);
    std::fill(silent.track.begin(), silent.track.end(), -120.0f);
    const CurveMatchResult none = matchCurve(silent.input(), settings);
    CHECK(none.bands.empty());
    CHECK(none.evaluations == 0);
    for (std::size_t i = 0; i < none.frequencies.size(); ++i)
        CHECK(none.targetDb[i] == 0.0);
}

TEST_CASE("CurveMatch: pesos del rango", "[curve_match]")
{
    const Spectra spectra;
    const CurveMatchSettings settings = fullAmount();
    const CurveMatchResult result = matchCurve(spectra.input(), settings);
    REQUIRE(result.frequencies.size() == static_cast<std::size_t>(kMatchGridPoints));
    CHECK(result.frequencies.front() == spectra.frequencies.front());
    CHECK(result.frequencies.back() == spectra.frequencies.back());
    for (std::size_t i = 0; i < result.frequencies.size(); ++i)
    {
        const double f = result.frequencies[i];
        CAPTURE(f);
        if (f >= settings.lowHz && f <= settings.highHz)
            CHECK(result.weights[i] == 1.0);
        else if (f < settings.lowHz / std::exp2(kMatchWeightRampOctaves) ||
                 f > settings.highHz * std::exp2(kMatchWeightRampOctaves))
            CHECK(result.weights[i] == kMatchOutsideWeight);
        else
        {
            CHECK(result.weights[i] > kMatchOutsideWeight);
            CHECK(result.weights[i] < 1.0);
        }
    }
}

TEST_CASE("CurveMatch: la propuesta es la curva de las bandas con el algoritmo y la frecuencia dados", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::Bell, 300.0, -4.0, 1.2),
                                  filter(FilterType::HighShelf, 6000.0, 5.0, kButterworthQ)};
    const Spectra spectra(known);
    for (const FilterAlgorithm algorithm : {FilterAlgorithm::Classic, FilterAlgorithm::Matched})
        for (const double rate : {44100.0, 96000.0})
        {
            CAPTURE(static_cast<int>(algorithm), rate);
            CurveMatchInput input = spectra.input();
            input.algorithm = algorithm;
            input.sampleRate = rate;
            const CurveMatchResult result = matchCurve(input, fullAmount());
            REQUIRE_FALSE(result.bands.empty());
            std::vector<double> expected(result.frequencies.size(), 0.0);
            for (const FilterParams& band : result.bands)
            {
                CHECK(band.algorithm == algorithm);
                const BandDesign design = designBand(band, rate);
                for (std::size_t i = 0; i < expected.size(); ++i)
                    expected[i] += magnitudeDb(design, result.frequencies[i], rate);
            }
            for (std::size_t i = 0; i < expected.size(); ++i)
                CHECK_THAT(result.proposedDb[i], WithinAbs(expected[i], 1e-9));
        }
}

TEST_CASE("CurveMatch: determinista, bit a bit", "[curve_match]")
{
    const FilterParams known[] = {filter(FilterType::LowShelf, 80.0, -4.0, kButterworthQ),
                                  filter(FilterType::Bell, 450.0, 3.0, 3.0),
                                  filter(FilterType::Bell, 2500.0, -5.0, 1.0),
                                  filter(FilterType::HighShelf, 10000.0, 3.0, kButterworthQ)};
    const Spectra spectra(known);
    const CurveMatchResult a = matchCurve(spectra.input(), fullAmount());
    const CurveMatchResult b = matchCurve(spectra.input(), fullAmount());
    REQUIRE_FALSE(a.bands.empty());
    REQUIRE(a.bands.size() == b.bands.size());
    auto same = [](double x, double y) { return std::memcmp(&x, &y, sizeof(double)) == 0; };
    for (std::size_t k = 0; k < a.bands.size(); ++k)
    {
        CHECK(a.bands[k].type == b.bands[k].type);
        CHECK(same(a.bands[k].frequencyHz, b.bands[k].frequencyHz));
        CHECK(same(a.bands[k].gainDb, b.bands[k].gainDb));
        CHECK(same(a.bands[k].q, b.bands[k].q));
    }
    CHECK(a.proposedDb == b.proposedDb);
    CHECK(a.evaluations == b.evaluations);
    CHECK(a.iterations == b.iterations);
}

TEST_CASE("CurveMatch: 24 bandas dentro del limite de evaluaciones", "[curve_match]")
{
    const std::vector<FilterParams> known = twentyFourBells();
    const Spectra spectra(known);
    CurveMatchSettings settings = fullAmount(24);
    settings.smoothingOctaves = 0.0;
    const CurveMatchResult result = matchCurve(spectra.input(), settings);
    CHECK(result.bands.size() == 24);
    checkLimits(result, settings);
    // El coste, sin depender de la máquina: medido, con margen (docs/dsp/igualacion.md).
    CHECK(result.evaluations <= 60000);
    CHECK(result.iterations <= 800);

    // Max bands manda.
    settings.maxBands = 5;
    CHECK(matchCurve(spectra.input(), settings).bands.size() == 5);
    settings.maxBands = 0;
    const CurveMatchResult none = matchCurve(spectra.input(), settings);
    CHECK(none.bands.empty());
    CHECK(none.evaluations == 0);
}

TEST_CASE("CurveMatch: cancelar vuelve sin bandas", "[curve_match]")
{
    const std::vector<FilterParams> known = twentyFourBells();
    const Spectra spectra(known);
    const std::atomic<bool> cancel{true};
    const CurveMatchResult result = matchCurve(spectra.input(), fullAmount(24), &cancel);
    CHECK(result.cancelled);
    CHECK(result.bands.empty());
    CHECK(result.evaluations == 0);
    CHECK(result.proposedDb == result.currentDb);
}

TEST_CASE("CurveMatch: entradas no validas", "[curve_match]")
{
    Spectra spectra;
    CurveMatchInput input = spectra.input();
    input.sampleRate = 0.0;
    CHECK(matchCurve(input, fullAmount()).frequencies.empty());

    input = spectra.input();
    input.trackDb = std::span<const float>(spectra.track).first(10);
    CHECK(matchCurve(input, fullAmount()).frequencies.empty());

    std::vector<double> decreasing = spectra.frequencies;
    std::swap(decreasing[3], decreasing[4]);
    input = spectra.input();
    input.frequencies = decreasing;
    CHECK(matchCurve(input, fullAmount()).frequencies.empty());

    // Niveles no finitos: no están presentes.
    spectra.reference[50] = std::nanf("");
    spectra.track[60] = -INFINITY;
    const CurveMatchResult result = matchCurve(spectra.input(), fullAmount());
    CHECK(result.bands.empty());
    for (const double w : result.weights)
        CHECK(std::isfinite(w));

    // Ajustes fuera de rango: se sanean.
    CurveMatchSettings settings = fullAmount();
    settings.amount = std::nan("");
    settings.lowHz = 50000.0;
    settings.highHz = -3.0;
    settings.maxBands = 1000;
    const CurveMatchResult sanitized = matchCurve(Spectra().input(), settings);
    CHECK(sanitized.bands.empty());
}

namespace {

/// Las bandas de tools/snapshot: un corte de graves, dos campanas (la de
/// graves en Mid), un shelf de agudos y un notch de Q 8 a 5,2 kHz.
std::vector<BandSettings> snapshotBands()
{
    FilterParams lowCut = filter(FilterType::LowCut, 35.0, 0.0, kButterworthQ);
    lowCut.slopeDbPerOct = 24;
    std::vector<BandSettings> bands = {enabled(lowCut), enabled(filter(FilterType::Bell, 240.0, -4.0, 1.4)),
                                       enabled(filter(FilterType::Bell, 2800.0, 3.5, 0.9)),
                                       enabled(filter(FilterType::HighShelf, 9000.0, 2.5, kButterworthQ)),
                                       enabled(filter(FilterType::Notch, 5200.0, 0.0, 8.0))};
    bands[1].channel = ChannelMode::Mid;
    return bands;
}

/// La referencia de tools/snapshot --match: menos graves y más brillo.
std::vector<FilterParams> snapshotReference()
{
    return {filter(FilterType::LowShelf, 120.0, -4.0, kButterworthQ), filter(FilterType::Bell, 2500.0, 3.0, 1.2),
            filter(FilterType::HighShelf, 7000.0, 5.0, kButterworthQ)};
}

/// La mayor ganancia, en valor absoluto, de las campanas propuestas a menos
/// de `octaves` de `hz`: lo que se propone donde ya hay una banda estrecha.
double gainNear(const CurveMatchResult& result, double hz, double octaves)
{
    double gainDb = 0.0;
    for (const FilterParams& band : result.bands)
        if (band.type == FilterType::Bell && std::abs(std::log2(band.frequencyHz / hz)) <= octaves)
            gainDb = std::max(gainDb, std::abs(band.gainDb));
    return gainDb;
}

/// `bands` más lo propuesto en `result`, en bandas nuevas: Apply.
std::vector<BandSettings> applied(std::vector<BandSettings> bands, const CurveMatchResult& result)
{
    for (const FilterParams& params : result.bands)
        bands.push_back(enabled(params));
    return bands;
}

} // namespace

TEST_CASE("CurveMatch: el Q de las campanas no pasa del ancho del suavizado", "[curve_match]")
{
    CHECK(matchMaxBellQ(1000.0, 0.0) == kMatchMaxBellQ);
    CHECK_THAT(matchMaxBellQ(1000.0, 1.0), WithinRel(qForBandwidth(1.0), 1e-12));
    CHECK_THAT(matchMaxBellQ(5000.0, 1.0 / 3.0), WithinRel(qForBandwidth(1.0 / 3.0), 1e-12));
    CHECK_THAT(matchMaxBellQ(5000.0, 1.0 / 6.0), WithinRel(qForBandwidth(1.0 / 6.0), 1e-12));
    // En graves, con el ancho mínimo, la ventana es más ancha y el Q, menor.
    const double minWidthHz = kMatchMinWindowBins * kRate / 8192.0;
    CHECK(matchMaxBellQ(50.0, 1.0 / 3.0, minWidthHz) < matchMaxBellQ(5000.0, 1.0 / 3.0, minWidthHz));
    CHECK_THAT(matchMaxBellQ(50.0, 1.0 / 3.0, minWidthHz),
               WithinRel(qForBandwidth(smoothWindowOctaves(50.0, 1.0 / 3.0, minWidthHz)), 1e-12));
    CHECK(matchMaxBellQ(20.0, 4.0) == kMatchMinBellQ);
    CHECK(matchMaxBellQ(0.0, 1.0) == kMatchMaxBellQ);
}

TEST_CASE("CurveMatch: una banda estrecha que ya hay cuenta por lo que quita al ancho del suavizado", "[curve_match]")
{
    // El caso de tools/snapshot --match. Sin suavizar la curva actual, una
    // campana de +17,7 dB y Q 16 a 5194 Hz deshacía el notch. Ahora el notch
    // cuenta por la potencia que quita en la ventana del suavizado (con 1/3
    // de octava, unos 3,7 dB), y lo que se propone junto a él no es más
    // estrecho que esa ventana (docs/dsp/igualacion.md).
    const std::vector<BandSettings> current = snapshotBands();
    const Spectra spectra(snapshotReference());
    const double minWidthHz = kMatchMinWindowBins * kRate / 8192.0;
    struct Case
    {
        double smoothing;
        double maxGainNearNotch; ///< Medido: 10,8, 6,2 y 2,0 dB (docs/dsp/igualacion.md).
    };
    for (const Case c : {Case{1.0 / 6.0, 12.0}, Case{1.0 / 3.0, 7.0}, Case{1.0, 3.0}})
        for (const double amount : {0.5, 1.0})
        {
            CAPTURE(c.smoothing, amount);
            CurveMatchSettings settings;
            settings.smoothingOctaves = c.smoothing;
            settings.amount = amount;
            CurveMatchInput input = spectra.input(current);
            input.resolutionHz = kRate / 8192.0;
            const CurveMatchResult result = matchCurve(input, settings);
            checkLimits(result, settings);
            for (const FilterParams& band : result.bands)
            {
                CAPTURE(static_cast<int>(band.type), band.frequencyHz, band.gainDb, band.q);
                if (band.type == FilterType::Bell)
                    CHECK(band.q <= matchMaxBellQ(band.frequencyHz, c.smoothing, minWidthHz) * (1.0 + 1e-9));
                if (std::abs(std::log2(band.frequencyHz / 5200.0)) > 0.25)
                    CHECK(std::abs(band.gainDb) <= 6.0); // lo demás, lo que pide la referencia
            }
            CHECK(gainNear(result, 5200.0, 0.25) <= c.maxGainNearNotch);

            // Aplicar dos veces: la segunda no propone nada.
            const std::vector<BandSettings> after = applied(current, result);
            input.currentBands = after;
            const CurveMatchResult again = matchCurve(input, settings);
            CHECK(again.bands.empty());
            CHECK(again.initialRmsDb < kMatchStopErrorDb);
        }
}

TEST_CASE("CurveMatch: la curva actual se compara suavizada en potencia", "[curve_match]")
{
    // Un notch solo, con la pista y la referencia iguales: lo que falta es
    // la potencia que quita en la ventana de cada punto, y nada más.
    const Spectra spectra;
    const BandSettings current[] = {enabled(filter(FilterType::Notch, 2000.0, 0.0, 8.0))};
    CurveMatchSettings settings = fullAmount();
    settings.maxBands = 0; // solo el objetivo y lo que falta
    const CurveMatchResult result = matchCurve(spectra.input(current), settings);
    // Lejos del notch, S(C) es 0 dB: no falta nada. En él, falta lo que quita.
    CHECK(result.initialRmsDb > 0.1);
    CHECK(result.initialRmsDb < 2.0);
    // currentDb sigue siendo la curva sin suavizar, con el notch entero.
    CHECK(*std::min_element(result.currentDb.begin(), result.currentDb.end()) < -20.0);

    // Sin suavizar, el notch se ve entero: lo que falta es mucho mayor.
    settings.smoothingOctaves = 0.0;
    const CurveMatchResult raw = matchCurve(spectra.input(current), settings);
    CHECK(raw.initialRmsDb > 2.0 * result.initialRmsDb);
}
