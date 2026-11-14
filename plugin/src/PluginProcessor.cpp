// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "esceq/core/band_model.h"
#include "esceq/core/state.h"
#include "esceq/dsp/decibels.h"

#include <algorithm>
#include <cmath>
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
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         // Bus auxiliar opcional para la detección externa de las
                         // bandas dinámicas. En FL Studio se enruta desde el mezclador.
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts_(*this, nullptr, "esceQ", createLayout())
{
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
    {
        const auto id = toJuce(core::parameters()[i].id);
        raw_[i] = apvts_.getRawParameterValue(id);
        params_[i] = apvts_.getParameter(id);
        jassert(raw_[i] != nullptr && params_[i] != nullptr);
    }
    history_ = std::make_unique<EditHistory>(*this);
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
    if (layouts.getMainInputChannelSet() != out)
        return false;

    // Sidechain: desconectado, mono o estéreo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet(true, 1);
        if (!sidechain.isDisabled() && sidechain != juce::AudioChannelSet::mono() &&
            sidechain != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
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

float PluginProcessor::dynamicGainDb(int band) const noexcept
{
    if (band < 0 || band >= core::kNumBands)
        return 0.0f;
    return dynamicGains_[static_cast<std::size_t>(band)].load(std::memory_order_relaxed);
}

void PluginProcessor::applyValues(const core::ParameterValues& values)
{
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
    {
        const float target = core::sanitize(core::parameters()[i], values[i]);
        const float now = raw_[i]->load(std::memory_order_relaxed);
        if (!(target < now) && !(target > now))
            continue; // ya tiene ese valor
        params_[i]->beginChangeGesture();
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(target));
        params_[i]->endChangeGesture();
    }
}

core::UiSettings PluginProcessor::uiSettings() const
{
    const juce::ScopedLock lock(uiSettingsLock_);
    return uiSettings_;
}

void PluginProcessor::setUiSettings(const core::UiSettings& settings)
{
    const juce::ScopedLock lock(uiSettingsLock_);
    uiSettings_ = core::sanitize(settings);
}

float PluginProcessor::takeOutputPeak(int channel) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(outputPeaks_.size()))
        return 0.0f;
    return outputPeaks_[static_cast<std::size_t>(channel)].exchange(0.0f, std::memory_order_relaxed);
}

double PluginProcessor::currentSampleRate() const noexcept
{
    const double rate = sampleRate_.load(std::memory_order_relaxed);
    return rate > 0.0 ? rate : 48000.0;
}

void PluginProcessor::pushParametersToEngine(bool force) noexcept
{
    // Traducir los 338 parámetros cuesta unos 2 µs: con bloques muy pequeños
    // (FL Studio los entrega a veces de pocas muestras) se nota. Si no ha
    // cambiado ninguno, el ecualizador ya tiene sus ajustes.
    const auto values = currentValues();
    if (!force && values == pushedValues_)
        return;
    pushedValues_ = values;
    for (int band = 0; band < core::kNumBands; ++band)
        equalizer_.setBand(band, core::bandSettings(values, band));
    equalizer_.setBypassed(values[core::kBypass] >= 0.5f);
}

void PluginProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_.store(sampleRate);
    pushParametersToEngine(true);
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

    // El búfer trae juntos los canales de todos los buses: el principal se
    // procesa en el sitio y el sidechain solo se lee.
    auto main = getBusBuffer(buffer, false, 0);
    float* const* channels = main.getArrayOfWritePointers();
    const int numChannels = main.getNumChannels();
    const int numSamples = main.getNumSamples();

    // El búfer del sidechain debe vivir hasta que el ecualizador lo lea:
    // sus punteros a los canales están dentro de él.
    juce::AudioBuffer<float> side;
    if (const auto* bus = getBus(true, 1); bus != nullptr && bus->isEnabled())
        side = getBusBuffer(buffer, true, 1);
    const float* const* sidechain = side.getNumChannels() > 0 ? side.getArrayOfReadPointers() : nullptr;
    const int sidechainChannels = side.getNumChannels();
    sidechainConnected_.store(sidechainChannels > 0, std::memory_order_relaxed);

    preFifo_.writeMix(channels, numChannels, numSamples);

    pushParametersToEngine();
    equalizer_.process(channels, numChannels, numSamples, sidechain, sidechainChannels);

    for (int band = 0; band < core::kNumBands; ++band)
        dynamicGains_[static_cast<std::size_t>(band)].store(static_cast<float>(equalizer_.dynamicGainDb(band)),
                                                            std::memory_order_relaxed);

    const bool bypassed = raw_[core::kBypass]->load(std::memory_order_relaxed) >= 0.5f;
    const float gainDb = raw_[core::kOutputGain]->load(std::memory_order_relaxed);
    outputGain_.setTarget(bypassed ? 1.0f : dsp::dbToGain(gainDb));
    outputGain_.process(channels, numChannels, numSamples);

    postFifo_.writeMix(channels, numChannels, numSamples);

    // Pico de salida para el medidor. El hilo de audio solo sube el valor; el
    // medidor lo lee y lo pone a cero. Si ambos coinciden puede perderse un
    // pico de un bloque: es un medidor, no una medida.
    outputChannels_.store(numChannels, std::memory_order_relaxed);
    for (int ch = 0; ch < std::min(numChannels, static_cast<int>(outputPeaks_.size())); ++ch)
    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(channels[ch][i]));
        auto& published = outputPeaks_[static_cast<std::size_t>(ch)];
        if (peak > published.load(std::memory_order_relaxed))
            published.store(peak, std::memory_order_relaxed);
    }
}

// El estado se guarda en el formato propio de core, no en el de JUCE:
// así sigue siendo legible aunque cambie el framework. Lleva los parámetros
// y las preferencias de la interfaz.
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const std::string text = core::serializePluginState({currentValues(), uiSettings()});
    destData.replaceAll(text.data(), text.size());
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const std::string_view text{static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes)};
    const auto state = core::deserializePluginState(text);
    if (!state)
        return;

    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(state->parameters[i]));

    setUiSettings(state->ui);
    // Lo que había antes de cargar ya no se puede deshacer.
    history_->reset();
    stateRevision_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace esceq

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new esceq::PluginProcessor();
}
