// SPDX-License-Identifier: MIT
#include "esceq/dsp/filter_design.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace esceq::dsp {

namespace {

constexpr double kPi = std::numbers::pi;

SectionCoeffs secondOrder(double g, double k, double m0, double m1, double m2) noexcept
{
    SectionCoeffs c;
    c.order = 2;
    c.g = g;
    c.k = k;
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    c.m0 = m0;
    c.m1 = m1;
    c.m2 = m2;
    return c;
}

SectionCoeffs firstOrder(double g, double m0, double m2) noexcept
{
    SectionCoeffs c;
    c.order = 1;
    c.g = g;
    c.k = 0.0;
    c.a1 = g / (1.0 + g);
    c.m0 = m0;
    c.m2 = m2;
    return c;
}

/// Cómo cambia la frecuencia de una sección respecto a la de la banda.
enum class Scale
{
    None,     ///< La de la banda.
    DivSqrtA, ///< Dividida por √A (LowShelf).
    MulSqrtA, ///< Multiplicada por √A (HighShelf).
    MulA,     ///< Multiplicada por A (Tilt).
};

double scaled(double value, Scale scale, double a) noexcept
{
    switch (scale)
    {
    case Scale::None:
        return value;
    case Scale::DivSqrtA:
        return value / std::sqrt(a);
    case Scale::MulSqrtA:
        return value * std::sqrt(a);
    case Scale::MulA:
        return value * a;
    }
    return value;
}

/// Transformada bilineal con la frecuencia predeformada: el algoritmo de la 0.1.
struct ClassicDigitizer
{
    double g; ///< tan(ω0/2)

    SectionCoeffs second(Scale scale, double a, double k, double m0, double m1, double m2) const noexcept
    {
        return secondOrder(scaled(g, scale, a), k, m0, m1, m2);
    }

