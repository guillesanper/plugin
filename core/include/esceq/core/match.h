// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/core/spectrum_session.h"
#include "esceq/dsp/curve_match.h"
#include "esceq/dsp/realtime.h"
#include "esceq/dsp/spsc_fifo.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

/// Igualar a una referencia (PLAN.md 5.6, ADR 0023): lo que el procesador
/// guarda mientras el plugin está abierto. Lo aprendido (MatchLearner), los
/// ajustes del panel (MatchSettings), la referencia elegida (MatchReference),
/// el hilo que calcula la propuesta (MatchWorker) y la cola del sidechain
/// (MatchSidechainTap). El ajuste en sí es dsp::matchCurve
/// (docs/dsp/igualacion.md); cómo se aprende y se aplica, ui/logic/match.h.
///
/// Nada de esto se guarda con el proyecto: los espectros no se pueden rehacer
/// sin el audio, y los identificadores de instancia cambian en cada sesión.
namespace esceq::core
{

/// Segundos útiles (con señal en las dos) antes de poder aplicar: con 5, los
/// graves de una medida de ruido fluctúan ±1,3 dB (docs/dsp/igualacion.md).
inline constexpr double kMatchRequiredSeconds = 10.0;

/// Con qué se iguala.
struct MatchReference
{
    enum class Kind
    {
        None,      ///< Nada elegido.
        Instance,  ///< Otra instancia de esceQ de la sesión de espectros: su salida.
        Sidechain, ///< La entrada de sidechain de esta instancia.
    };

    Kind kind = Kind::None;
    InstanceId instance = 0; ///< Solo con Kind::Instance.

    static MatchReference none() noexcept { return {}; }
    static MatchReference ofInstance(InstanceId id) noexcept { return {Kind::Instance, id}; }
    static MatchReference sidechain() noexcept { return {Kind::Sidechain, 0}; }

    friend bool operator==(const MatchReference&, const MatchReference&) = default;
};

/// Suavizado de los espectros (1/12 de octava no se ofrece: en la rejilla de
/// la sesión es no suavizar, docs/dsp/igualacion.md).
enum class MatchSmoothing
{
    SixthOctave,
    ThirdOctave,
    Octave,
};

/// Ancho del suavizado en octavas.
double smoothingOctaves(MatchSmoothing smoothing) noexcept;

/// Ajustes del panel. Viven con lo aprendido, en el procesador.
struct MatchSettings
{
    static constexpr double kMinHz = kSessionMinHz;
    static constexpr double kMaxHz = kSessionMaxHz;
    /// El rango abarca al menos esto, en octavas.
    static constexpr double kMinRangeOctaves = 1.0 / 3.0;

    /// Fracción de la diferencia que se iguala, de 0 a 1 (0 % a 100 %).
    double amount = 0.5;
    MatchSmoothing smoothing = MatchSmoothing::ThirdOctave;
    /// Bandas que se pueden crear como mucho, de 1 a kNumBands. Se usan
    /// como mucho las libres (effectiveMaxBands).
    int maxBands = 8;
    /// Rango que se iguala, en Hz.
    double lowHz = 30.0;
    double highHz = 16000.0;

    friend bool operator==(const MatchSettings&, const MatchSettings&) = default;
};

/// Ajustes válidos: la cantidad entre 0 y 1, las bandas entre 1 y
/// kNumBands, y el rango dentro de 20 Hz a 20 kHz, de al menos
/// kMinRangeOctaves (si no, se sube el máximo o, en el tope, se baja el
/// mínimo). Lo no finito toma el valor por defecto.
MatchSettings sanitize(const MatchSettings& settings) noexcept;

/// Bandas que puede crear el ajuste: las pedidas, como mucho las libres (0 si no hay ninguna libre).
int effectiveMaxBands(const MatchSettings& settings, int freeBands) noexcept;

/// Los ajustes de dsp::matchCurve, con como mucho `freeBands` bandas.
dsp::CurveMatchSettings toCurveMatchSettings(const MatchSettings& settings, int freeBands) noexcept;

/// Lo que se observa en un instante: los espectros promediados de la pista y
/// de la referencia (en dB, en la rejilla de la sesión, medidos igual), si
/// cada uno tiene señal, y cuánto tiempo representa.
struct MatchObservation
{
    std::span<const float> trackDb;
    std::span<const float> referenceDb;
    bool trackHasSignal = false;
    bool referenceHasSignal = false;
    /// Frecuencias de muestreo de los dos análisis (0 si no se sabe).
    double trackSampleRate = 0.0;
    double referenceSampleRate = 0.0;
    /// Tiempo desde la observación anterior que cuenta: lo que avanza la referencia.
    double dtSeconds = 0.0;
};

/// Aprendizaje: la media a largo plazo, en potencia, de los espectros de la
/// pista y de la referencia, ponderada por el tiempo y solo en los instantes
/// en que las dos tienen señal. Como el promedio exponencial de la sesión es
/// lineal en potencia, la media de lo que publica es la media de la señal.
///
/// Sin olvido: cuanto más tiempo, más estable la medida. No es de tiempo
/// real ni segura entre hilos: la usa el hilo de mensajes.
class MatchLearner
{
public:
    /// Una observación nunca pesa más que esto, aunque haya pasado más tiempo
    /// (la interfaz se ha parado un rato): sería ponderar de más un solo espectro.
    static constexpr double kMaxStepSeconds = 0.25;

