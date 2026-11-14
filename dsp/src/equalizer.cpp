// SPDX-License-Identifier: MIT
#include "esceq/dsp/equalizer.h"

#include <algorithm>
#include <cmath>

namespace esceq::dsp {

namespace {

bool isCut(FilterType type) noexcept
{
    return type == FilterType::LowCut || type == FilterType::HighCut;
}

double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

} // namespace

/// Lleva los valores a los mismos rangos que designBand(), para que el
/// suavizado trabaje siempre con valores positivos y comparables.
BandSettings sanitizeBand(BandSettings s) noexcept ESCEQ_NONBLOCKING
{
    // Los no finitos pasan a su valor por defecto antes de ajustar el rango: un
    // infinito ajustado seguiría rompiendo el suavizado logarítmico.
    s.filter.frequencyHz = std::max(finiteOr(s.filter.frequencyHz, 1000.0), kMinFrequencyHz);
    s.filter.q = std::clamp(finiteOr(s.filter.q, kButterworthQ), kMinQ, kMaxQ);
    s.filter.gainDb = std::clamp(finiteOr(s.filter.gainDb, 0.0), -kMaxGainDb, kMaxGainDb);
    s.filter.slopeDbPerOct = sanitizeSlope(s.filter.slopeDbPerOct);

    const DynamicSettings defaults{};
    DynamicSettings& d = s.dynamics;
    d.thresholdDb = std::clamp(finiteOr(d.thresholdDb, defaults.thresholdDb), -120.0, 24.0);
    d.rangeDb = std::clamp(finiteOr(d.rangeDb, defaults.rangeDb), -kMaxGainDb, kMaxGainDb);
    d.kneeDb = std::clamp(finiteOr(d.kneeDb, defaults.kneeDb), 0.0, 48.0);
    d.attackMs = std::clamp(finiteOr(d.attackMs, defaults.attackMs), kMinAttackMs, 10000.0);
    d.releaseMs = std::clamp(finiteOr(d.releaseMs, defaults.releaseMs), kMinReleaseMs, 60000.0);
    return s;
}

namespace {

/// Cambios que no se pueden suavizar y requieren un fundido.
bool isStructuralChange(const BandSettings& from, const BandSettings& to) noexcept
{
    if (from.enabled != to.enabled)
        return true;
    if (!to.enabled)
        return false;
    if (from.filter.type != to.filter.type || from.channel != to.channel)
        return true;
    return isCut(to.filter.type) && from.filter.slopeDbPerOct != to.filter.slopeDbPerOct;
}

/// La dinámica de la banda está pedida y tiene efecto con su tipo.
bool wantsDynamics(const BandSettings& s) noexcept
{
    return s.enabled && s.dynamics.enabled && supportsDynamics(s.filter.type);
}

double approachLog(double current, double target, double coeff) noexcept
{
    const double next = target * std::pow(current / target, coeff);
    return std::abs(std::log(next / target)) < 1.0e-4 ? target : next;
}

double approachLinear(double current, double target, double coeff, double snap) noexcept
{
    const double next = target + (current - target) * coeff;
    return std::abs(next - target) < snap ? target : next;
}

/// Parámetros que determinan el filtro del detector (la ganancia no influye).
FilterParams detectorKey(FilterParams params) noexcept
{
    params.gainDb = 0.0;
    return params;
}

} // namespace

void Equalizer::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothingCoeff_ = std::exp(-static_cast<double>(kChunkSize) / (kSmoothingSeconds * sampleRate_));
    sampleSmoothingCoeff_ = std::exp(-1.0 / (kSmoothingSeconds * sampleRate_));
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kCrossfadeSeconds * sampleRate_)));
    chunkPhase_ = 0;
    for (auto& band : bands_)
    {
        band.envelope.prepare(sampleRate_);
        band.prewarpedFor = -1.0; // la tangente depende de la frecuencia de muestreo
    }
    reset();
}

void Equalizer::setBand(int index, const BandSettings& settings) noexcept ESCEQ_NONBLOCKING
{
    if (index < 0 || index >= kMaxBands)
        return;

    Band& band = bands_[static_cast<std::size_t>(index)];
    band.requested = sanitizeBand(settings);
    const BandSettings next = targetFor(band.requested);
    if (next == band.target)
        return;

    band.target = next;
    if (!band.current.enabled && !next.enabled)
    {
        // Los ajustes de una banda apagada no suenan: se copian sin más.
        band.current = next;
        band.structuralPending = false;
        return;
    }
    if (!isStructuralChange(band.current, next))
        return; // frecuencia, ganancia, Q y dinámica: los suaviza smoothBand()

    // Con un fundido en curso, empezar otro descartaría de golpe el filtro
    // que se está desvaneciendo: se oiría un salto. El cambio espera.
    if (band.fadeRemaining > 0)
    {
        band.structuralPending = true;
        return;
    }
    beginStructuralChange(band);
}

