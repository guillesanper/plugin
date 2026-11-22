// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/parameters.h"

#include "text_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

namespace esceq::core {

namespace {

struct Table
{
    std::array<std::string, kNumParameters> ids;
    std::array<std::string, kNumParameters> names;
    std::array<ParameterSpec, kNumParameters> specs{};

    void set(std::size_t index, std::string id, std::string name, ParameterSpec spec)
    {
        ids[index] = std::move(id);
        names[index] = std::move(name);
        spec.id = ids[index];
        spec.name = names[index];
        specs[index] = spec;
    }

    Table()
    {
        using enum ParameterKind;
        using enum ParameterScale;

        set(kOutputGain, "output_gain", "Output Gain",
            {.unit = "dB", .kind = Continuous, .min = -24.0f, .max = 24.0f, .defaultValue = 0.0f});
        set(kBypass, "bypass", "Bypass", {.kind = Toggle, .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f});

        const auto lastType = static_cast<float>(kFilterTypeChoices.size() - 1);
        const auto lastSlope = static_cast<float>(kSlopeChoices.size() - 1);

        for (int band = 0; band < kNumBands; ++band)
        {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "b%02d_", band + 1);
            const std::string id = prefix;
            const std::string name = "Band " + std::to_string(band + 1) + " ";

            auto at = [band](BandParam p) { return bandParameter(band, p); };

            set(at(BandParam::Enabled), id + "on", name + "On",
                {.kind = Toggle, .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f});
            set(at(BandParam::Type), id + "type", name + "Type",
                {.kind = Choice, .min = 0.0f, .max = lastType, .defaultValue = 0.0f, .choices = kFilterTypeChoices});
            set(at(BandParam::Frequency), id + "freq", name + "Freq",
                {.unit = "Hz", .kind = Continuous, .min = 20.0f, .max = 20000.0f, .defaultValue = 1000.0f,
                 .scale = Logarithmic});
            set(at(BandParam::Gain), id + "gain", name + "Gain",
                {.unit = "dB", .kind = Continuous, .min = -30.0f, .max = 30.0f, .defaultValue = 0.0f});
            set(at(BandParam::Q), id + "q", name + "Q",
                {.kind = Continuous, .min = 0.1f, .max = 30.0f, .defaultValue = 0.7071f, .scale = Logarithmic});
            set(at(BandParam::Slope), id + "slope", name + "Slope",
                {.kind = Choice, .min = 0.0f, .max = lastSlope, .defaultValue = 1.0f, .choices = kSlopeChoices});
        }

        // Dinámica (fase 3): después de las 24 bandas, porque los parámetros
        // solo se añaden al final.
        for (int band = 0; band < kNumBands; ++band)
        {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "b%02d_", band + 1);
            const std::string id = prefix;
            const std::string name = "Band " + std::to_string(band + 1) + " ";

            auto at = [band](DynParam p) { return dynamicParameter(band, p); };

            set(at(DynParam::Enabled), id + "dyn", name + "Dynamic",
                {.kind = Toggle, .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f});
            set(at(DynParam::Threshold), id + "thr", name + "Threshold",
                {.unit = "dB", .kind = Continuous, .min = -60.0f, .max = 0.0f, .defaultValue = -24.0f});
            set(at(DynParam::Range), id + "range", name + "Range",
                {.unit = "dB", .kind = Continuous, .min = -30.0f, .max = 30.0f, .defaultValue = -6.0f});
            set(at(DynParam::Attack), id + "att", name + "Attack",
                {.unit = "ms", .kind = Continuous, .min = 0.1f, .max = 200.0f, .defaultValue = 5.0f,
                 .scale = Logarithmic});
            set(at(DynParam::Release), id + "rel", name + "Release",
                {.unit = "ms", .kind = Continuous, .min = 5.0f, .max = 5000.0f, .defaultValue = 100.0f,
                 .scale = Logarithmic});
            set(at(DynParam::Knee), id + "knee", name + "Knee",
                {.unit = "dB", .kind = Continuous, .min = 0.0f, .max = 24.0f, .defaultValue = 6.0f});
            set(at(DynParam::Source), id + "sc", name + "Sidechain",
                {.kind = Choice, .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f, .choices = kSidechainChoices});
        }

