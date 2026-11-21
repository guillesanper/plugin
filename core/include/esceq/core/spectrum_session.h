// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/dsp/realtime.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

/// Espectros compartidos entre las instancias de esceQ de una sesión.
///
/// Cada instancia se apunta a la sesión (join) y le pasa su audio de salida
/// desde el hilo de audio (Member::feed). Un trabajador calcula el espectro
/// promediado de cada una en una rejilla común, y cualquier interfaz puede
/// leer los de todas (snapshot) para superponerlos y detectar colisiones.
///
/// "Sesión" es el proceso: solo se ven las instancias que el host carga en el
/// mismo proceso y con el mismo binario (las VST3 entre sí, las CLAP entre sí).
/// Ver docs/decisiones/0011-espectros-compartidos.md.
///
/// Coste: mientras nadie mira (ningún Viewer vivo), feed() solo lee un atómico
/// y el trabajador duerme. No hay nada que procesar si no se usa.
namespace esceq::core
{

/// Identificador de una instancia dentro del proceso. Nunca se reutiliza.
/// No se guarda en el estado: cambia cada vez que se abre el proyecto.
using InstanceId = std::uint64_t;

/// Rejilla común de los espectros de la sesión: kSessionPoints frecuencias
/// logarítmicas de kSessionMinHz a kSessionMaxHz.
inline constexpr int kSessionPoints = 192;
inline constexpr double kSessionMinHz = 20.0;
inline constexpr double kSessionMaxHz = 20000.0;
const std::vector<double>& sessionFrequencies();

/// Nivel de un punto sin señal.
inline constexpr float kSessionFloorDb = -120.0f;
/// Un espectro con algún punto por encima de esto tiene señal (es el fondo de
/// la escala del analizador de la interfaz). La igualación usa el mismo umbral.
inline constexpr float kSessionSignalDb = -90.0f;

/// Lo que se sabe de una instancia en un momento dado.
struct InstanceSpectrum
{
    InstanceId id = 0;
    /// Número de la instancia (1, 2...): el menor libre al apuntarse. Sirve de
    /// nombre si el host no dice el de la pista.
    int number = 0;
    /// Nombre de la pista según el host, o vacío.
    std::string trackName;
    /// Color de la pista según el host (0xAARRGGBB), si lo dice.
    std::optional<std::uint32_t> trackColour;
    /// true si ha llegado audio con nivel visible hace poco.
    bool hasSignal = false;
    /// Nivel en dB en cada punto de sessionFrequencies(), con la inclinación
    /// del analizador y promediado en el tiempo.
    std::vector<float> levelsDb;
    /// Frecuencia de muestreo del audio que se analiza (0 si aún no se sabe).
    double sampleRate = 0.0;
    /// Segundos de audio analizados desde que alguien empezó a mirar: avanza
    /// en cada paso del trabajador en que llega audio de la instancia, lo que
    /// dura ese paso, y se para con el transporte. Cuánto ha cambiado entre
    /// dos copias dice cuánto tiempo representa el espectro nuevo (la
    /// igualación pondera por él, core/match.h).
    double audioSeconds = 0.0;

    /// Nombre para mostrar: el de la pista o "esceQ <número>".
    std::string displayName() const;
};

/// Instancias que una interfaz superpone a la suya, y si marca las colisiones.
/// No se guarda con el proyecto: los identificadores cambian en cada sesión.
struct CompareSelection
{
    std::vector<InstanceId> shown;
    bool showCollisions = true;

    bool contains(InstanceId id) const noexcept;
    /// La añade si no estaba; la quita si estaba.
    void toggle(InstanceId id);
    /// Quita las que ya no están en `instances`. true si ha quitado alguna.
    bool retainExisting(std::span<const InstanceSpectrum> instances);

    friend bool operator==(const CompareSelection&, const CompareSelection&) = default;
};

/// Se crea siempre con create() o shared(): join() y watch() necesitan que la
/// sesión esté en un std::shared_ptr, porque los Member y los Viewer la mantienen viva.
class SpectrumSession : public std::enable_shared_from_this<SpectrumSession>
{
    struct Key
    {
        explicit Key() = default;
    };

public:
    enum class Threading
    {
        Background, ///< Un hilo propio actualiza los espectros (lo normal).
        Manual,     ///< Solo update() los actualiza (tests).
    };

    /// Cada cuánto actualiza el trabajador, y la constante de tiempo del promedio.
    static constexpr double kUpdateSeconds = 1.0 / 20.0;
    static constexpr double kAverageSeconds = 0.4;
    /// Sin audio con nivel visible durante este tiempo, una instancia deja de
    /// tener señal y su espectro empieza a apagarse.
    static constexpr double kSignalHoldSeconds = 0.5;
    /// Inclinación de los espectros, la misma que la del analizador de la interfaz.
    static constexpr double kTiltDbPerOctave = 4.5;
    /// Orden de la FFT: 8192 puntos (5,9 Hz por bin a 48 kHz), para separar graves.
    static constexpr int kFftOrder = 13;

