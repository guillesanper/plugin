// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CurveView.h"

#include "Palette.h"
#include "PluginProcessor.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/filter_design.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace esceq::ui {

namespace {

constexpr int kAnalyzerPoints = 320;
constexpr float kNodeRadius = 7.0f;
constexpr float kHitRadius = 12.0f;
constexpr double kAnalyzerRiseSeconds = 0.06;  // constante de tiempo al subir
constexpr double kAnalyzerFallSeconds = 0.35;  // y al bajar: más lenta, para que se pueda leer
constexpr double kAnalyzerTiltDbPerOctave = 4.5;
constexpr float kDefaultQ = 0.7071f;

juce::String frequencyLabel(double hz)
{
    return hz >= 1000.0 ? juce::String(hz / 1000.0, 0) + "k" : juce::String(static_cast<int>(hz));
}

} // namespace

CurveView::CurveView(PluginProcessor& processor)
    : processor_(processor),
      preAnalyzer_(std::make_unique<dsp::SpectrumAnalyzer>(12)),
      postAnalyzer_(std::make_unique<dsp::SpectrumAnalyzer>(12)),
      analyzerFrequencies_(logFrequencies(kAnalyzerPoints)),
      preLevels_(kAnalyzerPoints, dsp::SpectrumAnalyzer::kFloorDb),
      postLevels_(kAnalyzerPoints, dsp::SpectrumAnalyzer::kFloorDb),
      scratch_(kAnalyzerPoints, 0.0f),
      readBuffer_(4096, 0.0f)
{
    preAnalyzer_->setTiltDbPerOctave(kAnalyzerTiltDbPerOctave);
    postAnalyzer_->setTiltDbPerOctave(kAnalyzerTiltDbPerOctave);
    values_ = processor_.currentValues();
    setOpaque(true);
}

CurveView::~CurveView() = default;

void CurveView::setSelectedBand(int band)
{
    if (band == selected_)
        return;
    selected_ = band;
    if (onSelectionChanged)
        onSelectionChanged(selected_);
    repaint();
}

void CurveView::setRangeDb(double rangeDb)
{
    rangeDb_ = rangeDb;
    repaint();
}

void CurveView::setAnalyzerMode(AnalyzerMode mode)
{
    analyzerMode_ = mode;
    repaint();
}

CurveMapping CurveView::mapping() const
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    return CurveMapping({bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()}, rangeDb_);
}

dsp::BandSettings CurveView::soundingSettings(int band) const
{
    auto settings = core::bandSettings(values_, band);
    if (settings.dynamics.enabled && core::usesDynamics(settings.filter.type))
        settings.filter.gainDb += dynamicGains_[static_cast<std::size_t>(band)];
    return settings;
}

void CurveView::resized()
{
    repaint();
}

void CurveView::refresh()
{
    values_ = processor_.currentValues();
    for (int band = 0; band < core::kNumBands; ++band)
        dynamicGains_[static_cast<std::size_t>(band)] = processor_.dynamicGainDb(band);

    // Si la banda seleccionada se ha desactivado (desde el host o el panel), se deselecciona.
    if (selected_ >= 0 && values_[core::bandParameter(selected_, core::BandParam::Enabled)] < 0.5f && dragging_ < 0)
        setSelectedBand(-1);

    updateAnalyzer();
    repaint();
}

// ---------------------------------------------------------------------------
// Analizador

void CurveView::updateAnalyzer()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastAnalyzerUpdateMs_ > 0.0 ? (now - lastAnalyzerUpdateMs_) / 1000.0 : 0.0;
    lastAnalyzerUpdateMs_ = now;

    const double sampleRate = processor_.currentSampleRate();
    const bool active = analyzerMode_ != AnalyzerMode::Off;

    auto drain = [this, active](dsp::SpscFifo& fifo, dsp::SpectrumAnalyzer& analyzer) {
        for (int n; (n = fifo.read(readBuffer_.data(), static_cast<int>(readBuffer_.size()))) > 0;)
            if (active)
                analyzer.push(readBuffer_.data(), n);
    };
    // Siempre se vacían las colas, aunque el analizador esté apagado, para no
    // mostrar audio viejo al encenderlo.
    drain(processor_.preAnalyzerFifo(), *preAnalyzer_);
    drain(processor_.postAnalyzerFifo(), *postAnalyzer_);

    if (!active)
        return;

    // Suavizado exponencial en dB: cada fotograma es una sola FFT y, sin
    // promediar, el espectro del ruido o la música sale muy dentado.
    const auto rise = static_cast<float>(1.0 - std::exp(-dt / kAnalyzerRiseSeconds));
    const auto fall = static_cast<float>(1.0 - std::exp(-dt / kAnalyzerFallSeconds));
    auto update = [&](dsp::SpectrumAnalyzer& analyzer, std::vector<float>& levels) {
        analyzer.setSampleRate(sampleRate);
        analyzer.compute(analyzerFrequencies_, scratch_);
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            float& level = levels[i];
            if (level <= dsp::SpectrumAnalyzer::kFloorDb + 1.0f)
                level = scratch_[i]; // primer dato: sin transición desde el silencio
            else
                level += (scratch_[i] - level) * (scratch_[i] > level ? rise : fall);
        }
    };

    update(*postAnalyzer_, postLevels_);
    if (analyzerMode_ == AnalyzerMode::PreAndPost)
        update(*preAnalyzer_, preLevels_);
}

