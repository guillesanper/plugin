// SPDX-License-Identifier: MIT
#include "esceq/dsp/curve_match.h"

#include "esceq/dsp/auto_gain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace esceq::dsp {

namespace {

/// Nivel que se da a lo que no es finito: muy por debajo de cualquier umbral de presencia.
constexpr float kAbsentDb = -200.0f;
/// Paso de las diferencias centrales del jacobiano, en las variables del ajuste
/// (octavas, dB y log Q).
constexpr double kJacobianStep = 1.0e-4;
/// Parada de Levenberg-Marquardt (Madsen, Nielsen y Tingleff, 2004, alg. 3.16):
/// gradiente casi nulo o paso casi nulo, y el μ inicial, relativo a la diagonal.
constexpr double kGradientTolerance = 1.0e-9;
constexpr double kStepTolerance = 1.0e-9;
constexpr double kInitialDamping = 1.0e-3;
/// Penalización de la ganancia de las bandas, en dB² de error cuadrático
/// medio por dB² de ganancia: sin ella, dos campanas anchas de ±18 dB casi
/// iguales se restan para dibujar un detalle (docs/dsp/igualacion.md).
constexpr double kGainPenalty = 1.0e-4;
/// El nivel no se iguala, pero lo que falta se centra en la mediana de la
/// diferencia, que con campanas anchas puede quedar unas décimas lejos del
/// nivel que mejor encaja: el ajuste tiene un nivel libre c con esta
/// penalización (por unidad de peso). Sin ella, c se va a donde un realce
/// de graves y agudos sale como un corte ancho de medios (docs/dsp/igualacion.md).
constexpr double kOffsetPenalty = 0.1;
/// Límite del nivel libre: muy por encima de lo que puede pedir un objetivo de ±12 dB.
constexpr double kMaxOffsetDb = 24.0;
/// Límites de la curva antes de pasarla a potencia para suavizarla: lo que no
/// es finito (el centro de un notch) o se sale queda en ellos.
constexpr double kMinCurveDb = -200.0;
constexpr double kMaxCurveDb = 200.0;

/// dB a potencia, con la curva dentro de [kMinCurveDb, kMaxCurveDb].
double curvePower(double db) noexcept
{
    const double clamped = std::isnan(db) ? kMinCurveDb : std::clamp(db, kMinCurveDb, kMaxCurveDb);
    return std::pow(10.0, 0.1 * clamped);
}

/// Interpolación suave (smoothstep) de 0 a 1.
double smooth01(double t) noexcept
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

bool isShelf(FilterType type) noexcept
{
    return type == FilterType::LowShelf || type == FilterType::HighShelf;
}

/// Una banda en las variables del ajuste.
struct Band
{
    FilterType type = FilterType::Bell;
    double octave = 0.0; ///< log2 de la frecuencia en Hz.
    double gainDb = 0.0;
    double logQ = 0.0; ///< ln Q.
};

/// Lo que no cambia durante el ajuste.
struct Problem
{
    std::vector<double> tangents; ///< responseTangent() de cada punto de la rejilla.
    std::vector<double> weights;
    std::vector<double> sqrtWeights;
    /// N: lo que falta, T − S(C), ya con el peso del rango y la presencia.
    std::vector<double> needed;
    /// Ventana del suavizado de cada punto, [windowBegin, windowEnd): la de
    /// smoothOctaves() con el ancho elegido, en la rejilla del ajuste.
    std::vector<std::size_t> windowBegin;
    std::vector<std::size_t> windowEnd;
    std::vector<double> currentDb;          ///< C, la curva estática de las bandas actuales.
    std::vector<double> currentWindowPower; ///< Σ de la potencia de C en la ventana de cada punto.
    double weightSum = 0.0;
    double sampleRate = 48000.0;
    FilterAlgorithm algorithm = FilterAlgorithm::Classic;
    double lowOctave = 0.0; ///< Límites de la frecuencia de las bandas, en log2(Hz).
    double highOctave = 0.0;
    double smoothingOctaves = 0.0; ///< Ancho del suavizado, en octavas.
    double minWidthHz = 0.0;       ///< Su ancho mínimo en graves, en Hz.
    std::size_t first = 0; ///< Primer y último punto de la rejilla dentro del rango.
    std::size_t last = 0;
    const std::atomic<bool>* cancel = nullptr;

    mutable std::int64_t evaluations = 0;
    mutable int iterations = 0;

    bool cancelled() const noexcept { return cancel != nullptr && cancel->load(std::memory_order_relaxed); }

    std::size_t size() const noexcept { return tangents.size(); }

