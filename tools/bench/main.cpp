// SPDX-License-Identifier: AGPL-3.0-or-later
//
// esceq_bench: mide el coste del hilo de audio de esceQ (dsp::PhaseEqualizer)
// con configuraciones fijas, para decidir optimizaciones con números
// (ADR 0020). Solo depende de dsp/.
//
// Uso: esceq_bench [opciones]
//   --scenario A,B   campanas, cortes, dinamicas, mezcla (por defecto, todos)
//   --mode A,B       iir, low, medium, high (por defecto, todos)
//   --rate A,B       frecuencias de muestreo en Hz (por defecto, 48000,192000)
//   --block A,B      tamaños de bloque del host (por defecto, 256)
//   --algorithm A    classic o matched (por defecto, classic)
//   --seconds S      segundos de audio por medida (por defecto, 2)
//   --repeat N       medidas por configuración (por defecto, 5)
//   --convolution    en vez del ecualizador, el coste de la llamada que
//                    completa un bloque de la convolución de fase lineal
//
// Imprime, por configuración, la mediana (y el mínimo y el máximo) del
// tiempo de proceso en % de la duración del audio: el % de un núcleo que
// ocupa una instancia estéreo. Compilar en Release: en Debug no significa nada.
// La señal no se apaga nunca, así que no aparecen denormales y no hace falta
// desactivarlos como hace el procesador.
//
// Escenarios (estéreo, ruido de un polo a unos −12 dBFS):
//   campanas   24 campanas estáticas de ±3 dB, Q 1, de 30 Hz a 16 kHz.
//   cortes     12 cortes de graves y 12 de agudos de 96 dB/oct (8 secciones cada uno).
//   dinamicas  24 campanas dinámicas comprimiendo todo el tiempo (umbral −50 dB, rango −12 dB).
//   mezcla     8 bandas de una mezcla real: 2 cortes de 24 dB/oct, 4 campanas
//              (2 de ellas dinámicas) y un shelf de agudos.
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/partitioned_convolver.h"
#include "esceq/dsp/phase_equalizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using esceq::dsp::BandSettings;
using esceq::dsp::FilterAlgorithm;
using esceq::dsp::FilterType;
using esceq::dsp::PhaseMode;
using Clock = std::chrono::steady_clock;

struct Options
{
    std::vector<std::string> scenarios{"campanas", "cortes", "dinamicas", "mezcla"};
    std::vector<std::string> modes{"iir", "low", "medium", "high"};
    std::vector<double> rates{48000.0, 192000.0};
    std::vector<int> blocks{256};
    FilterAlgorithm algorithm = FilterAlgorithm::Classic;
    double seconds = 2.0;
    int repeat = 5;
    bool convolution = false;
};

std::vector<std::string> splitList(std::string_view text)
{
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t comma = std::min(text.find(',', start), text.size());
        if (comma > start)
            items.emplace_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    return items;
}

/// Lee las opciones. false si alguna no se entiende.
bool parse(std::span<char* const> args, Options& options)
{
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string_view name = args[i];
        if (name == "--convolution")
        {
            options.convolution = true;
            continue;
        }
        if (i + 1 >= args.size())
            return false;
        const std::string_view value = args[++i];
        if (name == "--scenario")
            options.scenarios = splitList(value);
        else if (name == "--mode")
            options.modes = splitList(value);
        else if (name == "--rate" || name == "--block")
        {
            // Frecuencias positivas; bloques de una muestra o más.
            const bool isBlock = name == "--block";
            auto invalid = [isBlock](double n) { return isBlock ? !(n >= 1.0) : !(n > 0.0); };
            std::vector<double> numbers;
            for (const std::string& item : splitList(value))
                numbers.push_back(std::atof(item.c_str()));
            if (numbers.empty() || std::ranges::any_of(numbers, invalid))
                return false;
            if (name == "--rate")
                options.rates = numbers;
            else
            {
                options.blocks.clear();
                for (const double number : numbers)
                    options.blocks.push_back(static_cast<int>(number));
            }
        }
        else if (name == "--algorithm" && (value == "classic" || value == "matched"))
            options.algorithm = value == "matched" ? FilterAlgorithm::Matched : FilterAlgorithm::Classic;
        else if (name == "--seconds")
            options.seconds = std::atof(std::string(value).c_str());
        else if (name == "--repeat")
            options.repeat = std::atoi(std::string(value).c_str());
        else
            return false;
    }
    return options.seconds > 0.0 && options.repeat > 0;
}

bool parseMode(std::string_view name, PhaseMode& mode)
{
    constexpr std::string_view names[] = {"iir", "low", "medium", "high"};
    for (int i = 0; i < esceq::dsp::kNumPhaseModes; ++i)
        if (name == names[i])
        {
            mode = static_cast<PhaseMode>(i);
            return true;
        }
    return false;
}

