// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"
#include "esceq/dsp/band_filter.h"
#include "esceq/dsp/dynamics.h"
#include "esceq/dsp/filter_design.h"

#include <array>

namespace esceq::dsp {

inline constexpr int kMaxBands = 24;

/// Parte de la señal estéreo que procesa una banda.
///
/// CONTRATO: los valores numéricos se guardan en el estado. Los modos
/// nuevos se añaden al final.
enum class ChannelMode
{
    Stereo, ///< Los dos canales, con el mismo filtro.
    Left,   ///< Solo el canal izquierdo.
    Right,  ///< Solo el canal derecho.
    Mid,    ///< Solo la suma (L+R)/2; la diferencia no se toca.
    Side,   ///< Solo la diferencia (L−R)/2; la suma no se toca.
};

/// Ajustes de una banda del ecualizador.
struct BandSettings
{
    bool enabled = false;
    FilterParams filter{};
    DynamicSettings dynamics{};
    ChannelMode channel = ChannelMode::Stereo;

    friend bool operator==(const BandSettings&, const BandSettings&) = default;
};

/// Los ajustes tal y como los usa el ecualizador: los no finitos toman su
/// valor por defecto y el resto se ajusta a los rangos de designBand().
/// Seguro en tiempo real.
BandSettings sanitizeBand(BandSettings settings) noexcept ESCEQ_NONBLOCKING;

/// Ecualizador de hasta kMaxBands bandas en serie, con transiciones sin
/// chasquidos.
///
/// Cómo trata los cambios de parámetros (ver docs/dsp/ecualizador.md):
///  - Frecuencia, ganancia y Q se suavizan (en escala logarítmica o en dB)
///    y los coeficientes se recalculan cada kChunkSize muestras.
///  - Activar o desactivar una banda, o cambiar su tipo o su pendiente, hace
///    un fundido de kCrossfadeSeconds entre el filtro anterior y el nuevo.
///  - Cambiar el canal de una banda (ChannelMode) o su algoritmo de filtro
///    (FilterAlgorithm) hace el mismo fundido.
///  - Un cambio de esos que llega durante un fundido espera a que termine.
///  - El bypass hace un fundido entre la señal seca y la procesada.
///  - Umbral, rango y rodilla de la dinámica se suavizan con la misma
///    constante de tiempo, pero muestra a muestra; activar o desactivar la
///    dinámica hace un fundido de su efecto.
///
/// Dinámica (ver docs/dsp/dinamica.md): en las bandas con la dinámica
/// activada, un detector escucha la entrada del ecualizador (o el sidechain
/// externo) a través de un filtro centrado en la banda, y la ganancia
/// dinámica resultante se suma a la ganancia estática. Se evalúa en cada
/// muestra; los coeficientes se recalculan cuando cambia al menos
/// kDynamicGainResolutionDb. Con los tiempos automáticos
/// (DynamicSettings::autoTiming), ataque y relajación salen de la frecuencia
/// de la banda que suena (autoEnvelopeTimes()) y se recalculan en cada tramo
/// de kChunkSize muestras.
///
/// Canal (ver docs/dsp/ecualizador.md): cada banda procesa los dos canales,
/// solo el izquierdo, solo el derecho, solo la suma (Mid) o solo la
/// diferencia (Side). La parte que la banda no procesa pasa bit a bit, y el
/// detector de la dinámica escucha esa misma parte de la señal. Con audio
/// mono, Stereo, Left y Mid procesan el único canal; Right y Side no hacen nada.
///
/// Garantías:
///  - process() y setBand() son seguras en tiempo real.
///  - Con parámetros constantes, el resultado no depende de cómo se trocee
///    el audio en bloques.
///  - Con todas las bandas desactivadas el audio pasa bit a bit.
///  - Un NaN o un infinito en la entrada no deja el ecualizador estropeado:
///    se recupera en el bloque siguiente (ver process()).
///  - No es segura entre hilos: todas las llamadas deben venir del mismo hilo.
class Equalizer
{
public:
    static constexpr int kChunkSize = 32;
    static constexpr double kSmoothingSeconds = 0.015;
    static constexpr double kCrossfadeSeconds = 0.01;
    /// La ganancia dinámica se evalúa en cada muestra, pero los coeficientes
    /// solo se recalculan cuando cambia al menos esto (o vuelve a 0 dB).
    static constexpr double kDynamicGainResolutionDb = 1.0e-3;