    /// Límites de la variable `j` (0: octava, 1: ganancia, 2: log Q) de `band`.
    double lower(const Band& band, int j) const noexcept
    {
        if (j == 0)
            return lowOctave;
        if (j == 1)
            return -kMatchMaxGainDb;
        return std::log(isShelf(band.type) ? kMatchMinShelfQ : kMatchMinBellQ);
    }

    /// El Q de una campana no pasa del de una que abarca la ventana del
    /// suavizado en su frecuencia (matchMaxBellQ()): el objetivo no ve nada
    /// más estrecho, y sin ese límite el ajuste suavizado puede dibujar lo
    /// mismo con campanas estrechas y altas (docs/dsp/igualacion.md).
    double upper(const Band& band, int j) const noexcept
    {
        if (j == 0)
            return highOctave;
        if (j == 1)
            return kMatchMaxGainDb;
        if (isShelf(band.type))
            return std::log(kMatchMaxShelfQ);
        return std::log(matchMaxBellQ(std::exp2(band.octave), smoothingOctaves, minWidthHz));
    }

    FilterParams params(const Band& band) const noexcept
    {
        FilterParams params;
        params.type = band.type;
        params.frequencyHz = std::exp2(band.octave);
        params.gainDb = band.gainDb;
        params.q = std::exp(band.logQ);
        params.algorithm = algorithm;
        return params;
    }

    /// Curva de `band` en dB en toda la rejilla: una evaluación.
    void curve(const Band& band, std::span<double> out) const noexcept
    {
        const BandDesign design = designBand(params(band), sampleRate);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = magnitudeDbAtTangent(design, tangents[i]);
        ++evaluations;
    }
};

double& variable(Band& band, int j) noexcept
{
    return j == 0 ? band.octave : j == 1 ? band.gainDb : band.logQ;
}

/// Las bandas propuestas, el nivel libre y sus curvas.
struct Fit
{
    std::vector<Band> bands;
    double offsetDb = 0.0;
    std::vector<std::vector<double>> curves; ///< Una por banda, en la rejilla, sin suavizar.
    /// Lo que añaden las bandas propuestas a la curva actual, visto al ancho
    /// del suavizado: S(C + Σ Hk) − S(C), en dB. Lo calcula update().
    std::vector<double> effect;
    std::vector<double> power;       ///< Potencia de C + Σ Hk en cada punto.
    std::vector<double> windowPower; ///< Σ de `power` en la ventana de cada punto.

    /// Recalcula effect, power y windowPower tras cambiar las curvas.
    void update(const Problem& problem)
    {
        const std::size_t n = problem.size();
        power.resize(n);
        windowPower.resize(n);
        effect.resize(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            double sum = problem.currentDb[i];
            for (const auto& curve : curves)
                sum += curve[i];
            power[i] = curvePower(sum);
        }
        for (std::size_t i = 0; i < n; ++i)
        {
            double sum = 0.0;
            for (std::size_t j = problem.windowBegin[i]; j < problem.windowEnd[i]; ++j)
                sum += power[j];
            windowPower[i] = sum;
            effect[i] = 10.0 * std::log10(sum / problem.currentWindowPower[i]);
        }
    }

    /// S(C + Σ Hk) − S(C) en el punto `i`.
    double model(std::size_t i) const noexcept { return effect.empty() ? 0.0 : effect[i]; }

    /// Residuo sin ponderar: N + c − (S(C + Σ Hk) − S(C)).
    double residual(const Problem& problem, std::size_t i) const noexcept
    {
        return problem.needed[i] + offsetDb - model(i);
    }

    /// Σ w·r².
    double squaredError(const Problem& problem) const noexcept
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < problem.size(); ++i)
        {
            const double r = residual(problem, i);
            sum += problem.weights[i] * r * r;
        }
        return sum;
    }

    /// Σ g² de las bandas.
    double squaredGains() const noexcept
    {
        double sum = 0.0;
        for (const Band& band : bands)
            sum += band.gainDb * band.gainDb;
        return sum;
    }

    /// Lo que minimiza el refino: ½·(Σ w·r² + Σw·(kGainPenalty·Σ g² + kOffsetPenalty·c²)).
    double cost(const Problem& problem) const noexcept
    {
        return 0.5 * (squaredError(problem) +
                      problem.weightSum * (kGainPenalty * squaredGains() + kOffsetPenalty * offsetDb * offsetDb));
    }

    /// El nivel libre óptimo para las bandas actuales.
    void centreOffset(const Problem& problem) noexcept
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < problem.size(); ++i)
            sum += problem.weights[i] * (model(i) - problem.needed[i]);
        const double c = problem.weightSum > 0.0 ? sum / (problem.weightSum * (1.0 + kOffsetPenalty)) : 0.0;
        offsetDb = std::clamp(c, -kMaxOffsetDb, kMaxOffsetDb);
    }

    /// Error RMS ponderado, sin la penalización.
    double rmsDb(const Problem& problem) const noexcept
    {
        return problem.weightSum > 0.0 ? std::sqrt(squaredError(problem) / problem.weightSum) : 0.0;
    }

    /// Como rmsDb(), con la penalización: lo que compara el inicio voraz.
    double penalizedRmsDb(const Problem& problem) const noexcept
    {
        return problem.weightSum > 0.0 ? std::sqrt(2.0 * cost(problem) / problem.weightSum) : 0.0;
    }

    void add(const Problem& problem, const Band& band)
    {
        bands.push_back(band);
        curves.emplace_back(problem.size());
        problem.curve(band, curves.back());
        update(problem);
    }
};