BandSettings bell(double frequencyHz, double gainDb, double q, FilterAlgorithm algorithm)
{
    BandSettings band;
    band.enabled = true;
    band.filter.type = FilterType::Bell;
    band.filter.frequencyHz = frequencyHz;
    band.filter.gainDb = gainDb;
    band.filter.q = q;
    band.filter.algorithm = algorithm;
    return band;
}

BandSettings dynamic(BandSettings band)
{
    band.dynamics.enabled = true;
    band.dynamics.thresholdDb = -50.0;
    band.dynamics.rangeDb = -12.0;
    band.dynamics.attackMs = 5.0;
    band.dynamics.releaseMs = 50.0;
    return band;
}

BandSettings cut(FilterType type, double frequencyHz, int slope, FilterAlgorithm algorithm)
{
    BandSettings band;
    band.enabled = true;
    band.filter.type = type;
    band.filter.frequencyHz = frequencyHz;
    band.filter.slopeDbPerOct = slope;
    band.filter.algorithm = algorithm;
    return band;
}

/// Las bandas del escenario (vacío si no existe).
std::vector<BandSettings> scenarioBands(std::string_view name, FilterAlgorithm algorithm)
{
    // De 30 Hz a 16 kHz, repartidas en escala logarítmica.
    auto spread = [](int i) { return 30.0 * std::pow(16000.0 / 30.0, (i + 0.5) / esceq::dsp::kMaxBands); };
    if (name == "mezcla")
    {
        BandSettings shelf = bell(10000.0, 2.0, 0.7, algorithm);
        shelf.filter.type = FilterType::HighShelf;
        return {cut(FilterType::LowCut, 30.0, 24, algorithm),
                cut(FilterType::HighCut, 18000.0, 24, algorithm),
                bell(120.0, 2.0, 1.0, algorithm),
                bell(400.0, -3.0, 2.0, algorithm),
                bell(2500.0, 2.0, 1.5, algorithm),
                dynamic(bell(7000.0, 0.0, 3.0, algorithm)),
                dynamic(bell(250.0, 0.0, 2.0, algorithm)),
                shelf};
    }

    std::vector<BandSettings> bands;
    for (int i = 0; i < esceq::dsp::kMaxBands; ++i)
    {
        if (name == "campanas")
            bands.push_back(bell(spread(i), i % 2 == 0 ? -3.0 : 3.0, 1.0, algorithm));
        else if (name == "cortes")
            bands.push_back(i % 2 == 0 ? cut(FilterType::LowCut, 20.0, 96, algorithm)
                                       : cut(FilterType::HighCut, 18000.0, 96, algorithm));
        else if (name == "dinamicas")
            bands.push_back(dynamic(bell(spread(i), 0.0, 1.0, algorithm)));
    }
    return bands;
}

/// Ruido de un polo (más grave que el blanco) a unos −12 dBFS, distinto en
/// cada canal. Generador propio: el mismo en todas las plataformas.
std::vector<float> noise(int length, std::uint64_t seed)
{
    std::uint64_t state = seed;
    auto next = [&state]
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<float>(static_cast<double>(state >> 11) * 0x1.0p-52 - 1.0);
    };
    std::vector<float> samples(static_cast<std::size_t>(length));
    float low = 0.0f;
    for (float& sample : samples)
    {
        low = 0.95f * low + 0.15f * next();
        sample = low + 0.05f * next();
    }
    return samples;
}

double median(std::vector<double> values)
{
    std::ranges::sort(values);
    return values[values.size() / 2];
}

/// % de un núcleo de cada repetición, de menor a mayor.
std::vector<double> measureEqualizer(const std::vector<BandSettings>& bands, PhaseMode mode, double sampleRate,
                                     int block, const Options& options)
{
    const int length = static_cast<int>(options.seconds * sampleRate);
    const std::vector<float> left = noise(length, 0x9E3779B97F4A7C15ull);
    const std::vector<float> right = noise(length, 0xD1B54A32D192ED03ull);
    std::vector<float> ioLeft(left.size()), ioRight(right.size());

    std::vector<double> loads;
    for (int r = 0; r < options.repeat; ++r)
    {
        esceq::dsp::PhaseEqualizer equalizer;
        equalizer.prepare(sampleRate, 2);
        equalizer.setPhaseMode(mode);
        for (std::size_t b = 0; b < bands.size(); ++b)
            equalizer.setBand(static_cast<int>(b), bands[b]);

        // Calentamiento, hasta que el modo está activo y los fundidos han
        // acabado. El diseño (el hilo de control en el plugin) se hace aquí y
        // no se mide.
        auto run = [&](int from, int to)
        {
            for (int position = from; position + block <= to; position += block)
            {
                float* io[] = {ioLeft.data() + position, ioRight.data() + position};
                equalizer.process(io, 2, block);
            }
        };
        std::ranges::copy(left, ioLeft.begin());
        std::ranges::copy(right, ioRight.begin());
        for (int warmup = 0;
             warmup < 1000 && (warmup == 0 || !equalizer.isSettled() || equalizer.activePhaseMode() != mode); ++warmup)
        {
            while (equalizer.serviceDesigns())
            {
            }
            run(0, std::min(length, 4096));
        }

        std::ranges::copy(left, ioLeft.begin());
        std::ranges::copy(right, ioRight.begin());
        const auto start = Clock::now();
        run(0, length);
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        loads.push_back(100.0 * elapsed * sampleRate / static_cast<double>(length - length % block));
    }
    std::ranges::sort(loads);
    return loads;
}

