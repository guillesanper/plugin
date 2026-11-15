// SPDX-License-Identifier: MIT
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/partitioned_convolver.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;
using namespace esceq::test;

namespace
{

constexpr double kFs = 48000.0;
constexpr PhaseMode kLinearModes[] = {PhaseMode::LinearLow, PhaseMode::LinearMedium, PhaseMode::LinearHigh};

/// Respuesta compleja de unos coeficientes en `frequencyHz`.
std::complex<double> firResponse(const std::vector<double>& taps, double frequencyHz, double sampleRate)
{
    const double omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const std::complex<double> step = std::polar(1.0, -omega);
    std::complex<double> phasor = 1.0, sum = 0.0;
    for (const double tap : taps)
    {
        sum += tap * phasor;
        phasor *= step;
    }
    return sum;
}

double firMagnitudeDb(const std::vector<double>& taps, double frequencyHz, double sampleRate = kFs)
{
    return 20.0 * std::log10(std::max(std::abs(firResponse(taps, frequencyHz, sampleRate)), 1.0e-15));
}

double toDb(double magnitude)
{
    return 20.0 * std::log10(std::max(magnitude, 1.0e-15));
}

/// Mayor error en dB entre el FIR y su objetivo en [fromHz, toHz], en
/// frecuencias logarítmicas y donde el objetivo está por encima de -60 dB.
double worstErrorDb(
    const std::vector<double>& taps, std::span<const BandSettings> bands, double fromHz, double toHz,
    double (*pick)(const ChannelMatrix&) = [](const ChannelMatrix& m) { return m.ll; })
{
    double worst = 0.0;
    for (double f = fromHz; f <= toHz; f *= 1.03)
    {
        const double target = toDb(std::abs(pick(LinearPhaseDesigner::targetResponse(bands, f, kFs))));
        if (target < -60.0)
            continue;
        worst = std::max(worst, std::abs(firMagnitudeDb(taps, f) - target));
    }
    return worst;
}

std::vector<double> randomTaps(std::size_t length, std::uint32_t seed)
{
    std::vector<double> taps(length);
    for (auto& tap : taps)
    {
        seed = seed * 1664525u + 1013904223u;
        tap = (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * 0.2;
    }
    return taps;
}

std::vector<float> randomSignal(std::size_t length, std::uint32_t seed)
{
    std::vector<float> signal(length);
    for (auto& sample : signal)
    {
        seed = seed * 1664525u + 1013904223u;
        sample = 0.5f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return signal;
}

/// Convolución directa (la referencia).
std::vector<double> directConvolution(const std::vector<float>& x, const std::vector<double>& h)
{
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n)
        for (std::size_t k = 0; k < h.size() && k <= n; ++k)
            y[n] += h[k] * static_cast<double>(x[n - k]);
    return y;
}

/// Pasa `left` y `right` por el convolucionador en llamadas de `chunk` muestras.
void convolve(PartitionedConvolver& convolver, const FirKernel& kernel, std::vector<float>& left,
              std::vector<float>& right, int chunk)
{
    const int total = static_cast<int>(left.size());
    for (int pos = 0; pos < total;)
    {
        const int length = std::min({chunk, total - pos, convolver.samplesUntilBlock()});
        convolver.process(left.data() + pos, right.data() + pos, left.data() + pos, right.data() + pos, length, kernel);
        pos += length;
    }
}

} // namespace

// --- Tamaños y latencia ---

TEST_CASE("Fase lineal: tamanos y latencia de cada modo")
{
    CHECK(linearPhaseSize(PhaseMode::Iir, kFs) == LinearPhaseSize{});
    CHECK(latencySamples(PhaseMode::Iir, kFs) == 0);

    CHECK(linearPhaseSize(PhaseMode::LinearLow, kFs) == LinearPhaseSize{2048, 256});
    CHECK(linearPhaseSize(PhaseMode::LinearMedium, kFs) == LinearPhaseSize{8192, 512});
    CHECK(linearPhaseSize(PhaseMode::LinearHigh, kFs) == LinearPhaseSize{32768, 1024});
    CHECK(latencySamples(PhaseMode::LinearLow, kFs) == 1280);
    CHECK(latencySamples(PhaseMode::LinearMedium, kFs) == 4608);
    CHECK(latencySamples(PhaseMode::LinearHigh, kFs) == 17408);

    // Más calidad, más coeficientes y más latencia.
    CHECK(latencySamples(PhaseMode::LinearLow, kFs) < latencySamples(PhaseMode::LinearMedium, kFs));
    CHECK(latencySamples(PhaseMode::LinearMedium, kFs) < latencySamples(PhaseMode::LinearHigh, kFs));
}

TEST_CASE("Fase lineal: la resolucion en hercios no depende de la frecuencia de muestreo")
{
    for (const auto mode : kLinearModes)
    {
        const auto base = linearPhaseSize(mode, 44100.0);
        CHECK(linearPhaseSize(mode, 48000.0) == base);
        CHECK(linearPhaseSize(mode, 88200.0).firLength == 2 * base.firLength);
        CHECK(linearPhaseSize(mode, 96000.0) == LinearPhaseSize{2 * base.firLength, 2 * base.blockSize});
        CHECK(linearPhaseSize(mode, 192000.0) == LinearPhaseSize{4 * base.firLength, 4 * base.blockSize});
        CHECK(linearPhaseSize(mode, 384000.0) == LinearPhaseSize{8 * base.firLength, 8 * base.blockSize});
        CHECK(linearPhaseSize(mode, 768000.0) == LinearPhaseSize{8 * base.firLength, 8 * base.blockSize});
        // Una frecuencia de muestreo no válida usa los tamaños base.
        CHECK(linearPhaseSize(mode, 0.0) == base);
        CHECK(base.partitions() * base.blockSize == base.firLength);
    }
}

// --- Matriz de canales ---

TEST_CASE("Fase lineal: la matriz de cada canal hace lo mismo que el ecualizador IIR")
{
    const double h = 0.3, left = 0.8, right = -0.5;
    auto apply = [&](ChannelMode mode)
    {
        const auto m = channelMatrix(mode, h);
        return std::pair{m.ll * left + m.lr * right, m.rl * left + m.rr * right};
    };

    CHECK(apply(ChannelMode::Stereo) == std::pair{h * left, h * right});
    CHECK(apply(ChannelMode::Left) == std::pair{h * left, right});
    CHECK(apply(ChannelMode::Right) == std::pair{left, h * right});

    // Mid: la suma se multiplica por h y la diferencia no cambia.
    const auto [midL, midR] = apply(ChannelMode::Mid);
    CHECK_THAT(0.5 * (midL + midR), WithinAbs(h * 0.5 * (left + right), 1e-15));
    CHECK_THAT(0.5 * (midL - midR), WithinAbs(0.5 * (left - right), 1e-15));

    const auto [sideL, sideR] = apply(ChannelMode::Side);
    CHECK_THAT(0.5 * (sideL + sideR), WithinAbs(0.5 * (left + right), 1e-15));
    CHECK_THAT(0.5 * (sideL - sideR), WithinAbs(h * 0.5 * (left - right), 1e-15));

    // Una banda que no hace nada es la identidad en los cinco modos.
    for (const auto mode :
         {ChannelMode::Stereo, ChannelMode::Left, ChannelMode::Right, ChannelMode::Mid, ChannelMode::Side})
        CHECK(channelMatrix(mode, 1.0) == ChannelMatrix{});
}

TEST_CASE("Fase lineal: la composicion aplica las bandas en orden")
{
    const auto left = channelMatrix(ChannelMode::Left, 0.5);
    const auto mid = channelMatrix(ChannelMode::Mid, 2.0);
    // Left y después Mid: (L, R) = (1, 0) -> (0.5, 0) -> d = (2-1)·0.25 -> (0.75, 0.25).
    const auto total = mid * left;
    CHECK_THAT(total.ll, WithinAbs(0.75, 1e-15));
    CHECK_THAT(total.rl, WithinAbs(0.25, 1e-15));
    // En el otro orden no da lo mismo.
    CHECK(!(left * mid == total));
}

// --- Diseño ---

TEST_CASE("Fase lineal: sin bandas que hagan algo el FIR es un retardo puro")
{
    LinearPhaseDesigner designer;
    const BandSettings off[] = {{false, {FilterType::Bell, 1000.0, 12.0, 1.0}}};
    const BandSettings flat[] = {{true, {FilterType::Bell, 1000.0, 0.0, 1.0}}};
    const BandSettings allPass[] = {{true, {FilterType::AllPass, 300.0, 0.0, 2.0}}};

    for (std::span<const BandSettings> bands :
         {std::span<const BandSettings>{}, std::span<const BandSettings>(off), std::span<const BandSettings>(flat),
          std::span<const BandSettings>(allPass)})
    {
        const auto fir = designer.design(bands, PhaseMode::LinearLow, kFs, 2);
        CHECK(fir.identity);
        REQUIRE(fir.ll.size() == 2048);
        // Un impulso en el centro.
        CHECK_THAT(fir.ll[1024], WithinAbs(1.0, 1e-12));
        double rest = 0.0;
        for (std::size_t i = 0; i < fir.ll.size(); ++i)
            if (i != 1024)
                rest = std::max(rest, std::abs(fir.ll[i]));
        CHECK(rest < 1e-12);
    }

    const BandSettings bell[] = {{true, {FilterType::Bell, 1000.0, 3.0, 1.0}}};
    CHECK(!designer.design(bell, PhaseMode::LinearLow, kFs, 2).identity);
}

TEST_CASE("Fase lineal: IIR no tiene FIR")
{
    LinearPhaseDesigner designer;
    const BandSettings bell[] = {{true, {FilterType::Bell, 1000.0, 3.0, 1.0}}};
    const auto fir = designer.design(bell, PhaseMode::Iir, kFs, 2);
    CHECK(fir.size.firLength == 0);
    CHECK(fir.ll.empty());
}

TEST_CASE("Fase lineal: los coeficientes son simetricos, la fase es exactamente lineal")
{
    LinearPhaseDesigner designer;
    const BandSettings bands[] = {
        {true, {FilterType::LowCut, 80.0, 0.0, kButterworthQ, 24}},
        {true, {FilterType::Bell, 2500.0, -4.0, 3.0}},
        {true, {FilterType::HighShelf, 9000.0, 3.0, kButterworthQ}, {}, ChannelMode::Side},
        {true, {FilterType::Bell, 400.0, 5.0, 2.0}, {}, ChannelMode::Left},
    };
    for (const auto mode : kLinearModes)
    {
        const auto fir = designer.design(bands, mode, kFs, 2);
        const int length = fir.size.firLength;
        const int centre = length / 2;
        for (const auto* taps : {&fir.ll, &fir.lr, &fir.rl, &fir.rr})
        {
            REQUIRE(taps->size() == static_cast<std::size_t>(length));
            CHECK((*taps)[0] == 0.0);
            bool symmetric = true;
            for (int n = 1; n < centre; ++n)
                symmetric = symmetric && (*taps)[static_cast<std::size_t>(centre + n)] ==
                                             (*taps)[static_cast<std::size_t>(centre - n)];
            CHECK(symmetric);
        }

        // Fase lineal: quitando el retardo del centro, la respuesta es real.
        for (const double f : {50.0, 700.0, 6000.0, 15000.0})
        {
            const auto h = firResponse(fir.ll, f, kFs) * std::polar(1.0, 2.0 * std::numbers::pi * f * centre / kFs);
            CHECK(std::abs(h.imag()) < 1e-9 * std::max(1.0, std::abs(h)));
        }
    }
}

TEST_CASE("Fase lineal: la magnitud reproduce la de las bandas en todos los tipos")
{
    // Por encima de 1 kHz los tres modos tienen resolución de sobra.
    LinearPhaseDesigner designer;
    struct Case
    {
        BandSettings band;
        double fromHz;
    };
    const Case cases[] = {
        {{true, {FilterType::Bell, 3000.0, 6.0, 1.0}}, 1000.0},
        {{true, {FilterType::Bell, 8000.0, -12.0, 4.0}}, 1000.0},
        {{true, {FilterType::LowShelf, 2000.0, -6.0, kButterworthQ}}, 1000.0},
        {{true, {FilterType::HighShelf, 4000.0, 6.0, kButterworthQ}}, 1000.0},
        {{true, {FilterType::LowCut, 2000.0, 0.0, kButterworthQ, 48}}, 1000.0},
        {{true, {FilterType::HighCut, 6000.0, 0.0, kButterworthQ, 24}}, 1000.0},
        {{true, {FilterType::BandPass, 5000.0, 0.0, 2.0}}, 1000.0},
        {{true, {FilterType::Notch, 6000.0, 0.0, 1.0}}, 1000.0},
        {{true, {FilterType::Tilt, 3000.0, 4.0, kButterworthQ}}, 1000.0},
    };
    // Tolerancia por modo: más coeficientes, más precisión. El peor caso de
    // Low es el codo del corte de 48 dB/oct (0,10 dB medidos).
    const double tolerance[] = {0.15, 0.02, 0.005};
    for (int m = 0; m < 3; ++m)
        for (const auto& c : cases)
        {
            const auto mode = kLinearModes[m];
            INFO("modo " << static_cast<int>(mode) << ", " << typeName(c.band.filter.type));
            const BandSettings bands[] = {c.band};
            const auto fir = designer.design(bands, mode, kFs, 2);
            CHECK(worstErrorDb(fir.ll, bands, c.fromHz, 20000.0) < tolerance[m]);
        }
}

TEST_CASE("Fase lineal: mas coeficientes, mas resolucion en graves")
{
    // Una campana estrecha en graves: es lo que más resolución pide.
    LinearPhaseDesigner designer;
    const BandSettings bands[] = {{true, {FilterType::Bell, 60.0, 6.0, 2.0}}};
    double previous = 1.0e9;
    std::vector<double> errors;
    for (const auto mode : kLinearModes)
    {
        const auto fir = designer.design(bands, mode, kFs, 2);
        const double error = worstErrorDb(fir.ll, bands, 20.0, 400.0);
        errors.push_back(error);
        CHECK(error < previous / 3.0);
        previous = error;
    }
    // Lo medido al elegir la ventana (docs/dsp/fase-lineal.md).
    CHECK(errors[0] < 3.0);
    CHECK(errors[1] < 0.6);
    CHECK(errors[2] < 0.05);
}

TEST_CASE("Fase lineal: High es preciso en todo el rango audible")
{
    LinearPhaseDesigner designer;
    const BandSettings bands[] = {
        {true, {FilterType::LowCut, 30.0, 0.0, kButterworthQ, 24}},
        {true, {FilterType::Bell, 120.0, 4.0, 1.0}},
        {true, {FilterType::Bell, 1500.0, -3.0, 2.0}},
        {true, {FilterType::HighShelf, 10000.0, 2.0, kButterworthQ}},
    };
    const auto fir = designer.design(bands, PhaseMode::LinearHigh, kFs, 2);
    CHECK(worstErrorDb(fir.ll, bands, 40.0, 20000.0) < 0.05);
}

TEST_CASE("Fase lineal: la banda de rechazo de un corte de 96 dB/oct no se llena de fugas")
{
    LinearPhaseDesigner designer;
    const BandSettings bands[] = {{true, {FilterType::LowCut, 300.0, 0.0, kButterworthQ, 96}}};
    for (const auto mode : kLinearModes)
    {
        INFO("modo " << static_cast<int>(mode));
        const auto fir = designer.design(bands, mode, kFs, 2);
        double worst = -400.0;
        for (double f = 20.0; f <= 100.0; f *= 1.02)
            worst = std::max(worst, firMagnitudeDb(fir.ll, f));
        CHECK(worst < -100.0);
    }
}

TEST_CASE("Fase lineal: varias bandas suman sus respuestas como en el IIR")
{
    LinearPhaseDesigner designer;
    const BandSettings one[] = {{true, {FilterType::Bell, 2000.0, 5.0, 1.0}}};
    const BandSettings two[] = {{true, {FilterType::Bell, 2000.0, 5.0, 1.0}},
                                {true, {FilterType::Bell, 2000.0, 5.0, 1.0}}};
    const auto single = designer.design(one, PhaseMode::LinearMedium, kFs, 2);
    const auto both = designer.design(two, PhaseMode::LinearMedium, kFs, 2);
    for (const double f : {500.0, 2000.0, 7000.0})
        CHECK_THAT(firMagnitudeDb(both.ll, f), WithinAbs(2.0 * firMagnitudeDb(single.ll, f), 0.01));
}

TEST_CASE("Fase lineal: el canal de cada banda se respeta")
{
    LinearPhaseDesigner designer;
    const BandSettings bell{true, {FilterType::Bell, 3000.0, 9.0, 1.0}};

    SECTION("Stereo: un solo FIR para los dos canales")
    {
        const BandSettings bands[] = {bell};
        const auto fir = designer.design(bands, PhaseMode::LinearLow, kFs, 2);
        CHECK(fir.rr.empty());
        CHECK(fir.lr.empty());
        CHECK(fir.rl.empty());
    }
    SECTION("Left: el derecho pasa sin cambios y no se mezclan")
    {
        BandSettings band = bell;
        band.channel = ChannelMode::Left;
        const BandSettings bands[] = {band};
        const auto fir = designer.design(bands, PhaseMode::LinearLow, kFs, 2);
        REQUIRE(!fir.rr.empty());
        CHECK(fir.lr.empty());
        CHECK_THAT(firMagnitudeDb(fir.ll, 3000.0), WithinAbs(9.0, 0.05));
        CHECK_THAT(firMagnitudeDb(fir.rr, 3000.0), WithinAbs(0.0, 1e-6));
    }
    SECTION("Mid y Side: los canales se mezclan como en el IIR")
    {
        for (const auto mode : {ChannelMode::Mid, ChannelMode::Side})
        {
            BandSettings band = bell;
            band.channel = mode;
            const BandSettings bands[] = {band};
            const auto fir = designer.design(bands, PhaseMode::LinearMedium, kFs, 2);
            REQUIRE(!fir.lr.empty());
            REQUIRE(!fir.rl.empty());
            for (const double f : {1000.0, 3000.0, 9000.0})
            {
                const auto target = LinearPhaseDesigner::targetResponse(bands, f, kFs);
                CHECK_THAT(std::abs(firResponse(fir.ll, f, kFs)), WithinAbs(std::abs(target.ll), 1e-3));
                CHECK_THAT(std::abs(firResponse(fir.lr, f, kFs)), WithinAbs(std::abs(target.lr), 1e-3));
                CHECK_THAT(std::abs(firResponse(fir.rl, f, kFs)), WithinAbs(std::abs(target.rl), 1e-3));
                CHECK_THAT(std::abs(firResponse(fir.rr, f, kFs)), WithinAbs(std::abs(target.rr), 1e-3));
            }
        }
    }
    SECTION("Mono: solo cuentan Stereo, Left y Mid")
    {
        BandSettings right = bell, side = bell, mid = bell;
        right.channel = ChannelMode::Right;
        side.channel = ChannelMode::Side;
        mid.channel = ChannelMode::Mid;
        const BandSettings ignored[] = {right, side};
        CHECK(designer.design(ignored, PhaseMode::LinearLow, kFs, 1).identity);

        const BandSettings bands[] = {mid, right};
        const auto fir = designer.design(bands, PhaseMode::LinearLow, kFs, 1);
        CHECK(fir.numChannels == 1);
        CHECK(fir.rr.empty());
        CHECK(fir.lr.empty());
        CHECK_THAT(firMagnitudeDb(fir.ll, 3000.0), WithinAbs(9.0, 0.05));
    }
}

TEST_CASE("Fase lineal: mover una banda da lo mismo que disenar desde cero")
{
    // El diseñador guarda la magnitud de cada banda; la caché no debe
    // dejar nada viejo.
    LinearPhaseDesigner cached;
    BandSettings bands[] = {{true, {FilterType::Bell, 500.0, 4.0, 1.0}}, {true, {FilterType::HighCut, 9000.0}}};
    (void)cached.design(bands, PhaseMode::LinearLow, kFs, 2);
    bands[0].filter.frequencyHz = 700.0;
    const auto moved = cached.design(bands, PhaseMode::LinearLow, kFs, 2);
    (void)cached.design(bands, PhaseMode::LinearLow, 96000.0, 2);
    const auto back = cached.design(bands, PhaseMode::LinearLow, kFs, 2);

    LinearPhaseDesigner fresh;
    const auto expected = fresh.design(bands, PhaseMode::LinearLow, kFs, 2);
    CHECK(moved.ll == expected.ll);
    CHECK(back.ll == expected.ll);
}

// --- Convolución particionada ---

TEST_CASE("PartitionedConvolver: coincide con la convolucion directa, canales mezclados incluidos")
{
    constexpr int kBlock = 64;
    constexpr std::size_t kTaps = 256; // 4 particiones
    const auto ll = randomTaps(kTaps, 1u), lr = randomTaps(kTaps, 2u), rl = randomTaps(kTaps, 3u),
               rr = randomTaps(kTaps, 4u);
    const FirKernel kernel(kBlock, ll, lr, rl, rr);
    CHECK(kernel.partitions() == 4);
    CHECK(kernel.hasCrossTerms());

    const auto inL = randomSignal(3000, 11u), inR = randomSignal(3000, 12u);
    const auto yLL = directConvolution(inL, ll), yLR = directConvolution(inR, lr);
    const auto yRL = directConvolution(inL, rl), yRR = directConvolution(inR, rr);

    PartitionedConvolver convolver(kBlock, 4);
    auto left = inL, right = inR;
    convolve(convolver, kernel, left, right, 37);

    // La salida llega con kBlock muestras de retraso.
    double worst = 0.0;
    for (std::size_t n = kBlock; n < left.size(); ++n)
    {
        worst = std::max(worst, std::abs(left[n] - (yLL[n - kBlock] + yLR[n - kBlock])));
        worst = std::max(worst, std::abs(right[n] - (yRL[n - kBlock] + yRR[n - kBlock])));
    }
    CHECK(worst < 2e-5);
    for (std::size_t n = 0; n < kBlock; ++n)
        CHECK(left[n] == 0.0f);
}

TEST_CASE("PartitionedConvolver: el resultado no depende del tamano de las llamadas")
{
    constexpr int kBlock = 128;
    const auto taps = randomTaps(1024, 5u);
    const FirKernel kernel(kBlock, taps);
    CHECK(!kernel.hasCrossTerms());
    const auto inL = randomSignal(5000, 21u), inR = randomSignal(5000, 22u);

    std::vector<float> referenceL, referenceR;
    for (const int chunk : {1, 13, 64, 128, 511, 4096})
    {
        PartitionedConvolver convolver(kBlock, kernel.partitions());
        auto left = inL, right = inR;
        convolve(convolver, kernel, left, right, chunk);
        if (referenceL.empty())
        {
            referenceL = left;
            referenceR = right;
            continue;
        }
        INFO("llamadas de " << chunk);
        CHECK(left == referenceL);
        CHECK(right == referenceR);
    }
}

TEST_CASE("PartitionedConvolver: mono con la misma entrada en los dos lados")
{
    constexpr int kBlock = 32;
    const auto taps = randomTaps(96, 8u);
    const FirKernel kernel(kBlock, taps);
    PartitionedConvolver convolver(kBlock, kernel.partitions());
    auto signal = randomSignal(800, 30u);
    const auto expected = directConvolution(signal, taps);
    for (int pos = 0; pos < static_cast<int>(signal.size());)
    {
        const int length = std::min(convolver.samplesUntilBlock(), static_cast<int>(signal.size()) - pos);
        convolver.process(signal.data() + pos, signal.data() + pos, signal.data() + pos, nullptr, length, kernel);
        pos += length;
    }
    for (std::size_t n = kBlock; n < signal.size(); ++n)
        CHECK_THAT(signal[n], WithinAbs(expected[n - kBlock], 2e-5));
}

TEST_CASE("PartitionedConvolver: el cambio de nucleo es un fundido de un bloque")
{
    constexpr int kBlock = 64;
    std::vector<double> half(kBlock, 0.0), twice(kBlock, 0.0);
    half[0] = 0.5;
    twice[0] = 2.0;
    const FirKernel a(kBlock, half), b(kBlock, twice);

    PartitionedConvolver convolver(kBlock, 1);
    std::vector<float> ones(static_cast<std::size_t>(kBlock), 1.0f), out(static_cast<std::size_t>(kBlock));
    auto block = [&](const FirKernel& kernel, const FirKernel* fadeFrom)
    {
        convolver.process(ones.data(), ones.data(), out.data(), nullptr, kBlock, kernel, fadeFrom);
        return out;
    };

    (void)block(a, nullptr);         // la salida de este bloque sale en el siguiente
    const auto first = block(b, &a); // sale el de `a`; se calcula el fundido
    const auto fading = block(b, nullptr);
    const auto settled = block(b, nullptr);

    for (const float v : first)
        CHECK_THAT(v, WithinAbs(0.5, 1e-6));
    for (int i = 0; i < kBlock; ++i)
    {
        const double gain = static_cast<double>(i + 1) / kBlock;
        CHECK_THAT(fading[static_cast<std::size_t>(i)], WithinAbs(0.5 + gain * 1.5, 1e-5));
    }
    for (const float v : settled)
        CHECK_THAT(v, WithinAbs(2.0, 1e-5));
}

TEST_CASE("PartitionedConvolver: un nucleo que no encaja da silencio en lugar de basura")
{
    const FirKernel big(64, randomTaps(512, 3u)); // 8 particiones
    PartitionedConvolver convolver(64, 4);
    auto left = randomSignal(512, 1u), right = left;
    convolve(convolver, big, left, right, 64);
    CHECK(std::all_of(left.begin(), left.end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("PartitionedConvolver: reset vuelve al silencio previo")
{
    const auto taps = randomTaps(128, 9u);
    const FirKernel kernel(32, taps);
    PartitionedConvolver convolver(32, kernel.partitions());
    const auto input = randomSignal(600, 2u);

    auto firstL = input, firstR = input;
    convolve(convolver, kernel, firstL, firstR, 50);
    convolver.reset();
    auto secondL = input, secondR = input;
    convolve(convolver, kernel, secondL, secondR, 50);
    CHECK(firstL == secondL);
}

TEST_CASE("Fase lineal: el FIR disenado, convolucionado, retrasa exactamente la latencia")
{
    LinearPhaseDesigner designer;
    const BandSettings bands[] = {{true, {FilterType::Bell, 1000.0, 0.0, 1.0}}};
    const auto fir = designer.design(bands, PhaseMode::LinearLow, kFs, 2);
    const FirKernel kernel(fir.size.blockSize, fir.ll);
    PartitionedConvolver convolver(fir.size.blockSize, fir.size.partitions());

    const int latency = fir.size.latency();
    std::vector<float> left(static_cast<std::size_t>(latency + 600), 0.0f);
    left[3] = 1.0f;
    auto right = left;
    convolve(convolver, kernel, left, right, 100);
    const auto peak =
        std::max_element(left.begin(), left.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    CHECK(peak - left.begin() == latency + 3);
    CHECK_THAT(*peak, WithinAbs(1.0, 1e-5));
}
