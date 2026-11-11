// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "EqEditor.h"

#include "esceq/core/band_model.h"
#include "esceq/core/state.h"
#include "esceq/dsp/decibels.h"

#include <memory>
#include <string>
#include <string_view>

namespace esceq {

namespace {

constexpr double kGainRampSeconds = 0.02;

juce::String toJuce(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::StringArray choiceNames(const core::ParameterSpec& spec)
{
    juce::StringArray names;
    for (const auto choice : spec.choices)
        names.add(toJuce(choice));
    return names;
}

/// Rango de JUCE que delega en core la correspondencia valor <-> 0..1.
juce::NormalisableRange<float> rangeFor(const core::ParameterSpec* spec)
{
    return {spec->min, spec->max,
            [spec](float, float, float normalized) { return core::fromNormalized(*spec, normalized); },
            [spec](float, float, float value) { return core::toNormalized(*spec, value); },
            [spec](float, float, float value) { return core::sanitize(*spec, value); }};
}

} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "esceQ", createLayout())
{
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
    {
        const auto id = toJuce(core::parameters()[i].id);
        raw_[i] = apvts_.getRawParameterValue(id);
        params_[i] = apvts_.getParameter(id);
        jassert(raw_[i] != nullptr && params_[i] != nullptr);
    }
}

// Los parámetros del host se generan a partir de la tabla de core: no se
// declara ninguno a mano aquí.
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : core::parameters())
    {
        const juce::ParameterID id{toJuce(spec.id), 1};
        const auto name = toJuce(spec.name);

        switch (spec.kind)
        {
        case core::ParameterKind::Toggle:
            layout.add(std::make_unique<juce::AudioParameterBool>(id, name, spec.defaultValue >= 0.5f));
            break;

        case core::ParameterKind::Choice:
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, name, choiceNames(spec),
                                                                    static_cast<int>(spec.defaultValue)));
            break;

        case core::ParameterKind::Continuous:
        {
            const core::ParameterSpec* specPtr = &spec; // la tabla vive mientras viva el programa
            auto attributes =
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction([specPtr](float value, int) { return toJuce(core::formatValue(*specPtr, value)); })
                    .withValueFromStringFunction([specPtr](const juce::String& text) {
                        return core::parseValue(*specPtr, text.toStdString()).value_or(specPtr->defaultValue);
                    });
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, rangeFor(specPtr), spec.defaultValue,
                                                                   attributes));
            break;
        }
        }
    }

    return layout;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

juce::AudioProcessorParameter* PluginProcessor::getBypassParameter() const
{
    return params_[core::kBypass];
}

core::ParameterValues PluginProcessor::currentValues() const noexcept
{
    core::ParameterValues values{};
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        values[i] = raw_[i]->load(std::memory_order_relaxed);
    return values;
}

double PluginProcessor::currentSampleRate() const noexcept
{
    const double rate = sampleRate_.load(std::memory_order_relaxed);
    return rate > 0.0 ? rate : 48000.0;
}

void PluginProcessor::pushParametersToEngine() noexcept
{
    const auto values = currentValues();
    for (int band = 0; band < core::kNumBands; ++band)
        equalizer_.setBand(band, core::bandSettings(values, band));
    equalizer_.setBypassed(values[core::kBypass] >= 0.5f);
}

void PluginProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_.store(sampleRate);
    pushParametersToEngine();
    equalizer_.prepare(sampleRate);

    const bool bypassed = raw_[core::kBypass]->load() >= 0.5f;
    outputGain_.prepare(sampleRate, kGainRampSeconds);
    outputGain_.reset(bypassed ? 1.0f : dsp::dbToGain(raw_[core::kOutputGain]->load()));
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    float* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    preFifo_.writeMix(channels, numChannels, numSamples);

    pushParametersToEngine();
    equalizer_.process(channels, numChannels, numSamples);

    const bool bypassed = raw_[core::kBypass]->load(std::memory_order_relaxed) >= 0.5f;
    const float gainDb = raw_[core::kOutputGain]->load(std::memory_order_relaxed);
    outputGain_.setTarget(bypassed ? 1.0f : dsp::dbToGain(gainDb));
    outputGain_.process(channels, numChannels, numSamples);

    postFifo_.writeMix(channels, numChannels, numSamples);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new ui::EqEditor(*this);
}

// El estado se guarda en el formato propio de core, no en el de JUCE:
// así sigue siendo legible aunque cambie el framework.
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const std::string text = core::serializeState(currentValues());
    destData.replaceAll(text.data(), text.size());
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const std::string_view text{static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes)};
    const auto values = core::deserializeState(text);
    if (!values)
        return;

    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1((*values)[i]));
}

} // namespace esceq

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new esceq::PluginProcessor();
}