/// Resuelve (A + μI)·x = b por Cholesky; A es simétrica, de m×m, por filas.
/// false si la matriz no es definida positiva.
bool solveDamped(const std::vector<double>& a, double mu, const std::vector<double>& b, std::vector<double>& x,
                 std::vector<double>& factor) noexcept
{
    const std::size_t m = b.size();
    factor.assign(m * m, 0.0);
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j <= i; ++j)
        {
            double sum = a[i * m + j] + (i == j ? mu : 0.0);
            for (std::size_t k = 0; k < j; ++k)
                sum -= factor[i * m + k] * factor[j * m + k];
            if (i == j)
            {
                if (!(sum > 0.0))
                    return false;
                factor[i * m + i] = std::sqrt(sum);
            }
            else
                factor[i * m + j] = sum / factor[j * m + j];
        }
    x = b;
    for (std::size_t i = 0; i < m; ++i)
    {
        for (std::size_t k = 0; k < i; ++k)
            x[i] -= factor[i * m + k] * x[k];
        x[i] /= factor[i * m + i];
    }
    for (std::size_t i = m; i-- > 0;)
    {
        for (std::size_t k = i + 1; k < m; ++k)
            x[i] -= factor[k * m + i] * x[k];
        x[i] /= factor[i * m + i];
    }
    return true;
}

