// SPDX-License-Identifier: AGPL-3.0-or-later
//
// esceq_bench: mide el coste del hilo de audio de esceQ (dsp::PhaseEqualizer)
// con configuraciones fijas, para decidir optimizaciones con números
// (ADR 0020). Depende de dsp/ y, para --modulation y --macros, de core/
// (core::ControlThread y core::applyMacros); no de JUCE.
//
// Uso: esceq_bench [opciones]
//   --scenario A,B   campanas, cortes, dinamicas, mezcla, espectral, mezcla-esp,
//                    saturacion, mezcla-sat (por defecto, todos)
//   --mode A,B       iir, low, medium, high (por defecto, todos)
//   --rate A,B       frecuencias de muestreo en Hz (por defecto, 48000,192000)
//   --block A,B      tamaños de bloque del host (por defecto, 256)
//   --algorithm A    classic o matched (por defecto, classic)
//   --seconds S      segundos de audio por medida (por defecto, 2)
//   --repeat N       medidas por configuración (por defecto, 5)
//   --convolution    en vez del ecualizador, el coste de la llamada que
//                    completa un bloque de la convolución de fase lineal
//   --match          en vez del ecualizador, lo que tarda el ajuste de la
//                    igualación (dsp::matchCurve, docs/dsp/igualacion.md)
//   --spectral       en vez del ecualizador, el motor de la dinámica espectral
//                    solo (dsp::SpectralDynamics, docs/dsp/dinamica-espectral.md),
//                    con --rate, --block, --seconds y --repeat
//   --saturation     en vez del ecualizador, la saturación (dsp::Saturation,
//                    docs/dsp/saturacion.md): las alternativas contra el
//                    aliasing comparadas a 48 kHz y el coste del motor, con
//                    --rate, --block, --seconds y --repeat
//   --modulation     en vez del ecualizador, una banda modulada (como un
//                    automation clip) en cada modo de --mode: el render sin
//                    tiempo real y el tiempo real, con --rate, --block,
//                    --seconds (del render; el tiempo real, como mucho 2 s)
//                    y --repeat
//   --macros         en vez del ecualizador, lo que cuesta aplicar las macros
//                    (core::applyMacros) en un bloque, con --repeat
//
// Imprime, por configuración, la mediana (y el mínimo y el máximo) del
// tiempo de proceso en % de la duración del audio: el % de un núcleo que
// ocupa una instancia estéreo; y lo que tarda la llamada más lenta (la
// mediana entre repeticiones de la más lenta de cada una), frente a lo que
// dura el bloque. Compilar en Release: en Debug no significa nada.
// La señal no se apaga nunca, así que no aparecen denormales y no hace falta
// desactivarlos como hace el procesador.
//
// Escenarios (estéreo, ruido de un polo a unos −12 dBFS):
//   campanas   24 campanas estáticas de ±3 dB, Q 1, de 30 Hz a 16 kHz.
//   cortes     12 cortes de graves y 12 de agudos de 96 dB/oct (8 secciones cada uno).
//   dinamicas  24 campanas dinámicas comprimiendo todo el tiempo (umbral −50 dB, rango −12 dB).
//   mezcla     8 bandas de una mezcla real: 2 cortes de 24 dB/oct, 4 campanas
//              (2 de ellas dinámicas) y un shelf de agudos.
//   espectral  24 campanas espectrales (las de "24 bandas" de --spectral, con
//              `dynamics.spectral`): L/R y M/S, 4 escuchando el sidechain.
//   mezcla-esp la mezcla, con sus 2 campanas dinámicas espectrales.
//   saturacion sin bandas, solo la saturación: Warm, 12 dB de drive, mezcla al
//              50 % (lo más caro: la seca y la húmeda), con los filtros del modo.
//   mezcla-sat la mezcla, con la saturación Soft a 6 dB (por defecto) y al 100 %.
//
// Con --match, dos objetivos en la rejilla de la sesión (192 puntos), con la
// referencia igual a la pista por unas bandas conocidas, al 100 %:
//   24 campanas   24 campanas de Q 4 y ±5 a ±9 dB, separadas 0,36 octavas
//                 desde 32 Hz, sin suavizar: pide las 24 bandas (el caso del
//                 límite de evaluaciones de tests/dsp/curve_match_test.cpp).
//   4 bandas      low shelf, dos campanas y high shelf, con 1/3 de octava.
//   notch         la referencia y las bandas de tools/snapshot --match (con un
//                 notch de Q 8 a 5,2 kHz en las actuales), con 1/3 de octava.
// Después, la idempotencia: 300 casos aleatorios (generador fijo) de una a
// cuatro bandas en la referencia y hasta tres en la pista, un 50 % de ellas
// estrechas (notch o campana de Q 6 a 16), con 1/6, 1/3 o 1 octava y al 50
// o al 100 %. Se aplica la propuesta y se vuelve a pedir: cuántas veces
// sale algo la segunda, su mayor ganancia y lo que tarda la primera.
//
// Con --spectral (la misma señal; además del % de un núcleo, lo que tarda
// cada trama, que se calcula entera en la llamada que completa un salto):
//   1 banda       una campana Stereo a 3 kHz comprimiendo (umbral −40 dB,
//                 rodilla de 12 dB, rango −30 dB: casi todos los bins en la
//                 zona en que la curva cambia, el caso más caro).
//   24 bandas     24 campanas así, de 30 Hz a 16 kHz, repartidas entre
//                 Stereo, Left, Right, Mid y Side, la mitad con tiempos
//                 automáticos y 4 escuchando el sidechain: L/R y M/S a la vez.
//   24 en reposo  las mismas, con el umbral por encima de la señal.
//
// Con --saturation, primero las alternativas de la ADR 0026 con la curva Soft
// sin compensar y 12 dB de drive, a 48 kHz: el aliasing de un seno de 10 kHz
// a −18, −6 y 0 dBFS (todo lo que no es un armónico por debajo de Nyquist,
// relativo a la fundamental); la respuesta en agudos con señal pequeña
// (−60 dBFS, la húmeda dividida por el drive): la húmeda sola y la mezcla al
// 50 % con la seca, a 18 kHz y relativas a 1 kHz; y el coste con la señal de
// los escenarios, en bloques de 256. Luego, el motor a 48 kHz (×8) con 12 dB
// de drive: el mismo aliasing, el nivel de un seno de −18 dBFS a 1 kHz y la
// mezcla al 0 % a 18 kHz; y su coste (Soft y Warm, filtros IIR y de fase
// lineal) con --rate y --block.
//
// Con --modulation (ADR 0028, bloque 7.1): una campana a 1 kHz con la
// ganancia a ±6 dB y 2 Hz, ajustada en cada bloque. En el render, lo que
// tarda sin modular, modulada diseñando en cada bloque (como hasta la 0.5.0) y
// modulada al ritmo de core::ControlThread::renderBlock() (como mucho un
// diseño por cada 10 ms de audio o por bloque de la convolución), los diseños
// de cada uno y si dos renders al ritmo salen bit a bit iguales. En tiempo
// real, los bloques al ritmo del reloj con el hilo de control propio: el %
// de un núcleo del hilo de audio, su llamada más lenta, los diseños por
// segundo, lo que tarda uno, y el % de un núcleo que ocupa el hilo de control.
//
// Con --macros (ADR 0028, bloque 7.2): lo que tarda core::applyMacros, que el
// procesador llama en cada bloque en el que cambia algún parámetro (una macro
// moviéndose, por ejemplo), sin asignaciones y con 1, 8 y 64 destinos (todas
// las macros llenas: ganancias, frecuencias, en escala logarítmica, y
// umbrales), con las ocho macros moviéndose. Sin asignaciones el procesador
// ni la llama.
#include "esceq/core/control_thread.h"
#include "esceq/core/macros.h"
#include "esceq/dsp/auto_gain.h"
#include "esceq/dsp/curve_match.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/partitioned_convolver.h"
#include "esceq/dsp/phase_equalizer.h"
#include "esceq/dsp/saturation.h"
#include "esceq/dsp/spectral_dynamics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using esceq::dsp::BandSettings;
using esceq::dsp::FilterAlgorithm;
using esceq::dsp::FilterType;
using esceq::dsp::PhaseMode;
using esceq::dsp::SaturationMode;
using esceq::dsp::SaturationSettings;
using Clock = std::chrono::steady_clock;