    /// Con `points` puntos (los de la rejilla de la sesión).
    explicit MatchLearner(std::size_t points = static_cast<std::size_t>(kSessionPoints));

    /// Suma una observación si las dos tienen señal, los dos espectros tienen
    /// `points` puntos finitos y el tiempo es positivo (y finito). true si cuenta.
    bool add(const MatchObservation& observation);

    /// Olvida todo lo aprendido.
    void reset() noexcept;

    /// Segundos útiles: el tiempo que ha contado.
    double usefulSeconds() const noexcept { return seconds_; }
    /// Holgura de los segundos sumados (200 pasos de 0,05 s no suman 10 exactos en binario).
    static constexpr double kSecondsTolerance = 1.0e-6;

    /// true con kMatchRequiredSeconds útiles.
    bool ready() const noexcept { return seconds_ + kSecondsTolerance >= kMatchRequiredSeconds; }
    /// true si aún no ha contado nada.
    bool empty() const noexcept { return !(seconds_ > 0.0); }

    /// Espectros medios en dB (kSessionFloorDb sin nada aprendido).
    std::vector<float> trackDb() const;
    std::vector<float> referenceDb() const;

    /// Separación entre bins de la FFT de los análisis: la mayor frecuencia de
    /// muestreo vista entre 8192 (la de la pista y la de la referencia pueden
    /// no coincidir), o 0 si no se sabe.
    double resolutionHz() const noexcept;

    /// Cambia cada vez que cambia lo aprendido (al contar y al olvidar).
    std::uint64_t revision() const noexcept { return revision_; }

private:
    std::vector<float> average(const std::vector<double>& power) const;

    std::vector<double> trackPower_;     ///< Σ dt · 10^(dB/10).
    std::vector<double> referencePower_; ///< Ídem.
    double seconds_ = 0.0;
    double maxSampleRate_ = 0.0;
    std::uint64_t revision_ = 0;
};

/// Lo que el procesador guarda de la igualación mientras está abierto (como
/// la selección de instancias): la referencia, los ajustes y lo aprendido.
/// Solo desde el hilo de mensajes.
struct MatchState
{
    MatchReference reference;
    MatchSettings settings;
    MatchLearner learner;
    /// true mientras se aprende (entre Learn y Stop).
    bool learning = false;
};

/// Una petición de ajuste: todo lo que necesita dsp::matchCurve, copiado.
struct MatchRequest
{
    std::vector<double> frequencies;
    std::vector<float> trackDb;
    std::vector<float> referenceDb;
    double resolutionHz = 0.0;
    std::vector<dsp::BandSettings> currentBands;
    int numChannels = 2;
    double sampleRate = 48000.0;
    dsp::FilterAlgorithm algorithm = dsp::FilterAlgorithm::Classic;
    dsp::CurveMatchSettings settings;

    /// La entrada de dsp::matchCurve, que apunta a esta petición.
    dsp::CurveMatchInput input() const noexcept;
};

/// El resultado de una petición.
struct MatchProposal
{
    /// El número que devolvió MatchWorker::submit().
    std::uint64_t id = 0;
    dsp::CurveMatchResult result;
};

/// El hilo del ajuste de una instancia: recibe peticiones, las calcula con
/// dsp::matchCurve y publica la última propuesta. No es el hilo de control,
/// que no puede retrasar segundos un FIR ni la compensación de ganancia
/// (ADR 0021): con 24 bandas, el ajuste tarda hasta 1 s.
///
/// - El hilo se crea con la primera petición (sin uso, no hay hilo) y se para
///   con el objeto.
/// - Una petición nueva sustituye a la pendiente y cancela la que se esté
///   calculando, que vuelve tras su iteración en curso sin publicarse.
/// - Solo se publican las que terminan sin cancelar.
///
/// submit(), cancel(), latest(), busy() y lastSubmitted() se pueden llamar
/// desde cualquier hilo salvo el de audio.
class MatchWorker
{
public:
    enum class Threading
    {
        Background, ///< Un hilo propio (lo normal).
        Manual,     ///< Sin hilo: solo runPending() calcula (tests).
    };

