// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

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

} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "esceQ", createLayout())
{
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        raw_[i] = apvts_.getRawParameterValue(toJuce(core::kParameters[i].id));
}

// Los parámetros del host se generan a partir de la tabla de core: no se
// declara ninguno a mano aquí.
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : core::kParameters)
    {
        const juce::ParameterID id{toJuce(spec.id), 1};
        const auto name = toJuce(spec.name);

        if (spec.kind == core::ParameterKind::Toggle)
        {
            layout.add(std::make_unique<juce::AudioParameterBool>(id, name, spec.defaultValue >= 0.5f));
        }
        else
        {
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                id, name, juce::NormalisableRange<float>{spec.min, spec.max}, spec.defaultValue,
                juce::AudioParameterFloatAttributes().withLabel(toJuce(spec.unit))));
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
    return apvts_.getParameter(toJuce(core::kParameters[core::kBypass].id));
}

void PluginProcessor::prepareToPlay(double sampleRate, int)
{
    const bool bypassed = raw_[core::kBypass]->load() >= 0.5f;
    outputGain_.prepare(sampleRate, kGainRampSeconds);
    outputGain_.reset(bypassed ? 1.0f : dsp::dbToGain(raw_[core::kOutputGain]->load()));
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    const bool bypassed = raw_[core::kBypass]->load(std::memory_order_relaxed) >= 0.5f;
    const float gainDb = raw_[core::kOutputGain]->load(std::memory_order_relaxed);

    outputGain_.setTarget(bypassed ? 1.0f : dsp::dbToGain(gainDb));
    outputGain_.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    // Interfaz provisional hasta que exista ui/ (fase 2).
    return new juce::GenericAudioProcessorEditor(*this);
}

// El estado se guarda en el formato propio de core, no en el de JUCE:
// así sigue siendo legible aunque cambie el framework.
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    core::ParameterValues values{};
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        values[i] = raw_[i]->load();

    const std::string text = core::serializeState(values);
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
        if (auto* param = apvts_.getParameter(toJuce(core::kParameters[i].id)))
            param->setValueNotifyingHost(param->convertTo0to1((*values)[i]));
}

} // namespace esceq

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new esceq::PluginProcessor();
}
