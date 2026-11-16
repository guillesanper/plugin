// SPDX-License-Identifier: MIT
#include "esceq/dsp/auto_gain.h"

#include "esceq/dsp/decibels.h"
#include "esceq/dsp/filter_design.h"
#include "loudness_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;

namespace {

constexpr double kRate = 48000.0;

BandSettings band(FilterType type, double frequencyHz, double gainDb, double q,
                  ChannelMode channel = ChannelMode::Stereo, int slope = 12)
{
    BandSettings settings;
    settings.enabled = true;
    settings.filter.type = type;
    settings.filter.frequencyHz = frequencyHz;
    settings.filter.gainDb = gainDb;
    settings.filter.q = q;
    settings.filter.slopeDbPerOct = slope;
    settings.filter.algorithm = FilterAlgorithm::Matched;
    settings.channel = channel;
    return settings;
}

/// |K|² calculada aparte, con la fórmula del biquad en números complejos.
double referenceKWeighting(double frequencyHz)
{
    const std::complex<double> z1 = std::polar(1.0, -2.0 * std::numbers::pi * frequencyHz / 48000.0);
    const std::complex<double> z2 = z1 * z1;
    const auto shelf = (1.53512485958697 - 2.69169618940638 * z1 + 1.19839281085285 * z2) /
                       (1.0 - 1.69065929318241 * z1 + 0.73248077421585 * z2);
    const auto highPass = (1.0 - 2.0 * z1 + z2) / (1.0 - 1.99004745483398 * z1 + 0.99007225036621 * z2);
    return std::norm(shelf * highPass);
}

/// La integral de la definición, con bandas Stereo, por la regla del
/// trapecio en una rejilla uniforme muy fina (1/1024 de octava).
double referenceCompensationDb(const std::vector<BandSettings>& bands, double sampleRate = kRate)
{
    std::vector<BandDesign> designs;
    for (const auto& b : bands)
        designs.push_back(designBand(b.filter, sampleRate));
    const double low = std::log2(20.0);
    const double high = std::log2(20000.0);
    const int points = static_cast<int>((high - low) * 1024.0);
    double weighted = 0.0, total = 0.0;
    for (int i = 0; i <= points; ++i)
    {
        const double f = std::exp2(low + (high - low) * i / points);
        const double edge = (i == 0 || i == points) ? 0.5 : 1.0;
        double power = 1.0;
        for (const auto& design : designs)
            power *= std::norm(response(design, f, sampleRate));
        weighted += edge * referenceKWeighting(f) * power;
        total += edge * referenceKWeighting(f);
    }
    return -10.0 * std::log10(weighted / total);
}

} // namespace

TEST_CASE("AutoGain: la ponderacion K es la de BS.1770", "[auto_gain]")
{
    for (const double f : {20.0, 50.0, 100.0, 997.0, 2000.0, 5000.0, 10000.0, 20000.0})
        CHECK_THAT(kWeightingPower(f), WithinAbs(referenceKWeighting(f), 1.0e-9 * referenceKWeighting(f)));
    // Las −0,691 dB de la definición de LKFS compensan la ganancia en 997 Hz.
    CHECK_THAT(10.0 * std::log10(kWeightingPower(997.0)), WithinAbs(0.691, 0.002));
    // Shelf de unos +4 dB en agudos y paso alto en graves.
    CHECK_THAT(10.0 * std::log10(kWeightingPower(10000.0)), WithinAbs(4.0, 0.1));
    CHECK(10.0 * std::log10(kWeightingPower(20.0)) < -10.0);
}

TEST_CASE("AutoGain: con la curva plana la compensacion es exactamente 0", "[auto_gain]")
{
    CHECK(autoGainDb({}, kRate, 2) == 0.0);

    std::vector<BandSettings> flat{
        band(FilterType::Bell, 1000.0, 0.0, 1.0),      band(FilterType::LowShelf, 100.0, 0.0, 0.7),
        band(FilterType::HighShelf, 8000.0, 0.0, 0.7), band(FilterType::Tilt, 1000.0, 0.0, 0.7),
        band(FilterType::AllPass, 2000.0, 0.0, 4.0),
    };
    auto off = band(FilterType::Bell, 1000.0, 12.0, 1.0);
    off.enabled = false;
    flat.push_back(off);
    CHECK(autoGainDb(flat, kRate, 2) == 0.0);
    CHECK(autoGainDb(flat, kRate, 1) == 0.0);
}

