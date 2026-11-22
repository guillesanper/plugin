// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CurveView.h"

#include "Draw.h"
#include "JuceConvert.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/ui/control_logic.h"
#include "esceq/ui/meter_logic.h"
#include "esceq/ui/notes.h"
#include "esceq/ui/spectral_mask.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/filter_design.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace esceq::ui {

namespace {

constexpr int kAnalyzerPoints = 320;
constexpr float kNodeRadius = 8.0f;
constexpr float kHitRadius = 13.0f;
constexpr float kNodeEdgeMargin = kNodeRadius + 10.0f; // sitio para la flecha de "fuera de rango"

using namespace theme;

juce::String frequencyLabel(double hz)
{
    return hz >= 1000.0 ? juce::String(hz / 1000.0, 0) + "k" : juce::String(static_cast<int>(hz));
}

/// Mismo pico (o ninguno en los dos): el marcador no ha cambiado.
bool samePeak(const std::optional<SpectrumPeak>& a, const std::optional<SpectrumPeak>& b)
{
    if (!a || !b)
        return a.has_value() == b.has_value();
    return juce::exactlyEqual(a->frequencyHz, b->frequencyHz) && juce::exactlyEqual(a->levelDb, b->levelDb) &&
           juce::exactlyEqual(a->q, b->q);
}

bool isDynamic(const dsp::BandSettings& settings)
{
    return settings.dynamics.enabled && core::usesDynamics(settings.filter.type);
}

} // namespace

CurveView::CurveView(PluginProcessor& processor)
    : processor_(processor),
      preAnalyzer_(std::make_unique<dsp::SpectrumAnalyzer>(analyzerFftOrder(analyzerView_.fftSize))),
      postAnalyzer_(std::make_unique<dsp::SpectrumAnalyzer>(analyzerFftOrder(analyzerView_.fftSize))),
      analyzerFrequencies_(logFrequencies(kAnalyzerPoints)),
      preLevels_(kAnalyzerPoints, dsp::SpectrumAnalyzer::kFloorDb),
      postLevels_(kAnalyzerPoints, dsp::SpectrumAnalyzer::kFloorDb),
      postPeaks_(kAnalyzerPoints, dsp::SpectrumAnalyzer::kFloorDb),
      peakSpectrum_(kAnalyzerPoints),
      scratch_(kAnalyzerPoints, 0.0f),
      readBuffer_(4096, 0.0f)
{
    preAnalyzer_->setTiltDbPerOctave(analyzerView_.tiltDbPerOctave);
    postAnalyzer_->setTiltDbPerOctave(analyzerView_.tiltDbPerOctave);
    values_ = processor_.currentValues();
    // Con el editor cerrado, las colas del analizador se llenan y se quedan con
    // el audio de cuando se cerró. Se vacían para no dibujarlo al abrir.
    for (auto* fifo : {&processor_.preAnalyzerFifo(), &processor_.postAnalyzerFifo()})
        while (fifo->read(readBuffer_.data(), static_cast<int>(readBuffer_.size())) > 0)
        {
        }
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setTitle("Equalizer curve");
    setHelpText("Double-click to add a band. Drag a node to set frequency and gain; use the wheel for Q. "
                "With the keyboard: left and right select a band, up and down change its gain, "
                "Ctrl or Cmd with left and right change its frequency, Delete removes it. "
                "F expands the view and Escape returns. With peak hold on, click the gain scale to clear the peaks. "
                "Drag from a marked spectrum peak to add a band there; hold Alt when releasing to make it dynamic.");
}

CurveView::~CurveView()
{
    // Si la ventana se cierra en mitad de un arrastre, el gesto se cierra aquí.
    if (dragging_ >= 0)
    {
        if (!pressedPeak_)
            endGesture(dragging_, core::BandParam::Frequency);
        if (draggingGain_)
            endGesture(dragging_, core::BandParam::Gain);
    }
}

void CurveView::setSelectedBand(int band)
{
    if (band == selected_)
        return;
    selected_ = band;
    if (onSelectionChanged)
        onSelectionChanged(selected_);
    repaint();
}

void CurveView::setHoveredBand(int band)
{
    hovered_ = band;
    repaint();
}

void CurveView::setRangeDb(double rangeDb)
{
    rangeDb_ = rangeDb;
    repaint();
}

void CurveView::setNoteScale(bool visible, double a4Hz)
{
    // Solo cambia cuando el usuario la elige: se repinta siempre.
    notesVisible_ = visible;
    a4Hz_ = a4Hz;
    if (!visible)
        markedNote_.reset();
    repaint();
}

void CurveView::setMarkedNote(std::optional<int> midi)
{
    if (midi == markedNote_)
        return;
    markedNote_ = midi;
    repaint();
}

void CurveView::setAnalyzerMode(AnalyzerMode mode)
{
    if (mode != analyzerMode_)
    {
        // Los picos se buscan en otro espectro, o en ninguno.
        peakSpectrum_.clear();
        peaksStale_ = true;
    }
    analyzerMode_ = mode;
    updatePeakMarker();
    repaint();
}

void CurveView::setAnalyzerView(const AnalyzerView& view)
{
    if (view == analyzerView_)
        return;
    const bool resolution = view.fftSize != analyzerView_.fftSize;
    // Uno de unos pocos valores exactos.
    const bool tilt = !juce::exactlyEqual(view.tiltDbPerOctave, analyzerView_.tiltDbPerOctave);
    if (resolution)
    {
        // Empiezan vacíos: en lo que tarda en llenarse la FFT (de 85 a 341 ms
        // a 48 kHz), el espectro baja con la caída, sin saltos.
        preAnalyzer_ = std::make_unique<dsp::SpectrumAnalyzer>(analyzerFftOrder(view.fftSize));
        postAnalyzer_ = std::make_unique<dsp::SpectrumAnalyzer>(analyzerFftOrder(view.fftSize));
    }
    preAnalyzer_->setTiltDbPerOctave(view.tiltDbPerOctave);
    postAnalyzer_->setTiltDbPerOctave(view.tiltDbPerOctave);
    preLevels_.setFallSeconds(analyzerFallSeconds(view.fallMs));
    postLevels_.setFallSeconds(analyzerFallSeconds(view.fallMs));
    // Los picos de antes no se comparan con el espectro nuevo.
    if (resolution || tilt || !view.peakHold)
        postPeaks_.clear();
    if (resolution || tilt)
    {
        peakSpectrum_.clear();
        peaksStale_ = true;
    }
    analyzerView_ = view;
    repaint();
}

void CurveView::setExpanded(bool expanded)
{
    expanded_ = expanded;
    repaint();
}

CurveMapping CurveView::mapping() const
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    return CurveMapping({bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()}, rangeDb_);
}

dsp::BandSettings CurveView::soundingSettings(int band) const
{
    return soundingBandSettings(values_, band, algorithm_, dynamicGains_[static_cast<std::size_t>(band)]);
}

void CurveView::resized()
{
    repaint();
}

void CurveView::refresh()
{
    const auto values = processor_.currentValues();
    std::array<float, core::kNumBands> gains{};
    for (int band = 0; band < core::kNumBands; ++band)
        gains[static_cast<std::size_t>(band)] = processor_.dynamicGainDb(band);

    // Solo se repinta si algo ha cambiado: dibujar la curva con muchas bandas
    // en una ventana grande cuesta varios milisegundos.
    const double sampleRate = processor_.currentSampleRate(); // cambia la curva cerca de Nyquist
    const auto algorithm = processor_.filterAlgorithm();
    bool changed = values != values_ || algorithm != algorithm_ || gains != dynamicGains_ ||
                   !juce::exactlyEqual(sampleRate, lastSampleRate_);
    values_ = values;
    algorithm_ = algorithm;
    dynamicGains_ = gains;
    lastSampleRate_ = sampleRate;

    // La forma de la máscara de las bandas espectrales: solo llega mientras suena.
    if (const auto* mask = processor_.spectralMask().read();
        mask != nullptr && (mask->active != spectralMask_.active || mask->gainsDb != spectralMask_.gainsDb))
    {
        spectralMask_ = *mask;
        ++spectralMaskRevision_;
        changed = true;
    }

    // Si la banda seleccionada se ha desactivado (desde el host o el panel), se deselecciona.
    if (selected_ >= 0 && values_[core::bandParameter(selected_, core::BandParam::Enabled)] < 0.5f && dragging_ < 0)
        setSelectedBand(-1);

    changed = updateAnalyzer() || changed;
    if (pointer_)
        changed = updatePeakMarker() || changed;
    changed = updateSession(false) || changed;
    if (changed)
        repaint();
}