    /// Fija la frecuencia de muestreo y salta sin transición a los ajustes actuales.
    void prepare(double sampleRate) noexcept;

    /// Nuevos ajustes objetivo para una banda. Índices fuera de rango se ignoran.
    void setBand(int index, const BandSettings& settings) noexcept ESCEQ_NONBLOCKING;

    void setBypassed(bool bypassed) noexcept ESCEQ_NONBLOCKING;

    /// Modo "solo dinámica", para la fase lineal (ver docs/dsp/fase-lineal.md):
    /// la parte estática de las bandas la aplica otro filtro, y aquí cada banda
    /// aplica solo el cambio que añade su dinámica,
    ///
    ///     H(g + d) · H(−g)      (g: ganancia estática, d: dinámica)
    ///
    /// que es exactamente la unidad cuando d = 0, porque en los tipos con
    /// ganancia H(−g) = 1/H(g). Las bandas sin dinámica activa no se procesan.
    /// Cambiarlo salta sin transición a los ajustes nuevos, como reset().
    void setDynamicsOnly(bool dynamicsOnly) noexcept ESCEQ_NONBLOCKING;
    bool dynamicsOnly() const noexcept { return dynamicsOnly_; }

    /// Borra la memoria de los filtros y salta sin transición a los ajustes objetivo.
    void reset() noexcept ESCEQ_NONBLOCKING;

    /// Procesa en el sitio. Como mucho kMaxChannels canales; el resto no se toca.
    ///
    /// `sidechain` (opcional, solo lectura) es la señal que escuchan las bandas
    /// con detección externa, con `numSidechainChannels` canales y las mismas
    /// `numSamples` muestras. Sin sidechain, esas bandas no reciben señal y su
    /// ganancia dinámica vuelve a 0 dB.
    ///
    /// `detectorInput` (opcional, solo lectura, `numChannels` canales) es lo que
    /// escuchan los detectores internos en lugar de `channels`. La fase lineal
    /// lo usa para que escuchen la entrada del ecualizador completo, retrasada
    /// como el audio.
    ///
    /// Valores no finitos: si la salida tiene algún NaN o infinito (porque lo
    /// traía la entrada), el bloque sale en silencio y todos los filtros se
    /// reinician, como tras reset(). Si lo trae el sidechain, se reinicia el
    /// detector de las bandas afectadas. Con entrada finita no hace nada.
    void process(float* const* channels, int numChannels, int numSamples,
                 const float* const* sidechain = nullptr, int numSidechainChannels = 0,
                 const float* const* detectorInput = nullptr) noexcept ESCEQ_NONBLOCKING;

    /// Ajustes objetivo (los últimos recibidos, ya saneados; en el modo solo
    /// dinámica, desactivados si la banda no tiene la dinámica activa).
    const BandSettings& target(int index) const noexcept;

    /// true si no hay ningún suavizado ni fundido en curso. La ganancia
    /// dinámica puede seguir moviéndose: depende de la señal, no de los ajustes.
    bool isSettled() const noexcept;

    /// Ganancia dinámica que se aplica ahora mismo a la banda, en dB (0 si
    /// la banda no es dinámica). Para mostrarla en la interfaz: leerla desde
    /// el hilo de audio, tras process(), y publicarla por un canal seguro.
    double dynamicGainDb(int index) const noexcept;

private:
    struct Band
    {
        BandSettings requested{}; ///< Lo último recibido en setBand(), saneado.
        BandSettings target{};
        BandSettings current{}; ///< Valores que suenan ahora mismo.
        BandFilter active;
        BandFilter previous;    ///< Configuración anterior durante un fundido.
        ChannelMode previousChannel = ChannelMode::Stereo; ///< Canal de `previous`.
        int fadeRemaining = 0;
        /// Hay un cambio estructural en `target` esperando a que acabe el
        /// fundido en curso: empezar otro a medias haría un salto.
        bool structuralPending = false;

