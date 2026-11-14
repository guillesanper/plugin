// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"

#include "BinaryData.h"

#include <algorithm>
#include <cmath>

namespace esceq::ui::theme {

namespace {

juce::Typeface::Ptr typeface(Weight weight)
{
    // Se crean una vez y viven mientras viva el proceso: registrar una
    // tipografía es caro y varias instancias del plugin comparten las mismas.
    static const juce::Typeface::Ptr regular =
        juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static const juce::Typeface::Ptr medium =
        juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
    static const juce::Typeface::Ptr semiBold =
        juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);

    switch (weight)
    {
    case Weight::Medium:
        return medium;
    case Weight::SemiBold:
        return semiBold;
    case Weight::Regular:
        break;
    }
    return regular;
}

} // namespace

juce::Font font(float size, Weight weight, bool tabular)
{
    auto options = juce::FontOptions(typeface(weight)).withPointHeight(size);
    if (tabular)
        options = options.withFeatureEnabled("tnum");
    return juce::Font(options);
}

juce::Font captionFont()
{
    return juce::Font(juce::FontOptions(typeface(Weight::SemiBold)).withPointHeight(type::caption).withKerningFactor(0.08f));
}

juce::Typeface::Ptr defaultTypeface()
{
    return typeface(Weight::Regular);
}

juce::Colour band(int index)
{
    const float hue = std::fmod(0.58f + 0.618034f * static_cast<float>(index), 1.0f);
    return juce::Colour::fromHSV(hue, 0.52f, 0.96f, 1.0f);
}

juce::Colour instance(int number, std::optional<std::uint32_t> trackArgb)
{
    if (trackArgb)
    {
        // El color de la pista, opaco y con brillo suficiente para verse sobre
        // el fondo de la curva (muchos hosts usan tonos oscuros).
        const auto c = juce::Colour(*trackArgb).withAlpha(1.0f);
        return c.getBrightness() < 0.75f ? c.withBrightness(0.75f) : c;
    }
    // Sin color del host: tonos menos saturados que los de las bandas, para no
    // confundir un espectro ajeno con una curva propia.
    const float hue = std::fmod(0.08f + 0.618034f * static_cast<float>(std::max(number - 1, 0)), 1.0f);
    return juce::Colour::fromHSV(hue, 0.38f, 0.92f, 1.0f);
}

} // namespace esceq::ui::theme