// ---------------------------------------------------------------------------
// Analizador

bool CurveView::updateAnalyzer()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastAnalyzerUpdateMs_ > 0.0 ? (now - lastAnalyzerUpdateMs_) / 1000.0 : 0.0;
    lastAnalyzerUpdateMs_ = now;

    const double sampleRate = processor_.currentSampleRate();
    const bool active = analyzerMode_ != AnalyzerMode::Off;

    // La cola "pre" tiene un solo lector, esta vista: con el panel Match
    // abierto, lo que se lee va también al aprendizaje (la pista antes del ecualizador).
    auto drain = [this, active](dsp::SpscFifo& fifo, dsp::SpectrumAnalyzer& analyzer, bool track) {
        for (int n; (n = fifo.read(readBuffer_.data(), static_cast<int>(readBuffer_.size()))) > 0;)
        {
            if (active)
                analyzer.push(readBuffer_.data(), n);
            if (track && match_ != nullptr)
                match_->pushTrack(readBuffer_.data(), n);
        }
    };
    // Siempre se vacían las colas, aunque el analizador esté apagado, para no
    // mostrar audio viejo al encenderlo. Congelado, el audio sigue entrando en
    // el historial (al descongelar, el espectro es el de ahora) pero no se
    // calcula ni se mueve nada.
    drain(processor_.preAnalyzerFifo(), *preAnalyzer_, true);
    drain(processor_.postAnalyzerFifo(), *postAnalyzer_, false);

    if (!active || analyzerView_.frozen)
        return false;

    // Devuelve true si algún punto se ha movido lo bastante para verse. El
    // espectro en el que se buscan los picos (el post, o el pre en pre +
    // post) entra además en su promedio.
    auto update = [&](dsp::SpectrumAnalyzer& analyzer, SpectrumBallistics& levels, bool peakSource) {
        analyzer.setSampleRate(sampleRate);
        analyzer.compute(analyzerFrequencies_, scratch_);
        if (peakSource)
        {
            peakSpectrum_.update(scratch_, dt);
            peaksStale_ = true;
        }
        return levels.update(scratch_, dt);
    };

    const bool preAndPost = analyzerMode_ == AnalyzerMode::PreAndPost;
    bool moved = update(*postAnalyzer_, postLevels_, !preAndPost);
    if (analyzerView_.peakHold)
        moved = postPeaks_.update(postLevels_.levels()) || moved;
    if (preAndPost)
        moved = update(*preAnalyzer_, preLevels_, true) || moved;
    return moved;
}

// ---------------------------------------------------------------------------
// Pintado

void CurveView::compareChanged()
{
    updateSession(true);
    repaint();
}

bool CurveView::updateSession(bool force)
{
    auto& selection = processor_.compareSelection();
    auto& session = processor_.spectrumSession();

    if (selection.shown.empty())
    {
        sessionViewer_.release();
        const bool hadSomething = !overlays_.empty() || !collisions_.zones.empty();
        overlays_.clear();
        collisions_ = {};
        return hadSomething;
    }

    if (!sessionViewer_.active())
    {
        sessionViewer_ = session.watch();
        force = true;
    }
    const auto revision = session.revision();
    if (!force && revision == sessionRevision_)
        return false;
    sessionRevision_ = revision;

    session.snapshot(sessionSnapshot_);
    // Las instancias que se han ido (pista borrada, plugin quitado) dejan de estar seleccionadas.
    if (selection.retainExisting(sessionSnapshot_) && selection.shown.empty())
        return updateSession(true);

    const core::InstanceSpectrum* own = nullptr;
    overlays_.clear();
    for (const auto& instance : sessionSnapshot_)
    {
        if (instance.id == processor_.instanceId())
            own = &instance;
        else if (selection.contains(instance.id))
            overlays_.push_back({instance.id, instance.displayName(),
                                 theme::instance(instance.number, instance.trackColour), instance.hasSignal,
                                 instance.levelsDb});
    }

    showCollisions_ = selection.showCollisions && own != nullptr;
    collisions_ = {};
    if (showCollisions_)
    {
        std::vector<std::span<const float>> others;
        for (const auto& overlay : overlays_)
            others.emplace_back(overlay.levels);
        collisions_ = computeCollisions(core::sessionFrequencies(), own->levelsDb, others);
    }
    return true;
}

void CurveView::paint(juce::Graphics& g)
{
    g.fillAll(colour::window);
    const auto bounds = getLocalBounds().toFloat();
    const auto m = mapping();

    {
        // Todo lo de dentro se recorta a la tarjeta redondeada.
        juce::Graphics::ScopedSaveState save(g);
        juce::Path shape;
        shape.addRoundedRectangle(bounds, radius::card);
        g.reduceClipRegion(shape);
        g.fillAll(colour::plot);

        drawAnalyzer(g, m);
        drawCollisions(g, m);
        drawSession(g, m);
        drawMatchSpectra(g, m);
        drawGrid(g, m);
        updateCurveCache(m);
        drawCurves(g, m);
        drawMatchCurves(g, m);
        drawMarkedNote(g, m);
        drawMatchNodes(g, m);
        drawNodes(g, m);
        drawPeakMarker(g, m);
        drawCollisionLabels(g, m);
        drawReadout(g, m);
        drawFrozen(g, m);
        drawMatchLegend(g, m);
    }

    const bool focused = keyboardFocus_ && hasKeyboardFocus(false);
    g.setColour(focused ? colour::accent : colour::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius::card, focused ? stroke::thin : stroke::hairline);
}

void CurveView::drawGrid(juce::Graphics& g, const CurveMapping& m) const
{
    const auto area = m.area();
    g.setFont(font(type::caption, Weight::Regular, true));

    for (const auto& line : frequencyGrid())
    {
        const float x = std::round(m.xForFrequency(line.value));
        if (line.value > kMinDisplayHz && line.value < kMaxDisplayHz)
        {
            g.setColour(line.labelled ? colour::lineStrong.withAlpha(0.7f) : colour::line.withAlpha(0.7f));
            g.fillRect(juce::Rectangle<float>(x, area.y, 1.0f, area.height));
        }
        if (line.labelled && line.value < kMaxDisplayHz)
        {
            g.setColour(colour::textDim);
            g.drawText(frequencyLabel(line.value), juce::Rectangle<float>(x + 5.0f, area.y + area.height - 17.0f, 40.0f, 12.0f),
                       juce::Justification::centredLeft, false);
        }
    }

    for (const auto& line : gainGrid(m.rangeDb()))
    {
        const bool zero = std::abs(line.value) < 1.0e-9;
        const float y = std::round(m.yForGain(line.value));
        if (std::abs(line.value) < m.rangeDb() - 1.0e-9)
        {
            g.setColour(zero ? colour::lineStrong : colour::line.withAlpha(0.7f));
            g.fillRect(juce::Rectangle<float>(area.x, y, area.width, 1.0f));
        }
        const juce::String label = (line.value > 0.0 ? "+" : "") + juce::String(static_cast<int>(line.value));
        // Las etiquetas de los extremos se meten hacia dentro para no cortarse.
        const float labelY = std::clamp(y - 15.0f, area.y + 4.0f, area.y + area.height - 17.0f);
        g.setColour(zero ? colour::textMuted : colour::textDim);
        g.drawText(label, juce::Rectangle<float>(area.x + area.width - kGainScaleWidth, labelY, 32.0f, 12.0f),
                   juce::Justification::centredRight, false);
    }
}

void CurveView::drawAnalyzer(juce::Graphics& g, const CurveMapping& m) const
{
    if (analyzerMode_ == AnalyzerMode::Off)
        return;

    const auto area = m.area();
    const float bottom = area.y + area.height;

    auto makePath = [&](const std::vector<float>& levels, bool closed) {
        juce::Path path;
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            const float x = m.xForFrequency(analyzerFrequencies_[i]);
            const float y = std::clamp(m.yForAnalyzerDb(levels[i]), area.y, bottom);
            if (i > 0)
                path.lineTo(x, y);
            else if (closed)
            {
                path.startNewSubPath(x, bottom);
                path.lineTo(x, y);
            }
            else
                path.startNewSubPath(x, y);
        }
        if (closed && !levels.empty())
        {
            path.lineTo(m.xForFrequency(analyzerFrequencies_.back()), bottom);
            path.closeSubPath();
        }
        return path;
    };

    const auto fill = makePath(postLevels_.levels(), true);
    g.setGradientFill(juce::ColourGradient(colour::analyzerPost.withAlpha(0.30f), 0.0f, area.y + 0.35f * area.height,
                                           colour::analyzerPost.withAlpha(0.06f), 0.0f, bottom, false));
    g.fillPath(fill);
    g.setColour(colour::analyzerPost.withAlpha(0.75f));
    g.strokePath(makePath(postLevels_.levels(), false), juce::PathStrokeType(stroke::hairline));

    if (analyzerMode_ == AnalyzerMode::PreAndPost)
    {
        g.setColour(colour::analyzerPre.withAlpha(0.6f));
        g.strokePath(makePath(preLevels_.levels(), false), juce::PathStrokeType(stroke::hairline));
    }

    // Picos mantenidos: una línea fina, más clara que el espectro.
    if (analyzerView_.peakHold && postPeaks_.hasPeaks())
    {
        g.setColour(colour::analyzerPost.brighter(0.6f).withAlpha(0.7f));
        g.strokePath(makePath(postPeaks_.levels(), false), juce::PathStrokeType(stroke::hairline));
    }
}