/// Levenberg-Marquardt conjunto de todas las bandas y el nivel libre, con
/// límites proyectados, como mucho `maxIterations` iteraciones (aceptadas o
/// no). Las variables de la banda k son 3k (octava), 3k+1 (dB) y 3k+2
/// (log Q); la última, el nivel. false si se ha cancelado.
bool refine(const Problem& problem, Fit& fit, int maxIterations)
{
    const std::size_t n = problem.size();
    const std::size_t numBands = fit.bands.size();
    const std::size_t m = 3 * numBands + 1;
    if (numBands == 0)
    {
        fit.centreOffset(problem);
        return !problem.cancelled();
    }

    std::vector<double> jacobian(n * m); // por filas: jacobian[i·m + j]
    std::vector<double> plus(n), minus(n);
    std::vector<double> residual(n);
    std::vector<double> normal(m * m), gradient(m), negGradient(m), step(m), factor;

    // Residuo ponderado e = √w·(N + c − S(C + Σ Hk) + S(C)); su jacobiano es
    // −√w·∂S/∂θ para las variables de una banda y √w para el nivel. Solo
    // cambia la curva de esa banda, y la derivada del suavizado en potencia
    // es la media de ∂Hk/∂θ en la ventana ponderada por la potencia:
    // ∂S_i/∂θ = Σ_j∈ventana(i) p_j·∂Hk_j/∂θ / Σ_j∈ventana(i) p_j.
    std::vector<double> derivative(n);
    auto linearize = [&]() {
        for (std::size_t i = 0; i < n; ++i)
            residual[i] = problem.sqrtWeights[i] * fit.residual(problem, i);
        for (std::size_t k = 0; k < numBands; ++k)
            for (int j = 0; j < 3; ++j)
            {
                Band shifted = fit.bands[k];
                const double centre = variable(shifted, j);
                variable(shifted, j) = centre + kJacobianStep;
                problem.curve(shifted, plus);
                variable(shifted, j) = centre - kJacobianStep;
                problem.curve(shifted, minus);
                const std::size_t column = 3 * k + static_cast<std::size_t>(j);
                for (std::size_t i = 0; i < n; ++i)
                    derivative[i] = fit.power[i] * (plus[i] - minus[i]) / (2.0 * kJacobianStep);
                for (std::size_t i = 0; i < n; ++i)
                {
                    double sum = 0.0;
                    for (std::size_t l = problem.windowBegin[i]; l < problem.windowEnd[i]; ++l)
                        sum += derivative[l];
                    jacobian[i * m + column] = -problem.sqrtWeights[i] * sum / fit.windowPower[i];
                }
            }
        for (std::size_t i = 0; i < n; ++i)
            jacobian[i * m + m - 1] = problem.sqrtWeights[i];

        std::fill(normal.begin(), normal.end(), 0.0);
        std::fill(gradient.begin(), gradient.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double* row = &jacobian[i * m];
            if (problem.weights[i] == 0.0)
                continue;
            for (std::size_t a = 0; a < m; ++a)
            {
                gradient[a] += row[a] * residual[i];
                for (std::size_t b = 0; b <= a; ++b)
                    normal[a * m + b] += row[a] * row[b];
            }
        }
        // Las penalizaciones: un residuo √(λ·Σw)·g por banda y √(λc·Σw)·c,
        // de derivadas √(λ·Σw) y √(λc·Σw).
        const double penalty = kGainPenalty * problem.weightSum;
        for (std::size_t k = 0; k < numBands; ++k)
        {
            const std::size_t column = 3 * k + 1;
            gradient[column] += penalty * fit.bands[k].gainDb;
            normal[column * m + column] += penalty;
        }
        const double offsetPenalty = kOffsetPenalty * problem.weightSum;
        gradient[m - 1] += offsetPenalty * fit.offsetDb;
        normal[(m - 1) * m + m - 1] += offsetPenalty;
        for (std::size_t a = 0; a < m; ++a)
            for (std::size_t b = 0; b < a; ++b)
                normal[b * m + a] = normal[a * m + b];
    };

    double cost = fit.cost(problem);
    linearize();
    double maxDiagonal = 0.0;
    for (std::size_t a = 0; a < m; ++a)
        maxDiagonal = std::max(maxDiagonal, normal[a * m + a]);
    double mu = kInitialDamping * std::max(maxDiagonal, 1.0e-12);
    double nu = 2.0;

    Fit trial = fit;
    for (int iteration = 0; iteration < maxIterations; ++iteration)
    {
        if (problem.cancelled())
            return false;
        ++problem.iterations;

        double gradientNorm = 0.0;
        for (const double g : gradient)
            gradientNorm = std::max(gradientNorm, std::abs(g));
        if (gradientNorm <= kGradientTolerance)
            break;

        for (std::size_t a = 0; a < m; ++a)
            negGradient[a] = -gradient[a];
        if (!solveDamped(normal, mu, negGradient, step, factor))
        {
            mu *= nu;
            nu *= 2.0;
            continue;
        }

        // Paso proyectado sobre los límites, y lo que queda de él.
        trial.bands = fit.bands;
        double stepNorm = 0.0, valueNorm = 0.0;
        for (std::size_t j = 0; j < m; ++j)
        {
            double current = 0.0, next = 0.0;
            if (j + 1 == m)
            {
                current = fit.offsetDb;
                next = std::clamp(current + step[j], -kMaxOffsetDb, kMaxOffsetDb);
                trial.offsetDb = next;
            }
            else
            {
                Band& band = trial.bands[j / 3];
                const int which = static_cast<int>(j % 3);
                current = variable(band, which);
                next = std::clamp(current + step[j], problem.lower(band, which), problem.upper(band, which));
                variable(band, which) = next;
            }
            step[j] = next - current;
            stepNorm += step[j] * step[j];
            valueNorm += current * current;
        }
        stepNorm = std::sqrt(stepNorm);
        if (stepNorm <= kStepTolerance * (std::sqrt(valueNorm) + kStepTolerance))
            break;

        trial.curves.resize(numBands);
        for (std::size_t k = 0; k < numBands; ++k)
        {
            trial.curves[k].resize(n);
            problem.curve(trial.bands[k], trial.curves[k]);
        }
        trial.update(problem);
        const double trialCost = trial.cost(problem);

        // Reducción que predice el modelo lineal para el paso proyectado:
        // L(0) − L(δ) = −gᵀδ − ½·δᵀAδ.
        double predicted = 0.0;
        for (std::size_t a = 0; a < m; ++a)
        {
            double product = 0.0;
            for (std::size_t b = 0; b < m; ++b)
                product += normal[a * m + b] * step[b];
            predicted -= gradient[a] * step[a] + 0.5 * step[a] * product;
        }

        if (trialCost < cost && predicted > 0.0)
        {
            const double rho = (cost - trialCost) / predicted;
            std::swap(fit.bands, trial.bands);
            std::swap(fit.curves, trial.curves);
            std::swap(fit.effect, trial.effect);
            std::swap(fit.power, trial.power);
            std::swap(fit.windowPower, trial.windowPower);
            fit.offsetDb = trial.offsetDb;
            cost = trialCost;
            linearize();
            const double t = 2.0 * rho - 1.0;
            mu *= std::max(1.0 / 3.0, 1.0 - t * t * t);
            nu = 2.0;
        }
        else
        {
            mu *= nu;
            nu *= 2.0;
        }
    }
    return true;
}