    SectionCoeffs first(Scale scale, double a, double m0, double m2) const noexcept
    {
        return firstOrder(scaled(g, scale, a), m0, m2);
    }
};

/// |H(jx)|² del prototipo de orden 2, H(s) = (n2·s² + n1·s + n0)/(s² + k·s + 1), en s = jx.
double analogMagnitudeSquared(double n0, double n1, double n2, double k, double x) noexcept
{
    const double x2 = x * x;
    const double re = n0 - n2 * x2;
    const double den = (1.0 - x2) * (1.0 - x2) + k * k * x2;
    return (re * re + n1 * n1 * x2) / den;
}

/// Frecuencia (rad/muestra) donde se iguala la magnitud además de en
/// continua y en Nyquist: la de la sección, sin acercarse más a Nyquist
/// (ver docs/dsp/filtros.md).
inline constexpr double kMaxMatchOmega = 0.9 * kPi;

/// Lo que se pide a una sección además de sus polos: la magnitud en Nyquist y
/// en ωm. Por defecto, la del propio prototipo; matchedLowShelfBoost() pide
/// otra para que el producto de sus dos secciones sea el que se ajusta.
struct MatchTargets
{
    double nyquistMagnitude = -1.0;      ///< |H| en Nyquist; < 0: la del prototipo.
    double matchMagnitudeSquared = -1.0; ///< |H|² en ωm; < 0: la del prototipo.
};

/// Sección de orden 2 con la magnitud del prototipo analógico (Vicanek):
/// polos por invariancia al impulso y un numerador que iguala la magnitud
/// analógica en continua, en Nyquist y en la frecuencia de la sección. Los
/// ceros que el prototipo tiene en el eje imaginario (notch, paso alto) van al
/// círculo unidad en la misma frecuencia. Derivación en docs/dsp/filtros.md.
SectionCoeffs matchedSecondOrder(double w, double k, double m0, double m1, double m2,
                                 const MatchTargets& targets = {}) noexcept
{
    // Numerador analógico: m0 + (m1·s + m2)/(s² + k·s + 1) = (n2·s² + n1·s + n0)/(s² + k·s + 1).
    const double n2 = m0;
    const double n1 = m0 * k + m1;
    const double n0 = m0 + m2;

    // Polos z1, z2 = exp(w·(−ζ ± √(ζ² − 1))), ζ = k/2. Lo que hace falta:
    //   c = (1 − z1)(1 − z2) = 1 + a1 + a2   (pequeño en graves: sin restas)
    //   a = (1 + z1)(1 + z2) = 1 − a1 + a2
    //   b = 2·(1 − z1·z2)    = 2·(1 − a2)
    // y |A(e^jωm)|², el denominador digital al cuadrado en ωm.
    const double zeta = 0.5 * k;
    const double wm = std::min(w, kMaxMatchOmega);
    double c = 0.0, a = 0.0, denAtMatch = 0.0;
    if (zeta < 1.0)
    {
        // Polos r·e^±jθ. |e^jω − r·e^±jθ|² = (1 − r)² + 4r·sin²((ω ∓ θ)/2).
        const double r = std::exp(-zeta * w);
        const double oneMinusR = -std::expm1(-zeta * w);
        const double theta = w * std::sqrt(1.0 - zeta * zeta);
        const double sinHalf = std::sin(0.5 * theta);
        const double cosHalf = std::cos(0.5 * theta);
        c = oneMinusR * oneMinusR + 4.0 * r * sinHalf * sinHalf;
        a = oneMinusR * oneMinusR + 4.0 * r * cosHalf * cosHalf;
        const double sMinus = std::sin(0.5 * (wm - theta));
        const double sPlus = std::sin(0.5 * (wm + theta));
        denAtMatch =
            (oneMinusR * oneMinusR + 4.0 * r * sMinus * sMinus) * (oneMinusR * oneMinusR + 4.0 * r * sPlus * sPlus);
    }
    else
    {
        const double root = std::sqrt(zeta * zeta - 1.0);
        const double p1 = -w / (zeta + root); // = w·(−ζ + √(ζ² − 1)), sin restas
        const double p2 = -w * (zeta + root);
        const double z1 = std::exp(p1), z2 = std::exp(p2);
        const double e1 = std::expm1(p1), e2 = std::expm1(p2); // z − 1
        c = e1 * e2;
        a = (1.0 + z1) * (1.0 + z2);
        const double s = std::sin(0.5 * wm);
        denAtMatch = (e1 * e1 + 4.0 * z1 * s * s) * (e2 * e2 + 4.0 * z2 * s * s);
    }
    const double b = -2.0 * std::expm1(-k * w);

    // De vuelta al SVF: con p = (1 − z⁻¹)/(1 + z⁻¹) y s = p/g, el denominador
    // a·p² + b·p + c es c·(s² + kd·s + 1). Un numerador P·p² + Q·p + R da
    // m0 = P/a, n1 = Q/√(ac) y n0 = R/c.
    const double g = std::sqrt(c / a);
    const double rootAc = std::sqrt(a * c);
    const double kd = b / rootAc;

    // Un prototipo constante (la campana a 0 dB) queda constante exactamente:
    // el audio pasa bit a bit, como con la bilineal.
    if (m1 == 0.0 && m2 == 0.0)
        return secondOrder(g, kd, m0, 0.0, 0.0);

    const double nyquistMagnitude = targets.nyquistMagnitude >= 0.0
                                        ? targets.nyquistMagnitude
                                        : std::sqrt(analogMagnitudeSquared(n0, n1, n2, k, kPi / w));

    // Ceros en el eje imaginario, en s = ±j·√(n0/n2): en el círculo unidad,
    // en la misma frecuencia. Numerador β·(1, −2·cos ωz, 1), es decir
    // P = 4β·cos²(ωz/2), Q = 0 y R = 4β·sin²(ωz/2). β iguala la magnitud en
    // continua (notch) o, si allí hay un cero (paso alto), en ωm: con los ceros
    // fijos no queda más libertad, y ωm da menos error que Nyquist.
    const double zeroOmega = n2 > 0.0 ? w * std::sqrt(n0 / n2) : 0.0;
    if (n1 == 0.0 && n2 > 0.0 && zeroOmega < kPi)
    {
        const double sinZ = std::sin(0.5 * zeroOmega), cosZ = std::cos(0.5 * zeroOmega);
        const double sinM = std::sin(0.5 * wm);
        // |B(e^jωm)| = 4β·sin²(ωm/2) con los dos ceros en z = 1.
        const double beta =
            n0 > 0.0 ? n0 * c / (4.0 * sinZ * sinZ)
                     : std::sqrt(analogMagnitudeSquared(n0, n1, n2, k, wm / w) * denAtMatch) / (4.0 * sinM * sinM);
        const double mixHigh = 4.0 * beta * cosZ * cosZ / a;
        return secondOrder(g, kd, mixHigh, -mixHigh * kd, 4.0 * beta * sinZ * sinZ / c - mixHigh);
    }

    // Caso general: |B(e^jω)|² = B0·φ0 + B1·φ1 + B2·φ2, con φ0 = cos²(ω/2),
    // φ1 = sin²(ω/2) y φ2 = sin²ω. B0 y B1 salen de continua y Nyquist; B2,
    // de ωm.
    const double sqrtB0 = c * std::abs(n0);
    const double sqrtB1 = a * nyquistMagnitude;
    const double phi1 = std::sin(0.5 * wm) * std::sin(0.5 * wm);
    const double phi0 = 1.0 - phi1;
    const double phi2 = std::sin(wm) * std::sin(wm);
    const double target = (targets.matchMagnitudeSquared >= 0.0 ? targets.matchMagnitudeSquared
                                                                : analogMagnitudeSquared(n0, n1, n2, k, wm / w)) *
                          denAtMatch;
    const double b2 = (target - sqrtB0 * sqrtB0 * phi0 - sqrtB1 * sqrtB1 * phi1) / phi2;

    // Factorización: R = √B0, P = √B1 y, con W = (√B0 + √B1)/2,
    // Q = 2·(b0 − b2) = 2·√(W² + B2). Si el objetivo no es realizable
    // (W² + B2 < 0), lo más cercano que sí lo es: Q = 0.
    const double halfSum = 0.5 * (sqrtB0 + sqrtB1);
    const double diff = std::copysign(std::sqrt(std::max(0.0, halfSum * halfSum + b2)), n1);
    const double mixHigh = nyquistMagnitude;
    return secondOrder(g, kd, mixHigh, 2.0 * diff / rootAc - mixHigh * kd, std::abs(n0) - mixHigh);
}

/// Sección de orden 1 con la magnitud del prototipo, H(s) = m0 + m2/(s + 1):
/// magnitud igual a la analógica en continua, en Nyquist y en un tercer
/// punto. El polo sale de esa tercera condición (Vicanek, "Matched
/// One-Pole Digital Shelving Filters").
SectionCoeffs matchedFirstOrder(double w, double m0, double m2) noexcept
{
    if (m2 == 0.0)
        return firstOrder(std::tan(0.5 * std::min(w, kMaxMatchOmega)), m0, 0.0);

    // |H(jx)|² = (n0² + m0²·x²)/(1 + x²): G0² en continua, G1² en Nyquist, Gm² en ωm.
    const double n0 = m0 + m2;
    auto magnitudeSquared = [n0, m0](double x) { return (n0 * n0 + m0 * m0 * x * x) / (1.0 + x * x); };
    // Tercer punto: la media geométrica del polo y el cero (el pivote del
    // tilt) o, si el cero está en 0 o en infinito, el polo (los cortes).
    const double wm = std::min(m0 > 0.0 && n0 > 0.0 ? w * std::sqrt(n0 / m0) : w, kMaxMatchOmega);
    const double g0 = n0 * n0;
    const double g1 = magnitudeSquared(kPi / w);
    const double gm = magnitudeSquared(wm / w);

    // La sección digital vale exactamente (G1·s + G0)/(s + 1) en
    // s = j·tan(ω/2)/g: G0 en continua y G1 en Nyquist para cualquier g.
    // Igualar en ωm da g = tan(ωm/2)·√((Gm² − G1²)/(G0² − Gm²)).
    const double g = std::tan(0.5 * wm) * std::sqrt((gm - g1) / (g0 - gm));
    const double mixHigh = std::sqrt(g1);
    return firstOrder(g, mixHigh, std::abs(n0) - mixHigh);
}

/// Magnitud del prototipo analógico sin la deformación de la bilineal: el
/// algoritmo FilterAlgorithm::Matched.
struct MatchedDigitizer
{
    double omega; ///< ω0 = 2π·f/fs

