// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

/// Escalas y balística de los medidores, sin dependencias gráficas.
namespace esceq::ui {

inline constexpr double kMeterTopDb = 6.0;
inline constexpr double kMeterBottomDb = -60.0;
/// Por encima de este nivel el medidor avisa de recorte.
inline constexpr double kMeterClipDb = 0.0;

/// Altura 0..1 de un nivel en el medidor de salida. La escala es lineal en dB
/// por tramos: la zona útil (de -24 dB a +6 dB) ocupa el 70 % superior, y lo
/// que queda hasta -60 dB, el 30 % inferior. Niveles fuera de rango se recortan.
float meterFraction(double db) noexcept;

struct MeterTick
{
    double db = 0.0;
    float fraction = 0.0f;
    bool labelled = false;
};

/// Marcas de la escala del medidor, de arriba abajo.
std::vector<MeterTick> meterTicks();

/// Balística de un canal del medidor: sube al instante, baja a velocidad
/// constante en dB, y recuerda el pico máximo durante un tiempo.
///
/// No es segura entre hilos: la usa solo el hilo de la interfaz, que le pasa
/// el pico que publicó el hilo de audio.
class PeakMeter
{
public:
    static constexpr double kFallDbPerSecond = 24.0;
    static constexpr double kHoldSeconds = 1.2;
    static constexpr double kHoldFallDbPerSecond = 12.0;
    static constexpr double kSilenceDb = -120.0;

    /// `peak`: mayor valor absoluto (lineal) desde la última llamada.
    /// `dtSeconds`: tiempo transcurrido desde entonces.
    void update(double peak, double dtSeconds) noexcept;

    double levelDb() const noexcept { return levelDb_; }
    double holdDb() const noexcept { return holdDb_; }

    /// true desde que algún pico superó kMeterClipDb hasta resetClip().
    bool clipped() const noexcept { return clipped_; }
    void resetClip() noexcept { clipped_ = false; }

    void reset() noexcept { *this = PeakMeter{}; }

private:
    double levelDb_ = kSilenceDb;
    double holdDb_ = kSilenceDb;
    double holdAge_ = 0.0;
    bool clipped_ = false;
};

/// Medidor de la dinámica de una banda: cuánto del rango se está usando,
/// de 0 (nada) a 1 (todo el rango). Vale para reducción y para realce: el
/// sentido lo da el signo del rango. Una ganancia de signo contrario da 0.
float dynamicMeterFraction(double dynamicGainDb, double rangeDb) noexcept;

} // namespace esceq::ui