// ---------------------------------------------------------------------------
// Pintado

void CurveView::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
    const auto m = mapping();

    drawAnalyzer(g, m);
    drawGrid(g, m);
    drawCurves(g, m);
    drawNodes(g, m);

    g.setColour(palette::gridStrong);
    g.drawRect(getLocalBounds(), 1);
}

void CurveView::drawGrid(juce::Graphics& g, const CurveMapping& m) const
{
    const auto area = m.area();
    g.setFont(juce::FontOptions(11.0f));

    for (const auto& line : frequencyGrid())
    {
        const float x = m.xForFrequency(line.value);
        g.setColour(line.labelled ? palette::gridStrong : palette::grid);
        g.drawVerticalLine(juce::roundToInt(x), area.y, area.y + area.height);
        if (line.labelled && line.value < 20000.0)
        {
            g.setColour(palette::textDim);
            g.drawText(frequencyLabel(line.value), juce::Rectangle<float>(x + 3.0f, area.y + area.height - 16.0f, 40.0f, 14.0f),
                       juce::Justification::centredLeft);
        }
    }

    for (const auto& line : gainGrid(m.rangeDb()))
    {
        const float y = m.yForGain(line.value);
        g.setColour(std::abs(line.value) < 1.0e-9 ? palette::gridStrong.brighter(0.2f) : palette::grid);
        g.drawHorizontalLine(juce::roundToInt(y), area.x, area.x + area.width);
        g.setColour(palette::textDim);
        const juce::String label = (line.value > 0.0 ? "+" : "") + juce::String(static_cast<int>(line.value));
        g.drawText(label, juce::Rectangle<float>(area.x + area.width - 34.0f, y - 14.0f, 30.0f, 13.0f),
                   juce::Justification::centredRight);
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

    g.setColour(palette::analyzerPost.withAlpha(0.18f));
    g.fillPath(makePath(postLevels_, true));
    g.setColour(palette::analyzerPost.withAlpha(0.55f));
    g.strokePath(makePath(postLevels_, false), juce::PathStrokeType(1.0f));

    if (analyzerMode_ == AnalyzerMode::PreAndPost)
    {
        g.setColour(palette::analyzerPre.withAlpha(0.6f));
        g.strokePath(makePath(preLevels_, false), juce::PathStrokeType(1.0f));
    }
}

void CurveView::drawCurves(juce::Graphics& g, const CurveMapping& m) const
{
    const auto area = m.area();
    const double sampleRate = processor_.currentSampleRate();

    // Diseño que suena (con la ganancia dinámica) y, en las bandas dinámicas,
    // el estático como referencia. Ambos salen de dsp::designBand, como el audio.
    std::array<dsp::BandDesign, core::kNumBands> designs{};
    std::array<dsp::BandDesign, core::kNumBands> staticDesigns{};
    std::array<bool, core::kNumBands> enabled{};
    std::array<bool, core::kNumBands> dynamic{};
    bool any = false;
    for (int band = 0; band < core::kNumBands; ++band)
    {
        const auto index = static_cast<std::size_t>(band);
        const auto settings = core::bandSettings(values_, band);
        enabled[index] = settings.enabled;
        if (!settings.enabled)
            continue;
        any = true;
        dynamic[index] = settings.dynamics.enabled && core::usesDynamics(settings.filter.type);
        designs[index] = dsp::designBand(soundingSettings(band).filter, sampleRate);
        if (dynamic[index])
            staticDesigns[index] = dsp::designBand(settings.filter, sampleRate);
    }

    const int points = std::max(2, static_cast<int>(area.width / 2.0f));
    std::vector<float> xs(static_cast<std::size_t>(points));
    std::vector<double> total(static_cast<std::size_t>(points), 0.0);
    std::vector<double> freqs(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i)
    {
        const float x = area.x + area.width * static_cast<float>(i) / static_cast<float>(points - 1);
        xs[static_cast<std::size_t>(i)] = x;
        freqs[static_cast<std::size_t>(i)] = std::min(m.frequencyForX(x), 0.499 * sampleRate);
    }

    const float top = area.y;
    const float bottom = area.y + area.height;
    auto clampY = [&](double db) { return std::clamp(m.yForGain(db), top - 2.0f, bottom + 2.0f); };
    const float zeroY = m.yForGain(0.0);

    for (int band = 0; band < core::kNumBands; ++band)
    {
        const auto index = static_cast<std::size_t>(band);
        if (!enabled[index])
            continue;

        juce::Path line;
        juce::Path fill;
        juce::Path reference;
        fill.startNewSubPath(xs.front(), zeroY);
        for (int i = 0; i < points; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            const double db = dsp::magnitudeDb(designs[index], freqs[idx], sampleRate);
            total[idx] += db;
            const float y = clampY(db);
            i == 0 ? line.startNewSubPath(xs[idx], y) : line.lineTo(xs[idx], y);
            fill.lineTo(xs[idx], y);

            if (dynamic[index])
            {
                const float ys = clampY(dsp::magnitudeDb(staticDesigns[index], freqs[idx], sampleRate));
                i == 0 ? reference.startNewSubPath(xs[idx], ys) : reference.lineTo(xs[idx], ys);
            }
        }
        fill.lineTo(xs.back(), zeroY);
        fill.closeSubPath();

        const auto colour = palette::band(band);
        const bool isSelected = band == selected_;
        g.setColour(colour.withAlpha(isSelected ? 0.22f : 0.07f));
        g.fillPath(fill);
        g.setColour(colour.withAlpha(isSelected ? 0.9f : 0.45f));
        g.strokePath(line, juce::PathStrokeType(isSelected ? 1.5f : 1.0f));

        if (dynamic[index])
        {
            juce::Path dashed;
            const float dashes[] = {4.0f, 4.0f};
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, reference, dashes, 2);
            g.setColour(colour.withAlpha(isSelected ? 0.7f : 0.35f));
            g.fillPath(dashed);
        }
    }

    juce::Path curve;
    for (int i = 0; i < points; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        const float y = any ? clampY(total[idx]) : zeroY;
        i == 0 ? curve.startNewSubPath(xs[idx], y) : curve.lineTo(xs[idx], y);
    }
    g.setColour(palette::curve);
    g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CurveView::drawNodes(juce::Graphics& g, const CurveMapping& m) const
{
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));

    for (int band = 0; band < core::kNumBands; ++band)
    {
        const auto settings = core::bandSettings(values_, band);
        if (!settings.enabled)
            continue;

        const auto p = nodePosition(m, settings);
        const auto colour = palette::band(band);
        const bool isSelected = band == selected_;
        const float r = kNodeRadius + (isSelected || band == hovered_ ? 2.0f : 0.0f);
        const juce::Rectangle<float> circle(p.x - r, p.y - r, 2.0f * r, 2.0f * r);

        // Dinámica: barra hasta el límite del rango y marca de la ganancia actual.
        if (settings.dynamics.enabled && core::usesDynamics(settings.filter.type))
        {
            const double staticGain = settings.filter.gainDb;
            const float limitY = m.yForGain(staticGain + settings.dynamics.rangeDb);
            const float nowY = m.yForGain(staticGain + dynamicGains_[static_cast<std::size_t>(band)]);
            const float barX = p.x + r + 5.0f;
            g.setColour(colour.withAlpha(0.35f));
            g.fillRect(juce::Rectangle<float>(barX - 1.0f, std::min(p.y, limitY), 2.0f, std::abs(limitY - p.y)));
            g.drawHorizontalLine(juce::roundToInt(limitY), barX - 3.0f, barX + 3.0f);
            g.setColour(colour);
            g.fillRect(juce::Rectangle<float>(barX - 2.0f, std::min(p.y, nowY), 4.0f, std::max(1.5f, std::abs(nowY - p.y))));
        }

        g.setColour(colour.withAlpha(isSelected ? 1.0f : 0.85f));
        g.fillEllipse(circle);
        if (isSelected)
        {
            g.setColour(palette::curve);
            g.drawEllipse(circle.expanded(2.0f), 1.5f);
        }
        g.setColour(palette::background);
        g.drawText(juce::String(band + 1), circle, juce::Justification::centred);
    }
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
        nodes[static_cast<std::size_t>(band)] = nodePosition(m, settings);
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

