// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/fft.h"

#include <array>
#include <memory>
#include <span>
#include <vector>

/// Fase lineal: diseño de un FIR simétrico con la misma magnitud que las
/// bandas del ecualizador. Derivación, tamaños y límites en docs/dsp/fase-lineal.md.
namespace esceq::dsp
{

/// Modo de fase del ecualizador.
///
/// CONTRATO: los valores numéricos se guardan en el estado. Los modos nuevos
/// se añaden al final.
enum class PhaseMode
{
    Iir,          ///< Filtros IIR: desfase según la frecuencia, como un EQ analógico. Latencia 0.
    LinearLow,    ///< FIR de fase lineal corto: poca latencia, poca resolución en graves.
    LinearMedium, ///< FIR más largo: más precisión y más latencia. Para buses y mastering.
    LinearHigh,   ///< FIR más largo aún: máxima resolución en graves. Para mastering y render.
};

inline constexpr int kNumPhaseModes = 4;

constexpr bool isLinearPhase(PhaseMode mode) noexcept
{
    return mode != PhaseMode::Iir;
}

/// Tamaños del FIR y de la convolución de un modo.
struct LinearPhaseSize
{
    int firLength = 0; ///< Coeficientes del FIR (potencia de 2), simétricos alrededor de firLength/2.
    int blockSize = 0; ///< Bloque de la convolución particionada (potencia de 2).

    int partitions() const noexcept { return blockSize > 0 ? firLength / blockSize : 0; }
    /// Retardo total en muestras: el del FIR (su centro) más el de la convolución por bloques.
    int latency() const noexcept { return firLength > 0 ? firLength / 2 + blockSize : 0; }

    friend bool operator==(const LinearPhaseSize&, const LinearPhaseSize&) = default;
};

/// Por cuánto se multiplican los tamaños que dependen de la frecuencia de
/// muestreo (la fase lineal y la STFT de la dinámica espectral) para que la
/// resolución en hercios y la latencia en milisegundos no cambien: 1 hasta
/// 50 kHz, 2 hasta 100 kHz, 4 hasta 200 kHz y 8 por encima (o 1 si no es
/// positiva).
int sampleRateFactor(double sampleRate) noexcept;

/// Tamaños del modo a esa frecuencia de muestreo. IIR: {0, 0}.
///
/// Hasta 50 kHz: Low 2048 coeficientes y bloques de 256, Medium 8192 y 512,
/// High 32768 y 1024. Cada duplicación de la frecuencia de muestreo duplica
/// los dos, para que la resolución en hercios y la latencia en milisegundos
/// no cambien (hasta ×8, a partir de 400 kHz).
LinearPhaseSize linearPhaseSize(PhaseMode mode, double sampleRate) noexcept;

/// Latencia del modo en muestras (0 en IIR).
inline int latencySamples(PhaseMode mode, double sampleRate) noexcept
{
    return linearPhaseSize(mode, sampleRate).latency();
}

/// Efecto de un filtro de fase cero sobre los dos canales en una frecuencia:
///
///     L' = ll·L + lr·R
///     R' = rl·L + rr·R
struct ChannelMatrix
{
    double ll = 1.0, lr = 0.0, rl = 0.0, rr = 1.0;

    friend bool operator==(const ChannelMatrix&, const ChannelMatrix&) = default;
};

/// Matriz de una banda de magnitud `magnitude` en el canal `mode`, con las
/// mismas reglas que el ecualizador IIR: Left y Right no tocan el otro
/// canal; Mid filtra (L+R)/2 y Side (L−R)/2 sin tocar la otra componente.
ChannelMatrix channelMatrix(ChannelMode mode, double magnitude) noexcept;

/// `second` aplicada después de `first`.
ChannelMatrix operator*(const ChannelMatrix& second, const ChannelMatrix& first) noexcept;

/// Un FIR de fase lineal ya diseñado, en el tiempo.
struct LinearPhaseFir
{
    LinearPhaseSize size{};
    int numChannels = 2;
    /// firLength coeficientes cada uno, con h[0] = 0 y h[c + n] = h[c − n]
    /// para c = firLength/2: fase lineal exacta con retardo c. En mono solo
    /// se usa `ll`. `lr` y `rl` quedan vacíos si las bandas no mezclan los
    /// canales (ninguna es Mid ni Side); `rr` queda vacío si es igual a `ll`.
    std::vector<double> ll, lr, rl, rr;
    /// Ninguna banda cambia la señal: el FIR es un retardo puro.
    bool identity = true;
};

/// Diseña FIR de fase lineal a partir de los ajustes de las bandas.
///
/// Método: muestreo en frecuencia de la magnitud exacta de las bandas (la
/// misma que dibuja la interfaz) en una rejilla de 2·firLength puntos, FFT
/// inversa y ventana de Kaiser (β = kKaiserBeta) de firLength − 1 puntos.
///
/// Solo cuenta la parte estática de las bandas (su ganancia, sin la dinámica).
/// Las bandas desactivadas no cuentan.
///
/// Guarda la magnitud de cada banda: al mover una sola, solo se recalcula
/// esa. No es segura en tiempo real (asigna memoria; de 1 a 30 ms según el
/// modo) ni entre hilos.
class LinearPhaseDesigner
{
public:
    static constexpr double kKaiserBeta = 8.0;

    LinearPhaseDesigner();
    ~LinearPhaseDesigner();
    LinearPhaseDesigner(const LinearPhaseDesigner&) = delete;
    LinearPhaseDesigner& operator=(const LinearPhaseDesigner&) = delete;

    /// FIR para `bands` (como mucho kMaxBands; el orden es el de las bandas
    /// en serie). `numChannels`: 1 o 2. Para IIR devuelve un FIR vacío.
    LinearPhaseFir design(std::span<const BandSettings> bands, PhaseMode mode, double sampleRate, int numChannels);

    /// Respuesta que el diseño intenta reproducir, en `frequencyHz`: la
    /// composición de las magnitudes exactas de las bandas. En mono solo
    /// cuenta `ll`: el producto de las bandas Stereo, Left y Mid, que son las
    /// que procesan el único canal (como en el ecualizador IIR).
    static ChannelMatrix targetResponse(std::span<const BandSettings> bands, double frequencyHz, double sampleRate,
                                        int numChannels = 2);

private:
    struct Cache;
    std::unique_ptr<Cache> cache_;
};

} // namespace esceq::dsp
