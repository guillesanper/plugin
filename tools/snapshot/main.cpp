// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Uso: esceq_snapshot salida.png [ancho alto] [opciones]
//   --scale F     factor de escala de la captura (1, 1.25, 1.5, 2...). Por defecto, 2.
//   --select N    banda seleccionada (1..24; 0 = ninguna). Por defecto, 6.
//   --hover N     banda señalada por el ratón: muestra su etiqueta flotante.
//   --overflow    sube una banda por encima del rango visible.
//   --bypass      con el bypass activado.
//   --auto-gain   con la compensación automática de ganancia encendida.
//   --auto-timing con los tiempos automáticos en la banda dinámica (la 6).
//   --notes       con el teclado de la escala de notas bajo la curva.
//   --key N       con el teclado, la tecla de la nota N (MIDI, 69 = A4) señalada.
//   --expanded    en la vista ampliada (como al pulsar F en la curva).
//   --peaks       con los picos del analizador mantenidos.
//   --peak HZ     con una resonancia en HZ en el audio y el ratón junto a
//                 ella: el pico marcado para crear una banda desde el espectro.
//   --gallery     en vez del editor, la galería de estados de los controles.
//   --compare     con otras dos instancias en la sesión ("Bass" y "Keys"), superpuestas
//                 y con las colisiones marcadas.
//   --bench N     mide el coste de pintar la curva en vez de guardar una captura:
//                 pone las 24 bandas como campanas dinámicas y pinta N
//                 fotogramas de arrastre de un nodo, sin audio y con audio
//                 (media y mediana, en ms). Imprime además una huella de los
//                 píxeles de un fotograma fijo, para comprobar que una
//                 optimización no cambia lo que se dibuja.
//   --desktop QUE SEGUNDOS
//                 muestra el editor en el escritorio durante SEGUNDOS con algo
//                 abierto, para capturar la pantalla con otra herramienta (los
//                 menús y diálogos son ventanas aparte y no salen en la captura
//                 normal). QUE: presets (menú de presets), edit (campo de
//                 edición de un valor), save (diálogo de guardar preset),
//                 compare (menú Compare, con --compare).
//                 Ejemplo, en una sola línea:
//                   xvfb-run -a -s "-screen 0 1280x800x24" sh -c
//                     'esceq_snapshot x.png --desktop presets 6 & sleep 4; import -window root menu.png; wait'
// En Linux sin pantalla: xvfb-run -a ./esceq_snapshot salida.png
#include "CurveView.h"
#include "Draw.h"
#include "EqEditor.h"
#include "EsceqLookAndFeel.h"
#include "Theme.h"
#include "PluginProcessor.h"

#include "esceq/core/parameters.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <functional>
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

/// Resonancia para --peak: ruido blanco por un paso banda de Q 8 (0 dB en f0).
struct Resonance
{
    std::uint32_t state = 777u;
    double b0 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

    Resonance(double f0, double sampleRate)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f0 / sampleRate;
        const double alpha = std::sin(w) / (2.0 * 8.0);
        b0 = alpha / (1.0 + alpha);
        a1 = -2.0 * std::cos(w) / (1.0 + alpha);
        a2 = (1.0 - alpha) / (1.0 + alpha);
    }

    float next()
    {
        state = state * 1664525u + 1013904223u;
        const double white = static_cast<double>(state >> 8) / 8388608.0 - 1.0;
        const double y = b0 * white + z1;
        z1 = -a1 * y + z2;
        z2 = -b0 * white - a2 * y;
        return static_cast<float>(1.5 * y);
    }
};

/// Galería de estados de los controles, pintada con las mismas funciones
/// que usa la interfaz (Draw.h). Es la referencia visual de docs/ui.md.
class Gallery final : public juce::Component
{
public:
    Gallery() { setSize(980, 372); }

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
        constexpr float kMenuWidth = 204.0f;
        const float columnWidth = (static_cast<float>(getWidth()) - left - 32.0f - kMenuWidth) / static_cast<float>(columns.size());