struct Options
{
    std::vector<std::string> scenarios{"campanas",  "cortes",     "dinamicas",  "mezcla",
                                       "espectral", "mezcla-esp", "saturacion", "mezcla-sat"};
    std::vector<std::string> modes{"iir", "low", "medium", "high"};
    std::vector<double> rates{48000.0, 192000.0};
    std::vector<int> blocks{256};
    FilterAlgorithm algorithm = FilterAlgorithm::Classic;
    double seconds = 2.0;
    int repeat = 5;
    bool convolution = false;
    bool match = false;
    bool spectral = false;
    bool saturation = false;
    bool modulation = false;
    bool macros = false;
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
        if (name == "--match")
        {
            options.match = true;
            continue;
        }
        if (name == "--spectral")
        {
            options.spectral = true;
            continue;
        }
        if (name == "--saturation")
        {
            options.saturation = true;
            continue;
        }
        if (name == "--modulation")
        {
            options.modulation = true;
            continue;
        }
        if (name == "--macros")
        {
            options.macros = true;
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
    band.dynamics.rangeDb = -30.0;
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

std::vector<BandSettings> spectralBands(int count, bool resting);

/// Las bandas del escenario (vacío si no existe o si no tiene bandas).
std::vector<BandSettings> scenarioBands(std::string_view name, FilterAlgorithm algorithm)
{
    if (name == "mezcla-sat")
        return scenarioBands("mezcla", algorithm);
    if (name == "espectral")
    {
        auto bands = spectralBands(esceq::dsp::kMaxBands, false);
        for (BandSettings& band : bands)
        {
            band.filter.algorithm = algorithm;
            band.dynamics.spectral = true;
        }
        return bands;
    }
    if (name == "mezcla-esp")
    {
        auto bands = scenarioBands("mezcla", algorithm);
        for (BandSettings& band : bands)
            band.dynamics.spectral = band.dynamics.enabled;
        return bands;
    }
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

struct EqualizerMeasure
{
    std::vector<double> loads; ///< % de un núcleo de cada repetición, de menor a mayor.
    double slowestCallMs = 0.0; ///< Mediana entre repeticiones de la llamada más lenta de cada una.
};

/// La saturación del escenario: Off salvo en los de saturación.
SaturationSettings scenarioSaturation(std::string_view name)
{
    if (name == "saturacion")
        return {SaturationMode::Warm, 12.0, 0.5};
    if (name == "mezcla-sat")
        return {SaturationMode::Soft, 6.0, 1.0};
    return {};
}

EqualizerMeasure measureEqualizer(const std::vector<BandSettings>& bands, const SaturationSettings& saturation,
                                  PhaseMode mode, double sampleRate, int block, const Options& options)
{
    const int length = static_cast<int>(options.seconds * sampleRate);
    const std::vector<float> left = noise(length, 0x9E3779B97F4A7C15ull);
    const std::vector<float> right = noise(length, 0xD1B54A32D192ED03ull);
    std::vector<float> ioLeft(left.size()), ioRight(right.size());
    // El sidechain, solo si alguna banda lo escucha.
    const bool external = std::ranges::any_of(bands, [](const BandSettings& b) { return b.dynamics.external; });
    const std::vector<float> key = external ? noise(length, 0x94D049BB133111EBull) : std::vector<float>{};

    std::vector<double> loads;
    std::vector<double> slowest;
    for (int r = 0; r < options.repeat; ++r)
    {
        esceq::dsp::PhaseEqualizer equalizer;
        equalizer.prepare(sampleRate, 2);
        equalizer.setPhaseMode(mode);
        for (std::size_t b = 0; b < bands.size(); ++b)
            equalizer.setBand(static_cast<int>(b), bands[b]);
        equalizer.setSaturation(saturation);

        // Calentamiento, hasta que el modo está activo y los fundidos han
        // acabado. El diseño (el hilo de control en el plugin) se hace aquí y
        // no se mide.
        double slowestCall = 0.0;
        auto run = [&](int from, int to, bool timeCalls = false)
        {
            for (int position = from; position + block <= to; position += block)
            {
                float* io[] = {ioLeft.data() + position, ioRight.data() + position};
                const float* sidechain[] = {key.data() + position, key.data() + position};
                const auto callStart = timeCalls ? Clock::now() : Clock::time_point{};
                equalizer.process(io, 2, block, external ? sidechain : nullptr, external ? 2 : 0);
                if (timeCalls)
                    slowestCall =
                        std::max(slowestCall, std::chrono::duration<double>(Clock::now() - callStart).count());
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

        // La llamada más lenta, en otra pasada: medir cada llamada cuesta.
        std::ranges::copy(left, ioLeft.begin());
        std::ranges::copy(right, ioRight.begin());
        run(0, length, true);
        slowest.push_back(1.0e3 * slowestCall);
    }
    std::ranges::sort(loads);
    return {loads, median(slowest)};
}

void runEqualizer(const Options& options)
{
    std::printf("%-10s %-7s %7s %6s  %% de un núcleo: mediana (mín.–máx.)   llamada más lenta, ms (bloque)\n",
                "escenario", "modo", "Hz", "bloque");
    for (const std::string& scenario : options.scenarios)
    {
        const std::vector<BandSettings> bands = scenarioBands(scenario, options.algorithm);
        const SaturationSettings saturation = scenarioSaturation(scenario);
        if (bands.empty() && saturation.mode == SaturationMode::Off)
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
                    const EqualizerMeasure measure = measureEqualizer(bands, saturation, mode, rate, block, options);
                    const std::vector<double>& loads = measure.loads;
                    std::printf("%-10s %-7s %7.0f %6d  %6.2f (%.2f–%.2f)   %.3f (%.3f)\n", scenario.c_str(),
                                modeName.c_str(), rate, block, median(loads), loads.front(), loads.back(),
                                measure.slowestCallMs, 1.0e3 * block / rate);
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

esceq::dsp::FilterParams matchBand(FilterType type, double frequencyHz, double gainDb, double q)
{
    esceq::dsp::FilterParams params;
    params.type = type;
    params.frequencyHz = frequencyHz;
    params.gainDb = gainDb;
    params.q = q;
    return params;
}

/// Aplicar dos veces (ver la cabecera): casos aleatorios con un generador fijo.
void runMatchIdempotence(const std::vector<double>& frequencies)
{
    std::uint32_t state = 12345;
    auto random = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 16777216.0;
    };
    auto enabled = [](const esceq::dsp::FilterParams& params) {
        BandSettings band;
        band.enabled = true;
        band.filter = params;
        return band;
    };
    constexpr int kCases = 300;
    int withBands = 0;
    double worstGainDb = 0.0;
    std::vector<double> times;
    for (int c = 0; c < kCases; ++c)
    {
        std::vector<esceq::dsp::FilterParams> reference;
        const int referenceBands = 1 + static_cast<int>(random() * 4.0);
        for (int k = 0; k < referenceBands; ++k)
        {
            const double r = random();
            const FilterType type = r < 0.6 ? FilterType::Bell : r < 0.8 ? FilterType::LowShelf : FilterType::HighShelf;
            const double frequency = 40.0 * std::pow(400.0, random());
            const double gain = (random() * 2.0 - 1.0) * 8.0;
            reference.push_back(
                matchBand(type, frequency, gain, type == FilterType::Bell ? 0.5 + random() * 3.0 : 0.7071));
        }
        std::vector<float> track(frequencies.size(), -20.0f), referenceDb(frequencies.size());
        for (std::size_t i = 0; i < frequencies.size(); ++i)
        {
            double db = -20.0;
            for (const auto& band : reference)
                db += esceq::dsp::magnitudeDb(esceq::dsp::designBand(band, 48000.0), frequencies[i], 48000.0);
            referenceDb[i] = static_cast<float>(db);
        }
        std::vector<BandSettings> current;
        const int currentBands = static_cast<int>(random() * 4.0);
        for (int k = 0; k < currentBands; ++k)
        {
            const double r = random();
            if (r < 0.3)
            {
                const double frequency = 200.0 * std::pow(60.0, random());
                current.push_back(enabled(matchBand(FilterType::Notch, frequency, 0.0, 4.0 + random() * 8.0)));
            }
            else if (r < 0.5)
            {
                const double frequency = 100.0 * std::pow(100.0, random());
                const double gain = (random() * 2.0 - 1.0) * 12.0;
                current.push_back(enabled(matchBand(FilterType::Bell, frequency, gain, 6.0 + random() * 10.0)));
            }
            else
            {
                const double frequency = 50.0 * std::pow(300.0, random());
                const double gain = (random() * 2.0 - 1.0) * 6.0;
                current.push_back(enabled(matchBand(FilterType::Bell, frequency, gain, 0.5 + random() * 2.0)));
            }
        }
        constexpr double kSmoothing[] = {1.0 / 6.0, 1.0 / 3.0, 1.0};
        esceq::dsp::CurveMatchSettings settings;
        settings.smoothingOctaves = kSmoothing[static_cast<int>(random() * 3.0)];
        settings.amount = random() < 0.5 ? 0.5 : 1.0;
        esceq::dsp::CurveMatchInput input;
        input.frequencies = frequencies;
        input.trackDb = track;
        input.referenceDb = referenceDb;
        input.resolutionHz = 48000.0 / 8192.0;
        input.currentBands = current;
        const auto start = Clock::now();
        const esceq::dsp::CurveMatchResult first = esceq::dsp::matchCurve(input, settings);
        times.push_back(1.0e3 * std::chrono::duration<double>(Clock::now() - start).count());
        for (const auto& band : first.bands)
            current.push_back(enabled(band));
        input.currentBands = current;
        const esceq::dsp::CurveMatchResult second = esceq::dsp::matchCurve(input, settings);
        if (!second.bands.empty())
            ++withBands;
        for (const auto& band : second.bands)
            worstGainDb = std::max(worstGainDb, std::abs(band.gainDb));
    }
    double mean = 0.0;
    for (const double t : times)
        mean += t;
    mean /= static_cast<double>(times.size());
    std::printf("\naplicar dos veces, %d casos: la segunda propone algo en %d (%.1f %%), con %.2f dB como mucho;\n"
                "la primera tarda %.1f ms de media, %.1f ms como mucho\n",
                kCases, withBands, 100.0 * withBands / kCases, worstGainDb, mean, *std::ranges::max_element(times));
}

void runMatch(const Options& options)
{
    struct Target
    {
        const char* name;
        std::vector<esceq::dsp::FilterParams> bands;
        double smoothingOctaves;
        int maxBands;
        std::vector<BandSettings> current{};
    };
    std::vector<Target> targets;
    {
        Target bells{"24 campanas", {}, 0.0, 24};
        for (int i = 0; i < 24; ++i)
        {
            const double gain = (i % 2 == 0 ? 1.0 : -1.0) * (5.0 + static_cast<double>(i * 7 % 5));
            bells.bands.push_back(matchBand(FilterType::Bell, 32.0 * std::exp2(0.36 * i), gain, 4.0));
        }
        targets.push_back(bells);
        targets.push_back({"4 bandas",
                           {matchBand(FilterType::LowShelf, 120.0, 5.0, 0.7071),
                            matchBand(FilterType::Bell, 900.0, -4.0, 1.5), matchBand(FilterType::Bell, 3500.0, 3.0, 2.0),
                            matchBand(FilterType::HighShelf, 9000.0, 4.0, 0.7071)},
                           1.0 / 3.0, 8});
        // tools/snapshot --match: la referencia y las bandas de la pista.
        Target notch{"notch",
                     {matchBand(FilterType::LowShelf, 120.0, -4.0, 0.7071),
                      matchBand(FilterType::Bell, 2500.0, 3.0, 1.2),
                      matchBand(FilterType::HighShelf, 7000.0, 5.0, 0.7071)},
                     1.0 / 3.0,
                     8};
        for (const auto& params : {matchBand(FilterType::LowCut, 35.0, 0.0, 0.7071),
                                   matchBand(FilterType::Bell, 240.0, -4.0, 1.4),
                                   matchBand(FilterType::Bell, 2800.0, 3.5, 0.9),
                                   matchBand(FilterType::HighShelf, 9000.0, 2.5, 0.7071),
                                   matchBand(FilterType::Notch, 5200.0, 0.0, 8.0)})
        {
            BandSettings band;
            band.enabled = true;
            band.filter = params;
            notch.current.push_back(band);
        }
        notch.current[0].filter.slopeDbPerOct = 24;
        notch.current[1].channel = esceq::dsp::ChannelMode::Mid;
        targets.push_back(notch);
    }

    // La rejilla de la sesión: 192 puntos logarítmicos de 20 Hz a 20 kHz.
    std::vector<double> frequencies(192);
    for (std::size_t i = 0; i < frequencies.size(); ++i)
        frequencies[i] = 20.0 * std::pow(1000.0, static_cast<double>(i) / 191.0);

    std::printf("%-12s %-8s %7s  %10s %8s %6s %11s %12s\n", "objetivo", "filtro", "Hz", "mediana", "máximo", "bandas",
                "evaluaciones", "iteraciones");
    for (const Target& target : targets)
    {
        std::vector<float> track(frequencies.size(), -20.0f), reference(frequencies.size());
        for (std::size_t i = 0; i < frequencies.size(); ++i)
        {
            double db = -20.0;
            for (const auto& band : target.bands)
                db += esceq::dsp::magnitudeDb(esceq::dsp::designBand(band, 48000.0), frequencies[i], 48000.0);
            reference[i] = static_cast<float>(db);
        }
        for (const FilterAlgorithm algorithm : {FilterAlgorithm::Classic, FilterAlgorithm::Matched})
            for (const double rate : options.rates)
            {
                esceq::dsp::CurveMatchInput input;
                input.frequencies = frequencies;
                input.trackDb = track;
                input.referenceDb = reference;
                input.resolutionHz = 48000.0 / 8192.0;
                input.currentBands = target.current;
                input.sampleRate = rate;
                input.algorithm = algorithm;
                esceq::dsp::CurveMatchSettings settings;
                settings.amount = 1.0;
                settings.smoothingOctaves = target.smoothingOctaves;
                settings.maxBands = target.maxBands;

                std::vector<double> times;
                esceq::dsp::CurveMatchResult result;
                for (int r = 0; r < options.repeat; ++r)
                {
                    const auto start = Clock::now();
                    result = esceq::dsp::matchCurve(input, settings);
                    times.push_back(1.0e3 * std::chrono::duration<double>(Clock::now() - start).count());
                }
                std::printf("%-12s %-8s %7.0f  %7.1f ms %5.1f ms %6zu %11lld %12d\n", target.name,
                            algorithm == FilterAlgorithm::Matched ? "matched" : "classic", rate, median(times),
                            *std::ranges::max_element(times), result.bands.size(),
                            static_cast<long long>(result.evaluations), result.iterations);
                std::fflush(stdout);
            }
    }
    runMatchIdempotence(frequencies);
}

std::vector<BandSettings> spectralBands(int count, bool resting)
{
    using esceq::dsp::ChannelMode;
    constexpr ChannelMode kModes[] = {ChannelMode::Stereo, ChannelMode::Left, ChannelMode::Right, ChannelMode::Mid,
                                      ChannelMode::Side};
    std::vector<BandSettings> bands;
    for (int i = 0; i < count; ++i)
    {
        BandSettings band = bell(count == 1 ? 3000.0 : 30.0 * std::pow(16000.0 / 30.0, i / 23.0), 0.0, 1.0,
                                 FilterAlgorithm::Classic);
        band.dynamics.enabled = true;
        band.dynamics.thresholdDb = resting ? 6.0 : -40.0;
        band.dynamics.rangeDb = -30.0;
        band.dynamics.kneeDb = 12.0;
        band.dynamics.autoTiming = i % 2 == 1;
        band.dynamics.external = count > 1 && i % 6 == 5;
        band.channel = kModes[static_cast<std::size_t>(i % 5)];
        bands.push_back(band);
    }
    return bands;
}

void runSpectral(const Options& options)
{
    struct Scenario
    {
        const char* name;
        std::vector<BandSettings> bands;
    };
    const Scenario scenarios[] = {
        {"1 banda", spectralBands(1, false)},
        {"24 bandas", spectralBands(24, false)},
        {"24 en reposo", spectralBands(24, true)},
    };
    std::printf("%-13s %7s %6s  %% de un núcleo: mediana (mín.–máx.)   ms por trama (salto)\n", "escenario", "Hz",
                "bloque");
    for (const Scenario& scenario : scenarios)
        for (const double rate : options.rates)
            for (const int block : options.blocks)
            {
                const int length = static_cast<int>(options.seconds * rate);
                if (block > length)
                    continue;
                const std::vector<float> left = noise(length, 0x9E3779B97F4A7C15ull);
                const std::vector<float> right = noise(length, 0xD1B54A32D192ED03ull);
                const std::vector<float> key = noise(length, 0x94D049BB133111EBull);
                std::vector<float> ioLeft(left.size()), ioRight(right.size());
                std::vector<double> loads;
                double frameMs = 0.0;
                for (int r = 0; r < options.repeat; ++r)
                {
                    esceq::dsp::SpectralDynamics engine;
                    engine.prepare(rate);
                    for (std::size_t b = 0; b < scenario.bands.size(); ++b)
                        engine.setBand(static_cast<int>(b), scenario.bands[b]);
                    std::ranges::copy(left, ioLeft.begin());
                    std::ranges::copy(right, ioRight.begin());
                    const auto start = Clock::now();
                    for (int position = 0; position + block <= length; position += block)
                    {
                        float* io[] = {ioLeft.data() + position, ioRight.data() + position};
                        const float* sidechain[] = {key.data() + position, key.data() + position};
                        engine.process(io, 2, block, sidechain, 2);
                    }
                    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
                    loads.push_back(100.0 * elapsed * rate / static_cast<double>(length - length % block));
                    frameMs = std::max(frameMs, 1.0e3 * elapsed / static_cast<double>(engine.framesProcessed()));
                }
                std::ranges::sort(loads);
                const double hopMs = 1.0e3 * esceq::dsp::SpectralDynamics::fftSize(rate) /
                                     esceq::dsp::SpectralDynamics::kHopDivisor / rate;
                std::printf("%-13s %7.0f %6d  %6.2f (%.2f–%.2f)   %.3f (%.1f)\n", scenario.name, rate, block,
                            median(loads), loads.front(), loads.back(), frameMs, hopMs);
                std::fflush(stdout);
            }
}


/// Una alternativa contra el aliasing de la ADR 0026: tanh(d·x) sin
/// compensar, sobremuestreada con `stages` etapas (0: sin sobremuestreo) y
/// con o sin ADAA de primer orden (Parker, Zavalishin y Le Bivic, 2016;
/// Bilbao, Esqueda, Parker y Välimäki, 2017). `mix`: la parte de la húmeda,
/// dividida por el drive, con la seca por los mismos filtros.
class Alternative
{
public:
    Alternative(int stages, esceq::dsp::OversamplingFilter filter, bool adaa, double driveDb, double mix)
        : stages_(stages), adaa_(adaa), drive_(std::pow(10.0, driveDb / 20.0)), mix_(mix)
    {
        if (stages_ > 0)
            oversampler_.setup(filter, stages_);
    }

    void process(float* const* channels, int numChannels, int numSamples)
    {
        constexpr int kBlock = esceq::dsp::Oversampler::kMaxBlock;
        const int factor = 1 << stages_;
        for (int offset = 0; offset < numSamples; offset += kBlock)
        {
            const int length = std::min(kBlock, numSamples - offset);
            std::array<float*, 2> io{};
            std::array<float*, 2> work{};
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto c = static_cast<std::size_t>(ch);
                io[c] = channels[ch] + offset;
                work[c] = stages_ > 0 ? buffers_[c].data() : io[c];
            }
            if (stages_ > 0)
                oversampler_.upsample(io.data(), work.data(), numChannels, length);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                double& previous = previous_[static_cast<std::size_t>(ch)];
                float* w = work[static_cast<std::size_t>(ch)];
                for (int i = 0; i < length * factor; ++i)
                {
                    const double x = w[i];
                    const double u = drive_ * x;
                    double y = 0.0;
                    if (!adaa_)
                        y = std::tanh(u);
                    else
                    {
                        const double delta = u - previous;
                        y = std::abs(delta) > 1.0e-5 ? (logCosh(u) - logCosh(previous)) / delta
                                                     : std::tanh(0.5 * (u + previous));
                        previous = u;
                    }
                    w[i] = static_cast<float>(x + mix_ * (y / drive_ - x));
                }
            }
            if (stages_ > 0)
                oversampler_.downsample(work.data(), io.data(), numChannels, length);
        }
    }

private:
    static double logCosh(double u)
    {
        const double a = std::abs(u);
        return a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2;
    }

    int stages_;
    bool adaa_;
    double drive_;
    double mix_;
    esceq::dsp::Oversampler oversampler_;
    std::array<double, 2> previous_{};
    using Inner = std::array<float, esceq::dsp::Oversampler::kMaxBlock * esceq::dsp::Oversampler::kMaxFactor>;
    std::array<Inner, 2> buffers_{};
};

/// Potencia del componente de `hz` (amplitud²/2; en 0 Hz, la media²) en `n`
/// muestras desde `start`, con un número entero de sus periodos.
double powerAt(const std::vector<float>& x, std::size_t start, std::size_t n, double hz, double rate)
{
    double a = 0.0;
    double b = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate;
        a += x[start + i] * std::cos(phase);
        b += x[start + i] * std::sin(phase);
    }
    const double scale = hz == 0.0 ? 1.0 / static_cast<double>(n) : 2.0 / static_cast<double>(n);
    a *= scale;
    b *= scale;
    return hz == 0.0 ? a * a : 0.5 * (a * a + b * b);
}

std::vector<float> sine(std::size_t length, double hz, double amplitude, double rate)
{
    std::vector<float> out(length);
    for (std::size_t i = 0; i < length; ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    return out;
}

template <typename Processor>
std::vector<float> processMono(Processor& processor, std::vector<float> signal)
{
    for (std::size_t position = 0; position < signal.size(); position += 256)
    {
        float* channels[] = {signal.data() + position};
        processor.process(channels, 1, static_cast<int>(std::min<std::size_t>(256, signal.size() - position)));
    }
    return signal;
}

/// Aliasing de un seno de 10 kHz a 48 kHz, en dB relativos a la fundamental:
/// por Parseval, la potencia que no es la continua ni un armónico por debajo
/// de Nyquist. 4800 muestras contienen un número entero de periodos de todo.
double aliasingDb(const std::vector<float>& y)
{
    constexpr double kRate = 48000.0;
    constexpr std::size_t kStart = 24000;
    constexpr std::size_t kLength = 4800;
    double total = 0.0;
    for (std::size_t i = 0; i < kLength; ++i)
        total += static_cast<double>(y[kStart + i]) * y[kStart + i];
    double rest = total / static_cast<double>(kLength) - powerAt(y, kStart, kLength, 0.0, kRate);
    for (double hz = 10000.0; hz < kRate / 2.0; hz += 10000.0)
        rest -= powerAt(y, kStart, kLength, hz, kRate);
    return 10.0 * std::log10(std::max(rest, 1e-30) / powerAt(y, kStart, kLength, 10000.0, kRate));
}

/// Cuánto tarda un proceso estéreo con la señal de los escenarios, en % de un
/// núcleo: mediana, mínimo y máximo de `repeat` medidas.
template <typename Make>
std::vector<double> stereoLoads(Make make, double rate, int block, double seconds, int repeat)
{
    const int length = static_cast<int>(seconds * rate);
    const std::vector<float> left = noise(length, 0x9E3779B97F4A7C15ull);
    const std::vector<float> right = noise(length, 0xD1B54A32D192ED03ull);
    std::vector<float> ioLeft(left.size()), ioRight(right.size());
    std::vector<double> loads;
    for (int r = 0; r < repeat; ++r)
    {
        auto processor = make();
        std::ranges::copy(left, ioLeft.begin());
        std::ranges::copy(right, ioRight.begin());
        const auto start = Clock::now();
        for (int position = 0; position + block <= length; position += block)
        {
            float* io[] = {ioLeft.data() + position, ioRight.data() + position};
            processor->process(io, 2, block);
        }
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        loads.push_back(100.0 * elapsed * rate / static_cast<double>(length - length % block));
    }
    std::ranges::sort(loads);
    return loads;
}

void runSaturation(const Options& options)
{
    using esceq::dsp::OversamplingFilter;
    struct Candidate
    {
        const char* name;
        int stages;
        OversamplingFilter filter;
        bool adaa;
    };
    const Candidate candidates[] = {
        {"sin sobremuestreo", 0, OversamplingFilter::Iir, false},
        {"ADAA", 0, OversamplingFilter::Iir, true},
        {"x2 IIR", 1, OversamplingFilter::Iir, false},
        {"x2 IIR + ADAA", 1, OversamplingFilter::Iir, true},
        {"x4 IIR", 2, OversamplingFilter::Iir, false},
        {"x4 IIR + ADAA", 2, OversamplingFilter::Iir, true},
        {"x8 IIR", 3, OversamplingFilter::Iir, false},
        {"x4 fase lineal", 2, OversamplingFilter::LinearPhase, false},
        {"x8 fase lineal", 3, OversamplingFilter::LinearPhase, false},
    };
    constexpr double kRate = 48000.0;
    constexpr double kDriveDb = 12.0;
    std::printf("Alternativas, 48 kHz, 12 dB de drive (Soft, sin compensar)\n");
    std::printf("%-18s  aliasing (dB): -18    -6     0 dBFS   18 kHz: húmeda  mezcla 50 %%   %% de un núcleo\n",
                "alternativa");
    for (const Candidate& c : candidates)
    {
        double aliasing[3] = {};
        const double levels[] = {-18.0, -6.0, 0.0};
        for (int l = 0; l < 3; ++l)
        {
            Alternative alternative(c.stages, c.filter, c.adaa, kDriveDb, 1.0);
            const std::vector<float> x = sine(28800, 10000.0, std::pow(10.0, levels[l] / 20.0), kRate);
            aliasing[l] = aliasingDb(processMono(alternative, x));
        }
        // Respuesta con señal pequeña: frecuencias enteras, 1 s con un número entero de periodos.
        const auto response = [&](double mix) {
            const auto gain = [&](double hz) {
                Alternative alternative(c.stages, c.filter, c.adaa, kDriveDb, mix);
                const std::vector<float> y = processMono(alternative, sine(60000, hz, 1.0e-3, kRate));
                return powerAt(y, 12000, 48000, hz, kRate);
            };
            return 10.0 * std::log10(gain(18000.0) / gain(1000.0));
        };
        const double wet = response(1.0);
        const double half = response(0.5);
        const std::vector<double> loads = stereoLoads(
            [&] { return std::make_unique<Alternative>(c.stages, c.filter, c.adaa, kDriveDb, 1.0); }, kRate, 256,
            options.seconds, options.repeat);
        std::printf("%-18s  %20.1f %6.1f %6.1f   %14.2f %11.2f   %6.2f (%.2f–%.2f)\n", c.name, aliasing[0],
                    aliasing[1], aliasing[2], wet, half, median(loads), loads.front(), loads.back());
        std::fflush(stdout);
    }

    std::printf("\nMotor a 48 kHz (dsp::Saturation, x8), 12 dB de drive\n");
    std::printf("%-6s %-14s  aliasing (dB): -18    -6     0 dBFS   -18 dBFS a 1 kHz (dB)   mezcla 0 %%, 18 kHz (dB)\n",
                "curva", "filtros");
    for (const auto mode : {esceq::dsp::SaturationMode::Soft, esceq::dsp::SaturationMode::Warm})
        for (const auto filter : {OversamplingFilter::Iir, OversamplingFilter::LinearPhase})
        {
            const auto make = [&](double mix) {
                esceq::dsp::Saturation engine;
                engine.setFilter(filter);
                engine.setMode(mode);
                engine.setDriveDb(kDriveDb);
                engine.setMix(mix);
                engine.prepare(kRate);
                return engine;
            };
            double aliasing[3] = {};
            const double levels[] = {-18.0, -6.0, 0.0};
            for (int l = 0; l < 3; ++l)
            {
                esceq::dsp::Saturation engine = make(1.0);
                const std::vector<float> tone = sine(28800, 10000.0, std::pow(10.0, levels[l] / 20.0), kRate);
                aliasing[l] = aliasingDb(processMono(engine, tone));
            }
            const auto gainDb = [&](double mix, double hz, double amplitude) {
                esceq::dsp::Saturation engine = make(mix);
                const std::vector<float> x = sine(60000, hz, amplitude, kRate);
                const std::vector<float> y = processMono(engine, x);
                return 10.0 * std::log10(powerAt(y, 12000, 48000, hz, kRate) / powerAt(x, 12000, 48000, hz, kRate));
            };
            // El nivel del seno de calibración: su valor eficaz (con los armónicos).
            esceq::dsp::Saturation engine = make(1.0);
            const std::vector<float> x = sine(60000, 1000.0, std::pow(10.0, -18.0 / 20.0), kRate);
            const std::vector<float> y = processMono(engine, x);
            double in = 0.0;
            double out = 0.0;
            for (std::size_t i = 12000; i < 60000; ++i)
            {
                in += static_cast<double>(x[i]) * x[i];
                out += static_cast<double>(y[i]) * y[i];
            }
            std::printf("%-6s %-14s  %20.1f %6.1f %6.1f   %21.4f   %24.4f\n",
                        mode == esceq::dsp::SaturationMode::Soft ? "Soft" : "Warm",
                        filter == OversamplingFilter::Iir ? "IIR" : "fase lineal", aliasing[0], aliasing[1],
                        aliasing[2], 10.0 * std::log10(out / in), gainDb(0.0, 18000.0, 0.5));
            std::fflush(stdout);
        }

    std::printf("\nMotor (dsp::Saturation, 12 dB de drive, mezcla al 50 %%)\n");
    std::printf("%-6s %-14s %7s %6s  %% de un núcleo: mediana (mín.–máx.)\n", "curva", "filtros", "Hz", "bloque");
    for (const auto mode : {esceq::dsp::SaturationMode::Soft, esceq::dsp::SaturationMode::Warm})
        for (const auto filter : {OversamplingFilter::Iir, OversamplingFilter::LinearPhase})
            for (const double rate : options.rates)
                for (const int block : options.blocks)
                {
                    const auto make = [&] {
                        auto engine = std::make_unique<esceq::dsp::Saturation>();
                        engine->setFilter(filter);
                        engine->setMode(mode);
                        engine->setDriveDb(kDriveDb);
                        engine->setMix(0.5);
                        engine->prepare(rate);
                        return engine;
                    };
                    const std::vector<double> loads = stereoLoads(make, rate, block, options.seconds, options.repeat);
                    std::printf("%-6s %-14s %7.0f %6d  %6.2f (%.2f–%.2f)\n",
                                mode == esceq::dsp::SaturationMode::Soft ? "Soft" : "Warm",
                                filter == OversamplingFilter::Iir ? "IIR" : "fase lineal", rate, block,
                                median(loads), loads.front(), loads.back());
                    std::fflush(stdout);
                }
}

/// Cómo se diseña el FIR en el render del escenario de modulación.
enum class RenderDesign
{
    Static,     ///< Sin modular: los diseños de cada bloque no hacen nada.
    EachBlock,  ///< Como hasta la 0.5.0: flush() en cada bloque.
    Paced,      ///< Como ahora: core::ControlThread::renderBlock().
};

struct RenderMeasure
{
    double seconds = 0.0;         ///< Lo que tarda el render.
    std::uint64_t designs = 0;    ///< Veces que el hilo de control ha hecho algo.
    std::vector<float> output;    ///< El canal izquierdo, para comparar dos renders.
};

/// La ganancia de la campana del escenario de modulación en el instante `t`.
double modulatedGainDb(double t)
{
    return 6.0 * std::sin(2.0 * std::numbers::pi * 2.0 * t);
}

/// Renderiza `options.seconds` de la campana modulada sin tiempo real, como el
/// procesador: los ajustes de cada bloque, el diseño y el audio.
RenderMeasure renderModulated(PhaseMode mode, double rate, int block, double seconds, RenderDesign design,
                              bool keepOutput)
{
    esceq::dsp::PhaseEqualizer eq;
    esceq::dsp::AutoGain autoGain;
    BandSettings band = bell(1000.0, 0.0, 1.0, FilterAlgorithm::Classic);
    eq.setBand(0, band);
    eq.setPhaseMode(mode);
    eq.prepare(rate, 2);
    esceq::core::ControlThread control(eq, autoGain, esceq::core::ControlThread::Threading::Manual);
    control.flush();
    eq.update();
    const std::uint64_t before = control.serviceCount();

    const auto length = static_cast<int>(seconds * rate);
    const std::vector<float> left = noise(length, 1u), right = noise(length, 2u);
    std::vector<float> l(static_cast<std::size_t>(block)), r(l.size());
    RenderMeasure measure;
    if (keepOutput)
        measure.output.reserve(static_cast<std::size_t>(length));
    const auto start = Clock::now();
    for (int pos = 0; pos + block <= length; pos += block)
    {
        std::copy_n(left.begin() + pos, block, l.begin());
        std::copy_n(right.begin() + pos, block, r.begin());
        if (design != RenderDesign::Static)
        {
            band.filter.gainDb = modulatedGainDb(pos / rate);
            eq.setBand(0, band);
        }
        if (design == RenderDesign::EachBlock)
            control.flush();
        control.renderBlock(block, rate);
        float* channels[2] = {l.data(), r.data()};
        eq.process(channels, 2, block);
        if (keepOutput)
            measure.output.insert(measure.output.end(), l.begin(), l.end());
    }
    measure.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    measure.designs = control.serviceCount() - before;
    return measure;
}

/// Mediana, en ms, de lo que tarda un diseño de la campana en ese modo.
double designMs(PhaseMode mode, double rate, int repeat)
{
    esceq::dsp::PhaseEqualizer eq;
    BandSettings band = bell(1000.0, 0.0, 1.0, FilterAlgorithm::Classic);
    eq.setBand(0, band);
    eq.setPhaseMode(mode);
    eq.prepare(rate, 2);
    while (eq.serviceDesigns())
    {
    }
    eq.update();
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    float* channels[2] = {l.data(), r.data()};
    std::vector<double> times;
    for (int i = 0; i < std::max(repeat, 1) * 10; ++i)
    {
        band.filter.gainDb = modulatedGainDb(i * 0.01);
        eq.setBand(0, band);
        eq.process(channels, 2, 64); // pide el diseño
        const auto start = Clock::now();
        while (eq.serviceDesigns())
        {
        }
        times.push_back(1.0e3 * std::chrono::duration<double>(Clock::now() - start).count());
    }
    return median(times);
}

/// En tiempo real, al ritmo del reloj, durante `seconds`: el % de un núcleo
/// del hilo de audio (con el hilo de control diseñando a la vez), su llamada
/// más lenta y los diseños por segundo del hilo de control.
void realtimeModulated(PhaseMode mode, double rate, int block, double seconds, double& audioLoad,
                       double& slowestMs, double& designsPerSecond)
{
    esceq::dsp::PhaseEqualizer eq;
    esceq::dsp::AutoGain autoGain;
    BandSettings band = bell(1000.0, 0.0, 1.0, FilterAlgorithm::Classic);
    eq.setBand(0, band);
    eq.setPhaseMode(mode);
    eq.prepare(rate, 2);
    esceq::core::ControlThread control(eq, autoGain);
    control.flush();
    eq.update();
    const std::uint64_t before = control.serviceCount();

    const auto length = static_cast<int>(seconds * rate);
    const std::vector<float> left = noise(length, 1u), right = noise(length, 2u);
    std::vector<float> l(static_cast<std::size_t>(block)), r(l.size());
    double busy = 0.0;
    slowestMs = 0.0;
    const auto begin = Clock::now();
    for (int pos = 0; pos + block <= length; pos += block)
    {
        // El bloque llega cuando le toca, como desde la tarjeta de sonido.
        std::this_thread::sleep_until(begin + std::chrono::duration_cast<Clock::duration>(
                                                  std::chrono::duration<double>(pos / rate)));
        std::copy_n(left.begin() + pos, block, l.begin());
        std::copy_n(right.begin() + pos, block, r.begin());
        const auto start = Clock::now();
        band.filter.gainDb = modulatedGainDb(pos / rate);
        eq.setBand(0, band);
        control.realtimeBlock();
        float* channels[2] = {l.data(), r.data()};
        eq.process(channels, 2, block);
        const double ms = 1.0e3 * std::chrono::duration<double>(Clock::now() - start).count();
        busy += ms;
        slowestMs = std::max(slowestMs, ms);
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    audioLoad = 100.0 * busy * 1.0e-3 / (length / rate);
    designsPerSecond = static_cast<double>(control.serviceCount() - before) / elapsed;
}

void runModulation(const Options& options)
{
    std::printf("Modulación: una campana a 1 kHz, Q 1, con la ganancia a ±6 dB y 2 Hz (como un automation\n"
                "clip), %.1f s de audio estéreo.\n\n", options.seconds);
    std::printf("Render sin tiempo real: lo que tarda, y en %% de la duración del audio; diseños hechos.\n");
    std::printf("%-7s %7s %6s  %-16s %-26s %-26s %8s %8s\n", "modo", "Hz", "bloque", "sin modular",
                "cada bloque (hasta 0.5.0)", "al ritmo (renderBlock)", "/sin mod.", "iguales");
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
                auto timed = [&](RenderDesign design)
                {
                    std::vector<double> times;
                    RenderMeasure last;
                    for (int i = 0; i < options.repeat; ++i)
                    {
                        last = renderModulated(mode, rate, block, options.seconds, design, false);
                        times.push_back(last.seconds);
                    }
                    last.seconds = median(times);
                    return last;
                };
                const RenderMeasure still = timed(RenderDesign::Static);
                const RenderMeasure each = timed(RenderDesign::EachBlock);
                const RenderMeasure paced = timed(RenderDesign::Paced);
                // Dos renders al ritmo, bit a bit iguales.
                const bool same =
                    renderModulated(mode, rate, block, options.seconds, RenderDesign::Paced, true).output ==
                    renderModulated(mode, rate, block, options.seconds, RenderDesign::Paced, true).output;
                auto percent = [&](double s) { return 100.0 * s / options.seconds; };
                std::printf("%-7s %7.0f %6d  %6.2f s (%5.1f %%) %7.2f s (%6.1f %%) %6llu  %7.2f s (%6.1f %%) %6llu "
                            "%8.1f %8s\n",
                            modeName.c_str(), rate, block, still.seconds, percent(still.seconds), each.seconds,
                            percent(each.seconds), static_cast<unsigned long long>(each.designs), paced.seconds,
                            percent(paced.seconds), static_cast<unsigned long long>(paced.designs),
                            paced.seconds / std::max(still.seconds, 1.0e-9), same ? "sí" : "NO");
                std::fflush(stdout);
            }
    }

    const double realtimeSeconds = std::min(options.seconds, 2.0);
    std::printf("\nTiempo real, al ritmo del reloj (%.1f s): %% de un núcleo del hilo de audio, su llamada más\n"
                "lenta, y el hilo de control: diseños por segundo, lo que tarda uno y su %% de un núcleo.\n",
                realtimeSeconds);
    std::printf("%-7s %7s %6s  %8s %14s  %10s %10s %8s\n", "modo", "Hz", "bloque", "% audio", "más lenta, ms",
                "diseños/s", "ms/diseño", "% ctrl");
    for (const std::string& modeName : options.modes)
    {
        PhaseMode mode{};
        if (!parseMode(modeName, mode))
            continue;
        for (const double rate : options.rates)
            for (const int block : options.blocks)
            {
                double load = 0.0, slowest = 0.0, perSecond = 0.0;
                realtimeModulated(mode, rate, block, realtimeSeconds, load, slowest, perSecond);
                const double ms = mode == PhaseMode::Iir ? 0.0 : designMs(mode, rate, options.repeat);
                std::printf("%-7s %7.0f %6d  %8.2f %14.3f  %10.1f %10.3f %8.1f\n", modeName.c_str(), rate, block,
                            load, slowest, perSecond, ms, perSecond * ms / 10.0);
                std::fflush(stdout);
            }
    }
}

void runMacros(const Options& options)
{
    using namespace esceq::core;
    std::printf("Macros: core::applyMacros en un bloque, con las ocho macros moviéndose; µs por llamada\n"
                "(mediana de %d medidas de 20000 llamadas).\n\n", options.repeat);
    std::printf("%-10s %10s\n", "destinos", "µs");
    for (const int count : {0, 1, 8, 64})
    {
        MacroAssignments macros;
        for (int i = 0; i < count; ++i)
        {
            // Por orden: 24 ganancias, 24 frecuencias y 16 umbrales.
            const std::size_t target = i < 24   ? bandParameter(i, BandParam::Gain)
                                       : i < 48 ? bandParameter(i - 24, BandParam::Frequency)
                                                : dynamicParameter(i - 48, DynParam::Threshold);
            macros.setSlot(i / kMaxMacroTargets, i % kMaxMacroTargets, target, static_cast<float>(10 + i % 40));
        }
        ParameterValues base = defaultValues();
        constexpr int kCalls = 20000;
        std::vector<double> times;
        float sink = 0.0f;
        for (int r = 0; r < options.repeat; ++r)
        {
            const auto begin = Clock::now();
            for (int call = 0; call < kCalls; ++call)
            {
                ParameterValues values = base;
                for (int macro = 0; macro < kNumMacros; ++macro)
                    values[macroParameter(macro)] = static_cast<float>((call + 13 * macro) % 100);
                applyMacros(macros, values);
                sink += values[bandParameter(0, BandParam::Gain)];
            }
            times.push_back(std::chrono::duration<double, std::micro>(Clock::now() - begin).count() / kCalls);
        }
        // Lo mismo sin applyMacros: la copia de los valores, que no es suya.
        std::vector<double> copies;
        for (int r = 0; r < options.repeat; ++r)
        {
            const auto begin = Clock::now();
            for (int call = 0; call < kCalls; ++call)
            {
                ParameterValues values = base;
                for (int macro = 0; macro < kNumMacros; ++macro)
                    values[macroParameter(macro)] = static_cast<float>((call + 13 * macro) % 100);
                sink += values[bandParameter(0, BandParam::Gain)];
            }
            copies.push_back(std::chrono::duration<double, std::micro>(Clock::now() - begin).count() / kCalls);
        }
        std::printf("%-10d %10.3f\n", count, std::max(0.0, median(times) - median(copies)));
        if (sink == 12345.0f)
            std::printf(" \n"); // que el compilador no quite el bucle
        std::fflush(stdout);
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
    if (options.modulation)
        runModulation(options);
    else if (options.macros)
        runMacros(options);
    else if (options.match)
        runMatch(options);
    else if (options.spectral)
        runSpectral(options);
    else if (options.saturation)
        runSaturation(options);
    else if (options.convolution)
        runConvolution(options);
    else
        runEqualizer(options);
    return 0;
}
