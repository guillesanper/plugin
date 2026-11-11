// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>

namespace esceq::dsp {

/// Por debajo de este nivel la ganancia se considera silencio.
inline constexpr float kSilenceDb = -120.0f;

/// Convierte decibelios a ganancia lineal. Segura en tiempo real.
/// Valores <= kSilenceDb devuelven exactamente 0.
inline float dbToGain(float db) noexcept
{
    return db <= kSilenceDb ? 0.0f : std::pow(10.0f, db / 20.0f);
}

/// Convierte ganancia lineal a decibelios. Segura en tiempo real.
/// Ganancias <= 0 devuelven kSilenceDb; el resultado nunca baja de ahí.
inline float gainToDb(float gain) noexcept
{
    if (gain <= 0.0f)
        return kSilenceDb;
    const float db = 20.0f * std::log10(gain);
    return db < kSilenceDb ? kSilenceDb : db;
}

} // namespace esceq::dsp