        // A la derecha, un menú pintado con las funciones del LookAndFeel
        // (los menús de verdad son ventanas aparte y no salen en una captura).
        {
            auto& lf = getLookAndFeel();
            const juce::Rectangle<int> menu(getWidth() - static_cast<int>(kMenuWidth) - 16, 38, static_cast<int>(kMenuWidth), 26 * 9 + 9 + 10);
            draw::caption(g, "Menu", juce::Rectangle<float>(static_cast<float>(menu.getX()), 12.0f, kMenuWidth, 14.0f),
                          juce::Justification::centred, colour::textMuted);
            {
                juce::Graphics::ScopedSaveState state(g);
                g.setOrigin(menu.getPosition());
                lf.drawPopupMenuBackground(g, menu.getWidth(), menu.getHeight());
            }
            int rowY = menu.getY() + 5;
            auto row = [&](int height) {
                const juce::Rectangle<int> r(menu.getX() + 1, rowY, menu.getWidth() - 2, height);
                rowY += height;
                return r;
            };
            auto item = [&](const char* text, bool ticked = false, bool highlighted = false, bool active = true,
                            bool subMenu = false) {
                lf.drawPopupMenuItem(g, row(26), false, active, highlighted, ticked, subMenu, text, {}, nullptr, nullptr);
            };
            lf.drawPopupMenuSectionHeader(g, row(26), "Factory");
            item("De-esser");
            item("Resonance Control", false, true);
            item("Vocal", true);
            lf.drawPopupMenuSectionHeader(g, row(26), "User");
            item("My vocal chain");
            lf.drawPopupMenuItem(g, row(9), true, true, false, false, false, {}, {}, nullptr, nullptr);
            item("Save as...");
            item("Delete", false, false, false);
            item("Channel", false, false, true, true);
        }
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
        std::cerr << "uso: esceq_snapshot salida.png [ancho alto] [--scale F] [--select N] [--hover N] [--overflow] "
                     "[--bypass] [--auto-gain] [--auto-timing] [--notes] [--key N] [--expanded] [--peaks] [--peak HZ] "
                     "[--gallery] [--compare] [--bench N]\n";
        return 2;
    }

    int width = 0, height = 0, select = 6, hover = 0, desktopSeconds = 0, bench = 0, key = -1;
    double peakHz = 0.0;
    std::string desktop;
    float scale = 2.0f;
    bool overflow = false, bypass = false, autoGain = false, autoTiming = false, notes = false, gallery = false,
         compare = false, expanded = false, peaks = false;
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
        else if (arg == "--auto-gain")
            autoGain = true;
        else if (arg == "--auto-timing")
            autoTiming = true;
        else if (arg == "--notes")
            notes = true;
        else if (arg == "--key")
        {
            notes = true;
            key = std::atoi(next());
        }
        else if (arg == "--expanded")
            expanded = true;
        else if (arg == "--peaks")
            peaks = true;
        else if (arg == "--peak")
            peakHz = std::atof(next());
        else if (arg == "--gallery")
            gallery = true;
        else if (arg == "--compare")
            compare = true;
        else if (arg == "--bench")
            bench = std::max(1, std::atoi(next()));
        else if (arg == "--desktop")
        {
            desktop = next();
            desktopSeconds = std::atoi(next());
        }
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
    if (autoGain)
        processor->parameter(esceq::core::kAutoGain).setValueNotifyingHost(1.0f);
    if (autoTiming)
        processor->parameter(esceq::core::autoTimingParameter(5)).setValueNotifyingHost(1.0f);
    if (bench > 0)
    {
        // El peor caso: las 24 bandas, todas campanas dinámicas que actúan.
        for (int band = 0; band < esceq::core::kNumBands; ++band)
        {
            const float hz = 30.0f * std::pow(600.0f, static_cast<float>(band) / 23.0f);
            addBand(*processor, band, 0.0f, hz, (band % 2 == 0) ? 3.0f : -3.0f, 1.2f);
            setDynamic(*processor, band, DynParam::Threshold, -50.0f);
            setDynamic(*processor, band, DynParam::Range, -6.0f);
            setDynamic(*processor, band, DynParam::Enabled, 1.0f);
        }
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    processor->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    processor->prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    PinkNoise noise, noiseRight;
    noiseRight.state = 98765u;
    Resonance resonance(peakHz > 0.0 ? peakHz : 1000.0, sampleRate);

    // Con --compare, otras dos instancias en la misma sesión: un bajo (ruido
    // rosa con un corte de agudos) y unos teclados (ruido con cortes por los
    // dos lados), con el nombre y el color de su pista como los daría el host.
    std::vector<std::unique_ptr<esceq::PluginProcessor>> others;
    if (compare)
    {
        auto makeTrack = [&](const char* name, std::uint32_t colour) {
            auto& p = others.emplace_back(static_cast<esceq::PluginProcessor*>(createPluginFilter()));
            juce::AudioProcessor::TrackProperties track;
            track.name = juce::String(name);
            track.colourARGB = colour;
            p->updateTrackProperties(track);
            p->setPlayConfigDetails(2, 2, sampleRate, blockSize);
            p->prepareToPlay(sampleRate, blockSize);
            return p.get();
        };
        auto* bass = makeTrack("Bass", 0xffe0a040u);
        addBand(*bass, 0, 4.0f, 260.0f, 0.0f, 0.7071f, 3.0f);  // corte de agudos, 24 dB/oct
        addBand(*bass, 1, 0.0f, 80.0f, 9.0f, 1.0f);            // cuerpo
        auto* keys = makeTrack("Keys", 0xff9a7cf0u);
        addBand(*keys, 0, 3.0f, 400.0f, 0.0f, 0.7071f, 3.0f);  // corte de graves
        addBand(*keys, 1, 4.0f, 5000.0f, 0.0f, 0.7071f, 3.0f); // corte de agudos
        addBand(*keys, 2, 0.0f, 1500.0f, 6.0f, 0.8f);
    }
    std::vector<PinkNoise> otherNoise(others.size());
    for (std::size_t k = 0; k < otherNoise.size(); ++k)
        otherNoise[k].state = 4242u + 977u * static_cast<std::uint32_t>(k);

    auto feed = [&](int blocks) {
        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                // Algo de diferencia entre canales, para que el medidor no muestre dos barras idénticas.
                const float centre = noise.next() + (peakHz > 0.0 ? resonance.next() : 0.0f);
                const float side = 0.35f * noiseRight.next();
                buffer.setSample(0, i, centre + side);
                buffer.setSample(1, i, centre - side);
            }
            processor->processBlock(buffer, midi);

            for (std::size_t k = 0; k < others.size(); ++k)
            {
                juce::AudioBuffer<float> other(2, blockSize);
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = otherNoise[k].next();
                    other.setSample(0, i, v);
                    other.setSample(1, i, v);
                }
                others[k]->processBlock(other, midi);
            }
        }
    };

    {
        auto ui = processor->uiSettings();
        ui.pianoKeyboard = ui.pianoKeyboard || notes;
        ui.analyzerPeakHold = ui.analyzerPeakHold || peaks;
        processor->setUiSettings(ui);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    if (width > 0 && height > 0)
        editor->setSize(width, height);
    auto& curve = static_cast<esceq::ui::EqEditor&>(*editor).curve();
    if (expanded)
        curve.keyPressed(juce::KeyPress('f'));
    curve.setSelectedBand(select - 1);
    for (const auto& other : others)
        processor->compareSelection().toggle(other->instanceId());
    curve.compareChanged();

    // Audio en tiempo "real" mientras corre el temporizador del editor, para
    // que el analizador se asiente como lo haría en un host.
    for (int frame = 0; frame < 40; ++frame)
    {
        feed(3);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(33);
    }
    curve.setHoveredBand(hover - 1);
    if (peakHz > 0.0)
    {
        // El ratón un poco a la derecha de la resonancia, a media altura (como CurveView::mapping).
        const auto bounds = curve.getLocalBounds().toFloat().reduced(1.0f);
        const esceq::ui::CurveMapping m({bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()}, 18.0);
        curve.setPointer(juce::Point<float>(m.xForFrequency(peakHz * 1.04), bounds.getCentreY()));
    }
    if (key >= 0)
        static_cast<esceq::ui::EqEditor&>(*editor).keyboard().setHoveredNote(key);

    if (bench > 0)
    {
        // Como un arrastre: cada fotograma cambia la ganancia de una banda, el
        // editor lee los parámetros y la curva se pinta entera. Dos casos: sin
        // audio (solo cambia la banda arrastrada) y con audio (además se mueve
        // la ganancia dinámica de las 24 bandas en cada fotograma).
        auto& gain = processor->parameter(esceq::core::bandParameter(5, BandParam::Gain));
        // Se pinta siempre en la misma imagen, como en una ventana: reservar
        // una imagen nueva en cada fotograma mediría también al reservador.
        juce::Image target(juce::Image::RGB, juce::roundToInt(static_cast<float>(curve.getWidth()) * scale),
                           juce::roundToInt(static_cast<float>(curve.getHeight()) * scale), true);
        auto measure = [&](bool withAudio) {
            std::vector<double> times;
            for (int frame = 0; frame < bench; ++frame)
            {
                gain.setValueNotifyingHost(gain.convertTo0to1(-6.0f + 12.0f * static_cast<float>(frame % 50) / 49.0f));
                if (withAudio)
                    feed(3);
                curve.refresh();
                const auto start = std::chrono::steady_clock::now();
                {
                    juce::Graphics g(target);
                    g.addTransform(juce::AffineTransform::scale(scale));
                    curve.paintEntireComponent(g, true);
                }
                times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            }
            std::sort(times.begin(), times.end());
            double mean = 0.0;
            for (const double t : times)
                mean += t / static_cast<double>(times.size());
            std::cout << "curva " << curve.getWidth() << "x" << curve.getHeight() << " escala " << scale
                      << (withAudio ? ", con audio" : ", sin audio") << ": media " << mean << " ms, mediana "
                      << times[times.size() / 2] << " ms\n";
        };
        measure(false);
        measure(true);

        // Huella de un fotograma fijo, sin analizador (que depende del reloj).
        curve.setAnalyzerMode(esceq::ui::AnalyzerMode::Off);
        gain.setValueNotifyingHost(gain.convertTo0to1(2.0f));
        curve.refresh();
        const auto image = curve.createComponentSnapshot(curve.getLocalBounds(), true, scale);
        const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
        std::uint64_t hash = 14695981039346656037ull;
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
                hash = (hash ^ pixels.getPixelColour(x, y).getARGB()) * 1099511628211ull; // FNV-1a
        std::cout << "huella del fotograma fijo: " << std::hex << hash << std::dec << "\n";
        editor.reset();
        processor->releaseResources();
        return 0;
    }

    if (!desktop.empty())
    {
        editor->setOpaque(true);
        editor->addToDesktop(0);
        editor->setTopLeftPosition(20, 20);
        editor->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);

        // Busca un componente por su nombre accesible.
        std::function<juce::Component*(juce::Component&, const juce::String&)> find =
            [&find](juce::Component& parent, const juce::String& title) -> juce::Component* {
            for (auto* child : parent.getChildren())
            {
                if (child->getTitle() == title)
                    return child;
                if (auto* found = find(*child, title))
                    return found;
            }
            return nullptr;
        };

        if (desktop == "presets" || desktop == "save")
        {
            if (auto* bar = find(*editor, "Presets"))
                if (auto* button = dynamic_cast<juce::Button*>(find(*bar, "Presets")))
                    button->triggerClick();
            if (desktop == "save")
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
                juce::PopupMenu::dismissAllActiveMenus();
                processor->presets().load({"Vocal", true});
                // El diálogo de guardar se abre desde el menú; aquí, con el mismo aspecto.
                auto* window = new juce::AlertWindow("Save preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon,
                                                     editor.get());
                window->setLookAndFeel(&editor->getLookAndFeel());
                window->addTextEditor("name", "My vocal chain");
                window->addButton("Save", 1);
                window->addButton("Cancel", 0);
                window->enterModalState(true, nullptr, false);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(std::max(1, desktopSeconds) * 1000);
                delete window;
                editor.reset();
                processor->releaseResources();
                return 0;
            }
        }
        else if (desktop == "compare")
        {
            if (auto* button = dynamic_cast<juce::Button*>(find(*editor, "Compare with other tracks")))
                button->triggerClick();
        }
        else if (desktop == "edit")
        {
            if (auto* knob = find(*editor, "Band 6 Freq"))
                knob->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(std::max(1, desktopSeconds) * 1000);
        editor.reset();
        processor->releaseResources();
        return 0;
    }

    const bool ok = writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, scale), argv[1]);

    editor.reset();
    processor->releaseResources();
    return ok ? 0 : 1;
}
