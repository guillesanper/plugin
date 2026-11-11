// SPDX-License-Identifier: MIT
//
// Contrato de sonido: la respuesta de una configuración dada no cambia
// entre versiones. tests/fixtures/dsp/respuesta_v1.txt guarda la respuesta
// de referencia; si este test falla, un preset antiguo sonaría distinto.
//
// Si el cambio de sonido es intencionado NO se regenera este archivo: se
// añade un algoritmo nuevo con su propio archivo de referencia y se conserva
// el antiguo para los estados guardados (ver docs/dsp/filtros.md).
//
// Solo para crear un archivo de referencia nuevo:
//   ESCEQ_WRITE_GOLDEN=1 ./esceq_tests "Filtros: contrato de sonido*"
#include "esceq/dsp/filter_design.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;
using namespace esceq::test;

namespace {

struct Row
{
    int type;
    double frequency, gain, q;
    int slope;
    double sampleRate, probe, expectedDb;
};

const std::string kPath = std::string(ESCEQ_FIXTURES_DIR) + "/dsp/respuesta_v1.txt";

std::vector<Row> referenceCases()
{
    std::vector<Row> rows;
    int index = 0;
    for (auto type : kAllTypes)
        for (double sampleRate : {44100.0, 96000.0})
            for (double frequency : {80.0, 1000.0, 9000.0})
                for (double gain : {-9.0, 6.0})
                    for (double probe : {30.0, 200.0, 1000.0, 5000.0, 16000.0})
                    {
                        // Variedad determinista de Q y pendiente sin multiplicar el número de casos.
                        const double q = std::vector<double>{0.5, 0.7071, 1.4, 4.0}[static_cast<std::size_t>(index % 4)];
                        const int slope = std::vector<int>{6, 12, 30, 48, 96}[static_cast<std::size_t>(index % 5)];
                        ++index;
                        const auto design = designBand({type, frequency, gain, q, slope}, sampleRate);
                        rows.push_back({static_cast<int>(type), frequency, gain, q, slope, sampleRate, probe,
                                        magnitudeDb(design, probe, sampleRate)});
                    }
    return rows;
}

} // namespace

TEST_CASE("Filtros: contrato de sonido (respuesta de referencia v1)")
{
    if (std::getenv("ESCEQ_WRITE_GOLDEN") != nullptr)
    {
        std::ofstream out(kPath, std::ios::binary);
        REQUIRE(out.is_open());
        out.imbue(std::locale::classic());
        out << "# tipo frecuencia ganancia q pendiente fs sonda dB\n" << std::setprecision(10);
        for (const auto& r : referenceCases())
            out << r.type << ' ' << r.frequency << ' ' << r.gain << ' ' << r.q << ' ' << r.slope << ' ' << r.sampleRate
                << ' ' << r.probe << ' ' << r.expectedDb << '\n';
        SUCCEED("archivo de referencia escrito");
        return;
    }

    std::ifstream in(kPath, std::ios::binary);
    REQUIRE(in.is_open());

    int checked = 0;
    for (std::string line; std::getline(in, line);)
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        Row r{};
        REQUIRE(fields >> r.type >> r.frequency >> r.gain >> r.q >> r.slope >> r.sampleRate >> r.probe >> r.expectedDb);

        const auto design = designBand({static_cast<FilterType>(r.type), r.frequency, r.gain, r.q, r.slope}, r.sampleRate);
        const double actual = magnitudeDb(design, r.probe, r.sampleRate);

        INFO(line);
        // En una anulación casi total (el centro de un notch) el valor exacto
        // es ruido de redondeo y varía entre plataformas: basta con que siga anulado.
        if (r.expectedDb < -150.0)
            CHECK(actual < -150.0);
        else
            CHECK_THAT(actual, WithinAbs(r.expectedDb, 0.001));
        ++checked;
    }

    CHECK(checked == 540);
}
