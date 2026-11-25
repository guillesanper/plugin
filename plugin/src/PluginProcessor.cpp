// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "JuceText.h"

#include "esceq/core/band_model.h"
#include "esceq/core/state.h"
#include "esceq/dsp/decibels.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/spectral_dynamics.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

namespace esceq {

namespace {

constexpr double kGainRampSeconds = 0.02;

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
    : PluginProcessor(core::SpectrumSession::shared())
{
}

PluginProcessor::PluginProcessor(std::shared_ptr<core::SpectrumSession> session,
                                 core::MatchWorker::Threading matchThreading)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         // Bus auxiliar opcional para la detección externa de las
                         // bandas dinámicas. En FL Studio se enruta desde el mezclador.
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts_(*this, nullptr, "esceQ", createLayout()),
      matchWorker_(matchThreading)
{
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
    {
        const auto id = toJuce(core::parameters()[i].id);
        raw_[i] = apvts_.getRawParameterValue(id);
        params_[i] = apvts_.getParameter(id);
        jassert(raw_[i] != nullptr && params_[i] != nullptr);
    }
    history_ = std::make_unique<EditHistory>(*this);

    jassert(session != nullptr);
    session_ = std::move(session);
    sessionMember_ = session_->join();
}

// Fuera de línea: la sesión se suelta aquí, y si era la última instancia del
// proceso, su hilo se para aquí (en el hilo que destruye el plugin, nunca al
// descargar el binario).
PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::LatencyReporter::handleAsyncUpdate()
{
    // setLatencySamples() avisa al host, y eso no se hace desde el hilo de audio.
    const int latency = owner_.engineLatencySamples();
    if (latency != owner_.getLatencySamples())
        owner_.setLatencySamples(latency);
}

double PluginProcessor::getTailLengthSeconds() const
{
    const auto mode = core::phaseModeFromChoice(raw_[core::kPhaseMode]->load(std::memory_order_relaxed));
    const double rate = currentSampleRate();
    const auto size = dsp::linearPhaseSize(mode, rate);
    // Con alguna banda espectral, su latencia (lo que tarda en salir lo último
    // que entró; la trama que lo contiene ya está dentro de ella).
    bool spectral = false;
    for (int band = 0; band < core::kNumBands; ++band)
        spectral = spectral || raw_[core::spectralParameter(band)]->load(std::memory_order_relaxed) >= 0.5f;
    const int spectralLatency = spectral ? dsp::SpectralDynamics::latencySamples(rate) : 0;
    // Y fuera de Off, la de la saturación (0 en IIR).
    const bool saturating = core::saturationModeFromChoice(raw_[core::kSaturationMode]->load(
                                std::memory_order_relaxed)) != dsp::SaturationMode::Off;
    const int saturationLatency = saturating ? dsp::PhaseEqualizer::saturationLatency(mode, rate) : 0;
    return static_cast<double>(size.latency() + size.firLength / 2 + spectralLatency + saturationLatency) / rate;
}

void PluginProcessor::updateTrackProperties(const TrackProperties& properties)
{
    sessionMember_->setTrackName(properties.name.value_or(juce::String{}).trim().toStdString());
    sessionMember_->setTrackColour(properties.colourARGB);
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

float PluginProcessor::currentValue(std::size_t index) const noexcept
{
    return index < core::kNumParameters ? raw_[index]->load(std::memory_order_relaxed) : 0.0f;
}

float PluginProcessor::dynamicGainDb(int band) const noexcept
{
    if (band < 0 || band >= core::kNumBands)
        return 0.0f;
    return dynamicGains_[static_cast<std::size_t>(band)].load(std::memory_order_relaxed);
}

void PluginProcessor::setWithGesture(std::size_t index, float value)
{
    const float target = core::sanitize(core::parameters()[index], value);
    const float now = raw_[index]->load(std::memory_order_relaxed);
    if (!(target < now) && !(target > now))
        return; // ya tiene ese valor
    params_[index]->beginChangeGesture();
    params_[index]->setValueNotifyingHost(params_[index]->convertTo0to1(target));
    params_[index]->endChangeGesture();
}

void PluginProcessor::applyValues(const core::ParameterValues& values)
{
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        setWithGesture(i, values[i]);
}

void PluginProcessor::applyAction(const core::ParameterValues& values)
{
    // Lo que el usuario acaba de hacer es una acción aparte: deshacer esta
    // no lo deshace (como en PresetManager::load).
    history_->flushPending();
    applyValues(values);
    history_->commitNow();
}

void PluginProcessor::setFilterAlgorithm(dsp::FilterAlgorithm algorithm)
{
    if (filterAlgorithm_.exchange(algorithm, std::memory_order_relaxed) == algorithm)
        return;
    // No es un parámetro: sin este aviso, el host no sabría que el proyecto
    // ha cambiado y podría cerrarlo sin pedir que se guarde.
    updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
}

void PluginProcessor::changeFilterAlgorithm(dsp::FilterAlgorithm algorithm)
{
    if (algorithm == filterAlgorithm())
        return;
    // Lo que el usuario acaba de hacer (un arrastre que aún espera su
    // registro) es una acción aparte: deshacer el algoritmo no lo deshace.
    history_->flushPending();
    history_->touchFilterAlgorithm();
    setFilterAlgorithm(algorithm);
    history_->commitNow();
}

core::EditState PluginProcessor::currentEditState() const noexcept
{
    return {currentValues(), filterAlgorithm()};
}

void PluginProcessor::applyEdit(const core::EditAction& action)
{
    for (const auto& change : action.parameters)
        if (change.index < core::kNumParameters)
            setWithGesture(change.index, change.after);
    if (action.algorithmAfter)
        setFilterAlgorithm(*action.algorithmAfter);
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
    // Traducir los parámetros cuesta unos 2 µs: con bloques muy pequeños (FL
    // Studio los entrega a veces de pocas muestras) se nota. Si no ha cambiado
    // ninguno, el ecualizador ya tiene sus ajustes.
    const auto values = currentValues();
    const auto algorithm = filterAlgorithm();
    if (!force && values == pushedValues_ && algorithm == pushedAlgorithm_)
        return;
    pushedValues_ = values;
    pushedAlgorithm_ = algorithm;
    const auto bands = core::allBandSettings(values, algorithm);
    for (int band = 0; band < core::kNumBands; ++band)
        equalizer_.setBand(band, bands[static_cast<std::size_t>(band)]);
    equalizer_.setBypassed(values[core::kBypass] >= 0.5f);
    equalizer_.setPhaseMode(core::phaseModeFromChoice(values[core::kPhaseMode]));
    equalizer_.setSaturation(core::saturationSettings(values));

    // La compensación la calcula el hilo de control (ADR 0015), y solo si
    // está encendida. Al encenderla, lo calculado antes no vale: la ganancia
    // espera al valor de estos ajustes.
    const bool autoGain = values[core::kAutoGain] >= 0.5f;
    if (autoGain)
    {
        if (!autoGainPushed_ || force)
            autoGain_.restart();
        autoGain_.request(bands, currentSampleRate(), engineChannels_);
    }
    autoGainPushed_ = autoGain;
}

float PluginProcessor::autoGainDb() const noexcept
{
    return autoGainPushed_ ? autoGain_.latestDb().value_or(0.0f) : 0.0f;
}

void PluginProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_.store(sampleRate);
    sessionMember_->setSampleRate(sampleRate);
    engineChannels_ = std::clamp(getMainBusNumOutputChannels(), 1, 2);
    pushParametersToEngine(true);
    equalizer_.prepare(sampleRate, engineChannels_);
    spectralMask_.prepare(sampleRate);
    // En fase lineal, el FIR se diseña ya, y la compensación se calcula ya:
    // así el primer bloque suena en el modo pedido, con su ganancia, y el
    // host conoce la latencia antes de empezar.
    control_.flush();
    equalizer_.update();
    setLatencySamples(equalizer_.latencySamples());

    const bool bypassed = raw_[core::kBypass]->load() >= 0.5f;
    outputGain_.prepare(sampleRate, kGainRampSeconds);
    outputGain_.reset(bypassed ? 1.0f : dsp::dbToGain(raw_[core::kOutputGain]->load() + autoGainDb()));
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
    // Para aprender del sidechain (igualación). Apagada, solo lee un atómico.
    matchSidechain_.write(sidechain, sidechainChannels, numSamples);

    preFifo_.writeMix(channels, numChannels, numSamples);

    pushParametersToEngine();
    // Sin tiempo real (al renderizar), el FIR y la compensación de los ajustes
    // de este bloque se calculan aquí mismo: el resultado no depende de lo
    // rápido que vaya el render ni de lo que tarde el hilo de control.
    if (isNonRealtime())
        control_.flush();
    equalizer_.process(channels, numChannels, numSamples, sidechain, sidechainChannels);

    for (int band = 0; band < core::kNumBands; ++band)
        dynamicGains_[static_cast<std::size_t>(band)].store(static_cast<float>(equalizer_.dynamicGainDb(band)),
                                                            std::memory_order_relaxed);
    // La forma de la máscara, para el sombreado de la curva: solo se lee del ecualizador.
    spectralMask_.publish(equalizer_, numSamples);

    const bool bypassed = raw_[core::kBypass]->load(std::memory_order_relaxed) >= 0.5f;
    // La compensación se suma a la ganancia de salida; con bypass, ninguna de
    // las dos. Apagada, o con la curva plana, suma exactamente 0.
    const float gainDb = raw_[core::kOutputGain]->load(std::memory_order_relaxed) + autoGainDb();
    outputGain_.setTarget(bypassed ? 1.0f : dsp::dbToGain(gainDb));
    outputGain_.process(channels, numChannels, numSamples);

    postFifo_.writeMix(channels, numChannels, numSamples);
    // Lo que sale de la pista, para las demás instancias. Si nadie compara, no hace nada.
    sessionMember_->feed(channels, numChannels, numSamples);

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
// así sigue siendo legible aunque cambie el framework. Lleva los parámetros,
// el algoritmo de filtro y las preferencias de la interfaz.
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const std::string text = core::serializePluginState({currentValues(), filterAlgorithm(), uiSettings()});
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
    // Sin avisar al host: es el estado que acaba de darnos.
    filterAlgorithm_.store(state->filterAlgorithm, std::memory_order_relaxed);

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
