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

int normalizedSlope(int slope) noexcept
{
    const int clamped = std::clamp(slope, kMinSlopeDbPerOct, kMaxSlopeDbPerOct);
    return ((clamped + 3) / 6) * 6;
}

/// Lleva los valores a los mismos rangos que designBand(), para que el
/// suavizado trabaje siempre con valores positivos y comparables.
BandSettings sanitized(BandSettings s) noexcept
{
    s.filter.frequencyHz = std::max(s.filter.frequencyHz, kMinFrequencyHz);
    s.filter.q = std::clamp(s.filter.q, kMinQ, kMaxQ);
    s.filter.gainDb = std::clamp(s.filter.gainDb, -kMaxGainDb, kMaxGainDb);
    s.filter.slopeDbPerOct = normalizedSlope(s.filter.slopeDbPerOct);
    if (std::isnan(s.filter.frequencyHz))
        s.filter.frequencyHz = 1000.0;
    if (std::isnan(s.filter.q))
        s.filter.q = kButterworthQ;
    if (std::isnan(s.filter.gainDb))
        s.filter.gainDb = 0.0;
    return s;
}

/// Cambios que no se pueden suavizar y requieren un fundido.
bool isStructuralChange(const BandSettings& from, const BandSettings& to) noexcept
{
    if (from.enabled != to.enabled)
        return true;
    if (!to.enabled)
        return false;
    if (from.filter.type != to.filter.type)
        return true;
    return isCut(to.filter.type) && from.filter.slopeDbPerOct != to.filter.slopeDbPerOct;
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

} // namespace

void Equalizer::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothingCoeff_ = std::exp(-static_cast<double>(kChunkSize) / (kSmoothingSeconds * sampleRate_));
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kCrossfadeSeconds * sampleRate_)));
    chunkPhase_ = 0;
    reset();
}

void Equalizer::setBand(int index, const BandSettings& settings) noexcept
{
    if (index < 0 || index >= kMaxBands)
        return;

    Band& band = bands_[static_cast<std::size_t>(index)];
    const BandSettings next = sanitized(settings);
    if (next == band.target)
        return;

    band.target = next;
    if (!isStructuralChange(band.current, next))
        return; // frecuencia, ganancia y Q: los suaviza smoothBand()

    const bool wasEnabled = band.current.enabled;
    band.previous = band.active; // conserva su memoria para el fundido
    band.fadeRemaining = fadeLength_;
    band.current = next;

    if (!next.enabled)
    {
        band.active.setDesign(BandDesign{}); // identidad
        return;
    }
    if (!wasEnabled)
        band.active.reset();
    band.active.setDesign(designBand(next.filter, sampleRate_));
}

void Equalizer::setBypassed(bool bypassed) noexcept
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

void Equalizer::reset() noexcept
{
    for (auto& band : bands_)
        snapBand(band);
    wet_ = bypassed_ ? 0.0f : 1.0f;
}

void Equalizer::snapBand(Band& band) noexcept
{
    band.current = band.target;
    band.fadeRemaining = 0;
    band.active.reset();
    band.previous.reset();
    band.active.setDesign(band.current.enabled ? designBand(band.current.filter, sampleRate_) : BandDesign{});
}

const BandSettings& Equalizer::target(int index) const noexcept
{
    return bands_[static_cast<std::size_t>(std::clamp(index, 0, kMaxBands - 1))].target;
}

bool Equalizer::isSettled() const noexcept
{
    if (wet_ != (bypassed_ ? 0.0f : 1.0f))
        return false;
    return std::all_of(bands_.begin(), bands_.end(), [](const Band& band) {
        return band.fadeRemaining == 0 && band.current == band.target;
    });
}

void Equalizer::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    const int usedChannels = std::min(numChannels, kMaxChannels);
    int position = 0;

    // Trozos alineados con un contador global: así el suavizado ocurre en
    // los mismos instantes sea cual sea el tamaño de bloque del host.
    while (position < numSamples)
    {
        const int length = std::min(numSamples - position, kChunkSize - chunkPhase_);
        processChunk(channels, usedChannels, position, length);
        chunkPhase_ = (chunkPhase_ + length) % kChunkSize;
        position += length;
    }
}

void Equalizer::processChunk(float* const* channels, int numChannels, int offset, int length) noexcept
{
    if (chunkPhase_ == 0)
        for (auto& band : bands_)
            smoothBand(band);

    const float wetTarget = bypassed_ ? 0.0f : 1.0f;
    if (wet_ <= 0.0f && wetTarget <= 0.0f)
        return; // bypass completo: la señal pasa tal cual

    std::array<float*, kMaxChannels> io{};
    for (int ch = 0; ch < numChannels; ++ch)
        io[static_cast<std::size_t>(ch)] = channels[ch] + offset;

    const bool mixing = wet_ != wetTarget || wet_ < 1.0f;
    if (mixing)
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy_n(io[static_cast<std::size_t>(ch)], length, dry_[static_cast<std::size_t>(ch)].begin());

    for (auto& band : bands_)
        if (band.current.enabled || band.fadeRemaining > 0)
            processBand(band, io.data(), numChannels, length);

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

void Equalizer::processBand(Band& band, float* const* io, int numChannels, int length) noexcept
{
    if (band.fadeRemaining == 0)
    {
        band.active.process(io, numChannels, length);
        return;
    }

    std::array<float*, kMaxChannels> old{};
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& buffer = scratch_[static_cast<std::size_t>(ch)];
        std::copy_n(io[ch], length, buffer.begin());
        old[static_cast<std::size_t>(ch)] = buffer.data();
    }

    band.previous.process(old.data(), numChannels, length);
    band.active.process(io, numChannels, length);

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

void Equalizer::smoothBand(Band& band) noexcept
{
    if (!band.current.enabled || !band.target.enabled)
        return;

    FilterParams& now = band.current.filter;
    const FilterParams& goal = band.target.filter;

    // Tipo y pendiente solo llegan aquí cuando no influyen en el sonido
    // (por ejemplo, la pendiente de una campana): se copian sin más.
    now.type = goal.type;
    now.slopeDbPerOct = goal.slopeDbPerOct;

    if (now.frequencyHz == goal.frequencyHz && now.gainDb == goal.gainDb && now.q == goal.q)
        return;

    now.frequencyHz = approachLog(now.frequencyHz, goal.frequencyHz, smoothingCoeff_);
    now.q = approachLog(now.q, goal.q, smoothingCoeff_);
    now.gainDb = approachLinear(now.gainDb, goal.gainDb, smoothingCoeff_, 1.0e-3);
    band.active.setDesign(designBand(now, sampleRate_));
}

} // namespace esceq::dsp