TEST_CASE("AutoGain: campana de +6 dB, Q 1 a 1 kHz, como la integral numerica", "[auto_gain]")
{
    const std::vector bell{band(FilterType::Bell, 1000.0, 6.0, 1.0)};
    const double reference = referenceCompensationDb(bell);
    CHECK_THAT(autoGainDb(bell, kRate, 2), WithinAbs(reference, 0.05));
    CHECK_THAT(autoGainDb(bell, kRate, 1), WithinAbs(reference, 0.05));
    // Una atenuación compensa hacia arriba.
    CHECK(reference < -1.0);
    CHECK(autoGainDb(std::vector{band(FilterType::Bell, 1000.0, -6.0, 1.0)}, kRate, 2) > 0.5);
}

TEST_CASE("AutoGain: picos estrechos y shelves con Q alto, como la integral numerica", "[auto_gain]")
{
    // Una rejilla uniforme de 24 puntos por octava se equivoca hasta en 4 dB
    // con un pico de Q 30: la rejilla se refina alrededor de cada banda.
    for (const double f : {1000.0, 1015.0, 1031.0, 4321.0})
    {
        const std::vector peak{band(FilterType::Bell, f, 30.0, 30.0)};
        CHECK_THAT(autoGainDb(peak, kRate, 2), WithinAbs(referenceCompensationDb(peak), 0.05));
    }
    const std::vector shelf{band(FilterType::LowShelf, 950.0, -15.5, 29.0), band(FilterType::Tilt, 660.0, -13.0, 0.5)};
    CHECK_THAT(autoGainDb(shelf, kRate, 2), WithinAbs(referenceCompensationDb(shelf), 0.05));

    std::mt19937 rng(2026);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (int trial = 0; trial < 30; ++trial)
    {
        std::vector<BandSettings> bands;
        const int count = 1 + static_cast<int>(rng() % 6);
        for (int i = 0; i < count; ++i)
        {
            auto b =
                band(static_cast<FilterType>(rng() % 9), 20.0 * std::pow(1000.0, unit(rng)), -30.0 + 60.0 * unit(rng),
                     0.1 * std::pow(300.0, unit(rng)), ChannelMode::Stereo, 6 * (1 + static_cast<int>(rng() % 16)));
            bands.push_back(b);
        }
        const double reference = referenceCompensationDb(bands);
        if (std::abs(reference) >= kAutoGainLimitDb)
            continue;
        INFO("prueba " << trial);
        CHECK_THAT(autoGainDb(bands, kRate, 2), WithinAbs(reference, 0.05));
    }
}

TEST_CASE("AutoGain: la compensacion se limita a 12 dB", "[auto_gain]")
{
    CHECK(autoGainDb(std::vector{band(FilterType::Bell, 1000.0, 30.0, 0.1)}, kRate, 2) == -kAutoGainLimitDb);
    CHECK(autoGainDb(std::vector{band(FilterType::Bell, 1000.0, -30.0, 0.1)}, kRate, 2) == kAutoGainLimitDb);
    // Un corte que lo quita todo no da infinitos ni NaN.
    const std::vector silence{band(FilterType::HighCut, 1.0, 0.0, 0.7, ChannelMode::Stereo, 96),
                              band(FilterType::LowCut, 20000.0, 0.0, 0.7, ChannelMode::Stereo, 96)};
    CHECK(autoGainDb(silence, kRate, 2) == kAutoGainLimitDb);
}

TEST_CASE("AutoGain: solo cuenta la ganancia estatica", "[auto_gain]")
{
    auto stat = band(FilterType::Bell, 3000.0, 4.0, 2.0);
    auto dynamic = stat;
    dynamic.dynamics.enabled = true;
    dynamic.dynamics.rangeDb = -12.0;
    CHECK(autoGainDb(std::vector{dynamic}, kRate, 2) == autoGainDb(std::vector{stat}, kRate, 2));
}