void CurveView::drawFrozen(juce::Graphics& g, const CurveMapping& m) const
{
    if (!analyzerView_.frozen || analyzerMode_ == AnalyzerMode::Off)
        return;
    const auto area = m.area();
    draw::caption(g, "Analyzer frozen", juce::Rectangle<float>(area.x + 12.0f, area.y + 8.0f, 160.0f, 12.0f),
                  juce::Justification::centredLeft, colour::accent);
}

void CurveView::drawSession(juce::Graphics& g, const CurveMapping& m) const
{
    if (overlays_.empty())
        return;

    const auto area = m.area();
    const float bottom = area.y + area.height;
    const auto& frequencies = core::sessionFrequencies();
    for (const auto& overlay : overlays_)
    {
        juce::Path path;
        for (std::size_t i = 0; i < overlay.levels.size() && i < frequencies.size(); ++i)
        {
            const float x = m.xForFrequency(frequencies[i]);
            const float y = std::clamp(m.yForAnalyzerDb(overlay.levels[i]), area.y, bottom);
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        // Una instancia sin señal se queda atenuada: su espectro se va apagando.
        g.setColour(overlay.colour.withAlpha(overlay.hasSignal ? 0.85f : 0.35f));
        g.strokePath(path, juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved));
    }
}

// ---------------------------------------------------------------------------
// Igualación

void CurveView::setMatch(MatchController* controller)
{
    if (controller == match_)
        return;
    match_ = controller;
    if (controller == nullptr && matchDrawing_.shown)
        repaint();
    if (controller == nullptr)
        matchDrawing_ = {};
}

void CurveView::refreshMatch(juce::Colour referenceColour)
{
    if (match_ == nullptr)
        return;
    const auto& learner = match_->state().learner;
    auto& d = matchDrawing_;
    bool changed = false;
    if (!learner.ready())
    {
        changed = d.shown;
        d = {};
    }
    else
    {
        if (!d.shown || d.revision != learner.revision())
        {
            d.spectra = matchSpectraDisplay(learner.trackDb(), learner.referenceDb());
            d.revision = learner.revision();
            changed = true;
        }
        d.shown = true;
        if (d.referenceColour != referenceColour)
        {
            d.referenceColour = referenceColour;
            changed = true;
        }
        const auto proposal = match_->proposal();
        if (proposal != nullptr && proposal != d.proposal)
        {
            d.proposal = proposal;
            d.target = matchTargetCurve(proposal->result);
            d.runs = matchTargetRuns(proposal->result);
            changed = true;
        }
        const bool current = proposal != nullptr;
        changed = changed || current != d.current;
        d.current = current;
    }
    if (changed)
        repaint();
}

void CurveView::drawMatchSpectra(juce::Graphics& g, const CurveMapping& m) const
{
    const auto& d = matchDrawing_;
    if (!d.shown)
        return;
    const auto area = m.area();
    const float bottom = area.y + area.height;
    const auto& frequencies = core::sessionFrequencies();
    auto spectrum = [&](const std::vector<float>& levels, juce::Colour colour) {
        juce::Path path;
        for (std::size_t i = 0; i < levels.size() && i < frequencies.size(); ++i)
        {
            const float x = m.xForFrequency(frequencies[i]);
            const float y = std::clamp(m.yForAnalyzerDb(levels[i]), area.y, bottom);
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved));
    };
    spectrum(d.spectra.trackDb, colour::analyzerPre.withAlpha(0.9f));
    spectrum(d.spectra.referenceDb, d.referenceColour.withAlpha(0.9f));
}

void CurveView::drawMatchCurves(juce::Graphics& g, const CurveMapping& m) const
{
    const auto& d = matchDrawing_;
    if (!d.shown || d.proposal == nullptr)
        return;
    const auto& result = d.proposal->result;
    const auto area = m.area();
    auto y = [&](double db) { return std::clamp(m.yForGain(db), area.y, area.y + area.height); };
    const float alpha = d.current ? 1.0f : 0.4f;

    // Objetivo: discontinuo, solo donde cuenta, con el nivel libre del ajuste.
    juce::Path target;
    for (const auto& run : d.runs)
        for (std::size_t i = run.begin; i < run.end && i < d.target.size() && i < result.frequencies.size(); ++i)
        {
            const float x = m.xForFrequency(result.frequencies[i]);
            if (i == run.begin)
                target.startNewSubPath(x, y(d.target[i]));
            else
                target.lineTo(x, y(d.target[i]));
        }
    juce::Path dashed;
    const float dashes[] = {5.0f, 4.0f};
    juce::PathStrokeType(stroke::thin).createDashedStroke(dashed, target, dashes, 2);
    g.setColour(colour::text.withAlpha(0.75f * alpha));
    g.fillPath(dashed);

    // Propuesta: la curva total estática que se ve (la de las bandas de
    // ahora, sin la dinámica) más las bandas que se crearían, columna a
    // columna y con el diseño que sonaría. Así coincide con la curva blanca
    // donde no hay bandas nuevas, también con un notch estrecho.
    const auto& cache = curveCache_;
    const std::size_t points = cache.xs.size();
    std::vector<double> total(points, 0.0);
    for (int band = 0; band < core::kNumBands; ++band)
    {
        const auto& curve = cache.bands[static_cast<std::size_t>(band)];
        if (!curve.valid || curve.db.size() != points)
            continue;
        if (!curve.dynamic)
        {
            for (std::size_t i = 0; i < points; ++i)
                total[i] += curve.db[i];
            continue;
        }
        const auto design = dsp::designBand(curve.reference, cache.sampleRate);
        for (std::size_t i = 0; i < points; ++i)
            total[i] += dsp::magnitudeDbAtTangent(design, cache.tangents[i]);
    }
    for (const auto& band : result.bands)
    {
        const auto design = dsp::designBand(band, cache.sampleRate);
        for (std::size_t i = 0; i < points; ++i)
            total[i] += dsp::magnitudeDbAtTangent(design, cache.tangents[i]);
    }
    juce::Path proposed;
    for (std::size_t i = 0; i < points; ++i)
        i == 0 ? proposed.startNewSubPath(cache.xs[i], y(total[i])) : proposed.lineTo(cache.xs[i], y(total[i]));
    g.setColour(colour::accent.withAlpha(alpha));
    g.strokePath(proposed, juce::PathStrokeType(stroke::medium, juce::PathStrokeType::curved));
}

void CurveView::drawMatchNodes(juce::Graphics& g, const CurveMapping& m) const
{
    const auto& d = matchDrawing_;
    if (!d.shown || d.proposal == nullptr)
        return;
    const auto& bands = d.proposal->result.bands;
    // Cada nodo, con el color de la banda libre en la que lo crearía Apply.
    const auto slots = matchTargetBands(values_, static_cast<int>(bands.size()));
    const float alpha = d.current ? 1.0f : 0.4f;
    for (std::size_t k = 0; k < bands.size() && k < slots.size(); ++k)
    {
        dsp::BandSettings settings;
        settings.enabled = true;
        settings.filter = bands[k];
        const auto p = nodePosition(m, settings, kNodeEdgeMargin);
        const auto circle = juce::Rectangle<float>(2.0f * kNodeRadius, 2.0f * kNodeRadius).withCentre({p.x, p.y});
        const auto colour = theme::band(slots[k]);
        g.setColour(colour.withAlpha(0.18f * alpha));
        g.fillEllipse(circle);
        juce::Path ring;
        ring.addEllipse(circle.reduced(0.75f));
        juce::Path dashedRing;
        const float dashes[] = {3.0f, 2.5f};
        juce::PathStrokeType(stroke::thin).createDashedStroke(dashedRing, ring, dashes, 2);
        g.setColour(colour.withAlpha(0.9f * alpha));
        g.fillPath(dashedRing);
    }
}

