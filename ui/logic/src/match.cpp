// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/match.h"

#include "esceq/core/band_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace esceq::ui
{

namespace
{

const core::InstanceSpectrum* findInstance(std::span<const core::InstanceSpectrum> instances, core::InstanceId id)
{
    const auto it = std::find_if(instances.begin(), instances.end(), [id](const auto& s) { return s.id == id; });
    return it != instances.end() ? &*it : nullptr;
}

} // namespace

// --- Referencias ---

std::string MatchReferenceOption::text() const
{
    if (!enabled)
        return name + " (not connected)";
    return silent ? name + " (silent)" : name;
}

std::vector<MatchReferenceOption> matchReferenceOptions(std::span<const core::InstanceSpectrum> instances,
                                                        core::InstanceId self, bool sidechainConnected)
{
    std::vector<MatchReferenceOption> options;
    for (const auto& instance : instances)
        if (instance.id != self)
            options.push_back({core::MatchReference::ofInstance(instance.id), instance.displayName(), true,
                               !instance.hasSignal});
    options.push_back({core::MatchReference::sidechain(), "Sidechain", sidechainConnected, false});
    return options;
}

core::MatchReference defaultMatchReference(std::span<const MatchReferenceOption> options)
{
    using Kind = core::MatchReference::Kind;
    const auto isInstance = [](const MatchReferenceOption& o) { return o.reference.kind == Kind::Instance; };
    for (const auto& option : options)
        if (isInstance(option) && !option.silent)
            return option.reference;
    for (const auto& option : options)
        if (isInstance(option))
            return option.reference;
    for (const auto& option : options)
        if (option.reference.kind == Kind::Sidechain && option.enabled)
            return option.reference;
    return core::MatchReference::none();
}

bool canLearnFrom(const core::MatchReference& reference, std::span<const MatchReferenceOption> options)
{
    return reference.kind != core::MatchReference::Kind::None &&
           std::any_of(options.begin(), options.end(),
                       [&](const auto& o) { return o.reference == reference && o.enabled; });
}

// --- Textos ---

std::string matchBlockText(MatchBlock block)
{
    switch (block)
    {
    case MatchBlock::None:
        return {};
    case MatchBlock::NoReference:
        return "No reference";
    case MatchBlock::LearnFirst:
        return "Learn 10 s first";
    case MatchBlock::NoFreeBands:
        return "No free bands";
    case MatchBlock::Fitting:
        return "Fitting\xE2\x80\xA6";
    case MatchBlock::NothingToMatch:
        return "Nothing to match";
    }
    return {};
}

std::string matchNoticeText(MatchNotice notice)
{
    switch (notice)
    {
    case MatchNotice::None:
        return {};
    case MatchNotice::ReferenceRemoved:
        return "Reference removed";
    case MatchNotice::SidechainDisconnected:
        return "Sidechain disconnected";
    }
    return {};
}

std::string matchProgressText(double usefulSeconds)
{
    const double seconds = std::isfinite(usefulSeconds) ? std::max(usefulSeconds, 0.0) : 0.0;
    // Hacia abajo: con 9,96 s útiles todavía no se puede aplicar.
    const double tenths = std::floor((seconds + core::MatchLearner::kSecondsTolerance) * 10.0) / 10.0;
    char text[64];
    std::snprintf(text, sizeof(text), "%.1f s / %.0f s", tenths, core::kMatchRequiredSeconds);
    return text;
}

// --- Apply ---

int freeBandCount(const core::ParameterValues& values) noexcept
{
    int count = 0;
    for (int band = 0; band < core::kNumBands; ++band)
        if (values[core::bandParameter(band, core::BandParam::Enabled)] < 0.5f)
            ++count;
    return count;
}

MatchApply matchApplyChanges(const core::ParameterValues& values, std::span<const dsp::FilterParams> bands)
{
    MatchApply out;
    auto next = values;
    int band = 0;
    for (const auto& params : bands)
    {
        while (band < core::kNumBands && next[core::bandParameter(band, core::BandParam::Enabled)] >= 0.5f)
            ++band;
        if (band >= core::kNumBands)
            break;
        const auto changes = newBandChanges(next, band, params.frequencyHz, params.gainDb, params.q, params.type);
        next = withChanges(next, changes);
        out.changes.insert(out.changes.end(), changes.begin(), changes.end());
        if (!out.firstBand)
            out.firstBand = band;
        ++band;
    }
    return out;
}

core::ParameterValues withChanges(core::ParameterValues values, std::span<const ParameterChange> changes) noexcept
{
    for (const auto& change : changes)
        if (change.index < values.size())
            values[change.index] = change.value;
    return values;
}

// --- MatchController::Meter ---

MatchController::Meter::Meter()
    : analyzer(core::SpectrumSession::kFftOrder),
      averager(static_cast<std::size_t>(core::kSessionPoints), core::SpectrumSession::kAverageSeconds,
               core::kSessionFloorDb),
      levels(static_cast<std::size_t>(core::kSessionPoints), core::kSessionFloorDb),
      inaudibleSeconds(std::numeric_limits<double>::infinity())
{
    analyzer.setTiltDbPerOctave(core::SpectrumSession::kTiltDbPerOctave);
}

bool MatchController::Meter::update(double dtSeconds, double sampleRate)
{
    const bool gotAudio = received > 0;
    received = 0;
    if (!gotAudio)
    {
        inaudibleSeconds += dtSeconds;
        return false;
    }
    // Como en la sesión: el espectro de ahora entra en el promedio, y la
    // señal se juzga por el de ahora, no por el promedio.
    analyzer.setSampleRate(sampleRate > 0.0 ? sampleRate : 48000.0);
    analyzer.compute(core::sessionFrequencies(), levels);
    averager.update(levels, dtSeconds);
    const bool audible =
        std::any_of(levels.begin(), levels.end(), [](float db) { return db > core::kSessionSignalDb; });
    inaudibleSeconds = audible ? 0.0 : inaudibleSeconds + dtSeconds;
    return true;
}

void MatchController::Meter::clear()
{
    analyzer.clear();
    averager.reset();
    received = 0;
    inaudibleSeconds = std::numeric_limits<double>::infinity();
}

bool MatchController::Meter::hasSignal() const noexcept
{
    return inaudibleSeconds <= core::SpectrumSession::kSignalHoldSeconds;
}

// --- MatchController ---

MatchController::MatchController(core::MatchState& state, core::MatchWorker& worker, core::SpectrumSession& session,
                                 core::MatchSidechainTap& sidechain)
    : state_(state), worker_(worker), session_(session), sidechainTap_(sidechain)
{
    // La interfaz que se abre no estaba aprendiendo: lo aprendido sigue,
    // pero hace falta Learn para seguir (el panel se cerró, que es Stop).
    state_.learning = false;
}

MatchController::~MatchController()
{
    stop();
}

void MatchController::pushTrack(const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;
    track_.analyzer.push(samples, numSamples);
    track_.received += numSamples;
}

void MatchController::pushSidechain(const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;
    sidechain_.analyzer.push(samples, numSamples);
    sidechain_.received += numSamples;
}

void MatchController::selectReference(const core::MatchReference& reference)
{
    if (reference == state_.reference)
        return;
    stopLearning(MatchNotice::None);
    state_.reference = reference;
    state_.learner.reset();
    worker_.cancel();
    submittedKey_.reset();
}

void MatchController::setSettings(const core::MatchSettings& settings)
{
    state_.settings = core::sanitize(settings);
}

bool MatchController::learn(const MatchContext& context)
{
    const auto options = matchReferenceOptions(context.instances, context.self, context.sidechainConnected);
    // Sin referencia elegida, o si ya no está, la de por defecto. Si no hay
    // ninguna, no cambia nada: lo aprendido se queda.
    if (!canLearnFrom(state_.reference, options))
    {
        const auto fallback = defaultMatchReference(options);
        if (!canLearnFrom(fallback, options))
            return false;
        selectReference(fallback);
    }

    stopLearning(MatchNotice::None);
    state_.learner.reset();
    worker_.cancel();
    submittedKey_.reset();
    notice_ = MatchNotice::None;
    state_.learning = true;

    if (state_.reference.kind == core::MatchReference::Kind::Instance)
    {
        viewer_ = session_.watch();
        // Lo que la referencia publicó antes de Learn no cuenta.
        const auto* entry = findInstance(context.instances, state_.reference.instance);
        referenceClock_ = entry != nullptr ? entry->audioSeconds : 0.0;
    }
    else
    {
        sidechain_.clear();
        sidechainTap_.setEnabled(true);
    }
    return true;
}

void MatchController::stop()
{
    stopLearning(MatchNotice::None);
}

void MatchController::stopLearning(MatchNotice notice)
{
    if (state_.learning)
        notice_ = notice;
    state_.learning = false;
    viewer_.release();
    sidechainTap_.setEnabled(false);
}

void MatchController::tick(double dtSeconds, const MatchContext& context)
{
    const double dt = std::isfinite(dtSeconds) ? std::max(dtSeconds, 0.0) : 0.0;
    track_.update(dt, context.sampleRate);
    const bool sidechainAudio = sidechain_.update(dt, context.sampleRate);

    if (state_.learning)
    {
        using Kind = core::MatchReference::Kind;
        if (state_.reference.kind == Kind::Instance)
        {
            const auto* entry = findInstance(context.instances, state_.reference.instance);
            if (entry == nullptr)
                stopLearning(MatchNotice::ReferenceRemoved);
            else if (entry->audioSeconds < referenceClock_)
                referenceClock_ = entry->audioSeconds; // la sesión ha vuelto a empezar
            else if (entry->audioSeconds > referenceClock_)
            {
                // Un espectro nuevo de la referencia: pesa lo que ha avanzado su reloj de audio.
                state_.learner.add({.trackDb = track_.averager.levels(),
                                    .referenceDb = entry->levelsDb,
                                    .trackHasSignal = track_.hasSignal(),
                                    .referenceHasSignal = entry->hasSignal,
                                    .trackSampleRate = context.sampleRate,
                                    .referenceSampleRate = entry->sampleRate,
                                    .dtSeconds = entry->audioSeconds - referenceClock_});
                referenceClock_ = entry->audioSeconds;
            }
        }
        else if (state_.reference.kind == Kind::Sidechain)
        {
            if (!context.sidechainConnected)
                stopLearning(MatchNotice::SidechainDisconnected);
            else if (sidechainAudio)
                state_.learner.add({.trackDb = track_.averager.levels(),
                                    .referenceDb = sidechain_.averager.levels(),
                                    .trackHasSignal = track_.hasSignal(),
                                    .referenceHasSignal = sidechain_.hasSignal(),
                                    .trackSampleRate = context.sampleRate,
                                    .referenceSampleRate = context.sampleRate,
                                    .dtSeconds = dt});
        }
        else
            stopLearning(MatchNotice::None);
    }

    scheduleFit(dt, context);
}

MatchController::FitKey MatchController::keyOf(const core::MatchState& state, const MatchContext& context)
{
    FitKey key;
    key.settings = state.settings;
    if (context.values != nullptr)
    {
        const auto bands = core::allBandSettings(*context.values, context.algorithm);
        key.bands.assign(bands.begin(), bands.end());
    }
    key.sampleRate = context.sampleRate;
    key.numChannels = context.numChannels;
    key.algorithm = context.algorithm;
    return key;
}

void MatchController::scheduleFit(double dtSeconds, const MatchContext& context)
{
    const FitKey key = keyOf(state_, context);
    if (lastKey_ && *lastKey_ == key)
        sinceKeyChange_ += dtSeconds;
    else
        sinceKeyChange_ = 0.0;
    lastKey_ = key;

    const int freeBands = context.values != nullptr ? freeBandCount(*context.values) : 0;
    if (!state_.learner.ready() || freeBands == 0 || context.values == nullptr)
    {
        // Sin nada que proponer, lo pedido ya no sirve.
        if (submittedKey_)
        {
            worker_.cancel();
            submittedKey_.reset();
        }
        return;
    }

    const auto revision = state_.learner.revision();
    bool submit = false;
    if (!submittedKey_)
        submit = true; // la primera, en cuanto hay 10 s útiles
    else if (!(*submittedKey_ == key))
        submit = sinceKeyChange_ >= kRefitDelaySeconds;
    else if (revision != submittedRevision_)
        submit = !state_.learning ||
                 state_.learner.usefulSeconds() - submittedSeconds_ >= kLearningRefitSeconds;
    if (!submit)
        return;

    core::MatchRequest request;
    const auto& frequencies = core::sessionFrequencies();
    request.frequencies.assign(frequencies.begin(), frequencies.end());
    request.trackDb = state_.learner.trackDb();
    request.referenceDb = state_.learner.referenceDb();
    request.resolutionHz = state_.learner.resolutionHz();
    request.currentBands = key.bands;
    request.numChannels = key.numChannels;
    request.sampleRate = key.sampleRate;
    request.algorithm = key.algorithm;
    request.settings = core::toCurveMatchSettings(state_.settings, freeBands);
    submittedId_ = worker_.submit(std::move(request));
    submittedKey_ = key;
    submittedRevision_ = revision;
    submittedSeconds_ = state_.learner.usefulSeconds();
}

bool MatchController::upToDate(const MatchContext& context) const
{
    return submittedKey_ && *submittedKey_ == keyOf(state_, context) &&
           submittedRevision_ == state_.learner.revision();
}

std::shared_ptr<const core::MatchProposal> MatchController::proposal() const
{
    if (!submittedKey_ || worker_.busy())
        return nullptr;
    auto latest = worker_.latest();
    return latest && latest->id == submittedId_ ? latest : nullptr;
}

MatchStatus MatchController::status(const MatchContext& context) const
{
    MatchStatus s;
    s.options = matchReferenceOptions(context.instances, context.self, context.sidechainConnected);
    s.reference = state_.reference;
    s.learning = state_.learning;
    const bool referenceOk = canLearnFrom(state_.reference, s.options) ||
                             canLearnFrom(defaultMatchReference(s.options), s.options);
    s.canLearn = state_.learning || referenceOk;
    s.learnBlock = s.canLearn ? MatchBlock::None : MatchBlock::NoReference;
    s.usefulSeconds = state_.learner.usefulSeconds();
    s.progress = matchProgressText(s.usefulSeconds);
    s.showCurves = state_.learner.ready();
    s.notice = notice_;

    const int freeBands = context.values != nullptr ? freeBandCount(*context.values) : 0;
    const auto current = proposal();
    if (!state_.learner.ready())
        s.applyBlock = !referenceOk && !state_.learning ? MatchBlock::NoReference : MatchBlock::LearnFirst;
    else if (freeBands == 0)
        s.applyBlock = MatchBlock::NoFreeBands;
    else if (!upToDate(context) || current == nullptr)
        s.applyBlock = MatchBlock::Fitting;
    else if (current->result.bands.empty())
        s.applyBlock = MatchBlock::NothingToMatch;
    else
        s.applyBlock = MatchBlock::None;
    s.canApply = s.applyBlock == MatchBlock::None;
    return s;
}

std::optional<MatchApply> MatchController::applyChanges(const MatchContext& context) const
{
    if (!status(context).canApply || context.values == nullptr)
        return std::nullopt;
    const auto current = proposal();
    auto apply = matchApplyChanges(*context.values, current->result.bands);
    if (apply.changes.empty())
        return std::nullopt;
    return apply;
}

} // namespace esceq::ui