    SectionCoeffs second(Scale scale, double a, double k, double m0, double m1, double m2) const noexcept
    {
        return matchedSecondOrder(scaled(omega, scale, a), k, m0, m1, m2);
    }

    SectionCoeffs first(Scale scale, double a, double m0, double m2) const noexcept
    {
        return matchedFirstOrder(scaled(omega, scale, a), m0, m2);
    }
};

/// Q de la pareja de polos número `pair` (1..N/2) de un Butterworth de orden N.
double butterworthQ(int order, int pair) noexcept
{
    return 1.0 / (2.0 * std::sin((2.0 * pair - 1.0) * kPi / (2.0 * order)));
}

template <typename Digitizer>
void designCut(BandDesign& design, const Digitizer& d, bool lowCut, double q, int slope) noexcept
{
    const int order = sanitizeSlope(slope) / 6;
    const int pairs = order / 2;

    if (order % 2 == 1)
        design.sections[static_cast<std::size_t>(design.numSections++)] =
            lowCut ? d.first(Scale::None, 1.0, 1.0, -1.0) : d.first(Scale::None, 1.0, 0.0, 1.0);

    // De menor a mayor Q. El Q del usuario escala solo la sección más
    // resonante (pair == 1): con Q = 0,7071 el filtro es Butterworth puro.
    for (int pair = pairs; pair >= 1; --pair)
    {
        double sectionQ = butterworthQ(order, pair);
        if (pair == 1)
            sectionQ *= q / kButterworthQ;
        const double k = 1.0 / sectionQ;

        design.sections[static_cast<std::size_t>(design.numSections++)] =
            lowCut ? d.second(Scale::None, 1.0, k, 1.0, -k, -1.0) : d.second(Scale::None, 1.0, k, 0.0, 0.0, 1.0);
    }
}

/// Los prototipos analógicos de cada tipo (la tabla de docs/dsp/filtros.md),
/// digitalizados con `d`.
template <typename Digitizer> BandDesign designWith(const FilterParams& params, const Digitizer& d) noexcept
{
    const double q = std::clamp(params.q, kMinQ, kMaxQ);
    const double gainDb = std::clamp(params.gainDb, -kMaxGainDb, kMaxGainDb);
    const double k = 1.0 / q;

    BandDesign design;
    auto single = [&design](const SectionCoeffs& c) {
        design.sections[0] = c;
        design.numSections = 1;
    };

    switch (params.type)
    {
    case FilterType::Bell:
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double kBell = 1.0 / (q * a);
        single(d.second(Scale::None, a, kBell, 1.0, kBell * (a * a - 1.0), 0.0));
        break;
    }
    case FilterType::LowShelf:
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        single(d.second(Scale::DivSqrtA, a, k, 1.0, k * (a - 1.0), a * a - 1.0));
        break;
    }
    case FilterType::HighShelf:
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        single(d.second(Scale::MulSqrtA, a, k, a * a, k * (1.0 - a) * a, 1.0 - a * a));
        break;
    }
    case FilterType::LowCut:
        designCut(design, d, true, q, params.slopeDbPerOct);
        break;
    case FilterType::HighCut:
        designCut(design, d, false, q, params.slopeDbPerOct);
        break;
    case FilterType::Notch:
        single(d.second(Scale::None, 1.0, k, 1.0, -k, 0.0));
        break;
    case FilterType::BandPass:
        single(d.second(Scale::None, 1.0, k, 0.0, k, 0.0));
        break;
    case FilterType::AllPass:
        single(d.second(Scale::None, 1.0, k, 1.0, -2.0 * k, 0.0));
        break;
    case FilterType::Tilt:
    {
        // H(s) = (A·s + 1)/(s + A): 1/A en continua, A en agudos, 1 en el pivote.
        const double a = std::pow(10.0, gainDb / 20.0);
        single(d.first(Scale::MulA, a, a, 1.0 / a - a));
        break;
    }
    }

    return design;
}