void CurveView::drawMatchLegend(juce::Graphics& g, const CurveMapping& m) const
{
    const auto& d = matchDrawing_;
    if (!d.shown)
        return;
    const auto area = m.area();
    // Bajo el aviso de "Analyzer frozen", si está.
    const bool frozen = analyzerView_.frozen && analyzerMode_ != AnalyzerMode::Off;
    float x = area.x + 12.0f;
    const float y = area.y + (frozen ? 24.0f : 8.0f);
    const auto legendFont = captionFont();
    auto entry = [&](const juce::String& text, juce::Colour colour, bool dashed) {
        juce::Path line;
        line.startNewSubPath(x, y + 6.0f);
        line.lineTo(x + 14.0f, y + 6.0f);
        g.setColour(colour);
        if (dashed)
        {
            juce::Path pattern;
            const float dashes[] = {4.0f, 3.0f};
            juce::PathStrokeType(stroke::thin).createDashedStroke(pattern, line, dashes, 2);
            g.fillPath(pattern);
        }
        else
            g.strokePath(line, juce::PathStrokeType(stroke::medium));
        x += 18.0f;
        const float width = juce::GlyphArrangement::getStringWidth(legendFont, text.toUpperCase()) + 4.0f;
        draw::caption(g, text, {x, y, width, 12.0f}, juce::Justification::centredLeft, colour::textMuted);
        x += width + 10.0f;
    };
    entry("Track", colour::analyzerPre, false);
    entry("Reference", d.referenceColour, false);
    if (d.proposal != nullptr)
    {
        entry("Target", colour::text.withAlpha(0.75f), true);
        entry("Proposed", colour::accent, false);
    }
}

void CurveView::drawCollisions(juce::Graphics& g, const CurveMapping& m) const
{
    if (!showCollisions_ || collisions_.severity.empty())
        return;

    const auto area = m.area();
    const float bottom = area.y + area.height;
    const auto& frequencies = core::sessionFrequencies();
    const auto& severity = collisions_.severity;
    const std::size_t n = std::min(frequencies.size(), severity.size());

    // Cada punto ocupa la columna entre los puntos medios con sus vecinos.
    // Un velo de arriba abajo, y una franja más intensa en el borde inferior.
    constexpr float kStripHeight = 4.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (severity[i] < 0.02f)
            continue;
        const float x = m.xForFrequency(frequencies[i]);
        // Bordes en píxeles enteros: dos columnas vecinas comparten borde sin
        // que el suavizado de bordes deje una raya entre ellas.
        const float left = std::round(i > 0 ? 0.5f * (x + m.xForFrequency(frequencies[i - 1])) : area.x);
        const float right =
            std::round(i + 1 < n ? 0.5f * (x + m.xForFrequency(frequencies[i + 1])) : area.x + area.width);
        if (right <= left)
            continue;
        const juce::Rectangle<float> column{left, area.y, right - left, area.height};
        g.setColour(colour::danger.withAlpha(0.07f * severity[i]));
        g.fillRect(column);
        g.setColour(colour::danger.withAlpha(0.9f * severity[i]));
        g.fillRect(column.withTop(bottom - kStripHeight));
    }
}

void CurveView::drawCollisionLabels(juce::Graphics& g, const CurveMapping& m) const
{
    if (!showCollisions_ || collisions_.zones.empty())
        return;
    const auto area = m.area();

    // Etiquetas de las zonas más importantes, en una fila arriba. A la derecha
    // queda sitio para la escala de dB.
    CollisionViewSettings settings;
    constexpr std::size_t kMaxLabels = 4;
    constexpr float kLabelHeight = 20.0f;
    const auto labelFont = font(type::small, Weight::Medium);
    std::vector<float> centres;
    std::vector<float> widths;
    std::vector<const CollisionZone*> zones;
    for (const auto& zone : collisions_.zones)
    {
        if (zones.size() == kMaxLabels)
            break;
        if (zone.culprit < 0 || static_cast<std::size_t>(zone.culprit) >= overlays_.size() ||
            std::log2(zone.region.highHz / zone.region.lowHz) < settings.minLabelledOctaves)
            continue;
        const auto& culprit = overlays_[static_cast<std::size_t>(zone.culprit)];
        const auto text = toJuce(collisionLabel(culprit.name, zone.region.peakHz));
        zones.push_back(&zone);
        centres.push_back(m.xForFrequency(zone.region.peakHz));
        widths.push_back(juce::GlyphArrangement::getStringWidth(labelFont, text) + 2.0f * space::x2 + space::x3);
    }
    const Rect row{area.x + space::x2, area.y, area.width - 52.0f, area.height};
    const auto placed = placeLabels(centres, widths, row, area.y + space::x2, kLabelHeight, space::x1);

    g.setFont(labelFont);
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        if (!placed[i])
            continue;
        const auto& zone = *zones[i];
        const auto& culprit = overlays_[static_cast<std::size_t>(zone.culprit)];
        const auto r = toJuce(*placed[i]);
        g.setColour(colour::card.withAlpha(0.92f));
        g.fillRoundedRectangle(r, radius::control);
        g.setColour(colour::danger.withAlpha(0.35f + 0.65f * zone.region.peakSeverity));
        g.drawRoundedRectangle(r.reduced(0.5f), radius::control, stroke::hairline);

        auto inner = r.reduced(space::x2, 0.0f);
        g.setColour(culprit.colour);
        g.fillEllipse(inner.removeFromLeft(6.0f).withSizeKeepingCentre(6.0f, 6.0f));
        inner.removeFromLeft(space::x1 + 2.0f);
        g.setColour(colour::text);
        g.drawText(toJuce(collisionLabel(culprit.name, zone.region.peakHz)), inner,
                   juce::Justification::centredLeft, false);
    }
}

