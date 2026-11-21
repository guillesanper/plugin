// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/spectrum_peaks.h"

#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/ui/notes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace esceq::ui {

namespace {

/// Dos niveles que difieren menos que esto forman una meseta. Es mucho más
/// que el redondeo de quitar la inclinación (del orden de 1e-5 dB) y mucho
/// menos que lo que se distingue en pantalla.
constexpr double kFlatDb = 1.0e-3;

/// Una ganancia arrastrada menor que esto, en dB, se lee "0.0 dB".
constexpr float kNoGainDb = 0.05f;

/// Ancho a -3 dB respecto al pico.
constexpr double kHalfPowerDb = 3.0;

/// Posición, en log2(Hz), donde el tramo de `a` a `b` cruza `level` (con
/// `y[a]` y `y[b]` a lados distintos de `level`).
double crossing(std::span<const double> x, std::span<const double> y, std::size_t a, std::size_t b, double level)
{
    const double t = (level - y[a]) / (y[b] - y[a]);
    return x[a] + t * (x[b] - x[a]);
}

} // namespace

std::vector<SpectrumPeak> findPeaks(std::span<const float> levelsDb, std::span<const double> frequenciesHz,
                                    const PeakOptions& options)
{
    std::vector<SpectrumPeak> peaks;
    const std::size_t n = levelsDb.size();
    if (n < 3 || frequenciesHz.size() != n)
        return peaks;
    for (std::size_t i = 0; i < n; ++i)
        if (!(frequenciesHz[i] > 0.0) || !std::isfinite(frequenciesHz[i]) ||
            (i > 0 && !(frequenciesHz[i] > frequenciesHz[i - 1])))
            return peaks;

    // En octavas y sin la inclinación: el espectro real. Lo no finito queda como NaN.
    std::vector<double> x(n);
    std::vector<double> y(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        x[i] = std::log2(frequenciesHz[i]);
        const double level = levelsDb[i];
        y[i] = std::isfinite(level) ? level - dsp::SpectrumAnalyzer::tiltDb(frequenciesHz[i], options.tiltDbPerOctave)
                                    : std::numeric_limits<double>::quiet_NaN();
    }
    auto finite = [&y](std::size_t i) { return std::isfinite(y[i]); };

    std::size_t i = 1;
    while (i + 1 < n)
    {
        if (!finite(i))
        {
            ++i;
            continue;
        }
        // Meseta [a, b]: puntos seguidos al mismo nivel (con el analizador,
        // un seno en agudos ocupa varios puntos con el mismo máximo).
        const std::size_t a = i;
        std::size_t b = a;
        while (b + 1 < n && finite(b + 1) && std::abs(y[b + 1] - y[a]) <= kFlatDb)
            ++b;
        i = b + 1;
        if (b + 1 >= n || !finite(a - 1) || !finite(b + 1) || !(y[a - 1] < y[a]) || !(y[b + 1] < y[b]))
            continue;

        // El máximo local es el punto (o la meseta): con él se miden el nivel
        // mínimo y la prominencia, sin lo que añada la interpolación.
        const double top = y[a];
        if (top < options.minimumDb)
            continue;

        // Prominencia sobre el mínimo del entorno, centrado en el punto o en
        // el centro de la meseta.
        double centre = 0.5 * (x[a] + x[b]);
        double floor = top;
        for (std::size_t j = a; j-- > 0 && centre - x[j] <= options.neighbourhoodOctaves;)
            if (finite(j))
                floor = std::min(floor, y[j]);
        for (std::size_t j = b + 1; j < n && x[j] - centre <= options.neighbourhoodOctaves; ++j)
            if (finite(j))
                floor = std::min(floor, y[j]);
        if (top - floor < options.prominenceDb)
            continue;

        // Frecuencia y nivel refinados: el vértice de la parábola por los
        // tres puntos o, en una meseta, su centro.
        double level = top;
        if (a == b)
        {
            const double left = y[a - 1];
            const double right = y[a + 1];
            const double curvature = left - 2.0 * level + right;
            if (curvature < 0.0)
            {
                const double p = std::clamp(0.5 * (left - right) / curvature, -0.5, 0.5);
                centre = x[a] + p * (p >= 0.0 ? x[a + 1] - x[a] : x[a] - x[a - 1]);
                level -= 0.25 * (left - right) * p;
            }
        }

        // Ancho a -3 dB: desde la meseta hacia fuera hasta cruzar el nivel,
        // sin pasar de un valle (otro pico ya no es este).
        const double half = level - kHalfPowerDb;
        std::optional<double> lower;
        for (std::size_t j = a; j > 0 && finite(j - 1); --j)
        {
            if (y[j - 1] <= half)
            {
                lower = crossing(x, y, j - 1, j, half);
                break;
            }
            if (y[j - 1] > y[j] + kFlatDb)
                break;
        }
        std::optional<double> upper;
        for (std::size_t j = b; j + 1 < n && finite(j + 1); ++j)
        {
            if (y[j + 1] <= half)
            {
                upper = crossing(x, y, j + 1, j, half);
                break;
            }
            if (y[j + 1] > y[j] + kFlatDb)
                break;
        }
        // Con un solo lado, el pico se supone simétrico.
        double width = std::numeric_limits<double>::infinity();
        if (lower && upper)
            width = *upper - *lower;
        else if (lower)
            width = 2.0 * (centre - *lower);
        else if (upper)
            width = 2.0 * (*upper - centre);
        const double q = std::clamp(qForBandwidth(width), kPeakMinQ, kPeakMaxQ);

        peaks.push_back({std::exp2(centre), level, q});
    }
    return peaks;
}