std::complex<double> sectionResponse(const SectionCoeffs& c, double tanHalfOmega) noexcept
{
    // El SVF trapezoidal equivale a la transformada bilineal del prototipo
    // analógico normalizado, evaluado en s = j·tan(ω/2)/g.
    const std::complex<double> s{0.0, tanHalfOmega / c.g};

    if (c.order == 1)
        return c.m0 + c.m2 / (s + 1.0);

    return c.m0 + (c.m1 * s + c.m2) / (s * s + c.k * s + 1.0);
}

/// `out` = 1/H de la sección `c`. false si `c` no es de fase mínima.
bool invertSection(const SectionCoeffs& c, SectionCoeffs& out) noexcept
{
    if (c.order == 1)
    {
        // H = (m0·s + n0)/(s + 1)  →  1/H = (1/n0)·(s + 1)/(s/w + 1), w = n0/m0
        const double n0 = c.m0 + c.m2;
        if (!(n0 > 0.0 && c.m0 > 0.0))
            return false;
        out = firstOrder(c.g * n0 / c.m0, 1.0 / c.m0, 1.0 / n0 - 1.0 / c.m0);
        return true;
    }
    // H = (n2·s² + n1·s + n0)/(s² + k·s + 1). Con s = w·u, w = √(n0/n2):
    // 1/H = (w²·u² + k·w·u + 1)/(n0·(u² + k'·u + 1)), k' = n1/√(n0·n2).
    const double n2 = c.m0;
    const double n1 = c.m0 * c.k + c.m1;
    const double n0 = c.m0 + c.m2;
    if (!(n0 > 0.0 && n1 > 0.0 && n2 > 0.0))
        return false;
    const double w = std::sqrt(n0 / n2);
    const double k = n1 / std::sqrt(n0 * n2);
    const double m0 = 1.0 / n2; // w²/n0
    out = secondOrder(c.g * w, k, m0, c.k * w / n0 - m0 * k, 1.0 / n0 - m0);
    return true;
}

