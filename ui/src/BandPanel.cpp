// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BandPanel.h"

#include "JuceConvert.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/core/band_model.h"
#include "esceq/core/parameters.h"
#include "esceq/ui/control_logic.h"
#include "esceq/ui/meter_logic.h"
#include "esceq/ui/notes.h"

#include <cmath>
#include <initializer_list>
#include <iterator>

namespace esceq::ui {

namespace {

using namespace theme;

juce::Rectangle<int> toInt(const Rect& r)
{
    return toJuce(r).toNearestInt();
}

std::vector<Segmented::Option> channelOptions()
{
    // El orden es el de core::kChannelChoices.
    return {{"ST", "Stereo"}, {"L", "Left"}, {"R", "Right"}, {"M", "Mid"}, {"S", "Side"}};
}

} // namespace

BandPanel::BandPanel(PluginProcessor& processor)
    : processor_(processor),
      channel_(channelOptions(), "Channel")
{
    static_assert(core::kChannelChoices.size() == 5);

    for (juce::Component* c : std::initializer_list<juce::Component*>{&enabled_, &type_, &slope_, &channel_, &frequency_,
                                                                      &gain_, &q_, &dynamic_, &source_, &autoTiming_,
                                                                      &threshold_, &range_, &attack_, &release_, &knee_})
        addChildComponent(*c);

    // Orden de tabulación: de izquierda a derecha, tarjeta a tarjeta.
    int order = 1;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&enabled_, &type_, &slope_, &channel_, &frequency_,
                                                                      &gain_, &q_, &dynamic_, &source_, &autoTiming_,
                                                                      &threshold_, &range_, &attack_, &release_, &knee_})
        c->setExplicitFocusOrder(order++);

    setTitle("Band");
    setFocusContainerType(FocusContainerType::focusContainer);
    setBand(-1);
}

BandPanel::~BandPanel() = default;

void BandPanel::setBand(int band)
{
    band_ = band;

    // Primero se sueltan los enlaces antiguos: dos enlaces sobre el mismo
    // control lo dejarían escribiendo en dos parámetros.
    enabled_.detach();
    type_.detach();
    slope_.detach();
    channel_.detach();
    dynamic_.detach();
    source_.detach();
    autoTiming_.detach();
    for (ParamControl* c : {&frequency_, &gain_, &q_, &threshold_, &range_, &attack_, &release_, &knee_})
        c->detach();

    const bool hasBand = band >= 0;
    for (auto* c : getChildren())
        c->setVisible(hasBand);

    if (hasBand)
    {
        const auto& specs = core::parameters();
        using core::BandParam;
        using core::DynParam;

        auto bandIndex = [band](BandParam p) { return core::bandParameter(band, p); };
        auto dynIndex = [band](DynParam p) { return core::dynamicParameter(band, p); };
        auto knob = [&](ParamControl& control, std::size_t index) {
            control.attach(processor_.parameter(index), specs[index]);
        };

        enabled_.attach(processor_.parameter(bandIndex(BandParam::Enabled)));
        type_.attach(processor_.parameter(bandIndex(BandParam::Type)), specs[bandIndex(BandParam::Type)]);
        slope_.attach(processor_.parameter(bandIndex(BandParam::Slope)), specs[bandIndex(BandParam::Slope)]);
        channel_.attach(processor_.parameter(core::channelParameter(band)));
        knob(frequency_, bandIndex(BandParam::Frequency));
        knob(gain_, bandIndex(BandParam::Gain));
        knob(q_, bandIndex(BandParam::Q));

        dynamic_.attach(processor_.parameter(dynIndex(DynParam::Enabled)));
        source_.attach(processor_.parameter(dynIndex(DynParam::Source)), specs[dynIndex(DynParam::Source)]);
        autoTiming_.attach(processor_.parameter(core::autoTimingParameter(band)));
        knob(threshold_, dynIndex(DynParam::Threshold));
        knob(range_, dynIndex(DynParam::Range));
        knob(attack_, dynIndex(DynParam::Attack));
        knob(release_, dynIndex(DynParam::Release));
        knob(knee_, dynIndex(DynParam::Knee));

        // La banda tiñe sus controles: se ve de un vistazo a qué nodo pertenecen.
        const auto colour = theme::band(band);
        enabled_.setAccent(colour);
        dynamic_.setAccent(colour);
        autoTiming_.setAccent(colour);
        channel_.setAccent(colour);
        for (ParamControl* c : {&frequency_, &gain_, &q_, &threshold_, &range_, &attack_, &release_, &knee_})
            c->setAccent(colour);

        setTitle("Band " + juce::String(band + 1));
    }

    refresh();
    repaint();
}

void BandPanel::setNoteScale(bool visible, double a4Hz)
{
    notesVisible_ = visible;
    a4Hz_ = a4Hz;
    frequency_.setA4(a4Hz);
    refresh();
}