void Equalizer::beginStructuralChange(Band& band) noexcept
{
    band.structuralPending = false;
    const BandSettings& next = band.target;
    if (!isStructuralChange(band.current, next))
        return; // lo que esperaba se ha deshecho: lo que quede, lo suaviza smoothBand()

    const bool wasEnabled = band.current.enabled;
    const bool channelChanged = band.current.channel != next.channel;
    band.previous = band.active; // conserva su memoria para el fundido
    band.previousChannel = band.current.channel;
    band.fadeRemaining = fadeLength_;
    band.current = next;

    if (!next.enabled)
    {
        band.active.setDesign(BandDesign{}); // identidad
        band.designed = {};
        band.dynamicRunning = false;
        band.dynamicMix = 0.0;
        band.dynamicGain = 0.0;
        return;
    }
    // La memoria del filtro solo sirve si va a seguir oyendo la misma señal.
    if (!wasEnabled || channelChanged)
        band.active.reset();
    if (channelChanged)
    {
        band.detector.reset();
        band.envelope.reset();
    }
    updateKneeStart(band);
    if (!supportsDynamics(next.filter.type))
    {
        // El tipo nuevo no admite dinámica: el fundido oculta su desaparición.
        band.dynamicRunning = false;
        band.dynamicMix = 0.0;
        band.dynamicGain = 0.0;
    }
    applyDesign(band, true);
}

BandSettings Equalizer::targetFor(const BandSettings& requested) const noexcept
{
    BandSettings target = requested;
    if (dynamicsOnly_)
        target.enabled = wantsDynamics(requested);
    return target;
}

void Equalizer::setDynamicsOnly(bool dynamicsOnly) noexcept ESCEQ_NONBLOCKING
{
    if (dynamicsOnly == dynamicsOnly_)
        return;
    dynamicsOnly_ = dynamicsOnly;
    for (auto& band : bands_)
        band.target = targetFor(band.requested);
    reset();
}

void Equalizer::setBypassed(bool bypassed) noexcept ESCEQ_NONBLOCKING
{
    if (bypassed == bypassed_)
        return;

    // Al salir de un bypass completo los filtros llevan tiempo parados:
    // se reinician limpios y el fundido de entrada oculta el arranque.
    if (!bypassed && wet_ <= 0.0f)
        for (auto& band : bands_)
            snapBand(band);

    bypassed_ = bypassed;
}

void Equalizer::reset() noexcept ESCEQ_NONBLOCKING
{
    for (auto& band : bands_)
        snapBand(band);
    wet_ = bypassed_ ? 0.0f : 1.0f;
}

void Equalizer::snapBand(Band& band) noexcept
{
    band.current = band.target;
    band.fadeRemaining = 0;
    band.structuralPending = false;
    band.active.reset();
    band.previous.reset();
    band.detector.reset();
    band.envelope.reset();
    band.envelope.setTimes(band.current.dynamics.attackMs, band.current.dynamics.releaseMs);
    band.dynamicRunning = wantsDynamics(band.current);
    band.dynamicMix = band.dynamicRunning ? 1.0 : 0.0;
    band.dynamicGain = 0.0;
    band.detectorFor = detectorKey(band.current.filter);
    band.detector.setDesign(designDetector(band.current.filter, sampleRate_));
    updateKneeStart(band);

    if (band.current.enabled)
        applyDesign(band, true);
    else
    {
        band.active.setDesign(BandDesign{});
        band.designed = {};
    }
}

const BandSettings& Equalizer::target(int index) const noexcept
{
    return bands_[static_cast<std::size_t>(std::clamp(index, 0, kMaxBands - 1))].target;
}

double Equalizer::dynamicGainDb(int index) const noexcept
{
    if (index < 0 || index >= kMaxBands)
        return 0.0;
    return bands_[static_cast<std::size_t>(index)].dynamicGain;
}