/// Diseño con la respuesta exactamente inversa, 1/H. Si alguna sección no es
/// de fase mínima, `design` sin cambios.
BandDesign inverseBand(const BandDesign& design) noexcept
{
    // 1/(H1·H2·…) = …·(1/H2)·(1/H1): cada sección se invierte, en orden inverso.
    BandDesign inverse = design;
    for (int i = 0; i < design.numSections; ++i)
    {
        const auto from = static_cast<std::size_t>(design.numSections - 1 - i);
        if (!invertSection(design.sections[from], inverse.sections[static_cast<std::size_t>(i)]))
            return design;
    }
    return inverse;
}

/// Multiplica la respuesta de una banda de una sección por `factor`.
BandDesign scaledBand(BandDesign design, double factor) noexcept
{
    SectionCoeffs& c = design.sections[0];
    c.m0 *= factor;
    c.m1 *= factor;
    c.m2 *= factor;
    return design;
}

/// Amortiguamiento de los ceros auxiliares del low shelf resonante: Q = 0,7071.
inline constexpr double kShelfSplitDamping = 1.0 / kButterworthQ;

/// Low shelf con realce (gainDb >= 0) para Matched. El prototipo
///
///     LS(s) = N(s)/D(s) = (s² + k·a·s + a²)/(s² + k·s + 1)    (s en la frecuencia de los polos)
///
/// tiene polos y ceros con el mismo Q en frecuencias distintas. Con Q <= 0,7071
/// una sección ajustada en tres puntos lo sigue bien. Con más Q, un solo
/// numerador no puede reproducir a la vez el pico y la caída resonantes, y se
/// factoriza con unos ceros auxiliares E(s) = s² + ke·a·s + a² de Q 0,7071:
///
///     LS = (E/D)·(N/E)
///
/// E/D tiene los polos resonantes y ceros suaves, y se ajusta como siempre.
/// N/E es una campana relativa en la frecuencia de los ceros, una atenuación
/// que se diseña como la inversa de su realce. Vale exactamente 1 con Q = 0,7071,
/// así que no hay salto al cruzar ese Q. Si los ceros se acercan a Nyquist,
/// N/E se funde con la unidad entre 0,9π y 0,98π: allí sus polos ya no tienen
/// imagen digital. Ver docs/dsp/filtros.md.
BandDesign matchedLowShelfBoost(double omega, double gainDb, double q) noexcept
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double k = 1.0 / q;
    const double poles = omega / std::sqrt(a);
    BandDesign design;
    if (k >= kShelfSplitDamping)
    {
        design.sections[0] = matchedSecondOrder(poles, k, 1.0, k * (a - 1.0), a * a - 1.0);
        design.numSections = 1;
        return design;
    }
    // N/E primero: E/D se ajusta a lo que falta para el prototipo entero.
    const double ke = kShelfSplitDamping;
    const double zeros = poles * a;
    const double fade = std::clamp((zeros / kPi - 0.9) / 0.08, 0.0, 1.0);
    const double kz = k + (ke - k) * fade * fade * (3.0 - 2.0 * fade);
    // Realce (u² + ke·u + 1)/(u² + kz·u + 1), u en la frecuencia de los ceros.
    const SectionCoeffs boost = matchedSecondOrder(zeros, kz, 1.0, ke - kz, 0.0);
    SectionCoeffs relative;
    if (!invertSection(boost, relative))
        relative = secondOrder(1.0, 1.0, 1.0, 0.0, 0.0);

    // E/D, con la magnitud de N/D dividida por la de N/E digital en Nyquist
    // (allí la sección vale m0) y en la frecuencia de ajuste.
    BandDesign part;
    part.sections[0] = relative;
    part.numSections = 1;
    const double wm = std::min(poles, kMaxMatchOmega);
    const double relativeAtMatch = std::abs(responseAtTangent(part, std::tan(0.5 * wm)));
    const MatchTargets fit{std::sqrt(analogMagnitudeSquared(a * a, k * a, 1.0, k, kPi / poles)) / std::abs(relative.m0),
                           analogMagnitudeSquared(a * a, k * a, 1.0, k, wm / poles) /
                               (relativeAtMatch * relativeAtMatch)};
    // Con E = s² + ke·a·s + a² en el numerador: n1 = ke·a, y m1 = n1 − k.
    design.sections[0] = matchedSecondOrder(poles, k, 1.0, ke * a - k, a * a - 1.0, fit);
    design.sections[1] = relative;
    design.numSections = 2;
    return design;
}

