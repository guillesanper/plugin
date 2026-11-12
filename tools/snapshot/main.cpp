// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Uso: esceq_snapshot salida.png [ancho alto] [opciones]
//   --scale F     factor de escala de la captura (1, 1.25, 1.5, 2...). Por defecto, 2.
//   --select N    banda seleccionada (1..24; 0 = ninguna). Por defecto, 6.
//   --hover N     banda señalada por el ratón: muestra su etiqueta flotante.
//   --overflow    sube una banda por encima del rango visible.
//   --bypass      con el bypass activado.
//   --gallery     en vez del editor, la galería de estados de los controles.
// En Linux sin pantalla: xvfb-run -a ./esceq_snapshot salida.png
#include "CurveView.h"
#include "Draw.h"
#include "EqEditor.h"
#include "EsceqLookAndFeel.h"
#include "Theme.h"
#include "PluginProcessor.h"

#include "esceq/core/parameters.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using esceq::core::BandParam;
using esceq::core::DynParam;

void set(esceq::PluginProcessor& p, int band, BandParam param, float value)
{
    auto& parameter = p.parameter(esceq::core::bandParameter(band, param));
    parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
}

void addBand(esceq::PluginProcessor& p, int band, float type, float hz, float gain, float q, float slope = 1.0f)
{
    set(p, band, BandParam::Type, type);
    set(p, band, BandParam::Frequency, hz);
    set(p, band, BandParam::Gain, gain);
    set(p, band, BandParam::Q, q);
    set(p, band, BandParam::Slope, slope);
    set(p, band, BandParam::Enabled, 1.0f);
}

void setDynamic(esceq::PluginProcessor& p, int band, DynParam param, float value)
{
    auto& parameter = p.parameter(esceq::core::dynamicParameter(band, param));
    parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
}

/// Ruido rosa (filtro "economy" de Paul Kellet) para que el analizador muestre algo realista.
struct PinkNoise
{
    std::uint32_t state = 12345u;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;

    float next()
    {
        state = state * 1664525u + 1013904223u;
        const float white = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        return 0.08f * (b0 + b1 + b2 + white * 0.1848f);
    }
};

/// Galería de estados de los controles, pintada con las mismas funciones
/// que usa la interfaz (Draw.h). Es la referencia visual de docs/ui.md.
class Gallery final : public juce::Component
{
public:
    Gallery() { setSize(760, 372); }

    void paint(juce::Graphics& g) override
    {
        using namespace esceq::ui;
        using namespace esceq::ui::theme;
        g.fillAll(colour::window);

        const auto& specs = esceq::core::parameters();
        const auto& gain = specs[esceq::core::bandParameter(0, BandParam::Gain)];
        const auto& freq = specs[esceq::core::bandParameter(0, BandParam::Frequency)];
        const auto accent = theme::band(5);

        struct Column
        {
            const char* name;
            draw::ControlState state;
        };
        std::vector<Column> columns(6);
        columns[0].name = "Rest";
        columns[1].name = "Hover";
        columns[1].state.hover = true;
        columns[2].name = "Drag";
        columns[2].state.hover = true;
        columns[2].state.pressed = true;
        columns[3].name = "Focus";
        columns[3].state.focused = true;
        columns[4].name = "No effect";
        columns[4].state.dimmed = true;
        columns[5].name = "Disabled";
        columns[5].state.enabled = false;

        const float left = 112.0f;
        const float columnWidth = (static_cast<float>(getWidth()) - left - 16.0f) / static_cast<float>(columns.size());
        auto rowLabel = [&](const char* text, float y, float h) {
            draw::caption(g, text, {16.0f, y, left - 24.0f, h}, juce::Justification::centredLeft);
        };

        float y = 12.0f;
        for (std::size_t i = 0; i < columns.size(); ++i)
            draw::caption(g, columns[i].name, {left + columnWidth * static_cast<float>(i), y, columnWidth, 14.0f},
                          juce::Justification::centred, colour::textMuted);
        y += 26.0f;

        auto cell = [&](std::size_t i, float h) {
            return juce::Rectangle<float>(left + columnWidth * static_cast<float>(i), y, columnWidth, h);
        };

        rowLabel("Knob", y, 56.0f);
        for (std::size_t i = 0; i < columns.size(); ++i)
            draw::knob(g, cell(i, 56.0f).withSizeKeepingCentre(52.0f, 52.0f), valueArc(freq, 2400.0f), false, accent,
                       columns[i].state);
        y += 68.0f;

        rowLabel("Knob, bipolar", y, 56.0f);
        for (std::size_t i = 0; i < columns.size(); ++i)
            draw::knob(g, cell(i, 56.0f).withSizeKeepingCentre(52.0f, 52.0f), valueArc(gain, i % 2 == 0 ? -11.0f : 8.0f), true,
                       accent, columns[i].state);
        y += 68.0f;

        rowLabel("Bar", y, 24.0f);
        for (std::size_t i = 0; i < columns.size(); ++i)
            draw::bar(g, cell(i, 24.0f).reduced(10.0f, 0.0f), valueSpan(gain, 9.0f), true, colour::accent, columns[i].state);
        y += 38.0f;

        rowLabel("Switch", y, 24.0f);
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            auto pair = cell(i, 24.0f).withSizeKeepingCentre(64.0f, 24.0f);
            draw::toggle(g, pair.removeFromLeft(32.0f), false, accent, columns[i].state);
            draw::toggle(g, pair, true, accent, columns[i].state);
        }
        y += 38.0f;

