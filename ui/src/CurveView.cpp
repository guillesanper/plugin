// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CurveView.h"

#include "Draw.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/ui/control_logic.h"
#include "esceq/ui/meter_logic.h"

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
constexpr double kAnalyzerRiseSeconds = 0.06;  // constante de tiempo al subir
constexpr double kAnalyzerFallSeconds = 0.35;  // y al bajar: más lenta, para que se pueda leer
constexpr double kAnalyzerTiltDbPerOctave = 4.5;
constexpr float kAnalyzerRepaintDb = 0.05f; // por debajo de esto el espectro no se mueve en pantalla

using namespace theme;

juce::String frequencyLabel(double hz)
{
    return hz >= 1000.0 ? juce::String(hz / 1000.0, 0) + "k" : juce::String(static_cast<int>(hz));
}

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

bool isDynamic(const dsp::BandSettings& settings)
{
    return settings.dynamics.enabled && core::usesDynamics(settings.filter.type);
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
                "Ctrl or Cmd with left and right change its frequency, Delete removes it.");
}

CurveView::~CurveView()
{
    // Si la ventana se cierra en mitad de un arrastre, el gesto se cierra aquí.
    if (dragging_ >= 0)
    {
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
    const auto values = processor_.currentValues();
    std::array<float, core::kNumBands> gains{};
    for (int band = 0; band < core::kNumBands; ++band)
        gains[static_cast<std::size_t>(band)] = processor_.dynamicGainDb(band);

    // Solo se repinta si algo ha cambiado: dibujar la curva con muchas bandas
    // en una ventana grande cuesta varios milisegundos.
    const double sampleRate = processor_.currentSampleRate(); // cambia la curva cerca de Nyquist
    bool changed = values != values_ || gains != dynamicGains_ || !juce::exactlyEqual(sampleRate, lastSampleRate_);
    values_ = values;
    dynamicGains_ = gains;
    lastSampleRate_ = sampleRate;

    // Si la banda seleccionada se ha desactivado (desde el host o el panel), se deselecciona.
    if (selected_ >= 0 && values_[core::bandParameter(selected_, core::BandParam::Enabled)] < 0.5f && dragging_ < 0)
        setSelectedBand(-1);

    changed = updateAnalyzer() || changed;
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
        return false;

    // Suavizado exponencial en dB: cada fotograma es una sola FFT y, sin
    // promediar, el espectro del ruido o la música sale muy dentado.
    const auto rise = static_cast<float>(1.0 - std::exp(-dt / kAnalyzerRiseSeconds));
    const auto fall = static_cast<float>(1.0 - std::exp(-dt / kAnalyzerFallSeconds));
    // Devuelve true si algún punto se ha movido lo bastante para verse.
    auto update = [&](dsp::SpectrumAnalyzer& analyzer, std::vector<float>& levels) {
        analyzer.setSampleRate(sampleRate);
        analyzer.compute(analyzerFrequencies_, scratch_);
        bool moved = false;
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            float& level = levels[i];
            const float before = level;
            if (level <= dsp::SpectrumAnalyzer::kFloorDb + 1.0f)
                level = scratch_[i]; // primer dato: sin transición desde el silencio
            else
                level += (scratch_[i] - level) * (scratch_[i] > level ? rise : fall);
            moved = moved || std::abs(level - before) > kAnalyzerRepaintDb;
        }
        return moved;
    };

    bool moved = update(*postAnalyzer_, postLevels_);
    if (analyzerMode_ == AnalyzerMode::PreAndPost)
        moved = update(*preAnalyzer_, preLevels_) || moved;
    return moved;
}