std::optional<SpectrumPeak> nearestPeak(std::span<const SpectrumPeak> peaks, double frequencyHz,
                                        double maxOctaves) noexcept
{
    if (!(frequencyHz > 0.0))
        return std::nullopt;
    std::optional<SpectrumPeak> best;
    double bestDistance = maxOctaves;
    for (const auto& peak : peaks)
    {
        const double distance = std::abs(std::log2(peak.frequencyHz / frequencyHz));
        if (distance <= bestDistance)
        {
            best = peak;
            bestDistance = distance;
        }
    }
    return best;
}

std::string peakMarkerText(const SpectrumPeak& peak, double a4Hz)
{
    const auto& frequency = core::parameters()[core::bandParameter(0, core::BandParam::Frequency)];
    // Sumar 0 convierte -0.0 en 0.0: lo que redondea a 0 se escribe sin signo.
    const double level = std::round(peak.levelDb * 10.0) / 10.0 + 0.0;
    char text[24];
    std::snprintf(text, sizeof text, "%.1f dB", level);
    return core::formatValue(frequency, static_cast<float>(peak.frequencyHz)) + " \xC2\xB7 " +
           noteText(peak.frequencyHz, a4Hz) + " \xC2\xB7 " + text;
}

std::vector<ParameterChange> peakBandChanges(const core::ParameterValues& values, int band, const SpectrumPeak& peak,
                                             double gainDb)
{
    return newBandChanges(values, band, peak.frequencyHz, gainDb, peak.q);
}

std::vector<ParameterChange> dynamicPeakChanges(const core::ParameterValues& values, int band, const SpectrumPeak& peak)
{
    const auto& specs = core::parameters();
    std::vector<ParameterChange> changes;
    auto set = [&](std::size_t index, double value) {
        const float sane = core::sanitize(specs[index], static_cast<float>(value));
        if (values[index] != sane)
            changes.push_back({index, sane});
    };

    const auto gain = core::bandParameter(band, core::BandParam::Gain);
    const auto range = core::dynamicParameter(band, core::DynParam::Range);
    // La ganancia arrastrada es lo que la banda bajará (o subirá) como mucho.
    // Si es menos de lo que se lee (la etiqueta muestra décimas), el rango por defecto.
    set(range, std::abs(values[gain]) >= kNoGainDb ? values[gain] : specs[range].defaultValue);
    set(core::dynamicParameter(band, core::DynParam::Threshold), peak.levelDb - kPeakThresholdBelowDb);
    set(gain, 0.0);
    // La última: la dinámica empieza a actuar ya con sus valores.
    set(core::dynamicParameter(band, core::DynParam::Enabled), 1.0);
    return changes;
}

} // namespace esceq::ui