bool Equalizer::isSettled() const noexcept
{
    if (wet_ != (bypassed_ ? 0.0f : 1.0f))
        return false;
    return std::all_of(bands_.begin(), bands_.end(), [](const Band& band) {
        const double mixTarget = wantsDynamics(band.current) ? 1.0 : 0.0;
        return band.fadeRemaining == 0 && band.current == band.target && band.dynamicMix == mixTarget;
    });
}

void Equalizer::updateKneeStart(Band& band) noexcept
{
    const DynamicSettings& d = band.current.dynamics;
    band.kneeStartLinear = std::pow(10.0, (d.thresholdDb - 0.5 * d.kneeDb) / 20.0);
}

bool Equalizer::isDynamic(const Band& band) noexcept
{
    return band.current.enabled && supportsDynamics(band.current.filter.type) &&
           (band.current.dynamics.enabled || band.dynamicMix > 0.0);
}

void Equalizer::applyDesign(Band& band, bool force) noexcept
{
    FilterParams effective = band.current.filter;
    effective.gainDb += band.dynamicGain;
    const double staticGain = band.current.filter.gainDb;
    if (!force && effective == band.designed && (!dynamicsOnly_ || staticGain == band.designedStaticGain))
        return;
    band.designed = effective;
    band.designedStaticGain = staticGain;
    // La dinámica rediseña la banda hasta una vez por muestra cambiando solo la
    // ganancia: la tangente de la frecuencia se guarda y no se recalcula.
    if (effective.frequencyHz != band.prewarpedFor)
    {
        band.prewarpedFor = effective.frequencyHz;
        band.prewarped = prewarpedG(effective.frequencyHz, sampleRate_);
    }
    const BandDesign full = designBandPrewarped(effective, band.prewarped);
    if (!dynamicsOnly_)
    {
        band.active.setDesign(full);
        return;
    }

    // Solo el cambio dinámico: primero H(−g) = 1/H(g), que deshace la parte
    // estática, y después la banda entera, H(g + d). En ese orden, la sección
    // que cambia con la dinámica oye la misma señal que en el modo normal, y
    // sus transitorios son los mismos. Los tipos con ganancia usan una
    // sección, así que caben las dos.
    FilterParams inverse = band.current.filter;
    inverse.gainDb = -staticGain;
    BandDesign design = designBandPrewarped(inverse, band.prewarped);
    for (int i = 0; i < full.numSections && design.numSections < kMaxSections; ++i)
        design.sections[static_cast<std::size_t>(design.numSections++)] = full.sections[static_cast<std::size_t>(i)];
    band.active.setDesign(design);
}

void Equalizer::process(float* const* channels, int numChannels, int numSamples, const float* const* sidechain,
                        int numSidechainChannels, const float* const* detectorInput) noexcept ESCEQ_NONBLOCKING
{
    const int usedChannels = std::min(numChannels, kMaxChannels);
    Sources sc{sidechain, sidechain != nullptr ? std::clamp(numSidechainChannels, 0, kMaxChannels) : 0,
               detectorInput, 0};
    int position = 0;

    // Trozos alineados con un contador global: así el suavizado ocurre en
    // los mismos instantes sea cual sea el tamaño de bloque del host.
    while (position < numSamples)
    {
        const int length = std::min(numSamples - position, kChunkSize - chunkPhase_);
        sc.offset = position;
        processChunk(channels, usedChannels, position, length, sc);
        chunkPhase_ = (chunkPhase_ + length) % kChunkSize;
        position += length;
    }

    // Un NaN o un infinito que haya entrado se queda para siempre en la memoria
    // de los filtros. Se detecta en la salida (un estado no finito siempre
    // llega a ella) y se vuelve a empezar desde cero, con el bloque en silencio.
    bool finite = true;
    for (int ch = 0; ch < usedChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            finite = finite && std::isfinite(channels[ch][i]);
    if (!finite)
    {
        for (int ch = 0; ch < usedChannels; ++ch)
            std::fill_n(channels[ch], numSamples, 0.0f);
        reset();
    }
}

void Equalizer::processChunk(float* const* channels, int numChannels, int offset, int length,
                             const Sources& sources) noexcept
{
    if (chunkPhase_ == 0)
        for (auto& band : bands_)
        {
            if (band.structuralPending && band.fadeRemaining == 0)
                beginStructuralChange(band);
            smoothBand(band);
        }

    const float wetTarget = bypassed_ ? 0.0f : 1.0f;
    if (wet_ <= 0.0f && wetTarget <= 0.0f)
        return; // bypass completo: la señal pasa tal cual

    std::array<float*, kMaxChannels> io{};
    for (int ch = 0; ch < numChannels; ++ch)
        io[static_cast<std::size_t>(ch)] = channels[ch] + offset;

    // Los detectores internos escuchan la entrada del ecualizador, no la
    // salida de la banda anterior: así las bandas no se influyen entre sí.
    const bool anyDynamic = std::any_of(bands_.begin(), bands_.end(), isDynamic);
    if (anyDynamic)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* source = sources.detector != nullptr ? sources.detector[ch] + sources.offset
                                                              : io[static_cast<std::size_t>(ch)];
            std::copy_n(source, length, detectorInput_[static_cast<std::size_t>(ch)].begin());
        }

    const bool mixing = wet_ != wetTarget || wet_ < 1.0f;
    if (mixing)
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy_n(io[static_cast<std::size_t>(ch)], length, dry_[static_cast<std::size_t>(ch)].begin());

    for (auto& band : bands_)
        if (band.current.enabled || band.fadeRemaining > 0)
            processBand(band, io.data(), numChannels, length, sources);

    if (!mixing)
        return;

    const float step = 1.0f / static_cast<float>(fadeLength_);
    for (int i = 0; i < length; ++i)
    {
        wet_ = wetTarget > wet_ ? std::min(wetTarget, wet_ + step) : std::max(wetTarget, wet_ - step);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = dry_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
            float& sample = io[static_cast<std::size_t>(ch)][i];
            sample = dry + wet_ * (sample - dry);
        }
    }
}

