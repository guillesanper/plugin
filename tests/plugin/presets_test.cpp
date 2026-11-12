// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_support.h"

#include "esceq/core/presets.h"
#include "esceq/core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace esceq;
using namespace esceq::test;

namespace {

using Entry = PresetManager::Entry;

bool contains(const std::vector<Entry>& entries, const Entry& entry)
{
    return std::find(entries.begin(), entries.end(), entry) != entries.end();
}

/// Borra los presets de usuario de pruebas anteriores.
void cleanUserPresets()
{
    PresetManager::userDirectory().deleteRecursively();
}

} // namespace

TEST_CASE("Presets: la carpeta de usuario de las pruebas es la temporal")
{
    // Si esto falla, los tests escribirian en la carpeta real del usuario.
    const auto directory = PresetManager::userDirectory();
    CHECK(directory.getFullPathName().contains("esceq_tests_"));
    CHECK(directory.isAChildOf(juce::File::getSpecialLocation(juce::File::tempDirectory)));
}

TEST_CASE("Presets: la lista empieza por los de fabrica, en su orden")
{
    cleanUserPresets();
    auto p = makeProcessor();
    const auto entries = p->presets().presets();
    const auto factory = core::factoryPresets();
    REQUIRE(entries.size() == factory.size());
    for (std::size_t i = 0; i < factory.size(); ++i)
    {
        CHECK(entries[i].factory);
        CHECK(entries[i].name.toStdString() == factory[i].name);
    }
    CHECK_FALSE(p->presets().current().has_value());
    CHECK_FALSE(p->presets().isModified());
}

TEST_CASE("Presets: cargar uno de fabrica pone sus valores y se puede deshacer")
{
    cleanUserPresets();
    auto p = makeProcessor();
    drag(*p, core::kOutputGain, {3.0f});
    settle();
    const auto before = p->currentValues();

    REQUIRE(p->presets().load({"Vocal", true}));
    CHECK(p->currentValues() == *core::loadFactoryPreset("Vocal"));
    REQUIRE(p->presets().current().has_value());
    CHECK(p->presets().current()->name == "Vocal");
    CHECK(p->presets().current()->factory);
    CHECK_FALSE(p->presets().isModified());
    CHECK(p->uiSettings().presetName == "Vocal");

    // Cargar un preset es una sola accion.
    settle();
    p->history().undo();
    CHECK(p->currentValues() == before);
    p->history().redo();
    CHECK(p->currentValues() == *core::loadFactoryPreset("Vocal"));

    CHECK_FALSE(p->presets().load({"No existe", true}));
    CHECK_FALSE(p->presets().load({"No existe", false}));
}

TEST_CASE("Presets: tocar un parametro lo marca como modificado; la salida y el bypass, no")
{
    cleanUserPresets();
    auto p = makeProcessor();
    REQUIRE(p->presets().load({"De-esser", true}));
    CHECK_FALSE(p->presets().isModified());

    drag(*p, core::kOutputGain, {-4.0f});
    drag(*p, core::kBypass, {1.0f});
    CHECK_FALSE(p->presets().isModified());

    drag(*p, core::dynamicParameter(0, core::DynParam::Threshold), {-20.0f});
    CHECK(p->presets().isModified());
    drag(*p, core::dynamicParameter(0, core::DynParam::Threshold), {-30.0f});
    CHECK_FALSE(p->presets().isModified());
}

TEST_CASE("Presets: guardar, listar, cargar y borrar un preset de usuario")
{
    cleanUserPresets();
    auto p = makeProcessor();
    drag(*p, core::bandParameter(0, core::BandParam::Enabled), {1.0f});
    drag(*p, core::bandParameter(0, core::BandParam::Frequency), {777.0f});
    drag(*p, core::channelParameter(0), {3.0f});
    const auto saved = p->currentValues();

    REQUIRE(p->presets().saveUser("Mi preset"));
    CHECK(p->presets().userPresetExists("Mi preset"));
    CHECK(contains(p->presets().presets(), {"Mi preset", false}));
    CHECK(p->presets().current() == Entry{"Mi preset", false});
    CHECK_FALSE(p->presets().isModified());

    // El archivo esta en el formato de estado y solo lleva parametros.
    const auto file = PresetManager::userDirectory().getChildFile("Mi preset.esceq");
    REQUIRE(file.existsAsFile());
    const auto text = file.loadFileAsString().toStdString();
    CHECK(text.starts_with("esceQ-state 2\n"));
    CHECK(text.find('\r') == std::string::npos);
    CHECK(text.find("ui.") == std::string::npos);
    CHECK(core::deserializeState(text) == saved);

    // Otra instancia lo ve y lo carga.
    auto other = makeProcessor();
    REQUIRE(other->presets().load({"Mi preset", false}));
    CHECK(other->currentValues() == saved);

    // Los nombres se limpian; los inutilizables se rechazan.
    CHECK(p->presets().saveUser("  otro/nombre  "));
    CHECK(p->presets().userPresetExists("otronombre"));
    CHECK_FALSE(p->presets().saveUser("   "));
    CHECK_FALSE(p->presets().saveUser("CON"));

    CHECK(p->presets().deleteUser("otronombre"));
    CHECK_FALSE(p->presets().userPresetExists("otronombre"));
    CHECK(p->presets().deleteUser("Mi preset"));
    CHECK_FALSE(contains(p->presets().presets(), {"Mi preset", false}));
    CHECK_FALSE(p->presets().deleteUser("Mi preset"));
}

TEST_CASE("Presets: anterior y siguiente recorren la lista dando la vuelta")
{
    cleanUserPresets();
    auto p = makeProcessor();
    const auto entries = p->presets().presets();

    p->presets().step(1); // sin preset cargado: el primero
    CHECK(p->presets().current() == entries.front());
    p->presets().step(1);
    CHECK(p->presets().current() == entries[1]);
    p->presets().step(-1);
    p->presets().step(-1); // del primero al ultimo
    CHECK(p->presets().current() == entries.back());
    p->presets().step(1);
    CHECK(p->presets().current() == entries.front());
}

TEST_CASE("Presets: volver a los valores por defecto se puede deshacer")
{
    cleanUserPresets();
    auto p = makeProcessor();
    REQUIRE(p->presets().load({"Gentle Master", true}));
    settle();

    p->presets().loadDefault();
    CHECK(p->currentValues() == core::defaultValues());
    CHECK_FALSE(p->presets().current().has_value());
    settle();
    p->history().undo();
    CHECK(p->currentValues() == *core::loadFactoryPreset("Gentle Master"));
}

TEST_CASE("Presets: el nombre del preset viaja con el estado del proyecto")
{
    cleanUserPresets();
    auto a = makeProcessor();
    REQUIRE(a->presets().load({"Low-End Cleanup", true}));
    juce::MemoryBlock data;
    a->getStateInformation(data);

    auto b = makeProcessor();
    b->setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    REQUIRE(b->presets().current().has_value());
    CHECK(b->presets().current()->name == "Low-End Cleanup");
    CHECK_FALSE(b->presets().isModified());
    drag(*b, core::bandParameter(1, core::BandParam::Gain), {-6.0f});
    CHECK(b->presets().isModified());
}