void CurveView::beginGesture(int band, core::BandParam param)
{
    processor_.parameter(core::bandParameter(band, param)).beginChangeGesture();
}

void CurveView::endGesture(int band, core::BandParam param)
{
    processor_.parameter(core::bandParameter(band, param)).endChangeGesture();
}

void CurveView::mouseMove(const juce::MouseEvent& e)
{
    const int hovered = bandAt(e.position).value_or(-1);
    if (hovered != hovered_)
    {
        hovered_ = hovered;
        setMouseCursor(hovered_ >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
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
        setSelectedBand(-1);
        return;
    }

    setSelectedBand(*band);
    dragging_ = *band;
    dragLast_ = e.position;
    draggingGain_ = core::usesGain(core::bandSettings(values_, *band).filter.type);
    beginGesture(dragging_, core::BandParam::Frequency);
    if (draggingGain_)
        beginGesture(dragging_, core::BandParam::Gain);
}

void CurveView::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ < 0)
        return;

    const auto m = mapping();
    const auto settings = core::bandSettings(values_, dragging_);

    // Con Mayús el ratón se mueve 10 veces más despacio sobre la curva.
    const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
    const juce::Point<float> delta = (e.position - dragLast_) * scale;
    dragLast_ = e.position;

    const float x = m.xForFrequency(settings.filter.frequencyHz) + delta.x;
    const float y = m.yForGain(settings.filter.gainDb) + delta.y;

    setParameter(dragging_, core::BandParam::Frequency, static_cast<float>(m.frequencyForX(x)));
    if (draggingGain_)
        setParameter(dragging_, core::BandParam::Gain, static_cast<float>(m.gainForY(y)));
    repaint();
}