void Equalizer::processBand(Band& band, float* const* io, int numChannels, int length,
                            const Sources& sources) noexcept
{
    if (!isDynamic(band))
    {
        if (band.dynamicRunning)
        {
            // La dinámica ha terminado de desvanecerse: vuelve el diseño estático exacto.
            band.dynamicRunning = false;
            band.dynamicGain = 0.0;
            if (band.current.enabled)
                applyDesign(band);
        }
        processSegment(band, io, numChannels, length);
        return;
    }

    if (!band.dynamicRunning)
    {
        // Arranque de la dinámica: detector y envolvente desde cero.
        band.dynamicRunning = true;
        band.detector.reset();
        band.envelope.reset();
    }

    runDetector(band, numChannels, length, sources);

    // La ganancia dinámica se evalúa en cada muestra. Los coeficientes solo
    // se recalculan cuando cambia de verdad: el audio se procesa por tramos
    // con el mismo diseño, y cada cambio parte el tramo.
    std::array<float*, kMaxChannels> segment{};
    auto flush = [&](int from, int to) {
        if (to <= from)
            return;
        for (int ch = 0; ch < numChannels; ++ch)
            segment[static_cast<std::size_t>(ch)] = io[ch] + from;
        processSegment(band, segment.data(), numChannels, to - from);
    };

    int start = 0;
    for (int i = 0; i < length; ++i)
    {
        if (!updateDynamicGain(band, envelopeBuffer_[static_cast<std::size_t>(i)]))
            continue;
        flush(start, i);
        applyDesign(band);
        start = i;
    }
    flush(start, length);
}