        // Canal por banda (fase 4): al final, por la misma regla. El valor por
        // defecto, Stereo, es el comportamiento de los estados anteriores.
        const auto lastChannel = static_cast<float>(kChannelChoices.size() - 1);
        for (int band = 0; band < kNumBands; ++band)
        {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "b%02d_", band + 1);
            set(channelParameter(band), std::string(prefix) + "chan", "Band " + std::to_string(band + 1) + " Channel",
                {.kind = Choice, .min = 0.0f, .max = lastChannel, .defaultValue = 0.0f, .choices = kChannelChoices});
        }

        // Modo de fase (fase 5): al final. IIR es el comportamiento de
        // los estados anteriores (latencia cero).
        set(kPhaseMode, "phase_mode", "Phase Mode",
            {.kind = Choice, .min = 0.0f, .max = static_cast<float>(kPhaseModeChoices.size() - 1),
             .defaultValue = 0.0f, .choices = kPhaseModeChoices});

        // Compensación automática de ganancia (fase 5): al final. Apagada es el
        // comportamiento de los estados anteriores.
        set(kAutoGain, "auto_gain", "Auto Gain", {.kind = Toggle, .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f});

        // Tiempos automáticos de la dinámica (fase 5): al final, uno por banda.
        // Apagados es el comportamiento de los estados anteriores.
        for (int band = 0; band < kNumBands; ++band)
        {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "b%02d_", band + 1);
            set(autoTimingParameter(band), std::string(prefix) + "autotime",
                "Band " + std::to_string(band + 1) + " Auto Timing",
                {.kind = Toggle, .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f});
        }

        // Dinámica espectral (fase 6): al final, uno por banda. Apagados es el
        // comportamiento de los estados anteriores.
        for (int band = 0; band < kNumBands; ++band)
        {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "b%02d_", band + 1);
            set(spectralParameter(band), std::string(prefix) + "spectral",
                "Band " + std::to_string(band + 1) + " Spectral",
                {.kind = Toggle, .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f});
        }
    }
};

std::string lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string fixed(double value, int decimals)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.setf(std::ios::fixed);
    out.precision(decimals);
    out << value;
    return out.str();
}

} // namespace

const std::array<ParameterSpec, kNumParameters>& parameters() noexcept
{
    static const Table table;
    return table.specs;
}

ParameterValues defaultValues() noexcept
{
    ParameterValues values{};
    for (std::size_t i = 0; i < kNumParameters; ++i)
        values[i] = parameters()[i].defaultValue;
    return values;
}

std::optional<std::size_t> findParameter(std::string_view id) noexcept
{
    const auto& specs = parameters();
    for (std::size_t i = 0; i < kNumParameters; ++i)
        if (specs[i].id == id)
            return i;
    return std::nullopt;
}

float sanitize(const ParameterSpec& spec, float value) noexcept
{
    if (!std::isfinite(value))
        return spec.defaultValue;

    switch (spec.kind)
    {
    case ParameterKind::Toggle:
        return value >= 0.5f ? 1.0f : 0.0f;
    case ParameterKind::Choice:
        return std::clamp(std::round(value), spec.min, spec.max);
    case ParameterKind::Continuous:
        break;
    }
    return std::clamp(value, spec.min, spec.max);
}

namespace {

/// Redondea a `digits` cifras significativas.
double roundToSignificant(double value, int digits) noexcept
{
    if (value == 0.0 || !std::isfinite(value))
        return value;
    const double magnitude = std::floor(std::log10(std::abs(value)));
    const double scale = std::pow(10.0, static_cast<double>(digits - 1) - magnitude);
    return std::round(value * scale) / scale;
}

} // namespace

float toNormalized(const ParameterSpec& spec, float value) noexcept
{
    const float v = sanitize(spec, value);
    float normalized = 0.0f;

    if (spec.kind == ParameterKind::Continuous && spec.scale == ParameterScale::Logarithmic)
        normalized = static_cast<float>(std::log(static_cast<double>(v) / spec.min) / std::log(static_cast<double>(spec.max) / spec.min));
    else
        // En doble precisión: con float, la resta ya pierde lo que luego
        // hace falta para volver al mismo valor.
        normalized = static_cast<float>((static_cast<double>(v) - spec.min) / (static_cast<double>(spec.max) - spec.min));

    return std::clamp(normalized, 0.0f, 1.0f);
}