void CurveView::updateCurveCache(const CurveMapping& m)
{
    auto& cache = curveCache_;
    const auto area = m.area();
    const double sampleRate = processor_.currentSampleRate();

    // Geometría de las columnas: cambia con el tamaño, el rango o la frecuencia de muestreo.
    const bool sameGeometry = !cache.xs.empty() && juce::exactlyEqual(cache.area.x, area.x) &&
                              juce::exactlyEqual(cache.area.y, area.y) && juce::exactlyEqual(cache.area.width, area.width) &&
                              juce::exactlyEqual(cache.area.height, area.height) &&
                              juce::exactlyEqual(cache.rangeDb, m.rangeDb()) && juce::exactlyEqual(cache.sampleRate, sampleRate);
    if (!sameGeometry)
    {
        cache.area = area;
        cache.rangeDb = m.rangeDb();
        cache.sampleRate = sampleRate;
        const int points = std::max(2, static_cast<int>(area.width / 2.0f));
        cache.xs.resize(static_cast<std::size_t>(points));
        cache.tangents.resize(static_cast<std::size_t>(points));
        cache.hz.resize(static_cast<std::size_t>(points));
        for (int i = 0; i < points; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            const float x = area.x + area.width * static_cast<float>(i) / static_cast<float>(points - 1);
            cache.xs[idx] = x;
            cache.hz[idx] = m.frequencyForX(x);
            cache.tangents[idx] = dsp::responseTangent(std::min(cache.hz[idx], 0.499 * sampleRate), sampleRate);
        }
        for (auto& band : cache.bands)
        {
            band.valid = false;
            band.maskRevision = -1;
        }
    }

    const auto& xs = cache.xs;
    const std::size_t points = xs.size();
    const float top = area.y;
    const float bottom = area.y + area.height;
    auto clampY = [&](double db) { return std::clamp(m.yForGain(db), top - 2.0f, bottom + 2.0f); };
    const float zeroY = m.yForGain(0.0);

    for (int band = 0; band < core::kNumBands; ++band)
    {
        auto& curve = cache.bands[static_cast<std::size_t>(band)];
        const auto settings = core::bandSettings(values_, band, algorithm_);
        if (!settings.enabled)
        {
            curve.valid = false;
            continue;
        }
        const bool dynamic = isDynamic(settings);
        curve.spectral = dynamic && settings.dynamics.spectral;
        const auto sounding = soundingSettings(band).filter;
        if (curve.valid && curve.dynamic == dynamic && curve.sounding == sounding &&
            (!dynamic || curve.reference == settings.filter))
            continue;

        // La curva que suena: solo si ha cambiado (en una banda dinámica, casi siempre).
        if (!curve.valid || curve.sounding != sounding)
        {
            const auto design = dsp::designBand(sounding, sampleRate);
            curve.db.resize(points);
            curve.line.clear();
            curve.fill.clear();
            curve.fill.startNewSubPath(xs.front(), zeroY);
            for (std::size_t i = 0; i < points; ++i)
            {
                const double db = dsp::magnitudeDbAtTangent(design, cache.tangents[i]);
                curve.db[i] = db;
                const float y = clampY(db);
                i == 0 ? curve.line.startNewSubPath(xs[i], y) : curve.line.lineTo(xs[i], y);
                curve.fill.lineTo(xs[i], y);
            }
            curve.fill.lineTo(xs.back(), zeroY);
            curve.fill.closeSubPath();
        }

        // La referencia estática punteada solo depende del ajuste estático:
        // no se rehace aunque la ganancia dinámica se mueva.
        if (dynamic && (!curve.valid || !curve.dynamic || curve.reference != settings.filter))
        {
            const auto design = dsp::designBand(settings.filter, sampleRate);
            juce::Path reference;
            curve.referenceDb.resize(points);
            for (std::size_t i = 0; i < points; ++i)
            {
                curve.referenceDb[i] = dsp::magnitudeDbAtTangent(design, cache.tangents[i]);
                const float ys = clampY(curve.referenceDb[i]);
                i == 0 ? reference.startNewSubPath(xs[i], ys) : reference.lineTo(xs[i], ys);
            }
            curve.maskRevision = -1; // la máscara cuelga de la referencia
            const float dashes[] = {4.0f, 4.0f};
            curve.dashed.clear();
            juce::PathStrokeType(1.0f).createDashedStroke(curve.dashed, reference, dashes, 2);
        }
        if (!dynamic)
            curve.dashed.clear();

        curve.valid = true;
        curve.dynamic = dynamic;
        curve.sounding = sounding;
        curve.reference = settings.filter;
    }

    // Dinámica espectral: la forma de la máscara, como un sombreado que baja
    // (o sube, con un rango positivo) desde la referencia estática. Solo en
    // las bandas que la tienen ahora, aunque la última máscara publicada aún
    // no lo sepa, y solo si ha cambiado algo.
    for (int band = 0; band < core::kNumBands; ++band)
    {
        auto& curve = cache.bands[static_cast<std::size_t>(band)];
        const bool masked = curve.valid && curve.spectral && spectralMask_.active[static_cast<std::size_t>(band)];
        if (!masked)
        {
            curve.mask.clear();
            curve.maskDb.clear();
            curve.maskRevision = -1;
            continue;
        }
        if (curve.maskRevision == spectralMaskRevision_)
            continue;
        curve.maskDb.resize(points);
        spectralMaskColumns(spectralMask_.gainsDb[static_cast<std::size_t>(band)], cache.hz, curve.maskDb);
        curve.mask.clear();
        for (std::size_t i = 0; i < points; ++i)
        {
            const float y = clampY(curve.referenceDb[i]);
            i == 0 ? curve.mask.startNewSubPath(xs[i], y) : curve.mask.lineTo(xs[i], y);
        }
        for (std::size_t i = points; i-- > 0;)
            curve.mask.lineTo(xs[i], clampY(curve.referenceDb[i] + static_cast<double>(curve.maskDb[i])));
        curve.mask.closeSubPath();
        curve.maskRevision = spectralMaskRevision_;
    }
}