void Equalizer::runDetector(Band& band, int numChannels, int length, const Sources& sources) noexcept
{
    const FilterParams key = detectorKey(band.current.filter);
    if (!(key == band.detectorFor))
    {
        band.detectorFor = key;
        band.detector.setDesign(designDetector(band.current.filter, sampleRate_));
    }

    int sourceChannels = numChannels;
    std::array<float*, kMaxChannels> work{};
    if (band.current.dynamics.external)
    {
        sourceChannels = sources.numSidechainChannels;
        for (int ch = 0; ch < sourceChannels; ++ch)
            std::copy_n(sources.sidechain[ch] + sources.offset, length,
                        detectorScratch_[static_cast<std::size_t>(ch)].begin());
    }
    else
    {
        for (int ch = 0; ch < sourceChannels; ++ch)
            std::copy_n(detectorInput_[static_cast<std::size_t>(ch)].begin(), length,
                        detectorScratch_[static_cast<std::size_t>(ch)].begin());
    }
    for (int ch = 0; ch < sourceChannels; ++ch)
        work[static_cast<std::size_t>(ch)] = detectorScratch_[static_cast<std::size_t>(ch)].data();

    // El detector escucha la misma parte de la señal que procesa la banda.
    // Una fuente mono (el sidechain, por ejemplo) se escucha tal cual.
    if (sourceChannels == 2 && band.current.channel != ChannelMode::Stereo)
    {
        float* const left = work[0];
        float* const right = work[1];
        switch (band.current.channel)
        {
        case ChannelMode::Right:
            work[0] = right;
            break;
        case ChannelMode::Mid:
            for (int i = 0; i < length; ++i)
                left[i] = 0.5f * (left[i] + right[i]);
            break;
        case ChannelMode::Side:
            for (int i = 0; i < length; ++i)
                left[i] = 0.5f * (left[i] - right[i]);
            break;
        case ChannelMode::Stereo:
        case ChannelMode::Left:
            break;
        }
        sourceChannels = 1;
    }

    if (sourceChannels > 0)
        band.detector.process(work.data(), sourceChannels, length);

    // El detector no llega a la salida: si el sidechain trae un NaN o un
    // infinito, su memoria se estropea sin que se note y la banda deja de
    // actuar para siempre. Se vigila aquí y se reinicia.
    if (!band.detector.isFinite())
    {
        band.detector.reset();
        band.envelope.reset();
        for (int ch = 0; ch < sourceChannels; ++ch)
            std::fill_n(work[static_cast<std::size_t>(ch)], length, 0.0f);
    }

    // En estéreo la detección es enlazada: el canal más fuerte manda, y los
    // dos reciben la misma ganancia (la imagen estéreo no se mueve).
    for (int i = 0; i < length; ++i)
    {
        float level = 0.0f;
        for (int ch = 0; ch < sourceChannels; ++ch)
            level = std::max(level, std::abs(work[static_cast<std::size_t>(ch)][i]));
        envelopeBuffer_[static_cast<std::size_t>(i)] = band.envelope.process(static_cast<double>(level));
    }
}

bool Equalizer::updateDynamicGain(Band& band, double envelope) noexcept
{
    const double mixTarget = wantsDynamics(band.current) ? 1.0 : 0.0;
    if (band.dynamicMix != mixTarget)
    {
        const double mixStep = 1.0 / static_cast<double>(fadeLength_);
        band.dynamicMix = mixTarget > band.dynamicMix ? std::min(mixTarget, band.dynamicMix + mixStep)
                                                      : std::max(mixTarget, band.dynamicMix - mixStep);
    }

    DynamicSettings& d = band.current.dynamics;
    const DynamicSettings& goal = band.target.dynamics;
    if (d.thresholdDb != goal.thresholdDb || d.rangeDb != goal.rangeDb || d.kneeDb != goal.kneeDb)
    {
        d.thresholdDb = approachLinear(d.thresholdDb, goal.thresholdDb, sampleSmoothingCoeff_, 1.0e-3);
        d.rangeDb = approachLinear(d.rangeDb, goal.rangeDb, sampleSmoothingCoeff_, 1.0e-3);
        d.kneeDb = approachLinear(d.kneeDb, goal.kneeDb, sampleSmoothingCoeff_, 1.0e-3);
        updateKneeStart(band);
    }

    // Por debajo del principio de la rodilla la ganancia es 0: sin logaritmo.
    double gain = 0.0;
    if (envelope > band.kneeStartLinear)
    {
        const double levelDb = 20.0 * std::log10(envelope);
        gain = band.dynamicMix * dsp::dynamicGainDb(levelDb, d.thresholdDb, d.rangeDb, d.kneeDb);
    }

    // Cambios menores de 0,001 dB (una parte en 10^4,6) se acumulan hasta
    // superar ese umbral; volver a 0 dB exactos siempre se aplica.
    const double delta = gain - band.dynamicGain;
    if (delta == 0.0 || (std::abs(delta) < kDynamicGainResolutionDb && gain != 0.0))
        return false;
    band.dynamicGain = gain;
    return true;
}

