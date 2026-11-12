// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Uso: esceq_snapshot salida.png [ancho alto]
// En Linux sin pantalla: xvfb-run -a ./esceq_snapshot salida.png
#include "CurveView.h"
#include "PluginProcessor.h"

#include "esceq/core/parameters.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdint>
#include <iostream>
#include <memory>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

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

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "uso: esceq_snapshot salida.png [ancho alto]\n";
        return 2;
    }
    const int width = argc >= 4 ? std::atoi(argv[2]) : 1100;
    const int height = argc >= 4 ? std::atoi(argv[3]) : 620;

    juce::ScopedJuceInitialiser_GUI gui;

    std::unique_ptr<esceq::PluginProcessor> processor(static_cast<esceq::PluginProcessor*>(createPluginFilter()));
    addBand(*processor, 0, 3.0f, 35.0f, 0.0f, 0.7071f, 3.0f); // corte de graves, 24 dB/oct
    addBand(*processor, 1, 0.0f, 240.0f, -4.0f, 1.4f);         // campana
    addBand(*processor, 2, 0.0f, 2800.0f, 3.5f, 0.9f);         // campana
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

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    processor->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    processor->prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    PinkNoise noise;

    auto feed = [&](int blocks) {
        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float sample = noise.next();
                buffer.setSample(0, i, sample);
                buffer.setSample(1, i, sample);
            }
            processor->processBlock(buffer, midi);
        }
    };

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    editor->setSize(width, height);
    for (auto* child : editor->getChildren())
        if (auto* curve = dynamic_cast<esceq::ui::CurveView*>(child))
            curve->setSelectedBand(5);

    // Audio en tiempo "real" mientras corre el temporizador del editor, para
    // que el analizador se asiente como lo haría en un host.
    for (int frame = 0; frame < 40; ++frame)
    {
        feed(3);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(33);
    }

    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (!out.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, out))
    {
        std::cerr << "no se pudo escribir " << file.getFullPathName() << "\n";
        return 1;
    }

    editor.reset();
    processor->releaseResources();
    std::cout << file.getFullPathName() << "\n";
    return 0;
}
