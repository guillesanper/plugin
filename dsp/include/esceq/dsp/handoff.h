// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

/// Intercambio de datos entre el hilo de audio y un hilo que no es de tiempo
/// real, sin bloqueos y sin que el hilo de audio asigne ni libere memoria.
///
///  - TripleBuffer: el hilo de audio publica un valor (por ejemplo, los
///    ajustes que hay que diseñar) y otro hilo lee siempre el último.
///  - Handoff: otro hilo construye objetos (por ejemplo, un filtro ya
///    diseñado) y se los entrega al hilo de audio, que se los devuelve cuando
///    ya no los usa para que se destruyan fuera de él.
///
/// Los dos son para exactamente un hilo en cada lado.
namespace esceq::dsp
{

/// Último valor publicado por un escritor, para un lector. Tres copias de T:
/// el escritor nunca espera al lector ni al revés, y el lector solo ve
/// valores completos. Los valores intermedios que el lector no llega a leer
/// se pierden: solo importa el último.
///
/// T debe poder copiarse sin asignar memoria si se publica desde el hilo de audio.
template <typename T> class TripleBuffer
{
public:
    TripleBuffer() = default;
    explicit TripleBuffer(const T& initial) { slots_.fill(initial); }

    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    // --- Escritor (seguro en tiempo real) ---

    /// Copia en la que escribir el próximo valor. Conserva lo que tuviera de
    /// una publicación anterior (no necesariamente la última).
    T& writeSlot() noexcept { return slots_[static_cast<std::size_t>(back_)]; }

    /// Publica lo escrito en writeSlot().
    void publish() noexcept ESCEQ_NONBLOCKING
    {
        back_ = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel) & kIndexMask;
    }

    /// Escribe `value` y lo publica.
    void publish(const T& value) noexcept ESCEQ_NONBLOCKING
    {
        writeSlot() = value;
        publish();
    }

    // --- Lector ---

    /// El último valor publicado si hay uno que el lector aún no ha visto, o
    /// nullptr. El puntero vale hasta la siguiente llamada a read().
    const T* read() noexcept ESCEQ_NONBLOCKING
    {
        if ((middle_.load(std::memory_order_acquire) & kFresh) == 0)
            return nullptr;
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndexMask;
        return &slots_[static_cast<std::size_t>(front_)];
    }

private:
    static constexpr int kIndexMask = 3;
    static constexpr int kFresh = 4;

    std::array<T, 3> slots_{};
    int back_ = 0;  ///< Solo el escritor.
    int front_ = 2; ///< Solo el lector.
    std::atomic<int> middle_{1};
};

/// Entrega de objetos de un hilo que no es de tiempo real (el productor) al
/// hilo de audio (el consumidor), y devolución para destruirlos.
///
///  - El productor entrega con offer(). Si el consumidor no había recogido
///    el anterior, el anterior se destruye: solo importa el último.
///  - El consumidor recoge con take() y, cuando ya no lo necesita, lo
///    devuelve con retire(). Nunca lo destruye él.
///  - El productor destruye lo devuelto con collect().
///
/// take() solo entrega un objeto si caben al menos `kReserve` devoluciones,
/// para que el consumidor siempre pueda devolver lo que recoge y lo que
/// estaba usando. Con kCapacity devoluciones pendientes, deja de entregar
/// hasta que el productor llame a collect().
template <typename T, std::size_t kCapacity = 8> class Handoff
{
public:
    static constexpr int kReserve = 2;
    static_assert(kCapacity >= static_cast<std::size_t>(kReserve) && (kCapacity & (kCapacity - 1)) == 0,
                  "capacidad: potencia de 2");

    Handoff() = default;
    Handoff(const Handoff&) = delete;
    Handoff& operator=(const Handoff&) = delete;

    /// Destruye lo pendiente y lo devuelto. Ningún hilo debe estar usándolo.
    ~Handoff()
    {
        delete pending_.load(std::memory_order_acquire);
        collect();
    }

    // --- Productor (no es de tiempo real) ---

    /// Entrega `object` al consumidor, sustituyendo al que no haya recogido.
    void offer(std::unique_ptr<T> object) { delete pending_.exchange(object.release(), std::memory_order_acq_rel); }

    /// Destruye lo que el consumidor ha devuelto. Devuelve cuántos.
    int collect() noexcept
    {
        int count = 0;
        auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);
        for (; tail != head; ++tail, ++count)
        {
            auto& slot = retired_[tail % kCapacity];
            delete slot;
            slot = nullptr;
        }
        tail_.store(tail, std::memory_order_release);
        return count;
    }

    /// true si hay algo entregado que el consumidor aún no ha recogido.
    bool hasPending() const noexcept { return pending_.load(std::memory_order_acquire) != nullptr; }

    // --- Consumidor (seguro en tiempo real) ---

    /// Lo último entregado, o nullptr si no hay nada o no queda sitio para
    /// devolverlo. El consumidor es su dueño hasta que lo devuelve con retire():
    /// no debe dejar que se destruya.
    [[nodiscard]] std::unique_ptr<T> take() noexcept ESCEQ_NONBLOCKING
    {
        if (freeRetireSlots() < kReserve)
            return nullptr;
        return std::unique_ptr<T>(pending_.exchange(nullptr, std::memory_order_acq_rel));
    }

    /// Devuelve `object` para que lo destruya el productor. Siempre cabe si
    /// solo se devuelve lo recogido con take() y como mucho un objeto más
    /// por cada uno recogido. Un nullptr no hace nada.
    void retire(std::unique_ptr<T> object) noexcept ESCEQ_NONBLOCKING
    {
        if (object == nullptr)
            return;
        const auto head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= kCapacity)
        {
            // No debería ocurrir (ver take()). Antes que liberar memoria aquí,
            // se pierde el objeto: una fuga es mejor que un corte de audio.
            (void)object.release();
            return;
        }
        retired_[head % kCapacity] = object.release();
        head_.store(head + 1, std::memory_order_release);
    }

    /// Devoluciones que caben ahora mismo.
    int freeRetireSlots() const noexcept
    {
        const auto used = head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire);
        return static_cast<int>(kCapacity) - static_cast<int>(used);
    }

private:
    std::atomic<T*> pending_{nullptr};
    std::array<T*, kCapacity> retired_{};
    std::atomic<std::uint32_t> head_{0}; ///< Lo escribe el consumidor.
    std::atomic<std::uint32_t> tail_{0}; ///< Lo escribe el productor.
};

} // namespace esceq::dsp
