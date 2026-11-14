// SPDX-License-Identifier: MIT
#include "esceq/dsp/handoff.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace esceq::dsp;

namespace
{

/// Cuenta cuántos objetos viven, para comprobar que no se pierde ni se
/// destruye dos veces ninguno.
struct Tracked
{
    static inline std::atomic<int> alive{0};
    explicit Tracked(int v) : value(v) { alive.fetch_add(1); }
    ~Tracked() { alive.fetch_sub(1); }
    int value;
};

} // namespace

TEST_CASE("TripleBuffer: el lector ve el ultimo valor publicado, una sola vez")
{
    TripleBuffer<int> buffer;
    CHECK(buffer.read() == nullptr);

    buffer.publish(1);
    buffer.publish(2);
    const int* value = buffer.read();
    REQUIRE(value != nullptr);
    CHECK(*value == 2);
    CHECK(buffer.read() == nullptr);

    buffer.writeSlot() = 3;
    buffer.publish();
    REQUIRE(buffer.read() != nullptr);
}

TEST_CASE("TripleBuffer: con dos hilos el lector solo ve valores completos y en orden")
{
    struct Pair
    {
        int a = 0;
        int b = 0;
    };
    TripleBuffer<Pair> buffer;
    constexpr int kCount = 200000;
    std::thread writer(
        [&]
        {
            for (int i = 1; i <= kCount; ++i)
                buffer.publish({i, -i});
        });

    int last = 0;
    bool consistent = true, ordered = true;
    while (last < kCount)
        if (const Pair* p = buffer.read())
        {
            consistent = consistent && p->a == -p->b;
            ordered = ordered && p->a > last;
            last = p->a;
        }
    writer.join();
    CHECK(consistent);
    CHECK(ordered);
}

TEST_CASE("Handoff: entrega lo ultimo y destruye lo que el consumidor no recogio")
{
    {
        Handoff<Tracked> handoff;
        handoff.offer(std::make_unique<Tracked>(1));
        handoff.offer(std::make_unique<Tracked>(2)); // el 1 se destruye: nadie lo había recogido
        CHECK(Tracked::alive == 1);
        CHECK(handoff.hasPending());

        auto taken = handoff.take();
        REQUIRE(taken != nullptr);
        CHECK(taken->value == 2);
        CHECK(handoff.take() == nullptr);

        // El consumidor lo devuelve; se destruye en collect(), no antes.
        handoff.retire(std::move(taken));
        CHECK(Tracked::alive == 1);
        CHECK(handoff.collect() == 1);
        CHECK(Tracked::alive == 0);

        handoff.offer(std::make_unique<Tracked>(3));
        handoff.retire(nullptr); // no hace nada
        CHECK(handoff.collect() == 0);
    }
    // El destructor se lleva lo pendiente.
    CHECK(Tracked::alive == 0);
}

TEST_CASE("Handoff: no entrega nada si no hay sitio para devolverlo")
{
    Handoff<Tracked, 4> handoff;
    // Con 4 devoluciones de capacidad y una reserva de 2: tras dos devoluciones
    // sin recoger, take() se niega.
    for (int i = 0; i < 2; ++i)
    {
        handoff.offer(std::make_unique<Tracked>(i));
        handoff.retire(handoff.take());
    }
    CHECK(handoff.freeRetireSlots() == 2);
    handoff.offer(std::make_unique<Tracked>(9));
    auto third = handoff.take();
    REQUIRE(third != nullptr);
    handoff.retire(std::move(third));
    CHECK(handoff.freeRetireSlots() == 1);
    handoff.offer(std::make_unique<Tracked>(10));
    CHECK(handoff.take() == nullptr);

    handoff.collect();
    CHECK(handoff.freeRetireSlots() == 4);
    CHECK(handoff.take() != nullptr);
}

TEST_CASE("Handoff: con dos hilos no se pierde ni se destruye dos veces ningun objeto")
{
    {
        Handoff<Tracked> handoff;
        constexpr int kCount = 20000;

        // El productor recoge las devoluciones después de cada entrega: tras
        // la última, siempre queda sitio para que el consumidor la recoja.
        std::thread producer(
            [&]
            {
                for (int i = 1; i <= kCount; ++i)
                {
                    handoff.offer(std::make_unique<Tracked>(i));
                    handoff.collect();
                }
            });

        std::unique_ptr<Tracked> current;
        int last = 0;
        bool ordered = true;
        while (last < kCount)
        {
            if (auto next = handoff.take())
            {
                ordered = ordered && next->value > last;
                last = next->value;
                handoff.retire(std::move(current));
                current = std::move(next);
            }
        }
        producer.join();
        CHECK(ordered);
        handoff.retire(std::move(current));
    }
    CHECK(Tracked::alive == 0);
}