/// Banda inicial en el punto de mayor error ponderado, dentro del rango
/// (PLAN.md 5.6, "Inicio voraz"), o nullopt si ese error no llega a la parada.
std::optional<Band> initialBand(const Problem& problem, const Fit& fit, std::span<const double> octaves)
{
    const std::size_t n = problem.size();
    std::vector<double> r(n);
    for (std::size_t i = 0; i < n; ++i)
        r[i] = fit.residual(problem, i);

    std::size_t peak = problem.first;
    double peakError = -1.0;
    for (std::size_t i = problem.first; i <= problem.last; ++i)
    {
        const double error = std::abs(r[i]) * problem.weights[i];
        if (error > peakError)
        {
            peakError = error;
            peak = i;
        }
    }
    if (!(peakError >= kMatchStopErrorDb))
        return std::nullopt;

    // El lóbulo: el tramo alrededor con el mismo signo y más de la mitad del
    // error máximo, sin cruzar puntos que no cuentan.
    const double sign = r[peak] > 0.0 ? 1.0 : -1.0;
    const double half = 0.5 * std::abs(r[peak]);
    auto inside = [&](std::size_t i) { return problem.weights[i] > 0.0 && sign * r[i] > half; };
    std::size_t left = peak, right = peak;
    while (left > 0 && inside(left - 1))
        --left;
    while (right + 1 < n && inside(right + 1))
        ++right;

    // Bordes del lóbulo, interpolados donde el error cruza la mitad.
    auto crossing = [&](std::size_t in, std::size_t out) {
        if (problem.weights[out] == 0.0)
            return octaves[in];
        const double a = sign * r[in] - half, b = sign * r[out] - half;
        const double t = a / (a - b);
        return octaves[in] + t * (octaves[out] - octaves[in]);
    };
    const double leftOctave = left > 0 ? crossing(left, left - 1) : octaves[left];
    const double rightOctave = right + 1 < n ? crossing(right, right + 1) : octaves[right];

    const bool touchesLow = left <= problem.first;
    const bool touchesHigh = right >= problem.last;
    Band band;
    if (touchesLow && (!touchesHigh || octaves[peak] - octaves[problem.first] <= octaves[problem.last] - octaves[peak]))
    {
        band.type = FilterType::LowShelf;
        band.octave = rightOctave;
        band.gainDb = r[problem.first];
        band.logQ = std::log(kButterworthQ);
    }
    else if (touchesHigh)
    {
        band.type = FilterType::HighShelf;
        band.octave = leftOctave;
        band.gainDb = r[problem.last];
        band.logQ = std::log(kButterworthQ);
    }
    else
    {
        band.type = FilterType::Bell;
        band.octave = octaves[peak];
        band.gainDb = r[peak];
        band.logQ = std::log(std::clamp(qForBandwidth(rightOctave - leftOctave), kMatchMinBellQ, kMatchMaxBellQ));
    }
    for (int j = 0; j < 3; ++j)
        variable(band, j) = std::clamp(variable(band, j), problem.lower(band, j), problem.upper(band, j));
    return band;
}

/// Interpola `levels` (en la rejilla `from`) en las frecuencias `to`,
/// linealmente en log2(f); fuera, el extremo.
void interpolate(std::span<const double> fromOctaves, std::span<const float> levels, std::span<const double> toOctaves,
                 std::span<double> out) noexcept
{
    std::size_t j = 0;
    for (std::size_t i = 0; i < toOctaves.size(); ++i)
    {
        const double x = toOctaves[i];
        while (j + 2 < fromOctaves.size() && fromOctaves[j + 1] < x)
            ++j;
        const double t = std::clamp((x - fromOctaves[j]) / (fromOctaves[j + 1] - fromOctaves[j]), 0.0, 1.0);
        out[i] = static_cast<double>(levels[j]) + t * static_cast<double>(levels[j + 1] - levels[j]);
    }
}

/// Mediana de `reference − track` con los pesos `weights` (alguno positivo):
/// el primer valor, de menor a mayor, en el que el peso acumulado llega a la
/// mitad. Determinista: a igual valor, por orden de índice.
double weightedMedian(std::span<const double> reference, std::span<const double> track, std::span<const double> weights)
{
    std::vector<std::pair<double, std::size_t>> values;
    double total = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i)
        if (weights[i] > 0.0)
        {
            values.emplace_back(reference[i] - track[i], i);
            total += weights[i];
        }
    std::sort(values.begin(), values.end());
    double accumulated = 0.0;
    for (const auto& [value, index] : values)
    {
        accumulated += weights[index];
        if (accumulated >= 0.5 * total)
            return value;
    }
    return values.empty() ? 0.0 : values.back().first;
}