TEST_CASE("AutoGain: con canales, la media de la potencia de las dos salidas", "[auto_gain]")
{
    const double stereo = autoGainDb(std::vector{band(FilterType::Bell, 2000.0, 9.0, 0.8)}, kRate, 2);
    // Media de la potencia de un canal sin tocar y la del otro con la banda.
    const double half = -10.0 * std::log10(0.5 * (1.0 + std::pow(10.0, -stereo / 10.0)));
    for (const auto mode : {ChannelMode::Left, ChannelMode::Right, ChannelMode::Mid, ChannelMode::Side})
    {
        INFO("canal " << static_cast<int>(mode));
        CHECK_THAT(autoGainDb(std::vector{band(FilterType::Bell, 2000.0, 9.0, 0.8, mode)}, kRate, 2),
                   WithinAbs(half, 1.0e-9));
    }
    // En mono, Left y Mid procesan el único canal; Right y Side no hacen nada.
    for (const auto mode : {ChannelMode::Left, ChannelMode::Mid})
        CHECK_THAT(autoGainDb(std::vector{band(FilterType::Bell, 2000.0, 9.0, 0.8, mode)}, kRate, 1),
                   WithinAbs(stereo, 1.0e-9));
    for (const auto mode : {ChannelMode::Right, ChannelMode::Side})
        CHECK_THAT(autoGainDb(std::vector{band(FilterType::Bell, 2000.0, 9.0, 0.8, mode)}, kRate, 1),
                   WithinAbs(0.0, 1.0e-12));
    // Mid y Side a la vez son diagonales en M/S: la media de las dos.
    const double side = autoGainDb(std::vector{band(FilterType::Bell, 2000.0, -9.0, 0.8)}, kRate, 2);
    const double both = -10.0 * std::log10(0.5 * (std::pow(10.0, -stereo / 10.0) + std::pow(10.0, -side / 10.0)));
    CHECK_THAT(autoGainDb(std::vector{band(FilterType::Bell, 2000.0, 9.0, 0.8, ChannelMode::Mid),
                                      band(FilterType::Bell, 2000.0, -9.0, 0.8, ChannelMode::Side)},
                          kRate, 2),
               WithinAbs(both, 1.0e-9));
}

TEST_CASE("AutoGain: casi no depende de la frecuencia de muestreo", "[auto_gain]")
{
    const std::vector curve{band(FilterType::LowShelf, 120.0, 3.0, 0.7), band(FilterType::Bell, 2500.0, -4.0, 1.5),
                            band(FilterType::HighShelf, 9000.0, 5.0, 0.7)};
    const double at48 = autoGainDb(curve, kRate, 2);
    for (const double rate : {44100.0, 88200.0, 96000.0, 192000.0})
        CHECK_THAT(autoGainDb(curve, rate, 2), WithinAbs(at48, 0.05));
    // Por debajo de 40 kHz, la integral acaba en 0,49·fs.
    CHECK(std::isfinite(autoGainDb(curve, 22050.0, 2)));
    CHECK(autoGainDb(curve, 0.0, 2) == 0.0);
}