/// FilterAlgorithm::Matched. Las atenuaciones de los tipos con ganancia se
/// diseñan como la inversa exacta del realce simétrico, H(−g) = 1/H(g): con
/// polos por invariancia al impulso, una atenuación estrecha cerca de Nyquist
/// no siempre tiene un numerador que iguale los tres puntos, y su inversa sí.
/// El high shelf sale del low shelf, HS(g) = 10^(g/20)/LS(g), porque sus polos
/// pueden quedar por encima de Nyquist, y el low shelf con Q alto usa dos
/// secciones (matchedLowShelfBoost()). Un realce y la atenuación simétrica se
/// cancelan exactamente, como con Classic.
BandDesign designMatched(FilterParams params, double omega) noexcept
{
    const MatchedDigitizer d{omega};
    const double gainDb = std::clamp(params.gainDb, -kMaxGainDb, kMaxGainDb);
    params.gainDb = std::abs(gainDb);
    switch (params.type)
    {
    case FilterType::Bell:
    case FilterType::Tilt:
        return gainDb < 0.0 ? inverseBand(designWith(params, d)) : designWith(params, d);
    case FilterType::LowShelf:
    case FilterType::HighShelf:
    {
        const BandDesign boost = matchedLowShelfBoost(omega, params.gainDb, std::clamp(params.q, kMinQ, kMaxQ));
        if (params.type == FilterType::LowShelf)
            return gainDb < 0.0 ? inverseBand(boost) : boost;
        return scaledBand(gainDb > 0.0 ? inverseBand(boost) : boost, std::pow(10.0, gainDb / 20.0));
    }
    default:
        return designWith(params, d);
    }
}

} // namespace