        rowLabel("Selector", y, 24.0f);
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            auto group = cell(i, 24.0f).withSizeKeepingCentre(84.0f, 24.0f);
            draw::pill(g, group.removeFromLeft(28.0f), "ST", true, accent, columns[i].state, draw::Corners::Left);
            draw::pill(g, group.removeFromLeft(28.0f).expanded(1.0f, 0.0f), "L", false, accent, columns[i].state,
                       draw::Corners::Middle);
            draw::pill(g, group, "R", false, accent, columns[i].state, draw::Corners::Right);
        }
        y += 38.0f;

        rowLabel("Button", y, 24.0f);
        for (std::size_t i = 0; i < columns.size(); ++i)
            draw::pill(g, cell(i, 24.0f).withSizeKeepingCentre(72.0f, 24.0f), "Bypass", i % 2 == 1, colour::warning,
                       columns[i].state);
        y += 38.0f;

        rowLabel("Dropdown", y, 24.0f);
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            const auto box = cell(i, 24.0f).withSizeKeepingCentre(92.0f, 24.0f);
            draw::dropdown(g, box, columns[i].state);
            g.setColour(columns[i].state.enabled ? colour::text : colour::textDim);
            g.setFont(font(type::body));
            g.drawText("Bell", box.withTrimmedLeft(8.0f), juce::Justification::centredLeft, false);
        }
    }
};

bool writePng(const juce::Image& image, const char* path)
{
    juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(path);
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (!out.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, out))
    {
        std::cerr << "no se pudo escribir " << file.getFullPathName() << "\n";
        return false;
    }
    std::cout << file.getFullPathName() << "\n";
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "uso: esceq_snapshot salida.png [ancho alto] [--scale F] [--select N] [--hover N] "
                     "[--overflow] [--bypass] [--gallery]\n";
        return 2;
    }

    int width = 0, height = 0, select = 6, hover = 0;
    float scale = 2.0f;
    bool overflow = false, bypass = false, gallery = false;
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&] { return i + 1 < argc ? argv[++i] : "0"; };
        if (arg == "--scale")
            scale = static_cast<float>(std::atof(next()));
        else if (arg == "--select")
            select = std::atoi(next());
        else if (arg == "--hover")
            hover = std::atoi(next());
        else if (arg == "--overflow")
            overflow = true;
        else if (arg == "--bypass")
            bypass = true;
        else if (arg == "--gallery")
            gallery = true;
        else
            positional.push_back(arg);
    }
    if (positional.size() >= 2)
    {
        width = std::atoi(positional[0].c_str());
        height = std::atoi(positional[1].c_str());
    }
    if (scale <= 0.0f)
        scale = 2.0f;

    juce::ScopedJuceInitialiser_GUI gui;

    if (gallery)
    {
        esceq::ui::EsceqLookAndFeel lookAndFeel;
        Gallery component;
        component.setLookAndFeel(&lookAndFeel);
        const bool ok = writePng(component.createComponentSnapshot(component.getLocalBounds(), true, scale), argv[1]);
        component.setLookAndFeel(nullptr);
        return ok ? 0 : 1;
    }

    std::unique_ptr<esceq::PluginProcessor> processor(static_cast<esceq::PluginProcessor*>(createPluginFilter()));
    addBand(*processor, 0, 3.0f, 35.0f, 0.0f, 0.7071f, 3.0f); // corte de graves, 24 dB/oct
    addBand(*processor, 1, 0.0f, 240.0f, -4.0f, 1.4f);         // campana
    addBand(*processor, 2, 0.0f, 2800.0f, overflow ? 24.0f : 3.5f, 0.9f); // campana
    addBand(*processor, 3, 2.0f, 9000.0f, 2.5f, 0.7071f);      // shelf de agudos
    {
        auto& channel = processor->parameter(esceq::core::channelParameter(1)); // la campana de graves, solo en Mid
        channel.setValueNotifyingHost(channel.convertTo0to1(3.0f));
    }
    addBand(*processor, 4, 5.0f, 5200.0f, 0.0f, 8.0f);         // notch
    addBand(*processor, 5, 0.0f, 7000.0f, 0.0f, 1.5f);         // de-esser: campana dinámica
    setDynamic(*processor, 5, DynParam::Threshold, -48.0f);
    setDynamic(*processor, 5, DynParam::Range, -9.0f);
    setDynamic(*processor, 5, DynParam::Attack, 1.0f);
    setDynamic(*processor, 5, DynParam::Enabled, 1.0f);
    if (bypass)
        processor->parameter(esceq::core::kBypass).setValueNotifyingHost(1.0f);

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    processor->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    processor->prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    PinkNoise noise, noiseRight;
    noiseRight.state = 98765u;

    auto feed = [&](int blocks) {
        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                // Algo de diferencia entre canales, para que el medidor no muestre dos barras idénticas.
                const float centre = noise.next();
                const float side = 0.35f * noiseRight.next();
                buffer.setSample(0, i, centre + side);
                buffer.setSample(1, i, centre - side);
            }
            processor->processBlock(buffer, midi);
        }
    };

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    if (width > 0 && height > 0)
        editor->setSize(width, height);
    auto& curve = static_cast<esceq::ui::EqEditor&>(*editor).curve();
    curve.setSelectedBand(select - 1);

    // Audio en tiempo "real" mientras corre el temporizador del editor, para
    // que el analizador se asiente como lo haría en un host.
    for (int frame = 0; frame < 40; ++frame)
    {
        feed(3);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(33);
    }
    curve.setHoveredBand(hover - 1);

    const bool ok = writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, scale), argv[1]);

    editor.reset();
    processor->releaseResources();
    return ok ? 0 : 1;
}