TEST_CASE("AutoGain: ruido rosa por la curva compensada suena igual de fuerte (BS.1770)", "[auto_gain]")
{
    constexpr std::size_t kLength = 8 * 48000;
    const std::vector<std::vector<float>> dry{esceq::test::pinkNoise(kLength, 1), esceq::test::pinkNoise(kLength, 2)};
    const double dryLoudness = esceq::test::integratedLoudness(dry);

    struct Curve
    {
        std::string name;
        std::vector<BandSettings> bands;
    };
    const std::vector<Curve> curves{
        {"campana +6 dB", {band(FilterType::Bell, 1000.0, 6.0, 1.0)}},
        {"sonrisa", {band(FilterType::LowShelf, 100.0, 4.0, 0.7), band(FilterType::HighShelf, 8000.0, 5.0, 0.7)}},
        {"voz",
         {band(FilterType::LowCut, 80.0, 0.0, 0.7, ChannelMode::Stereo, 24), band(FilterType::Bell, 300.0, -3.0, 1.4),
          band(FilterType::Bell, 3000.0, 4.0, 2.0), band(FilterType::HighShelf, 10000.0, 2.0, 0.7)}},
        {"banda estrecha", {band(FilterType::LowCut, 150.0, 0.0, 0.7), band(FilterType::HighCut, 6000.0, 0.0, 0.7)}},
        {"resonancia", {band(FilterType::Bell, 2000.0, 18.0, 20.0)}},
        {"oscura", {band(FilterType::Tilt, 1000.0, -4.0, 0.7), band(FilterType::Notch, 4000.0, 0.0, 2.0)}},
        {"mid/side",
         {band(FilterType::Bell, 250.0, -4.0, 1.0, ChannelMode::Mid),
          band(FilterType::HighShelf, 5000.0, 6.0, 0.7, ChannelMode::Side), band(FilterType::Bell, 1200.0, 3.0, 2.0)}},
        {"izquierda", {band(FilterType::Bell, 600.0, 8.0, 0.7, ChannelMode::Left)}},
    };

    for (const auto& curve : curves)
    {
        INFO(curve.name);
        const double compensation = autoGainDb(curve.bands, kRate, 2);
        REQUIRE(std::abs(compensation) < kAutoGainLimitDb);

        Equalizer eq;
        for (std::size_t i = 0; i < curve.bands.size(); ++i)
            eq.setBand(static_cast<int>(i), curve.bands[i]);
        eq.prepare(kRate);
        auto wet = dry;
        float* channels[] = {wet[0].data(), wet[1].data()};
        eq.process(channels, 2, static_cast<int>(kLength));
        const float gain = dbToGain(static_cast<float>(compensation));
        for (auto& channel : wet)
            for (auto& sample : channel)
                sample *= gain;

        const double uncompensated = esceq::test::integratedLoudness(wet) - compensation;
        CHECK(std::abs(uncompensated - dryLoudness) > 0.3); // la curva sí cambia la sonoridad
        CHECK_THAT(esceq::test::integratedLoudness(wet), WithinAbs(dryLoudness, 0.5));
    }
}

TEST_CASE("AutoGain: entrega entre el hilo de audio y el de control", "[auto_gain]")
{
    AutoGain autoGain;
    const std::vector boost{band(FilterType::Bell, 1000.0, 6.0, 1.0)};
    const std::vector cut{band(FilterType::Bell, 1000.0, -6.0, 1.0)};

    CHECK_FALSE(autoGain.latestDb().has_value());
    CHECK_FALSE(autoGain.service());

    autoGain.request(boost, kRate, 2);
    CHECK_FALSE(autoGain.latestDb().has_value()); // aún sin calcular
    CHECK(autoGain.service());
    REQUIRE(autoGain.latestDb().has_value());
    CHECK(*autoGain.latestDb() == static_cast<float>(autoGainDb(boost, kRate, 2)));
    CHECK_FALSE(autoGain.service()); // nada nuevo

    // Mientras se calcula una petición nueva, sigue la anterior.
    autoGain.request(cut, kRate, 2);
    CHECK(*autoGain.latestDb() == static_cast<float>(autoGainDb(boost, kRate, 2)));
    CHECK(autoGain.service());
    CHECK(*autoGain.latestDb() == static_cast<float>(autoGainDb(cut, kRate, 2)));

    // Tras restart(), lo anterior no vale hasta calcular una petición nueva.
    autoGain.restart();
    CHECK_FALSE(autoGain.latestDb().has_value());
    autoGain.request(boost, kRate, 1);
    CHECK_FALSE(autoGain.latestDb().has_value());
    CHECK(autoGain.service());
    CHECK(*autoGain.latestDb() == static_cast<float>(autoGainDb(boost, kRate, 1)));

    // De varias peticiones seguidas, se calcula la última.
    autoGain.request(boost, kRate, 2);
    autoGain.request(cut, kRate, 2);
    CHECK(autoGain.service());
    CHECK(*autoGain.latestDb() == static_cast<float>(autoGainDb(cut, kRate, 2)));
    CHECK_FALSE(autoGain.service());
}