void BandPanel::refresh()
{
    if (band_ < 0)
        return;

    const auto values = processor_.currentValues();
    const auto settings = core::bandSettings(values, band_);
    const auto type = settings.filter.type;

    gain_.setEnabled(core::usesGain(type));
    slope_.setEnabled(core::usesSlope(type));
    q_.setEnabled(core::usesQ(type, settings.filter.slopeDbPerOct));

    // La dinámica solo actúa en los tipos con ganancia. Mientras está apagada
    // sus controles se atenúan, pero siguen siendo editables.
    const bool supported = core::usesDynamics(type);
    const bool active = supported && settings.dynamics.enabled;
    dynamic_.setEnabled(supported);
    source_.setEnabled(supported);
    autoTiming_.setEnabled(supported);
    // Con los tiempos automáticos, ataque y relajación no mandan: se atenúan y
    // dicen "Auto", pero conservan su valor (y se pueden editar) para cuando se apaguen.
    const bool autoTiming = settings.dynamics.autoTiming;
    const auto timeText = toJuce(dynamicTimeValueText(autoTiming));
    for (ParamControl* c : {&threshold_, &range_, &attack_, &release_, &knee_})
    {
        const bool isTime = c == &attack_ || c == &release_;
        c->setEnabled(supported);
        c->setDimmed(supported && (!active || (isTime && autoTiming)));
        if (isTime)
            c->setValueOverride(timeText);
    }
    autoTiming_.setTooltip(toJuce(autoTimingTooltip(settings.filter.frequencyHz)));
    // Con la escala de notas, la etiqueta de la frecuencia dice su nota: en la
    // línea del valor no cabe junto a él ("1.20 kHz · D6 +37 ct", ADR 0017).
    frequency_.setLabel(notesVisible_ ? toJuce(noteText(settings.filter.frequencyHz, a4Hz_)) : juce::String("Freq"));

    const auto typeName = toJuce(core::parameters()[core::bandParameter(band_, core::BandParam::Type)]
                                     .choices[static_cast<std::size_t>(type)]);
    // Aviso útil en FL Studio: detección externa elegida, pero sin nada conectado.
    const bool missing = active && settings.dynamics.external && !processor_.sidechainConnected();
    const float gainDb = active ? processor_.dynamicGainDb(band_) : 0.0f;
    const auto rangeDb = static_cast<float>(settings.dynamics.rangeDb);

    if (typeName != typeName_ || supported != dynamicsSupported_ || active != dynamicsActive_ || missing != sidechainMissing_ ||
        std::abs(gainDb - dynamicGainDb_) > 0.01f || std::abs(rangeDb - rangeDb_) > 0.0f)
    {
        typeName_ = typeName;
        dynamicsSupported_ = supported;
        dynamicsActive_ = active;
        sidechainMissing_ = missing;
        dynamicGainDb_ = gainDb;
        rangeDb_ = rangeDb;
        repaint();
    }
}

void BandPanel::resized()
{
    const auto bounds = getLocalBounds().toFloat();
    layout_ = panelLayout({bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()});

    enabled_.setBounds(toInt(layout_.enabled));
    type_.setBounds(toInt(layout_.type));
    slope_.setBounds(toInt(layout_.slope));
    channel_.setBounds(toInt(layout_.channel));

    frequency_.setBounds(toInt(layout_.eqKnobs[0]));
    gain_.setBounds(toInt(layout_.eqKnobs[1]));
    q_.setBounds(toInt(layout_.eqKnobs[2]));

    dynamic_.setBounds(toInt(layout_.dynamicsEnabled));
    source_.setBounds(toInt(layout_.source));
    autoTiming_.setBounds(toInt(layout_.autoTiming));
    threshold_.setBounds(toInt(layout_.dynamicKnobs[0]));
    range_.setBounds(toInt(layout_.dynamicKnobs[1]));
    attack_.setBounds(toInt(layout_.dynamicKnobs[2]));
    release_.setBounds(toInt(layout_.dynamicKnobs[3]));
    knee_.setBounds(toInt(layout_.dynamicKnobs[4]));
}