void CurveView::drawCurves(juce::Graphics& g, const CurveMapping& m) const
{
    const auto& cache = curveCache_;
    const auto& xs = cache.xs;
    const std::size_t points = xs.size();
    const float top = m.area().y;
    const float bottom = m.area().y + m.area().height;
    auto clampY = [&](double db) { return std::clamp(m.yForGain(db), top - 2.0f, bottom + 2.0f); };
    const float zeroY = m.yForGain(0.0);

    std::vector<double> total(points, 0.0);
    bool any = false;
    for (int band = 0; band < core::kNumBands; ++band)
    {
        const auto& curve = cache.bands[static_cast<std::size_t>(band)];
        if (!curve.valid)
            continue;
        any = true;
        for (std::size_t i = 0; i < points; ++i)
            total[i] += curve.db[i];
        // La máscara de la dinámica espectral suena encima de la curva estática de la banda.
        for (std::size_t i = 0; i < curve.maskDb.size(); ++i)
            total[i] += static_cast<double>(curve.maskDb[i]);

        const auto colour = theme::band(band);
        const bool isSelected = band == selected_;
        const bool isHot = isSelected || band == hovered_ || band == dragging_;
        g.setColour(colour.withAlpha(isSelected ? 0.20f : isHot ? 0.14f : 0.06f));
        g.fillPath(curve.fill);
        if (!curve.mask.isEmpty())
        {
            g.setColour(colour.withAlpha(isSelected ? 0.50f : isHot ? 0.42f : 0.30f));
            g.fillPath(curve.mask);
        }
        g.setColour(colour.withAlpha(isHot ? 0.95f : 0.45f));
        g.strokePath(curve.line, juce::PathStrokeType(isHot ? stroke::thin : stroke::hairline,
                                                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // La referencia estática punteada; en una banda espectral, la curva ya es la estática.
        if (curve.dynamic && !curve.spectral)
        {
            g.setColour(colour.withAlpha(isSelected ? 0.7f : 0.35f));
            g.fillPath(curve.dashed);
        }
    }

    juce::Path curve;
    for (std::size_t i = 0; i < points; ++i)
    {
        const float y = any ? clampY(total[i]) : zeroY;
        i == 0 ? curve.startNewSubPath(xs[i], y) : curve.lineTo(xs[i], y);
    }
    // Con el bypass puesto la curva no suena: se atenúa.
    g.setColour(colour::curve.withAlpha(values_[core::kBypass] >= 0.5f ? 0.35f : 1.0f));
    g.strokePath(curve,
                 juce::PathStrokeType(stroke::medium, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CurveView::drawNodes(juce::Graphics& g, const CurveMapping& m) const
{
    const auto area = m.area();
    const bool bypassed = values_[core::kBypass] >= 0.5f;

    // De atrás adelante, y el nodo activo el último: queda siempre encima.
    std::array<int, core::kNumBands> order{};
    int count = 0;
    const int top = dragging_ >= 0 ? dragging_ : (hovered_ >= 0 ? hovered_ : selected_);
    for (int band = 0; band < core::kNumBands; ++band)
        if (band != top && core::bandSettings(values_, band).enabled)
            order[static_cast<std::size_t>(count++)] = band;
    if (top >= 0 && core::bandSettings(values_, top).enabled)
        order[static_cast<std::size_t>(count++)] = top;

    for (int n = 0; n < count; ++n)
    {
        const int band = order[static_cast<std::size_t>(n)];
        const auto settings = core::bandSettings(values_, band);
        const auto p = nodePosition(m, settings, kNodeEdgeMargin);
        const juce::Point<float> centre{p.x, p.y};
        const auto colour = bypassed ? theme::band(band).withSaturation(0.15f).withMultipliedBrightness(0.7f) : theme::band(band);

        const bool isSelected = band == selected_;
        const bool isHovered = band == hovered_;
        const bool isDragged = band == dragging_;
        const float r = kNodeRadius + (isDragged ? 2.0f : isSelected || isHovered ? 1.0f : 0.0f);
        const auto circle = juce::Rectangle<float>(2.0f * r, 2.0f * r).withCentre(centre);

        // Dinámica: junto al nodo, una guía hasta el límite del rango y una
        // barra con la ganancia que se está aplicando ahora. Van en el mismo
        // eje de dB que la curva: se lee cuánto baja o sube la banda.
        if (isDynamic(settings))
        {
            const double staticGain = settings.filter.gainDb;
            const float gainNow = dynamicGains_[static_cast<std::size_t>(band)];
            const float limitY = std::clamp(m.yForGain(staticGain + settings.dynamics.rangeDb), area.y + 3.0f,
                                            area.y + area.height - 3.0f);
            const float fraction = dynamicMeterFraction(gainNow, settings.dynamics.rangeDb);
            const float nowY = p.y + fraction * (limitY - p.y);
            const float x = p.x + r + 7.0f;

            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(juce::Rectangle<float>(x - 1.0f, std::min(p.y, limitY), 2.0f, std::abs(limitY - p.y)), 1.0f);
            g.fillRoundedRectangle(juce::Rectangle<float>(x - 4.0f, limitY - 1.0f, 8.0f, 2.0f), 1.0f);
            if (std::abs(nowY - p.y) > 0.5f)
            {
                g.setColour(colour);
                g.fillRoundedRectangle(juce::Rectangle<float>(x - 2.5f, std::min(p.y, nowY), 5.0f, std::abs(nowY - p.y)), 2.0f);
            }
        }

        // Halo: el nodo responde antes de moverse.
        if (isHovered || isDragged)
        {
            g.setColour(colour.withAlpha(isDragged ? 0.30f : 0.20f));
            g.fillEllipse(circle.expanded(isDragged ? 9.0f : 6.0f));
        }

        // Cuerpo, con un reborde del color del fondo que lo separa de las curvas.
        g.setColour(colour::plot);
        g.fillEllipse(circle.expanded(1.5f));
        g.setColour(colour);
        g.fillEllipse(circle);
        if (isSelected)
        {
            g.setColour(colour::text);
            g.drawEllipse(circle.expanded(2.5f), stroke::thin);
        }

        g.setColour(colour::plot);
        g.setFont(font(type::caption, Weight::SemiBold, true));
        g.drawText(juce::String(band + 1), circle, juce::Justification::centred, false);

        // Canal: una insignia con su letra cuando la banda no es estéreo.
        if (settings.channel != dsp::ChannelMode::Stereo)
        {
            static constexpr const char* kLetters[] = {"", "L", "R", "M", "S"};
            const auto badge = juce::Rectangle<float>(12.0f, 12.0f).withCentre({p.x - r - 2.0f, p.y - r - 2.0f});
            g.setColour(colour::plot);
            g.fillEllipse(badge);
            g.setColour(colour);
            g.drawEllipse(badge.reduced(0.5f), stroke::hairline);
            g.setFont(font(type::caption - 2.0f, Weight::SemiBold));
            g.drawText(kLetters[static_cast<int>(settings.channel)], badge, juce::Justification::centred, false);
        }

        // Ganancia fuera del rango visible: una flecha hacia donde sigue.
        if (const int overflow = gainOverflow(m, settings); overflow != 0)
        {
            const float direction = overflow > 0 ? -1.0f : 1.0f; // hacia arriba o hacia abajo en pantalla
            const float base = p.y + direction * (r + 4.0f);
            juce::Path arrow;
            arrow.addTriangle(p.x - 4.5f, base, p.x + 4.5f, base, p.x, base + direction * 5.0f);
            g.setColour(colour);
            g.fillPath(arrow);
        }
    }
}

void CurveView::drawReadout(juce::Graphics& g, const CurveMapping& m) const
{
    // Etiqueta flotante de la banda que se está tocando o señalando. En la
    // vista ampliada, sin el panel, con detalle (canal y dinámica). Solo
    // aparece al señalar: fija, taparía los nodos de alrededor.
    const int band = dragging_ >= 0 ? dragging_ : hovered_;
    if (band < 0)
        return;
    const auto settings = core::bandSettings(values_, band);
    if (!settings.enabled)
        return;

    const auto readout = nodeReadout(values_, band, notesVisible_ ? std::optional(a4Hz_) : std::nullopt, expanded_);
    const auto titleFont = captionFont();
    const auto valueFont = font(type::body, Weight::Medium, true);

    juce::StringArray fields;
    fields.add(toJuce(readout.frequency));
    if (!readout.gain.empty())
        fields.add(toJuce(readout.gain));
    if (!readout.q.empty())
        fields.add(toJuce(readout.q));

    // La ganancia dinámica del momento, si la banda es dinámica.
    juce::String dynamicText;
    if (isDynamic(settings))
    {
        const float gain = dynamicGains_[static_cast<std::size_t>(band)];
        dynamicText = "Dyn " + toJuce(core::formatValue(
                                   core::parameters()[core::dynamicParameter(band, core::DynParam::Range)], gain));
    }

    constexpr float kPadding = space::x2 + 2.0f;
    constexpr float kFieldGap = space::x3;
    auto width = [](const juce::Font& f, const juce::String& text) { return juce::GlyphArrangement::getStringWidth(f, text); };

    float valuesWidth = 0.0f;
    for (const auto& field : fields)
        valuesWidth += width(valueFont, field) + kFieldGap;
    if (dynamicText.isNotEmpty())
        // Con el ancho del valor más largo, para que la etiqueta no cambie de tamaño al moverse la ganancia.
        valuesWidth += width(valueFont, "Dyn -30.0 dB") + kFieldGap;
    valuesWidth -= kFieldGap;

    // En la vista ampliada, una segunda fila con la dinámica.
    const auto detailFont = font(type::small, Weight::Regular, true);
    float detailWidth = 0.0f;
    for (const auto& field : readout.dynamics)
        detailWidth += width(detailFont, toJuce(field)) + kFieldGap;
    detailWidth = std::max(0.0f, detailWidth - kFieldGap);
    constexpr float kDetailHeight = 16.0f;

    const auto title = toJuce(readout.title).toUpperCase();
    const float labelWidth =
        std::ceil(std::max({valuesWidth, detailWidth, width(titleFont, title)}) + 2.0f * kPadding + 4.0f);
    const float labelHeight = 44.0f + (readout.dynamics.empty() ? 0.0f : kDetailHeight);

    const auto node = nodePosition(m, settings, kNodeEdgeMargin);
    const auto r = floatingLabelRect(m.area(), node, labelWidth, labelHeight, kNodeRadius + 12.0f);
    const juce::Rectangle<float> box(r.x, r.y, r.width, r.height);
    const auto colour = theme::band(band);

    g.setColour(colour::card.withAlpha(0.96f));
    g.fillRoundedRectangle(box, radius::control);
    g.setColour(colour::lineStrong);
    g.drawRoundedRectangle(box.reduced(0.5f), radius::control, stroke::hairline);
    g.setColour(colour);
    g.fillRoundedRectangle(box.withWidth(3.0f).reduced(0.0f, 7.0f).translated(5.0f, 0.0f), 1.5f);

    auto content = box.reduced(kPadding, 6.0f).withTrimmedLeft(4.0f);
    g.setFont(titleFont);
    g.setColour(colour::textMuted);
    g.drawText(title, content.removeFromTop(13.0f), juce::Justification::centredLeft, false);
    content.removeFromTop(3.0f);

    if (!readout.dynamics.empty())
    {
        auto detail = content.removeFromBottom(kDetailHeight).withTrimmedTop(2.0f);
        g.setFont(detailFont);
        g.setColour(colour::textMuted);
        for (const auto& field : readout.dynamics)
        {
            const auto text = toJuce(field);
            const float w = width(detailFont, text);
            g.drawText(text, detail.removeFromLeft(w + 1.0f), juce::Justification::centredLeft, false);
            detail.removeFromLeft(kFieldGap - 1.0f);
        }
    }

    g.setFont(valueFont);
    g.setColour(colour::text);
    for (const auto& field : fields)
    {
        const float w = width(valueFont, field);
        g.drawText(field, content.removeFromLeft(w + 1.0f), juce::Justification::centredLeft, false);
        content.removeFromLeft(kFieldGap - 1.0f);
    }
    if (dynamicText.isNotEmpty())
    {
        g.setColour(colour);
        g.drawText(dynamicText, content, juce::Justification::centredLeft, false);
    }
}

void CurveView::drawMarkedNote(juce::Graphics& g, const CurveMapping& m) const
{
    // La tecla señalada en el teclado de notas: su frecuencia y su nombre.
    if (!markedNote_)
        return;
    const double hz = frequencyForNote(*markedNote_, a4Hz_);
    const auto area = m.area();
    const float x = std::round(m.xForFrequency(hz));
    g.setColour(colour::accent.withAlpha(0.8f));
    g.fillRect(juce::Rectangle<float>(x, area.y, 1.0f, area.height));

    const auto text = toJuce(noteName(*markedNote_)) + juce::String::fromUTF8(" \xC2\xB7 ") +
                      toJuce(core::formatValue(core::parameters()[core::bandParameter(0, core::BandParam::Frequency)],
                                               static_cast<float>(hz)));
    const auto labelFont = font(type::small, Weight::Medium, true);
    const float width = std::ceil(juce::GlyphArrangement::getStringWidth(labelFont, text)) + 2.0f * space::x2;
    constexpr float kHeight = 18.0f;
    // Junto a la línea, a la derecha si cabe, y en lo alto de la curva.
    const float left = x + space::x1 + width <= area.x + area.width ? x + space::x1 : x - space::x1 - width;
    const juce::Rectangle<float> box(left, area.y + space::x2, width, kHeight);
    g.setColour(colour::card.withAlpha(0.92f));
    g.fillRoundedRectangle(box, radius::small);
    g.setColour(colour::accent);
    g.setFont(labelFont);
    g.drawText(text, box, juce::Justification::centred, false);
}

void CurveView::drawPeakMarker(juce::Graphics& g, const CurveMapping& m) const
{
    if (!peakMarker_ || !pointer_)
        return;
    const auto area = m.area();
    const auto labelFont = font(type::small, Weight::Medium, true);
    constexpr float kHeight = 18.0f;
    auto drawLabel = [&](const juce::String& text, juce::Rectangle<float> box, juce::Colour colour) {
        g.setColour(colour::card.withAlpha(0.92f));
        g.fillRoundedRectangle(box, radius::small);
        g.setColour(colour);
        g.setFont(labelFont);
        g.drawText(text, box, juce::Justification::centred, false);
    };
    auto labelWidth = [&](const juce::String& text) {
        return std::ceil(juce::GlyphArrangement::getStringWidth(labelFont, text)) + 2.0f * space::x2;
    };

    // Las 24 bandas en uso: no se marca el pico, y el aviso va junto al ratón.
    if (!freeBand_)
    {
        const juce::String text("No free bands");
        const auto r = floatingLabelRect(area, {pointer_->x, pointer_->y}, labelWidth(text), kHeight, 14.0f);
        drawLabel(text, {r.x, r.y, r.width, r.height}, colour::textMuted);
        return;
    }

    // El marcador va sobre el espectro que se pinta, con la inclinación.
    const auto& peak = *peakMarker_;
    const float x = m.xForFrequency(peak.frequencyHz);
    const double shownDb =
        peak.levelDb + dsp::SpectrumAnalyzer::tiltDb(peak.frequencyHz, analyzerView_.tiltDbPerOctave);
    const float y = std::clamp(m.yForAnalyzerDb(shownDb), area.y, area.y + area.height);
    constexpr float kRadius = 5.0f;
    g.setColour(colour::accent.withAlpha(0.35f));
    g.fillRect(
        juce::Rectangle<float>(std::round(x), y + kRadius, 1.0f, std::max(0.0f, area.y + area.height - y - kRadius)));
    g.setColour(colour::accent);
    g.drawEllipse(juce::Rectangle<float>(2.0f * kRadius, 2.0f * kRadius).withCentre({x, y}), stroke::thin);

    const auto text = toJuce(peakMarkerText(peak, a4Hz_));
    const auto r = floatingLabelRect(area, {x, y}, labelWidth(text), kHeight, kRadius + 6.0f);
    drawLabel(text, {r.x, r.y, r.width, r.height}, colour::text);
}

// ---------------------------------------------------------------------------
// Ratón

std::optional<int> CurveView::bandAt(juce::Point<float> position) const
{
    const auto m = mapping();
    std::array<Point, core::kNumBands> nodes{};
    std::array<bool, core::kNumBands> visible{};
    for (int band = 0; band < core::kNumBands; ++band)
    {
        const auto settings = core::bandSettings(values_, band);
        visible[static_cast<std::size_t>(band)] = settings.enabled;
        nodes[static_cast<std::size_t>(band)] = nodePosition(m, settings, kNodeEdgeMargin);
    }
    return hitTestNodes(nodes, visible, {position.x, position.y}, kHitRadius);
}

void CurveView::setParameter(int band, core::BandParam param, float value)
{
    const auto index = core::bandParameter(band, param);
    auto& p = processor_.parameter(index);
    p.setValueNotifyingHost(p.convertTo0to1(value));
    values_[index] = core::sanitize(core::parameters()[index], value);
}

void CurveView::setParameterAsGesture(std::size_t index, float value)
{
    auto& p = processor_.parameter(index);
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(value));
    p.endChangeGesture();
    values_[index] = core::sanitize(core::parameters()[index], value);
    repaint();
}

void CurveView::beginGesture(int band, core::BandParam param)
{
    processor_.parameter(core::bandParameter(band, param)).beginChangeGesture();
}

void CurveView::endGesture(int band, core::BandParam param)
{
    processor_.parameter(core::bandParameter(band, param)).endChangeGesture();
}

void CurveView::setPointer(std::optional<juce::Point<float>> position)
{
    const int hovered = position ? bandAt(*position).value_or(-1) : -1;
    const bool hoverChanged = hovered != hovered_;
    hovered_ = hovered;
    pointer_ = position;
    const bool markerChanged = updatePeakMarker();
    updateCursor();
    if (hoverChanged || markerChanged)
        repaint();
}

void CurveView::updateCursor()
{
    if (hovered_ >= 0 || dragging_ >= 0)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else if (peakMarker_ && freeBand_)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor); // arrastrar en vertical crea la banda
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

bool CurveView::updatePeakMarker()
{
    // Solo con el ratón en un hueco de la curva y el analizador encendido.
    std::optional<SpectrumPeak> marker;
    bool freeBand = true;
    if (pointer_ && analyzerMode_ != AnalyzerMode::Off && hovered_ < 0 && dragging_ < 0)
    {
        if (peaksStale_)
        {
            peaks_ = findPeaks(peakSpectrum_.levels(), analyzerFrequencies_,
                               {.tiltDbPerOctave = analyzerView_.tiltDbPerOctave});
            peaksStale_ = false;
        }
        marker = nearestPeak(peaks_, mapping().frequencyForX(pointer_->x));
        freeBand = core::firstFreeBand(values_).has_value();
    }
    const bool changed = !samePeak(marker, peakMarker_) || (marker && freeBand != freeBand_);
    peakMarker_ = marker;
    freeBand_ = freeBand;
    return changed;
}

void CurveView::mouseMove(const juce::MouseEvent& e)
{
    setPointer(e.position);
}

void CurveView::mouseDown(const juce::MouseEvent& e)
{
    const auto band = bandAt(e.position);

    if (e.mods.isPopupMenu())
    {
        if (band)
        {
            setSelectedBand(*band);
            showBandMenu(*band);
        }
        return;
    }

    if (!band)
    {
        const auto m = mapping();
        if (analyzerView_.peakHold && onGainScale(m.area(), {e.position.x, e.position.y}))
        {
            postPeaks_.clear();
            repaint();
        }
        setSelectedBand(-1);
        // Desde un pico marcado, un arrastre crea su banda (un clic solo deselecciona).
        if (peakMarker_ && freeBand_)
        {
            pressedPeak_ = peakMarker_;
            dragLast_ = e.position;
        }
        return;
    }

    setSelectedBand(*band);
    dragging_ = *band;
    dragLast_ = e.position;
    dragFrequencyHz_ = core::bandSettings(values_, *band).filter.frequencyHz;
    draggingGain_ = core::usesGain(core::bandSettings(values_, *band).filter.type);
    beginGesture(dragging_, core::BandParam::Frequency);
    if (draggingGain_)
        beginGesture(dragging_, core::BandParam::Gain);
}

void CurveView::mouseDrag(const juce::MouseEvent& e)
{
    if (pressedPeak_ && dragging_ < 0)
    {
        // La banda se crea cuando el ratón se ha movido algo, no al pulsar.
        if (!e.mouseWasDraggedSinceMouseDown())
            return;
        createPeakBand(*pressedPeak_);
    }
    if (dragging_ < 0)
        return;

    const auto m = mapping();
    const auto settings = core::bandSettings(values_, dragging_);

    // Con Mayús el ratón se mueve 10 veces más despacio sobre la curva.
    const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
    const juce::Point<float> delta = (e.position - dragLast_) * scale;
    dragLast_ = e.position;

    // Con Alt, la banda va a la nota más cercana a donde estaría sin ajustar.
    // La de un pico se queda en su frecuencia: solo cambia la ganancia.
    if (!pressedPeak_)
    {
        const auto& frequency = core::parameters()[core::bandParameter(dragging_, core::BandParam::Frequency)];
        dragFrequencyHz_ = std::clamp(m.frequencyForX(m.xForFrequency(dragFrequencyHz_) + delta.x),
                                      static_cast<double>(frequency.min), static_cast<double>(frequency.max));
        if (e.mods.isAltDown())
            setParameter(dragging_, core::BandParam::Frequency,
                         static_cast<float>(snapToSemitone(dragFrequencyHz_, a4Hz_, frequency.min, frequency.max)));
        else
        {
            setParameter(dragging_, core::BandParam::Frequency, static_cast<float>(dragFrequencyHz_));
            dragFrequencyHz_ = values_[core::bandParameter(dragging_, core::BandParam::Frequency)];
        }
    }
    if (draggingGain_)
        setParameter(dragging_, core::BandParam::Gain,
                     static_cast<float>(draggedGain(m, settings.filter.gainDb, delta.y)));
    repaint();
}

void CurveView::mouseUp(const juce::MouseEvent& e)
{
    if (dragging_ >= 0)
    {
        if (pressedPeak_)
        {
            // Con Alt, la banda del pico queda dinámica. Va dentro del gesto
            // de la ganancia, que sigue abierto: todo es una sola acción.
            if (e.mods.isAltDown())
                applyChanges(dynamicPeakChanges(values_, dragging_, *pressedPeak_),
                             core::bandParameter(dragging_, core::BandParam::Gain));
        }
        else
            endGesture(dragging_, core::BandParam::Frequency);
        if (draggingGain_)
            endGesture(dragging_, core::BandParam::Gain);
        dragging_ = -1;
    }
    pressedPeak_.reset();
    // Si se suelta fuera de la curva, el ratón ya no está en ella.
    setPointer(getLocalBounds().toFloat().contains(e.position) ? std::optional(e.position) : std::nullopt);
    repaint();
}

void CurveView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (const auto band = bandAt(e.position))
        removeBand(*band);
    else
        addBandAt(e.position);
}

void CurveView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = bandAt(e.position).value_or(selected_);
    if (band < 0)
        return;

    const auto settings = core::bandSettings(values_, band);
    if (!core::usesQ(settings.filter.type, settings.filter.slopeDbPerOct))
        return;

    const float delta = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    const auto q = static_cast<float>(wheelQ(settings.filter.q, static_cast<double>(delta)));

    beginGesture(band, core::BandParam::Q);
    setParameter(band, core::BandParam::Q, q);
    endGesture(band, core::BandParam::Q);
    repaint();
}

void CurveView::mouseExit(const juce::MouseEvent&)
{
    setPointer(std::nullopt);
}

bool CurveView::keyPressed(const juce::KeyPress& key)
{
    const bool left = key.isKeyCode(juce::KeyPress::leftKey);
    const bool right = key.isKeyCode(juce::KeyPress::rightKey);
    const bool up = key.isKeyCode(juce::KeyPress::upKey);
    const bool down = key.isKeyCode(juce::KeyPress::downKey);
    const auto mods = key.getModifiers();

    // F amplía o reduce la vista; Esc, ampliada, vuelve.
    const bool expandKey = key.getKeyCode() == 'F' || key.getKeyCode() == 'f';
    if ((expandKey && !mods.isAnyModifierKeyDown()) || (expanded_ && key.isKeyCode(juce::KeyPress::escapeKey)))
    {
        if (onExpandToggled)
            onExpandToggled();
        return true;
    }

    // Izquierda y derecha, sin modificadores: banda anterior o siguiente (en orden de número).
    if ((left || right) && !mods.isCommandDown())
    {
        if (const auto band = stepSelectedBand(values_, selected_, right ? 1 : -1))
        {
            keyboardFocus_ = true;
            setSelectedBand(*band);
        }
        return true;
    }

    if (selected_ < 0)
        return false;

    if (key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey))
    {
        removeBand(selected_);
        return true;
    }

    // Arriba y abajo: ganancia. Ctrl/Cmd + izquierda y derecha: frecuencia. Mayús: ajuste fino.
    auto nudge = [&](core::BandParam param, int steps) {
        const auto index = core::bandParameter(selected_, param);
        const float next = keyStepValue(core::parameters()[index], values_[index], steps, mods.isShiftDown());
        beginGesture(selected_, param);
        setParameter(selected_, param, next);
        endGesture(selected_, param);
        repaint();
    };

    if (up || down)
    {
        if (core::usesGain(core::bandSettings(values_, selected_).filter.type))
            nudge(core::BandParam::Gain, up ? 1 : -1);
        return true;
    }
    if (left || right)
    {
        // De semitono en semitono; con Mayús, en pasos finos como cualquier valor.
        if (mods.isShiftDown())
            nudge(core::BandParam::Frequency, right ? 1 : -1);
        else
        {
            const auto index = core::bandParameter(selected_, core::BandParam::Frequency);
            const auto& spec = core::parameters()[index];
            setParameterAsGesture(
                index, static_cast<float>(stepSemitones(values_[index], a4Hz_, right ? 1 : -1, spec.min, spec.max)));
        }
        return true;
    }
    return false;
}

void CurveView::focusGained(FocusChangeType cause)
{
    keyboardFocus_ = cause == focusChangedByTabKey;
    repaint();
}

void CurveView::focusLost(FocusChangeType)
{
    keyboardFocus_ = false;
    repaint();
}

void CurveView::applyChanges(const std::vector<ParameterChange>& changes, std::optional<std::size_t> openGesture)
{
    for (const auto& change : changes)
    {
        auto& p = processor_.parameter(change.index);
        const bool ownGesture = change.index != openGesture;
        if (ownGesture)
            p.beginChangeGesture();
        p.setValueNotifyingHost(p.convertTo0to1(change.value));
        if (ownGesture)
            p.endChangeGesture();
        values_[change.index] = change.value;
    }
}

void CurveView::addBandAt(juce::Point<float> position)
{
    const auto band = core::firstFreeBand(values_);
    if (!band)
        return;

    // Una campana en el punto del doble clic, con el resto de ajustes por
    // defecto: la banda no hereda la dinámica ni el canal de cuando estuvo activa.
    const auto m = mapping();
    applyChanges(newBandChanges(values_, *band, m.frequencyForX(position.x), m.gainForY(position.y)));
    setSelectedBand(*band);
}

void CurveView::createPeakBand(const SpectrumPeak& peak)
{
    // Una banda puede haberse ocupado desde el host entre pulsar y arrastrar.
    const auto band = core::firstFreeBand(values_);
    if (!band)
    {
        pressedPeak_.reset();
        return;
    }

    // El gesto de la ganancia se abre el primero y se cierra al soltar: crear
    // la banda, arrastrarla y, con Alt, hacerla dinámica es una sola acción
    // de deshacer (EditHistory agrupa lo que pasa con algún gesto abierto).
    const auto gain = core::bandParameter(*band, core::BandParam::Gain);
    beginGesture(*band, core::BandParam::Gain);
    applyChanges(peakBandChanges(values_, *band, peak, 0.0), gain);
    setSelectedBand(*band);
    dragging_ = *band;
    draggingGain_ = true;
    peakMarker_.reset();
    updateCursor();
}

void CurveView::removeBand(int band)
{
    beginGesture(band, core::BandParam::Enabled);
    setParameter(band, core::BandParam::Enabled, 0.0f);
    endGesture(band, core::BandParam::Enabled);
    if (selected_ == band)
        setSelectedBand(-1);
    hovered_ = -1;
    repaint();
}

void CurveView::showBandMenu(int band)
{
    const auto current = static_cast<int>(values_[core::bandParameter(band, core::BandParam::Type)]);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addSectionHeader("Band " + juce::String(band + 1));
    for (std::size_t i = 0; i < core::kFilterTypeChoices.size(); ++i)
    {
        const auto& name = core::kFilterTypeChoices[i];
        menu.addItem(static_cast<int>(i) + 1, toJuce(name), true, static_cast<int>(i) == current);
    }
    juce::PopupMenu channels;
    channels.setLookAndFeel(&getLookAndFeel());
    constexpr int kFirstChannelId = 200;
    const auto channelIndex = core::channelParameter(band);
    for (std::size_t i = 0; i < core::kChannelChoices.size(); ++i)
    {
        const auto& name = core::kChannelChoices[i];
        channels.addItem(kFirstChannelId + static_cast<int>(i), toJuce(name), true,
                         static_cast<int>(values_[channelIndex]) == static_cast<int>(i));
    }
    menu.addSeparator();
    menu.addSubMenu("Channel", channels);
    constexpr int kDynamicId = 99;
    constexpr int kDeleteId = 100;
    const auto settings = core::bandSettings(values_, band);
    menu.addItem(kDynamicId, "Dynamic", core::usesDynamics(settings.filter.type), settings.dynamics.enabled);
    menu.addItem(kDeleteId, "Delete band");

    juce::Component::SafePointer<CurveView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                       [safe, band](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           if (result == kDeleteId)
                           {
                               safe->removeBand(band);
                               return;
                           }
                           if (result >= kFirstChannelId)
                               safe->setParameterAsGesture(core::channelParameter(band),
                                                           static_cast<float>(result - kFirstChannelId));
                           else if (result == kDynamicId)
                           {
                               const auto index = core::dynamicParameter(band, core::DynParam::Enabled);
                               safe->setParameterAsGesture(index, safe->values_[index] >= 0.5f ? 0.0f : 1.0f);
                           }
                           else
                               safe->setParameterAsGesture(core::bandParameter(band, core::BandParam::Type),
                                                           static_cast<float>(result - 1));
                       });
}

} // namespace esceq::ui