void runEqualizer(const Options& options)
{
    std::printf("%-10s %-7s %7s %6s  %% de un núcleo: mediana (mín.–máx.)\n", "escenario", "modo", "Hz", "bloque");
    for (const std::string& scenario : options.scenarios)
    {
        const std::vector<BandSettings> bands = scenarioBands(scenario, options.algorithm);
        if (bands.empty())
        {
            std::fprintf(stderr, "escenario desconocido: %s\n", scenario.c_str());
            continue;
        }
        for (const std::string& modeName : options.modes)
        {
            PhaseMode mode{};
            if (!parseMode(modeName, mode))
            {
                std::fprintf(stderr, "modo desconocido: %s\n", modeName.c_str());
                continue;
            }
            for (const double rate : options.rates)
                for (const int block : options.blocks)
                {
                    if (block > static_cast<int>(options.seconds * rate))
                    {
                        std::fprintf(stderr, "bloque de %d muestras mayor que el audio medido a %.0f Hz\n", block,
                                     rate);
                        continue;
                    }
                    const std::vector<double> loads = measureEqualizer(bands, mode, rate, block, options);
                    std::printf("%-10s %-7s %7.0f %6d  %6.2f (%.2f–%.2f)\n", scenario.c_str(), modeName.c_str(), rate,
                                block, median(loads), loads.front(), loads.back());
                    std::fflush(stdout);
                }
        }
    }
}

/// Mediana, en ms, de la llamada que completa un bloque de convolución.
double convolutionCallMs(const esceq::dsp::LinearPhaseSize& size, bool crossfade, int repeat)
{
    std::vector<double> taps(static_cast<std::size_t>(size.firLength));
    for (std::size_t i = 0; i < taps.size(); ++i)
        taps[i] = 1.0e-3 * static_cast<double>(static_cast<int>(i * 7919 % 101) - 50);
    const esceq::dsp::FirKernel kernel(size.blockSize, taps);
    esceq::dsp::PartitionedConvolver convolver(size.blockSize, size.partitions());
    std::vector<float> left(static_cast<std::size_t>(size.blockSize), 0.1f);
    std::vector<float> right(left.size(), -0.1f);

    std::vector<double> times;
    for (int r = 0; r < std::max(repeat, 1) * 10; ++r)
    {
        const auto start = Clock::now();
        convolver.process(left.data(), right.data(), left.data(), right.data(), size.blockSize, kernel,
                          crossfade ? &kernel : nullptr);
        times.push_back(1.0e3 * std::chrono::duration<double>(Clock::now() - start).count());
    }
    return median(times);
}

void runConvolution(const Options& options)
{
    std::printf("%-7s %7s %7s %11s  %8s %12s %10s\n", "modo", "Hz", "bloque", "particiones", "llamada", "con fundido",
                "periodo");
    for (const std::string& modeName : options.modes)
    {
        PhaseMode mode{};
        if (!parseMode(modeName, mode) || !esceq::dsp::isLinearPhase(mode))
            continue;
        for (const double rate : options.rates)
        {
            const esceq::dsp::LinearPhaseSize size = esceq::dsp::linearPhaseSize(mode, rate);
            std::printf("%-7s %7.0f %7d %11d  %5.3f ms %9.3f ms %7.2f ms\n", modeName.c_str(), rate, size.blockSize,
                        size.partitions(), convolutionCallMs(size, false, options.repeat),
                        convolutionCallMs(size, true, options.repeat),
                        1.0e3 * static_cast<double>(size.blockSize) / rate);
            std::fflush(stdout);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse(std::span<char* const>(argv + 1, static_cast<std::size_t>(argc - 1)), options))
    {
        std::fprintf(stderr, "Opciones no válidas. Ver la cabecera de tools/bench/main.cpp.\n");
        return 1;
    }
    if (options.convolution)
        runConvolution(options);
    else
        runEqualizer(options);
    return 0;
}