        // Dinámica
        FilterParams designed{};  ///< Parámetros efectivos (con la ganancia dinámica) del diseño activo.
        double designedStaticGain = 0.0; ///< Ganancia estática del diseño activo (modo solo dinámica).
        double preparedFor = -1.0;       ///< Frecuencia para la que se calculó `prepared` (-1: ninguna).
        PreparedFrequency prepared{};    ///< De esa frecuencia: la dinámica solo cambia la ganancia.
        BandFilter detector;             ///< Filtro del detector.
        FilterParams detectorFor{};
        EnvelopeFollower envelope;
        bool dynamicRunning = false;
        double dynamicMix = 0.0;  ///< 0..1: fundido al activar o desactivar la dinámica.
        double dynamicGain = 0.0; ///< dB que se suman ahora mismo a la ganancia estática.
        double kneeStartLinear = 0.0; ///< Nivel lineal donde empieza la rodilla: por debajo, ganancia 0.
    };

    /// Señales que escuchan los detectores, y posición del tramo en ellas.
    struct Sources
    {
        const float* const* sidechain = nullptr;
        int numSidechainChannels = 0;
        const float* const* detector = nullptr; ///< nullptr: la entrada de cada tramo.
        int offset = 0;
    };

    void processChunk(float* const* channels, int numChannels, int offset, int length,
                      const Sources& sources) noexcept;
    void processBand(Band& band, float* const* channels, int numChannels, int length,
                     const Sources& sources) noexcept;
    void processSegment(Band& band, float* const* channels, int numChannels, int length) noexcept;
    /// Aplica `filter` a la parte de la señal que indica `mode`.
    void processRouted(BandFilter& filter, ChannelMode mode, float* const* channels, int numChannels,
                       int length) noexcept;
    void runDetector(Band& band, int numChannels, int length, const Sources& sources) noexcept;
    /// Avanza la ganancia dinámica una muestra. true si hay que recalcular el diseño.
    bool updateDynamicGain(Band& band, double envelope) noexcept;
    static void updateKneeStart(Band& band) noexcept;
    void applyDesign(Band& band, bool force = false) noexcept;
    void smoothBand(Band& band) noexcept;
    /// Empieza el fundido hacia los ajustes de `target` (activar, tipo, pendiente, canal).
    void beginStructuralChange(Band& band) noexcept;
    void snapBand(Band& band) noexcept;
    static bool isDynamic(const Band& band) noexcept;
    /// Lo que debe sonar en la banda con estos ajustes, según el modo.
    BandSettings targetFor(const BandSettings& requested) const noexcept;

    std::array<Band, kMaxBands> bands_{};
    double sampleRate_ = 48000.0;
    double smoothingCoeff_ = 0.0;       ///< Por tramo de kChunkSize muestras.
    double sampleSmoothingCoeff_ = 0.0; ///< Por muestra (umbral, rango y rodilla).
    int fadeLength_ = 480;
    int chunkPhase_ = 0;

    bool bypassed_ = false;
    bool dynamicsOnly_ = false;
    float wet_ = 1.0f;

    std::array<std::array<float, kChunkSize>, kMaxChannels> scratch_{};
    std::array<std::array<float, kChunkSize>, kMaxChannels> dry_{};
    std::array<std::array<float, kChunkSize>, kMaxChannels> detectorInput_{}; ///< Entrada del ecualizador.
    std::array<std::array<float, kChunkSize>, kMaxChannels> detectorScratch_{};
    std::array<double, kChunkSize> envelopeBuffer_{};
    std::array<float, kChunkSize> routeBuffer_{}; ///< Suma o diferencia de canales (Mid/Side).
    std::array<float, kChunkSize> routeDry_{};
};

} // namespace esceq::dsp