    /// Avisos, para los tests y para quien quiera saber cuándo hay algo
    /// nuevo. Se llaman en el hilo que calcula, sin cerrojos tomados, y
    /// pueden llamar a submit() y cancel(). `started`, justo antes de
    /// empezar una petición; `finished`, al terminarla, cancelada o no.
    struct Observer
    {
        std::function<void(std::uint64_t id)> started;
        std::function<void(const MatchProposal& proposal)> finished;
    };

    explicit MatchWorker(Threading threading = Threading::Background, Observer observer = {});
    /// Cancela lo que haya y espera a que vuelva el hilo (tras la iteración en curso).
    ~MatchWorker();

    MatchWorker(const MatchWorker&) = delete;
    MatchWorker& operator=(const MatchWorker&) = delete;

    /// Pide un ajuste. Devuelve su número (1, 2...), que sube con cada petición.
    std::uint64_t submit(MatchRequest request);

    /// Olvida la petición pendiente y cancela la que se esté calculando. La
    /// última propuesta publicada se conserva.
    void cancel();

    /// La última propuesta terminada sin cancelar, o nula si no hay ninguna.
    std::shared_ptr<const MatchProposal> latest() const;

    /// true si hay una petición pendiente o calculándose.
    bool busy() const;

    /// Número de la última petición (0 si no ha habido ninguna).
    std::uint64_t lastSubmitted() const;

    /// true desde que el objeto empieza a destruirse.
    bool stopping() const noexcept { return stop_.load(std::memory_order_acquire); }

    /// Con Threading::Manual, calcula en el hilo que llama las peticiones
    /// pendientes hasta que no quede ninguna. Devuelve cuántas ha empezado.
    /// Con Threading::Background no hace nada.
    int runPending();

private:
    /// Calcula la petición pendiente, si la hay. false si no había ninguna.
    bool runOne();
    void run();

    const Threading threading_;
    const Observer observer_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<MatchRequest> pending_; ///< Con mutex_.
    std::uint64_t pendingId_ = 0;         ///< Con mutex_.
    std::uint64_t runningId_ = 0;         ///< Con mutex_; 0 si no calcula nada.
    std::uint64_t nextId_ = 1;            ///< Con mutex_.
    std::shared_ptr<const MatchProposal> latest_; ///< Con mutex_.
    /// La cancelación de la petición que se calcula. Cada petición tiene la
    /// suya, para que cancelar una no cancele la siguiente.
    std::shared_ptr<std::atomic<bool>> cancel_; ///< El puntero, con mutex_.
    std::atomic<bool> stop_{false};
    std::thread thread_; ///< Se crea con la primera petición, con mutex_.
};

/// La cola del sidechain para aprender de él: el hilo de audio escribe la
/// mezcla mono del sidechain solo mientras está encendida (un atómico que se
/// lee una vez por bloque), y la interfaz la lee. Apagada, write() solo lee
/// el atómico: processBlock no cambia.
class MatchSidechainTap
{
public:
    /// Más de 150 ms a 192 kHz, de sobra para los 33 ms entre dos refrescos de la interfaz.
    static constexpr int kCapacity = 1 << 15;

    /// Enciende o apaga la cola. Al encenderla se vacía lo que quedara de la
    /// vez anterior. Desde el hilo del lector (mensajes), nunca el de audio.
    void setEnabled(bool on) noexcept;
    bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }

    /// Hilo de audio: si está encendida y hay sidechain, escribe su mezcla
    /// mono; si la cola está llena, descarta. Nunca bloquea ni asigna.
    void write(const float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING;

    /// Lector único: la interfaz que aprende. Lo mismo que dsp::SpscFifo::read.
    int read(float* destination, int maxSamples) noexcept { return fifo_.read(destination, maxSamples); }

private:
    std::atomic<bool> enabled_{false};
    dsp::SpscFifo fifo_{kCapacity};
};

} // namespace esceq::core