    /// La sesión del proceso. Se crea con la primera instancia que la pide y se
    /// destruye con la última que la suelta (no en la descarga del binario,
    /// donde parar un hilo puede bloquear el proceso). Segura entre hilos.
    static std::shared_ptr<SpectrumSession> shared();

    /// Una sesión independiente de la del proceso (tests).
    static std::shared_ptr<SpectrumSession> create(Threading threading);

    SpectrumSession(Key, Threading threading);
    ~SpectrumSession();

    SpectrumSession(const SpectrumSession&) = delete;
    SpectrumSession& operator=(const SpectrumSession&) = delete;

    class Member;
    class Viewer;

    /// Apunta una instancia. Se borra de la sesión al destruir el Member.
    /// Asigna memoria: no desde el hilo de audio.
    std::unique_ptr<Member> join();

    /// Empieza a mirar: mientras viva algún Viewer, las instancias publican su
    /// espectro. Desde cualquier hilo salvo el de audio.
    Viewer watch();

    /// true si alguien mira. Desde cualquier hilo.
    bool watched() const noexcept { return viewers_.load(std::memory_order_relaxed) > 0; }

    /// Copia el estado de todas las instancias, en el orden en que se apuntaron.
    /// Reutiliza la memoria de `out`. Desde cualquier hilo salvo el de audio.
    void snapshot(std::vector<InstanceSpectrum>& out) const;

    /// Cambia cada vez que cambia algo de lo que devuelve snapshot().
    std::uint64_t revision() const noexcept { return revision_.load(std::memory_order_acquire); }

    /// Un paso del trabajador: lee el audio nuevo de cada instancia y actualiza
    /// su espectro como si hubieran pasado `dtSeconds`. Con Threading::Manual
    /// es la única forma de que avance. No hace nada si nadie mira.
    /// Solo un hilo a la vez (el trabajador, o el test).
    void update(double dtSeconds);

    /// Número de instancias apuntadas.
    std::size_t size() const;

private:
    struct Slot;

    void leave(const std::shared_ptr<Slot>& slot);
    void addViewer() noexcept;
    void removeViewer() noexcept;
    void run();
    void bumpRevision() noexcept { revision_.fetch_add(1, std::memory_order_acq_rel); }

    mutable std::mutex mutex_; ///< Protege slots_ y los datos publicados de cada Slot.
    std::vector<std::shared_ptr<Slot>> slots_;
    InstanceId nextId_ = 1;

    std::atomic<int> viewers_{0};
    std::atomic<std::uint64_t> revision_{0};
    /// Sube al empezar a mirar: el trabajador descarta el audio que quedara
    /// en las colas de la vez anterior.
    std::atomic<std::uint64_t> watchGeneration_{0};
    std::uint64_t seenGeneration_ = 0; ///< Solo el hilo que llama a update().

    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool stopping_ = false; ///< Protegido por wakeMutex_.
    std::thread worker_;
};

/// Una instancia apuntada a la sesión. Mantiene viva la sesión.
class SpectrumSession::Member
{
public:
    ~Member();
    Member(const Member&) = delete;
    Member& operator=(const Member&) = delete;

    InstanceId id() const noexcept;

    /// Frecuencia de muestreo del audio de feed(). Desde cualquier hilo.
    void setSampleRate(double sampleRate) noexcept;

    /// Nombre y color de la pista según el host. Desde cualquier hilo salvo el de audio.
    void setTrackName(std::string name);
    void setTrackColour(std::optional<std::uint32_t> argb);

    /// Hilo de audio: entrega un bloque (se mezcla a mono). Si nadie mira, no
    /// hace nada; si la cola está llena, descarta. Nunca bloquea ni asigna.
    void feed(const float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING;

private:
    friend class SpectrumSession;
    Member(std::shared_ptr<SpectrumSession> session, std::shared_ptr<Slot> slot);

    std::shared_ptr<SpectrumSession> session_;
    std::shared_ptr<Slot> slot_;
};

/// Testigo de que alguien mira. Mueve, no copia. Mantiene viva la sesión.
class SpectrumSession::Viewer
{
public:
    Viewer() = default;
    ~Viewer() { release(); }
    Viewer(Viewer&& other) noexcept : session_(std::move(other.session_)) {}
    Viewer& operator=(Viewer&& other) noexcept;
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    bool active() const noexcept { return session_ != nullptr; }
    /// Deja de mirar (lo mismo que destruirlo).
    void release() noexcept;

private:
    friend class SpectrumSession;
    explicit Viewer(std::shared_ptr<SpectrumSession> session) : session_(std::move(session)) {}

    std::shared_ptr<SpectrumSession> session_;
};

} // namespace esceq::core