float fromNormalized(const ParameterSpec& spec, float normalized) noexcept
{
    const double n = std::isfinite(normalized) ? std::clamp(static_cast<double>(normalized), 0.0, 1.0) : 0.0;

    double value = spec.min + n * (spec.max - spec.min);
    if (spec.kind == ParameterKind::Continuous && spec.scale == ParameterScale::Logarithmic)
        value = spec.min * std::pow(static_cast<double>(spec.max) / spec.min, n);

    const float exact = sanitize(spec, static_cast<float>(value));
    if (spec.kind != ParameterKind::Continuous)
        return exact;

    // La posición 0..1 es un float: tiene menos resolución que el valor, y
    // 6 dB volvería del host como 6,0000014 dB. Entre todos los valores que
    // corresponden a ESTA MISMA posición se devuelve el más "redondo" (el de
    // menos cifras significativas). Así los valores escritos a mano, los de
    // los presets y los valores por defecto dan la vuelta exactos, y la
    // correspondencia con la posición (el contrato de automatización) no
    // cambia: el valor devuelto se normaliza a la misma posición que el valor
    // exacto, que puede diferir de `normalized` en menos de 1e-7 (la posición
    // es un float). Se comprobó con todas las posiciones float de cada rango:
    // la ida y vuelta es exacta y la correspondencia sigue siendo monótona.
    const float position = toNormalized(spec, exact);
    for (int digits = 1; digits <= 7; ++digits)
    {
        const float candidate = sanitize(spec, static_cast<float>(roundToSignificant(exact, digits)));
        if (toNormalized(spec, candidate) == position)
            return candidate;
    }
    return exact;
}

std::string formatValue(const ParameterSpec& spec, float value)
{
    const float v = sanitize(spec, value);

    switch (spec.kind)
    {
    case ParameterKind::Toggle:
        return v >= 0.5f ? "On" : "Off";
    case ParameterKind::Choice:
    {
        const auto index = static_cast<std::size_t>(v);
        return index < spec.choices.size() ? std::string(spec.choices[index]) : fixed(v, 0);
    }
    case ParameterKind::Continuous:
        break;
    }

    if (spec.unit == "Hz")
    {
        if (v >= 10000.0f)
            return fixed(v / 1000.0, 1) + " kHz";
        if (v >= 1000.0f)
            return fixed(v / 1000.0, 2) + " kHz";
        return fixed(v, v < 100.0f ? 1 : 0) + " Hz";
    }
    if (spec.unit == "ms")
    {
        if (v >= 1000.0f)
            return fixed(v / 1000.0, 2) + " s";
        return fixed(v, v < 10.0f ? 2 : v < 100.0f ? 1 : 0) + " ms";
    }
    if (spec.unit == "dB")
    {
        const double rounded = std::round(v * 10.0) / 10.0;
        // El "+" solo tiene sentido si el parámetro también admite negativos.
        const bool signedRange = spec.min < 0.0f;
        // Sin `"+" + std::string`: GCC 12 da un falso aviso -Wrestrict (bug 105651).
        std::string text = signedRange && rounded > 0.0 ? "+" : "";
        text += fixed(rounded == 0.0 ? 0.0 : rounded, 1);
        text += " dB";
        return text;
    }
    return fixed(v, 2);
}

std::optional<float> parseValue(const ParameterSpec& spec, std::string_view text)
{
    const std::string clean = lower(detail::trim(text));
    if (clean.empty())
        return std::nullopt;

    if (spec.kind == ParameterKind::Toggle)
    {
        if (clean == "on" || clean == "true")
            return 1.0f;
        if (clean == "off" || clean == "false")
            return 0.0f;
    }

    if (spec.kind == ParameterKind::Choice)
        for (std::size_t i = 0; i < spec.choices.size(); ++i)
            if (lower(spec.choices[i]) == clean)
                return static_cast<float>(i);

    std::istringstream in(clean);
    in.imbue(std::locale::classic());
    double number = 0.0;
    if (!(in >> number))
        return std::nullopt;

    std::string suffix;
    in >> suffix;
    if (!suffix.empty() && suffix.front() == 'k' && spec.unit == "Hz")
        number *= 1000.0;
    if (suffix == "s" && spec.unit == "ms")
        number *= 1000.0;

    return sanitize(spec, static_cast<float>(number));
}

} // namespace esceq::core
