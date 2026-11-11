// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/state.h"

#include <limits>
#include <locale>
#include <sstream>

namespace esceq::core {

namespace {

constexpr std::string_view kMagic = "esceQ-state";

/// Devuelve la siguiente línea de `text` (sin '\r' final) y la consume.
std::string_view nextLine(std::string_view& text)
{
    const auto end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    return line;
}

/// Lee `clave valor` con la configuración regional clásica ("C").
template <typename T>
bool parsePair(std::string_view line, std::string& key, T& value)
{
    std::istringstream in{std::string{line}};
    in.imbue(std::locale::classic());
    return static_cast<bool>(in >> key >> value);
}

} // namespace

std::string serializeState(const ParameterValues& values)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);

    out << kMagic << ' ' << kStateVersion << '\n';
    for (std::size_t i = 0; i < kNumParameters; ++i)
        out << parameters()[i].id << ' ' << sanitize(parameters()[i], values[i]) << '\n';
    return out.str();
}

std::optional<ParameterValues> deserializeState(std::string_view text)
{
    std::string key;
    int version = 0;
    if (!parsePair(nextLine(text), key, version) || key != kMagic || version < 1)
        return std::nullopt;

    // Aquí irán las migraciones cuando kStateVersion suba:
    // cada una transforma el contenido de la versión N a la N+1.

    ParameterValues values = defaultValues();
    while (!text.empty())
    {
        float value = 0.0f;
        if (!parsePair(nextLine(text), key, value))
            continue;
        if (const auto index = findParameter(key))
            values[*index] = sanitize(parameters()[*index], value);
    }
    return values;
}

} // namespace esceq::core