BandDesign designBand(const FilterParams& params, double sampleRate) noexcept ESCEQ_NONBLOCKING
{
    return designBandPrepared(params, prepareFrequency(params.frequencyHz, sampleRate));
}

PreparedFrequency prepareFrequency(double frequencyHz, double sampleRate) noexcept ESCEQ_NONBLOCKING
{
    const double frequency = std::clamp(frequencyHz, kMinFrequencyHz, kMaxFrequencyRatio * sampleRate);
    return {std::tan(kPi * frequency / sampleRate), 2.0 * kPi * frequency / sampleRate};
}

BandDesign designBandPrepared(const FilterParams& params, const PreparedFrequency& frequency) noexcept ESCEQ_NONBLOCKING
{
    switch (params.algorithm)
    {
    case FilterAlgorithm::Classic:
        break;
    case FilterAlgorithm::Matched:
        return designMatched(params, frequency.omega);
    }
    return designWith(params, ClassicDigitizer{frequency.g});
}

double responseTangent(double frequencyHz, double sampleRate) noexcept ESCEQ_NONBLOCKING
{
    const double frequency = std::clamp(frequencyHz, 0.0, 0.4999 * sampleRate);
    return std::tan(kPi * frequency / sampleRate);
}

std::complex<double> responseAtTangent(const BandDesign& design, double tangent) noexcept ESCEQ_NONBLOCKING
{
    std::complex<double> h{1.0, 0.0};
    for (int i = 0; i < design.numSections; ++i)
        h *= sectionResponse(design.sections[static_cast<std::size_t>(i)], tangent);
    return h;
}

double magnitudeDbAtTangent(const BandDesign& design, double tangent) noexcept ESCEQ_NONBLOCKING
{
    const double magnitude = std::abs(responseAtTangent(design, tangent));
    return 20.0 * std::log10(std::max(magnitude, 1.0e-15));
}

std::complex<double> response(const BandDesign& design, double frequencyHz, double sampleRate) noexcept ESCEQ_NONBLOCKING
{
    return responseAtTangent(design, responseTangent(frequencyHz, sampleRate));
}

double magnitudeDb(const BandDesign& design, double frequencyHz, double sampleRate) noexcept ESCEQ_NONBLOCKING
{
    return magnitudeDbAtTangent(design, responseTangent(frequencyHz, sampleRate));
}

} // namespace esceq::dsp
