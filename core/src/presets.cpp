// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/presets.h"

#include "esceq/core/state.h"

#include "text_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <locale>
#include <sstream>

namespace esceq::core {

namespace {

// Los presets de fábrica van escritos en el formato de estado, con solo los
// parámetros que cambian respecto al valor por defecto. Tipos: 0 campana,
// 1 shelf de graves, 2 shelf de agudos, 3 corte de graves. Pendiente: índice
// i = 6·(i+1) dB/oct. Canal: 4 = Side.
constexpr std::array<FactoryPreset, 5> kFactoryPresets{{
    {"De-esser",
     // Una campana dinámica sobre las eses: solo baja cuando suenan.
     "esceQ-state 2\n"
     "b01_on 1\nb01_type 0\nb01_freq 6500\nb01_gain 0\nb01_q 2\n"
     "b01_dyn 1\nb01_thr -30\nb01_range -8\nb01_att 0.5\nb01_rel 60\nb01_knee 6\n"},

    {"Resonance Control",
     // Tres campanas dinámicas estrechas en zonas donde suelen acumularse
     // resonancias. Hay que mover cada una a la resonancia real del material.
     "esceQ-state 2\n"
     "b01_on 1\nb01_freq 250\nb01_q 4\nb01_dyn 1\nb01_thr -28\nb01_range -6\nb01_att 5\nb01_rel 80\nb01_knee 6\n"
     "b02_on 1\nb02_freq 800\nb02_q 5\nb02_dyn 1\nb02_thr -30\nb02_range -6\nb02_att 5\nb02_rel 80\nb02_knee 6\n"
     "b03_on 1\nb03_freq 3200\nb03_q 5\nb03_dyn 1\nb03_thr -32\nb03_range -6\nb03_att 3\nb03_rel 80\nb03_knee 6\n"},

    {"Low-End Cleanup",
     // Corte de subgraves, algo menos de "barro" y graves solo en el centro
     // (corte de graves en Side).
     "esceQ-state 2\n"
     "b01_on 1\nb01_type 3\nb01_freq 30\nb01_slope 3\n"
     "b02_on 1\nb02_type 0\nb02_freq 250\nb02_gain -2.5\nb02_q 1.2\n"
     "b03_on 1\nb03_type 3\nb03_freq 120\nb03_slope 1\nb03_chan 4\n"},

    {"Vocal",
     // Limpieza de graves, menos caja, presencia, aire y un de-esser suave.
     "esceQ-state 2\n"
     "b01_on 1\nb01_type 3\nb01_freq 90\nb01_slope 2\n"
     "b02_on 1\nb02_type 0\nb02_freq 300\nb02_gain -2.5\nb02_q 1.4\n"
     "b03_on 1\nb03_type 0\nb03_freq 3500\nb03_gain 2.5\nb03_q 1\n"
     "b04_on 1\nb04_type 2\nb04_freq 10000\nb04_gain 3\n"
     "b05_on 1\nb05_type 0\nb05_freq 7000\nb05_gain 0\nb05_q 2\n"
     "b05_dyn 1\nb05_thr -30\nb05_range -6\nb05_att 0.5\nb05_rel 60\nb05_knee 6\n"},

    {"Gentle Master",
     // Retoques de un decibelio: subgraves fuera, algo de cuerpo y de aire.
     "esceQ-state 2\n"
     "b01_on 1\nb01_type 3\nb01_freq 25\nb01_slope 1\n"
     "b02_on 1\nb02_type 1\nb02_freq 90\nb02_gain 1\n"
     "b03_on 1\nb03_type 0\nb03_freq 350\nb03_gain -1\nb03_q 0.8\n"
     "b04_on 1\nb04_type 2\nb04_freq 9000\nb04_gain 1.5\n"},
}};

constexpr std::size_t kMaxFileNameLength = 64;

bool isReservedWindowsName(std::string_view name)
{
    // Se compara sin la extensión y sin distinguir mayúsculas: "con.txt" también está reservado.
    std::string base;
    for (const char c : name.substr(0, name.find('.')))
        base.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    for (const std::string_view reserved : {"CON", "PRN", "AUX", "NUL"})
        if (base == reserved)
            return true;
    if (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")))
        return base[3] >= '1' && base[3] <= '9';
    return false;
}

} // namespace

std::span<const FactoryPreset> factoryPresets() noexcept
{
    return kFactoryPresets;
}

std::optional<ParameterValues> loadFactoryPreset(std::string_view name)
{
    for (const auto& preset : kFactoryPresets)
        if (preset.name == name)
            return deserializeState(preset.state);
    return std::nullopt;
}

std::string serializePreset(const ParameterValues& values)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);

    out << "esceQ-state " << kStateVersion << '\n';
    for (std::size_t i = 0; i < kNumParameters; ++i)
    {
        const auto& spec = parameters()[i];
        const float value = sanitize(spec, values[i]);
        if (value != spec.defaultValue)
            out << spec.id << ' ' << value << '\n';
    }
    return out.str();
}

std::string sanitizePresetName(std::string_view name)
{
    std::string out;
    for (const char c : name)
    {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f)
            continue; // caracteres de control
        if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
            continue; // reservados en Windows (y '/' en todas partes)
        out.push_back(c);
    }

    detail::truncateUtf8(out, kMaxFileNameLength);

    // Windows no admite nombres que acaben en espacio o punto; al principio estorban.
    const auto isEdge = [](char c) { return c == ' ' || c == '.' || c == '\t'; };
    while (!out.empty() && isEdge(out.back()))
        out.pop_back();
    std::size_t start = 0;
    while (start < out.size() && isEdge(out[start]))
        ++start;
    out.erase(0, start);

    if (isReservedWindowsName(out))
        return {};
    return out;
}

} // namespace esceq::core