void BandPanel::paint(juce::Graphics& g)
{
    g.fillAll(colour::window);

    if (band_ < 0)
    {
        paintEmpty(g);
        return;
    }

    draw::card(g, toJuce(layout_.bandCard));
    draw::card(g, toJuce(layout_.eqCard));
    draw::card(g, toJuce(layout_.dynamicsCard));

    // Cabecera: una marca con el color de la banda, su número y su tipo.
    const auto colour = theme::band(band_);
    auto title = toJuce(layout_.bandTitle);
    const auto chip = title.removeFromLeft(18.0f).withSizeKeepingCentre(18.0f, 18.0f);
    g.setColour(colour);
    g.fillEllipse(chip);
    g.setColour(colour::window);
    g.setFont(font(type::small, Weight::SemiBold, true));
    g.drawText(juce::String(band_ + 1), chip, juce::Justification::centred, false);

    title.removeFromLeft(space::x2);
    g.setColour(colour::text);
    g.setFont(font(type::title, Weight::SemiBold));
    g.drawText("Band " + juce::String(band_ + 1), title, juce::Justification::centredLeft, true);

    draw::caption(g, "EQ", toJuce(layout_.eqTitle), juce::Justification::centredLeft);
    draw::caption(g, "Dynamics", toJuce(layout_.dynamicsTitle), juce::Justification::centredLeft,
                  dynamicsSupported_ ? colour::textDim : colour::textDim.withAlpha(0.5f));

    // Mensaje bajo la fuente del detector.
    juce::String note;
    juce::Colour noteColour = colour::textDim;
    if (!dynamicsSupported_)
        note = "Not for " + typeName_;
    else if (sidechainMissing_)
    {
        note = "No sidechain";
        noteColour = colour::warning;
    }
    if (note.isNotEmpty())
    {
        g.setColour(noteColour);
        g.setFont(font(type::small));
        g.drawText(note, toJuce(layout_.sourceWarning), juce::Justification::centredLeft, true);
    }

    paintMeter(g);
}

void BandPanel::paintMeter(juce::Graphics& g) const
{
    // Medidor de la dinámica: la reducción crece hacia abajo y el realce
    // hacia arriba, como se mueve la curva.
    auto area = toJuce(layout_.dynamicsMeter);
    const auto labelArea = area.removeFromBottom(kKnobValueHeight);
    const auto well = area.withSizeKeepingCentre(6.0f, area.getHeight());
    const float alpha = dynamicsSupported_ ? (dynamicsActive_ ? 1.0f : 0.5f) : 0.3f;

    g.setColour(colour::track.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(well, radius::small);

    if (dynamicsActive_)
    {
        const float fraction = dynamicMeterFraction(dynamicGainDb_, rangeDb_);
        const float length = fraction * well.getHeight();
        if (length > 0.5f)
        {
            const auto fill = rangeDb_ < 0.0f ? well.withHeight(length) : well.withTop(well.getBottom() - length);
            g.setColour(theme::band(band_));
            g.fillRoundedRectangle(fill, radius::small);
        }
    }

    g.setColour(colour::textMuted.withMultipliedAlpha(alpha));
    g.setFont(font(type::caption, Weight::Medium, true));
    const float shown = std::round(dynamicGainDb_ * 10.0f) / 10.0f;
    const juce::String text = !dynamicsActive_ || std::abs(shown) < 0.05f ? juce::String("0") : juce::String(shown, 1);
    g.drawText(text, labelArea.expanded(8.0f, 0.0f), juce::Justification::centred, false);
}

void BandPanel::paintEmpty(juce::Graphics& g) const
{
    // Sin banda seleccionada: una sola tarjeta con las instrucciones de uso.
    const auto bounds = getLocalBounds().toFloat();
    const auto card = bounds.withTrimmedTop(space::x2).reduced(space::x3, 0.0f).withTrimmedBottom(space::x3);
    draw::card(g, card);

    struct Hint
    {
        const char* gesture;
        const char* effect;
    };
    static constexpr Hint kHints[] = {
        {"Double-click", "Add a band"},
        {"Drag", "Frequency and gain"},
        {"Shift + drag", "Fine adjust"},
        {"Wheel", "Q"},
        {"Right-click", "Type, channel, dynamics"},
        {"Double-click a node", "Remove it"},
        {"Drag a marked peak", "Band on the peak"},
        {"Alt on release", "Make that band dynamic"},
    };

    constexpr int kColumns = 4;
    constexpr float kRowHeight = 40.0f;
    const auto grid = card.reduced(space::x6, 0.0f).withSizeKeepingCentre(card.getWidth() - 2.0f * space::x6, 2.0f * kRowHeight);
    const float columnWidth = grid.getWidth() / static_cast<float>(kColumns);

    for (int i = 0; i < static_cast<int>(std::size(kHints)); ++i)
    {
        const auto cell = juce::Rectangle<float>(grid.getX() + columnWidth * static_cast<float>(i % kColumns),
                                                 grid.getY() + kRowHeight * static_cast<float>(i / kColumns), columnWidth,
                                                 kRowHeight)
                              .reduced(space::x2, space::x1);
        draw::caption(g, kHints[i].gesture, cell.withHeight(14.0f), juce::Justification::centredLeft, colour::accent);
        g.setColour(colour::textMuted);
        g.setFont(font(type::body));
        g.drawText(kHints[i].effect, cell.withTrimmedTop(15.0f), juce::Justification::centredLeft, true);
    }
}

} // namespace esceq::ui
