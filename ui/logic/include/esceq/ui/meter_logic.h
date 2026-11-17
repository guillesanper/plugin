// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
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

/// Balística del analizador de espectro: suavizado exponencial en dB de cada
/// punto. Cada fotograma es una sola FFT y, sin promediar, el espectro del
/// ruido o de la música sale muy dentado. Sube con una constante de tiempo
/// corta y baja con una larga, para que se pueda leer. Un punto que está en el
/// suelo (silencio) toma el dato nuevo tal cual, sin transición.
///
/// No es segura entre hilos: la usa solo el hilo de la interfaz.
class SpectrumBallistics
{
public:
    static constexpr double kRiseSeconds = 0.06;
    /// Constante de tiempo de bajada por defecto (la "media" de los ajustes).
    static constexpr double kFallSeconds = 0.35;
    /// Por debajo de esto un punto no se mueve en pantalla.
    static constexpr float kRepaintDb = 0.05f;

    /// `points` niveles, todos en `floorDb` (el suelo del analizador).
    SpectrumBallistics(std::size_t points, float floorDb);

    /// Lleva cada nivel hacia `target` (mismo tamaño) tras `dtSeconds`.
    /// Devuelve true si algún punto se ha movido más de kRepaintDb.
    bool update(const std::vector<float>& target, double dtSeconds) noexcept;

    /// Constante de tiempo de bajada, en segundos (> 0). Un valor que no es
    /// un número positivo y finito se ignora. La subida no cambia.
    void setFallSeconds(double seconds) noexcept;
    double fallSeconds() const noexcept { return fallSeconds_; }

    const std::vector<float>& levels() const noexcept { return levels_; }

private:
    std::vector<float> levels_;
    float floorDb_;
    double fallSeconds_ = kFallSeconds;
};

/// Picos mantenidos del analizador: el máximo que ha alcanzado cada punto del
/// espectro que se pinta (ya suavizado por SpectrumBallistics) desde el último
/// clear(). Se dibuja como una línea fina sobre el espectro.
///
/// No es segura entre hilos: la usa solo el hilo de la interfaz.
class SpectrumPeakHold
{
public:
    /// `points` niveles, todos en `floorDb` (sin picos).
    SpectrumPeakHold(std::size_t points, float floorDb);

    /// Sube cada pico hasta `levels` si lo supera. Devuelve true si alguno ha
    /// subido más de SpectrumBallistics::kRepaintDb (hay que repintar).
    bool update(const std::vector<float>& levels) noexcept;

    /// Borra los picos: todos al suelo.
    void clear() noexcept;

    /// true si algún punto está por encima del suelo (hay algo que dibujar).
    bool hasPeaks() const noexcept;

    const std::vector<float>& levels() const noexcept { return levels_; }

private:
    std::vector<float> levels_;
    float floorDb_;
};

/// Promedio del espectro en el que se buscan los picos (PLAN.md 5.3, ADR
/// 0019): media exponencial de la potencia de cada punto, igual al subir que
/// al bajar, con una constante de tiempo de kSeconds. El espectro que se
/// pinta sigue cada fotograma (una sola FFT, suavizada con
/// SpectrumBallistics) y en el ruido tiene un rizado de más de 6 dB; en este
/// promedio, un pico es lo que se mantiene: una resonancia, no el azar.
///
/// No es segura entre hilos: la usa solo el hilo de la interfaz.
class SpectrumAverage
{
public:
    static constexpr double kSeconds = 1.0;

    /// `points` niveles, sin datos (empty()).
    explicit SpectrumAverage(std::size_t points);

    /// Lleva el promedio hacia `levelsDb` (mismo tamaño; si no, no hace
    /// nada) tras `dtSeconds`. Sin datos, toma `levelsDb` tal cual. Los
    /// niveles no finitos cuentan como dsp::SpectrumAnalyzer::kFloorDb.
    void update(const std::vector<float>& levelsDb, double dtSeconds) noexcept;

    /// Olvida el promedio: el siguiente update() empieza de cero. Para
    /// cuando cambia el espectro que se promedia (modo, resolución o inclinación).
    void clear() noexcept;

    bool empty() const noexcept { return empty_; }

    /// Nivel promedio de cada punto, en dB (en el suelo si empty()).
    const std::vector<float>& levels() const noexcept { return levels_; }

private:
    std::vector<double> power_;
    std::vector<float> levels_;
    bool empty_ = true;
};

/// Ajustes del analizador en la interfaz (PLAN.md 5.2, ADR 0018). Los cuatro
/// primeros se guardan en las preferencias (core::UiSettings, mismos valores
/// por defecto y mismos valores válidos); `frozen` no se guarda: un proyecto
/// siempre abre con el analizador en marcha.
struct AnalyzerView
{
    int fftSize = 4096;           ///< Puntos de la FFT: uno de core::kAnalyzerFftSizes.
    int fallMs = 350;             ///< Constante de tiempo de bajada: una de core::kAnalyzerFallTimesMs.
    double tiltDbPerOctave = 4.5; ///< Inclinación: una de core::kAnalyzerTilts.
    bool peakHold = false;        ///< Mantener los picos.
    bool frozen = false;          ///< Congelado: el espectro no se actualiza.

    /// Igualdad exacta a propósito: la inclinación es uno de unos pocos valores exactos.
    // NOLINTNEXTLINE(clang-diagnostic-float-equal)
    friend bool operator==(const AnalyzerView&, const AnalyzerView&) = default;
};

/// Orden de la FFT (log2 de los puntos) de un tamaño guardado: 12, 13 o 14.
/// Un tamaño que no es uno de core::kAnalyzerFftSizes da 12 (4096 puntos).
int analyzerFftOrder(int fftSize) noexcept;

/// Constante de tiempo de bajada, en segundos, de una caída guardada en ms.
/// Una que no es una de core::kAnalyzerFallTimesMs da SpectrumBallistics::kFallSeconds.
double analyzerFallSeconds(int fallMs) noexcept;

/// Medidor de la dinámica de una banda: cuánto del rango se está usando,
/// de 0 (nada) a 1 (todo el rango). Vale para reducción y para realce: el
/// sentido lo da el signo del rango. Una ganancia de signo contrario da 0.
float dynamicMeterFraction(double dynamicGainDb, double rangeDb) noexcept;

} // namespace esceq::ui
