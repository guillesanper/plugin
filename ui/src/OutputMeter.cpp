// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OutputMeter.h"

#include "Draw.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace esceq::ui {

using namespace theme;

OutputMeter::OutputMeter(PluginProcessor& processor)
    : processor_(processor)
{
    setTitle("Output level");
    setHelpText("Peak level after the equalizer. Click to clear the clip indicator.");
    setOpaque(true);
}

void OutputMeter::refresh()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastUpdateMs_ > 0.0 ? (now - lastUpdateMs_) / 1000.0 : 0.0;
    lastUpdateMs_ = now;

    const int channels = std::clamp(processor_.outputChannelCount(), 1, 2);
    bool changed = channels != channels_;
    channels_ = channels;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& meter = meters_[static_cast<std::size_t>(ch)];
        const double level = meter.levelDb();
        const double hold = meter.holdDb();
        const bool clipped = meter.clipped();
        meter.update(processor_.takeOutputPeak(ch), dt);
        changed = changed || !juce::exactlyEqual(meter.levelDb(), level) || !juce::exactlyEqual(meter.holdDb(), hold) ||
                  meter.clipped() != clipped;
    }
    // En silencio el medidor no se mueve: no hace falta repintarlo.
    if (changed)
        repaint();
}

void OutputMeter::mouseDown(const juce::MouseEvent&)
{
    for (auto& meter : meters_)
        meter.resetClip();
    repaint();
}

void OutputMeter::paint(juce::Graphics& g)
{
    g.fillAll(colour::window);
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::plot);
    g.fillRoundedRectangle(bounds, radius::card);
    g.setColour(colour::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius::card, stroke::hairline);

    auto area = bounds.reduced(space::x2, space::x2);

    // Arriba, el pico máximo: en rojo si ha habido recorte.
    const bool clipped = meters_[0].clipped() || meters_[1].clipped();
    const double hold = std::max(meters_[0].holdDb(), meters_[1].holdDb());
    const auto readout = area.removeFromTop(14.0f);
    g.setFont(font(type::caption, Weight::Medium, true));
    g.setColour(clipped ? colour::danger : colour::textMuted);
    const double shown = std::round(hold * 10.0) / 10.0;
    g.drawText(hold <= kMeterBottomDb ? juce::String("-inf") : (shown > 0.0 ? "+" : "") + juce::String(shown, 1),
               readout.expanded(space::x2, 0.0f),
               juce::Justification::centred, false);
    area.removeFromTop(space::x1);

    // A la izquierda la escala, a la derecha las barras.
    constexpr float kBarWidth = 6.0f;
    constexpr float kBarGap = 3.0f;
    const float barsWidth = static_cast<float>(channels_) * kBarWidth + static_cast<float>(channels_ - 1) * kBarGap;
    auto bars = area.removeFromRight(barsWidth);
    const auto scale = area.withTrimmedRight(space::x1);

    auto yFor = [&](double db) { return bars.getBottom() - meterFraction(db) * bars.getHeight(); };

    g.setFont(font(type::caption - 1.0f, Weight::Regular, true));
    for (const auto& tick : meterTicks())
    {
        const float y = bars.getBottom() - tick.fraction * bars.getHeight();
        g.setColour(tick.labelled ? colour::lineStrong : colour::line);
        g.fillRect(juce::Rectangle<float>(scale.getRight() - 1.0f, std::round(y), 3.0f, 1.0f));
        if (tick.labelled)
        {
            g.setColour(std::abs(tick.db) < 0.5 ? colour::textMuted : colour::textDim);
            g.drawText(juce::String(static_cast<int>(std::abs(tick.db))),
                       juce::Rectangle<float>(scale.getX() - space::x1, y - 6.0f, scale.getWidth() - 1.0f, 12.0f),
                       juce::Justification::centredRight, false);
        }
    }

    for (int ch = 0; ch < channels_; ++ch)
    {
        const auto& meter = meters_[static_cast<std::size_t>(ch)];
        const auto well = bars.removeFromLeft(kBarWidth);
        bars.removeFromLeft(kBarGap);

        g.setColour(colour::track.withAlpha(0.6f));
        g.fillRoundedRectangle(well, radius::small);

        // La barra cambia de color por tramos: normal, cerca del tope y por encima de 0 dBFS.
        const float top = yFor(meter.levelDb());
        if (well.getBottom() - top > 0.5f)
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle(well.withTop(top), radius::small);
            g.reduceClipRegion(clip);
            g.setColour(colour::accent);
            g.fillRect(well.withTop(yFor(-6.0)));
            g.setColour(colour::warning);
            g.fillRect(well.withTop(yFor(kMeterClipDb)).withBottom(yFor(-6.0)));
            g.setColour(colour::danger);
            g.fillRect(well.withBottom(yFor(kMeterClipDb)));
            g.restoreState();
        }

        // Pico retenido.
        if (meter.holdDb() > kMeterBottomDb)
        {
            g.setColour(meter.holdDb() > kMeterClipDb ? colour::danger : colour::text);
            g.fillRect(juce::Rectangle<float>(well.getX(), std::round(yFor(meter.holdDb())), well.getWidth(), 1.5f));
        }
    }
}

std::unique_ptr<juce::AccessibilityHandler> OutputMeter::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::image);
}

} // namespace esceq::ui