void CurveView::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ < 0)
        return;
    endGesture(dragging_, core::BandParam::Frequency);
    if (draggingGain_)
        endGesture(dragging_, core::BandParam::Gain);
    dragging_ = -1;
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
    const auto q = static_cast<float>(settings.filter.q * std::pow(2.0, static_cast<double>(delta) * 1.5));

    beginGesture(band, core::BandParam::Q);
    setParameter(band, core::BandParam::Q, q);
    endGesture(band, core::BandParam::Q);
    repaint();
}

void CurveView::addBandAt(juce::Point<float> position)
{
    const auto band = core::firstFreeBand(values_);
    if (!band)
        return;

    const auto m = mapping();
    const std::array<std::pair<core::BandParam, float>, 6> settings{{
        {core::BandParam::Type, 0.0f},
        {core::BandParam::Frequency, static_cast<float>(m.frequencyForX(position.x))},
        {core::BandParam::Gain, static_cast<float>(m.gainForY(position.y))},
        {core::BandParam::Q, kDefaultQ},
        {core::BandParam::Slope, 1.0f},
        {core::BandParam::Enabled, 1.0f}, // el último: la banda suena ya con sus valores
    }};

    for (const auto& [param, value] : settings)
    {
        beginGesture(*band, param);
        setParameter(*band, param, value);
        endGesture(*band, param);
    }

    setSelectedBand(*band);
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
    menu.addSectionHeader("Band " + juce::String(band + 1));
    for (std::size_t i = 0; i < core::kFilterTypeChoices.size(); ++i)
    {
        const auto& name = core::kFilterTypeChoices[i];
        menu.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(name.data(), static_cast<int>(name.size())), true,
                     static_cast<int>(i) == current);
    }
    menu.addSeparator();
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
                           if (result == kDynamicId)
                           {
                               const auto index = core::dynamicParameter(band, core::DynParam::Enabled);
                               auto& p = safe->processor_.parameter(index);
                               const float next = safe->values_[index] >= 0.5f ? 0.0f : 1.0f;
                               p.beginChangeGesture();
                               p.setValueNotifyingHost(p.convertTo0to1(next));
                               p.endChangeGesture();
                               safe->values_[index] = next;
                               safe->repaint();
                               return;
                           }
                           safe->beginGesture(band, core::BandParam::Type);
                           safe->setParameter(band, core::BandParam::Type, static_cast<float>(result - 1));
                           safe->endGesture(band, core::BandParam::Type);
                           safe->repaint();
                       });
}

} // namespace esceq::ui