// ---------------------------------------------------------------------------
// Pintado

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
        drawGrid(g, m);
        drawCurves(g, m);
        drawNodes(g, m);
        drawReadout(g, m);
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
        g.drawText(label, juce::Rectangle<float>(area.x + area.width - 40.0f, labelY, 32.0f, 12.0f),
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

    const auto fill = makePath(postLevels_, true);
    g.setGradientFill(juce::ColourGradient(colour::analyzerPost.withAlpha(0.30f), 0.0f, area.y + 0.35f * area.height,
                                           colour::analyzerPost.withAlpha(0.06f), 0.0f, bottom, false));
    g.fillPath(fill);
    g.setColour(colour::analyzerPost.withAlpha(0.75f));
    g.strokePath(makePath(postLevels_, false), juce::PathStrokeType(stroke::hairline));

    if (analyzerMode_ == AnalyzerMode::PreAndPost)
    {
        g.setColour(colour::analyzerPre.withAlpha(0.6f));
        g.strokePath(makePath(preLevels_, false), juce::PathStrokeType(stroke::hairline));
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

        const auto colour = theme::band(band);
        const bool isSelected = band == selected_;
        const bool isHot = isSelected || band == hovered_ || band == dragging_;
        g.setColour(colour.withAlpha(isSelected ? 0.20f : isHot ? 0.14f : 0.06f));
        g.fillPath(fill);
        g.setColour(colour.withAlpha(isHot ? 0.95f : 0.45f));
        g.strokePath(line, juce::PathStrokeType(isHot ? stroke::thin : stroke::hairline, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

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
    // Con el bypass puesto la curva no suena: se atenúa.
    g.setColour(colour::curve.withAlpha(values_[core::kBypass] >= 0.5f ? 0.35f : 1.0f));
    g.strokePath(curve, juce::PathStrokeType(stroke::medium, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
    // Etiqueta flotante de la banda que se está tocando o señalando.
    const int band = dragging_ >= 0 ? dragging_ : hovered_;
    if (band < 0)
        return;
    const auto settings = core::bandSettings(values_, band);
    if (!settings.enabled)
        return;

    const auto readout = nodeReadout(values_, band);
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

    const auto title = toJuce(readout.title).toUpperCase();
    const float labelWidth = std::ceil(std::max(valuesWidth, width(titleFont, title)) + 2.0f * kPadding + 4.0f);
    constexpr float kLabelHeight = 44.0f;

    const auto node = nodePosition(m, settings, kNodeEdgeMargin);
    const auto r = floatingLabelRect(m.area(), node, labelWidth, kLabelHeight, kNodeRadius + 12.0f);
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

    setParameter(dragging_, core::BandParam::Frequency, static_cast<float>(m.frequencyForX(x)));
    if (draggingGain_)
        setParameter(dragging_, core::BandParam::Gain,
                     static_cast<float>(draggedGain(m, settings.filter.gainDb, delta.y)));
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
    if (hovered_ >= 0)
    {
        hovered_ = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

bool CurveView::keyPressed(const juce::KeyPress& key)
{
    const bool left = key.isKeyCode(juce::KeyPress::leftKey);
    const bool right = key.isKeyCode(juce::KeyPress::rightKey);
    const bool up = key.isKeyCode(juce::KeyPress::upKey);
    const bool down = key.isKeyCode(juce::KeyPress::downKey);
    const auto mods = key.getModifiers();

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
        nudge(core::BandParam::Frequency, right ? 1 : -1);
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

void CurveView::addBandAt(juce::Point<float> position)
{
    const auto band = core::firstFreeBand(values_);
    if (!band)
        return;

    // Una campana en el punto del doble clic, con el resto de ajustes por
    // defecto: la banda no hereda la dinámica ni el canal de cuando estuvo activa.
    const auto m = mapping();
    for (const auto& change : newBandChanges(values_, *band, m.frequencyForX(position.x), m.gainForY(position.y)))
    {
        auto& p = processor_.parameter(change.index);
        p.beginChangeGesture();
        p.setValueNotifyingHost(p.convertTo0to1(change.value));
        p.endChangeGesture();
        values_[change.index] = change.value;
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
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addSectionHeader("Band " + juce::String(band + 1));
    for (std::size_t i = 0; i < core::kFilterTypeChoices.size(); ++i)
    {
        const auto& name = core::kFilterTypeChoices[i];
        menu.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(name.data(), static_cast<int>(name.size())), true,
                     static_cast<int>(i) == current);
    }
    juce::PopupMenu channels;
    channels.setLookAndFeel(&getLookAndFeel());
    constexpr int kFirstChannelId = 200;
    const auto channelIndex = core::channelParameter(band);
    for (std::size_t i = 0; i < core::kChannelChoices.size(); ++i)
    {
        const auto& name = core::kChannelChoices[i];
        channels.addItem(kFirstChannelId + static_cast<int>(i),
                         juce::String::fromUTF8(name.data(), static_cast<int>(name.size())), true,
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
                           {
                               const auto index = core::channelParameter(band);
                               const auto next = static_cast<float>(result - kFirstChannelId);
                               auto& p = safe->processor_.parameter(index);
                               p.beginChangeGesture();
                               p.setValueNotifyingHost(p.convertTo0to1(next));
                               p.endChangeGesture();
                               safe->values_[index] = next;
                               safe->repaint();
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
