// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/parameters.h"

#include <algorithm>
#include <cmath>

namespace esceq::core {

ParameterValues defaultValues() noexcept
{
    ParameterValues values{};
    for (std::size_t i = 0; i < kNumParameters; ++i)
        values[i] = kParameters[i].defaultValue;
    return values;
}

std::optional<std::size_t> findParameter(std::string_view id) noexcept
{
    for (std::size_t i = 0; i < kNumParameters; ++i)
        if (kParameters[i].id == id)
            return i;
    return std::nullopt;
}

float sanitize(const ParameterSpec& spec, float value) noexcept
{
    if (!std::isfinite(value))
        return spec.defaultValue;
    if (spec.kind == ParameterKind::Toggle)
        return value >= 0.5f ? 1.0f : 0.0f;
    return std::clamp(value, spec.min, spec.max);
}

} // namespace esceq::core