void Equalizer::processSegment(Band& band, float* const* io, int numChannels, int length) noexcept
{
    if (band.fadeRemaining == 0)
    {
        processRouted(band.active, band.current.channel, io, numChannels, length);
        return;
    }

    std::array<float*, kMaxChannels> old{};
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& buffer = scratch_[static_cast<std::size_t>(ch)];
        std::copy_n(io[ch], length, buffer.begin());
        old[static_cast<std::size_t>(ch)] = buffer.data();
    }

    processRouted(band.previous, band.previousChannel, old.data(), numChannels, length);
    processRouted(band.active, band.current.channel, io, numChannels, length);

    const float fadeLength = static_cast<float>(fadeLength_);
    for (int i = 0; i < length; ++i)
    {
        float gain = 1.0f;
        if (band.fadeRemaining > 0)
        {
            gain = static_cast<float>(fadeLength_ - band.fadeRemaining + 1) / fadeLength;
            --band.fadeRemaining;
        }
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float before = old[static_cast<std::size_t>(ch)][i];
            io[ch][i] = before + gain * (io[ch][i] - before);
        }
    }
}

void Equalizer::processRouted(BandFilter& filter, ChannelMode mode, float* const* io, int numChannels,
                              int length) noexcept
{
    if (numChannels < 2)
    {
        // Mono: el único canal es a la vez el izquierdo y la suma.
        if (numChannels == 1 && mode != ChannelMode::Right && mode != ChannelMode::Side)
            filter.process(io, 1, length);
        return;
    }

    switch (mode)
    {
    case ChannelMode::Stereo:
        filter.process(io, numChannels, length);
        return;
    case ChannelMode::Left:
        filter.process(io, 1, length);
        return;
    case ChannelMode::Right:
        filter.process(io + 1, 1, length);
        return;
    case ChannelMode::Mid:
    case ChannelMode::Side:
        break;
    }

    // Mid/Side: se filtra la suma (o la diferencia) y se reparte entre los
    // canales solo lo que el filtro ha CAMBIADO. Así la otra componente pasa
    // bit a bit, y también toda la señal si el filtro no hace nada.
    const float sign = mode == ChannelMode::Mid ? 1.0f : -1.0f;
    float* const left = io[0];
    float* const right = io[1];
    for (int i = 0; i < length; ++i)
    {
        const float value = 0.5f * (left[i] + sign * right[i]);
        routeBuffer_[static_cast<std::size_t>(i)] = value;
        routeDry_[static_cast<std::size_t>(i)] = value;
    }

    float* work = routeBuffer_.data();
    filter.process(&work, 1, length);

    for (int i = 0; i < length; ++i)
    {
        const float delta = routeBuffer_[static_cast<std::size_t>(i)] - routeDry_[static_cast<std::size_t>(i)];
        left[i] += delta;
        right[i] += sign * delta;
    }
}

void Equalizer::smoothBand(Band& band) noexcept
{
    // Con un cambio estructural esperando, la banda sigue sonando como está.
    if (band.structuralPending || !band.current.enabled || !band.target.enabled)
        return;

    FilterParams& now = band.current.filter;
    const FilterParams& goal = band.target.filter;

    // Tipo y pendiente solo llegan aquí cuando no influyen en el sonido
    // (por ejemplo, la pendiente de una campana): se copian sin más.
    now.type = goal.type;
    now.slopeDbPerOct = goal.slopeDbPerOct;

    // Dinámica: los interruptores y los tiempos se aplican al momento (el
    // fundido de la mezcla y la envolvente evitan los saltos). Umbral, rango y
    // rodilla se suavizan muestra a muestra en updateDynamicGain(), porque la
    // ganancia dinámica los sigue 1:1 y unos escalones cada kChunkSize
    // muestras se oirían. Si la dinámica no está sonando, saltan al objetivo.
    DynamicSettings& dynNow = band.current.dynamics;
    const DynamicSettings& dynGoal = band.target.dynamics;
    dynNow.enabled = dynGoal.enabled;
    dynNow.external = dynGoal.external;
    dynNow.attackMs = dynGoal.attackMs;
    dynNow.releaseMs = dynGoal.releaseMs;
    band.envelope.setTimes(dynNow.attackMs, dynNow.releaseMs);
    if (!isDynamic(band) && !(dynNow == dynGoal))
    {
        dynNow = dynGoal;
        updateKneeStart(band);
    }

    if (now.frequencyHz == goal.frequencyHz && now.gainDb == goal.gainDb && now.q == goal.q)
        return;

    now.frequencyHz = approachLog(now.frequencyHz, goal.frequencyHz, smoothingCoeff_);
    now.q = approachLog(now.q, goal.q, smoothingCoeff_);
    now.gainDb = approachLinear(now.gainDb, goal.gainDb, smoothingCoeff_, 1.0e-3);

    applyDesign(band);
}

} // namespace esceq::dsp