bool validInput(const CurveMatchInput& input) noexcept
{
    const auto& f = input.frequencies;
    if (f.size() < 2 || input.trackDb.size() != f.size() || input.referenceDb.size() != f.size() ||
        !(input.sampleRate > 0.0) || !std::isfinite(input.sampleRate))
        return false;
    for (std::size_t i = 0; i < f.size(); ++i)
        if (!(f[i] > 0.0) || !std::isfinite(f[i]) || (i > 0 && !(f[i] > f[i - 1])))
            return false;
    return true;
}

} // namespace

double matchMaxBellQ(double frequencyHz, double smoothingOctaves, double minWidthHz) noexcept
{
    if (!(frequencyHz > 0.0) || !std::isfinite(frequencyHz))
        return kMatchMaxBellQ;
    const double width = smoothWindowOctaves(frequencyHz, std::max(smoothingOctaves, 0.0), minWidthHz);
    return std::clamp(qForBandwidth(width), kMatchMinBellQ, kMatchMaxBellQ);
}

CurveMatchResult matchCurve(const CurveMatchInput& input, const CurveMatchSettings& rawSettings,
                            const std::atomic<bool>* cancel)
{
    CurveMatchResult result;
    if (!validInput(input))
        return result;

    // Ajustes saneados.
    CurveMatchSettings settings = rawSettings;
    settings.amount = std::isfinite(settings.amount) ? std::clamp(settings.amount, 0.0, 1.0) : 0.0;
    settings.smoothingOctaves =
        std::isfinite(settings.smoothingOctaves) ? std::clamp(settings.smoothingOctaves, 0.0, 4.0) : 0.0;
    settings.maxBands = std::clamp(settings.maxBands, 0, kMaxBands);
    const double gridLow = input.frequencies.front();
    const double gridHigh = input.frequencies.back();
    double lowHz = std::isfinite(settings.lowHz) ? settings.lowHz : gridLow;
    double highHz = std::isfinite(settings.highHz) ? settings.highHz : gridHigh;
    if (lowHz > highHz)
        std::swap(lowHz, highHz);
    lowHz = std::clamp(lowHz, gridLow, gridHigh);
    highHz = std::clamp(highHz, gridLow, gridHigh);

    // 1. Los dos espectros, suavizados en potencia en su rejilla.
    const std::size_t inputSize = input.frequencies.size();
    std::vector<float> track(inputSize), reference(inputSize);
    for (std::size_t i = 0; i < inputSize; ++i)
    {
        track[i] = std::isfinite(input.trackDb[i]) ? std::max(input.trackDb[i], kAbsentDb) : kAbsentDb;
        reference[i] = std::isfinite(input.referenceDb[i]) ? std::max(input.referenceDb[i], kAbsentDb) : kAbsentDb;
    }
    std::vector<float> smoothTrack(inputSize), smoothReference(inputSize);
    // En graves, la ventana abarca al menos kMatchMinWindowBins bins de la FFT.
    const double minWidthHz =
        std::isfinite(input.resolutionHz) && input.resolutionHz > 0.0 ? kMatchMinWindowBins * input.resolutionHz : 0.0;
    smoothOctaves(input.frequencies, track, settings.smoothingOctaves, smoothTrack, minWidthHz);
    smoothOctaves(input.frequencies, reference, settings.smoothingOctaves, smoothReference, minWidthHz);

    // 2. Presencia de las dos (la de las colisiones, sobre lo suavizado), y
    // fuera también lo que está a menos de un ancho del suavizado de algo
    // ausente: su valor suavizado mezcla lo que hay con lo que no.
    const float trackPeak = *std::max_element(smoothTrack.begin(), smoothTrack.end());
    const float referencePeak = *std::max_element(smoothReference.begin(), smoothReference.end());
    std::vector<double> inputOctaves(inputSize);
    std::vector<char> present(inputSize);
    for (std::size_t i = 0; i < inputSize; ++i)
    {
        inputOctaves[i] = std::log2(input.frequencies[i]);
        // Un nivel no finito no es un dato: no cuenta, ni lo que lo suaviza.
        present[i] = std::isfinite(input.trackDb[i]) && std::isfinite(input.referenceDb[i]) &&
                     presence(smoothTrack[i], trackPeak, settings.presence) >= 0.5f &&
                     presence(smoothReference[i], referencePeak, settings.presence) >= 0.5f;
    }
    std::vector<char> reliable(inputSize);
    for (std::size_t i = 0; i < inputSize; ++i)
    {
        const double width = smoothWindowOctaves(input.frequencies[i], settings.smoothingOctaves, minWidthHz);
        bool ok = present[i] != 0;
        for (std::size_t j = i; ok && j-- > 0 && inputOctaves[i] - inputOctaves[j] <= width;)
            ok = present[j] != 0;
        for (std::size_t j = i + 1; ok && j < inputSize && inputOctaves[j] - inputOctaves[i] <= width; ++j)
            ok = present[j] != 0;
        reliable[i] = ok;
    }

    // 3. La rejilla del ajuste y lo que se iguala en ella.
    const std::size_t n = kMatchGridPoints;
    const double lowOctave = inputOctaves.front();
    const double highOctave = inputOctaves.back();
    std::vector<double> octaves(n);
    result.frequencies.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        octaves[i] = i + 1 == n ? highOctave
                                : lowOctave + (highOctave - lowOctave) * static_cast<double>(i) /
                                                  static_cast<double>(n - 1);
        result.frequencies[i] = std::exp2(octaves[i]);
    }
    result.frequencies.front() = input.frequencies.front();
    result.frequencies.back() = input.frequencies.back();
    std::vector<double> trackGrid(n), referenceGrid(n);
    interpolate(inputOctaves, smoothTrack, octaves, trackGrid);
    interpolate(inputOctaves, smoothReference, octaves, referenceGrid);

    // Peso: dentro del rango, 1 si las dos están presentes y 0 si no; fuera,
    // kMatchOutsideWeight (ahí las bandas nuevas no deben crear nada); entre
    // medias, una transición de kMatchWeightRampOctaves.
    const double rangeLow = std::log2(lowHz), rangeHigh = std::log2(highHz);
    std::vector<double> inRange(n), counts(n);
    {
        std::size_t j = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = octaves[i];
            const double outside = std::max(rangeLow - x, x - rangeHigh);
            inRange[i] = outside <= 0.0 ? 1.0 : 1.0 - smooth01(outside / kMatchWeightRampOctaves);
            while (j + 2 < inputSize && inputOctaves[j + 1] < x)
                ++j;
            // Un punto entre dos de la entrada cuenta si los dos cuentan.
            bool counted = reliable[j] != 0 && reliable[j + 1] != 0;
            if (x <= inputOctaves[j])
                counted = reliable[j] != 0;
            else if (x >= inputOctaves[j + 1])
                counted = reliable[j + 1] != 0;
            counts[i] = counted ? inRange[i] : 0.0;
        }
    }

    double countSum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        countSum += counts[i];

    result.weights.resize(n);
    result.targetDb.assign(n, 0.0);
    result.currentDb.resize(n);
    staticCurveDb(input.currentBands, input.sampleRate, input.numChannels, result.frequencies, result.currentDb);
    result.proposedDb = result.currentDb;
    for (std::size_t i = 0; i < n; ++i)
        result.weights[i] = counts[i] + kMatchOutsideWeight * (1.0 - inRange[i]);
    if (!(countSum > 0.0))
        return result; // sin contenido común: nada que igualar

    // 4. Objetivo T y lo que falta N.
    Problem problem;
    problem.sampleRate = input.sampleRate;
    problem.algorithm = input.algorithm;
    problem.cancel = cancel;
    problem.smoothingOctaves = settings.smoothingOctaves;
    problem.minWidthHz = minWidthHz;
    problem.lowOctave = rangeLow;
    problem.highOctave = std::min(rangeHigh, std::log2(kMaxFrequencyRatio * input.sampleRate));
    problem.lowOctave = std::min(problem.lowOctave, problem.highOctave);
    problem.first = n;
    for (std::size_t i = 0; i < n; ++i)
        if (inRange[i] >= 1.0)
        {
            problem.first = std::min(problem.first, i);
            problem.last = i;
        }
    if (problem.first == n)
        return result; // el rango no tiene ningún punto de la rejilla

    // La curva actual se compara con el objetivo al mismo ancho: S(C), su
    // promedio en potencia en la ventana del suavizado de cada punto. Sin
    // suavizarla, una banda estrecha que ya hay (un notch) se veía entera
    // en lo que falta y el ajuste la rellenaba (docs/dsp/igualacion.md).
    problem.windowBegin.resize(n);
    problem.windowEnd.resize(n);
    problem.currentDb = result.currentDb;
    problem.currentWindowPower.resize(n);
    std::vector<double> currentPower(n), smoothCurrent(n);
    for (std::size_t i = 0; i < n; ++i)
        currentPower[i] = curvePower(result.currentDb[i]);
    {
        std::size_t begin = 0, end = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double half = 0.5 * smoothWindowOctaves(result.frequencies[i], settings.smoothingOctaves, minWidthHz);
            while (end < n && octaves[end] <= octaves[i] + half)
                ++end;
            while (begin < i && octaves[begin] < octaves[i] - half)
                ++begin;
            end = std::max(end, i + 1);
            problem.windowBegin[i] = begin;
            problem.windowEnd[i] = end;
            double sum = 0.0;
            for (std::size_t j = begin; j < end; ++j)
                sum += currentPower[j];
            problem.currentWindowPower[i] = sum;
            smoothCurrent[i] = 10.0 * std::log10(sum / static_cast<double>(end - begin));
        }
    }

    // El nivel no se iguala: la diferencia se centra en su mediana ponderada,
    // así que se iguala lo que difiere de lo más común (docs/dsp/igualacion.md).
    const double centre = weightedMedian(referenceGrid, trackGrid, counts);
    std::vector<double> target(n), needed(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = std::clamp(settings.amount * (referenceGrid[i] - trackGrid[i] - centre), -kMatchMaxTargetDb,
                                    kMatchMaxTargetDb);
        target[i] = counts[i] * t;
        needed[i] = counts[i] * (t - smoothCurrent[i]);
    }
    // Fuera del rango, lo de su borde (el punto que cuenta más cercano): las
    // bandas nuevas no crean nada ahí, pero un shelf puede seguir.
    double lowTarget = 0.0, lowNeeded = 0.0, highTarget = 0.0, highNeeded = 0.0;
    for (std::size_t i = problem.first; i <= problem.last; ++i)
        if (counts[i] >= 1.0)
        {
            lowTarget = target[i];
            lowNeeded = needed[i];
            break;
        }
    for (std::size_t i = problem.last + 1; i-- > problem.first;)
        if (counts[i] >= 1.0)
        {
            highTarget = target[i];
            highNeeded = needed[i];
            break;
        }

    problem.tangents.resize(n);
    problem.weights = result.weights;
    problem.sqrtWeights.resize(n);
    problem.needed.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double outside = 1.0 - inRange[i];
        const bool below = i < problem.first;
        result.targetDb[i] = target[i] + outside * (below ? lowTarget : highTarget);
        problem.needed[i] = needed[i] + outside * (below ? lowNeeded : highNeeded);
        problem.tangents[i] = responseTangent(result.frequencies[i], input.sampleRate);
        problem.sqrtWeights[i] = std::sqrt(problem.weights[i]);
        problem.weightSum += problem.weights[i];
    }

    // 5. Inicio voraz con refino tras cada banda, refino final y limpieza.
    Fit fit;
    fit.centreOffset(problem);
    double penalized = fit.penalizedRmsDb(problem);
    result.initialRmsDb = fit.rmsDb(problem);
    auto finish = [&](bool cancelled) {
        result.evaluations = problem.evaluations;
        result.iterations = problem.iterations;
        if (cancelled)
        {
            result.cancelled = true;
            result.finalRmsDb = result.initialRmsDb;
            return std::move(result);
        }
        std::vector<std::size_t> order(fit.bands.size());
        for (std::size_t k = 0; k < order.size(); ++k)
            order[k] = k;
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return fit.bands[a].octave < fit.bands[b].octave; });
        for (const std::size_t k : order)
        {
            result.bands.push_back(problem.params(fit.bands[k]));
            for (std::size_t i = 0; i < n; ++i)
                result.proposedDb[i] += fit.curves[k][i];
        }
        result.offsetDb = fit.offsetDb;
        result.finalRmsDb = fit.rmsDb(problem);
        return std::move(result);
    };

    while (static_cast<int>(fit.bands.size()) < settings.maxBands)
    {
        if (problem.cancelled())
            return finish(true);
        const std::optional<Band> band = initialBand(problem, fit, octaves);
        if (!band)
            break;
        Fit candidate = fit;
        candidate.add(problem, *band);
        candidate.centreOffset(problem);
        if (!refine(problem, candidate, kMatchStepIterations))
            return finish(true);
        const double candidatePenalized = candidate.penalizedRmsDb(problem);
        // La mejora, como el RMS del error que quita la banda: no depende del
        // error que queda en el resto (docs/dsp/igualacion.md).
        if (!(std::sqrt(std::max(penalized * penalized - candidatePenalized * candidatePenalized, 0.0)) >=
              kMatchMinImprovementDb))
            break;
        fit = std::move(candidate);
        penalized = candidatePenalized;
    }
    if (!fit.bands.empty() && !refine(problem, fit, kMatchFinalIterations))
        return finish(true);

    // Limpieza: fuera las bandas que no llegan a kMatchMinBandGainDb, y otro refino.
    bool removed = false;
    for (std::size_t k = fit.bands.size(); k-- > 0;)
        if (std::abs(fit.bands[k].gainDb) < kMatchMinBandGainDb)
        {
            fit.bands.erase(fit.bands.begin() + static_cast<std::ptrdiff_t>(k));
            fit.curves.erase(fit.curves.begin() + static_cast<std::ptrdiff_t>(k));
            removed = true;
        }
    if (removed)
        fit.update(problem);
    if (removed && !refine(problem, fit, kMatchFinalIterations))
        return finish(true);
    return finish(false);
}

} // namespace esceq::dsp
