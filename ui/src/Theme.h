// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/// Sistema de diseño de esceQ: el único sitio donde se definen colores,
/// tipografía, espaciado, radios y grosores. Ningún componente escribe un
/// color o una medida "a mano": todo sale de aquí. Ver docs/ui.md.
namespace esceq::ui::theme {

/// Colores. Los nombres dicen para qué sirve cada uno, no qué tono tiene.
namespace colour {

inline const juce::Colour window{0xff0f1114};      ///< Fondo de la ventana.
inline const juce::Colour plot{0xff0a0c0e};        ///< Fondo de la curva y de los medidores.
inline const juce::Colour card{0xff171a1f};        ///< Tarjetas del panel.
inline const juce::Colour control{0xff20252c};     ///< Cuerpo de los controles en reposo.
inline const juce::Colour controlHover{0xff2a313a}; ///< Cuerpo de los controles con el ratón encima.
inline const juce::Colour controlDown{0xff333b46};  ///< Cuerpo de los controles pulsados.
inline const juce::Colour track{0xff2b323b};       ///< Recorrido vacío de arcos, barras e interruptores.
inline const juce::Colour line{0xff262b32};        ///< Bordes y rejilla secundaria.
inline const juce::Colour lineStrong{0xff373e48};  ///< Bordes marcados y rejilla principal.

inline const juce::Colour text{0xffe7eaee};        ///< Texto principal y valores.
inline const juce::Colour textMuted{0xff9aa3ae};   ///< Texto secundario.
inline const juce::Colour textDim{0xff67707c};     ///< Etiquetas y escalas.

inline const juce::Colour accent{0xff5fe3c0};      ///< Color propio de esceQ: controles globales y foco.
inline const juce::Colour curve{0xfff3f5f8};       ///< Curva total.
inline const juce::Colour analyzerPost{0xff3f8f96};
inline const juce::Colour analyzerPre{0xff7b8592};
inline const juce::Colour warning{0xfff0b057};
inline const juce::Colour danger{0xffff5d62};

} // namespace colour

/// Espaciado: rejilla de 4 px.
namespace space {

inline constexpr float x1 = 4.0f;
inline constexpr float x2 = 8.0f;
inline constexpr float x3 = 12.0f;
inline constexpr float x4 = 16.0f;
inline constexpr float x6 = 24.0f;

} // namespace space

/// Radios de esquina.
namespace radius {

inline constexpr float small = 3.0f;  ///< Marcas y medidores.
inline constexpr float control = 6.0f; ///< Desplegables, botones, campos de texto.
inline constexpr float card = 10.0f;  ///< Tarjetas y la curva.

} // namespace radius

/// Grosores de línea.
namespace stroke {

inline constexpr float hairline = 1.0f; ///< Bordes y rejilla.
inline constexpr float thin = 1.5f;     ///< Curvas de banda, anillo de foco.
inline constexpr float medium = 2.0f;   ///< Curva total, indicador del potenciómetro.
inline constexpr float arc = 3.0f;      ///< Arco de valor de potenciómetros y barras.

} // namespace stroke

/// Tamaños de letra, en píxeles de cuerpo (em).
namespace type {

inline constexpr float caption = 9.5f; ///< Etiquetas en mayúsculas y escalas.
inline constexpr float small = 11.0f;  ///< Texto de apoyo.
inline constexpr float body = 12.0f;   ///< Controles y valores.
inline constexpr float title = 13.0f;  ///< Títulos.
inline constexpr float logo = 17.0f;

} // namespace type

enum class Weight
{
    Regular,
    Medium,
    SemiBold,
};

/// Fuente Inter embebida (ui/assets/fonts, licencia OFL). Con `tabular`, las
/// cifras tienen todas el mismo ancho: los valores no "bailan" al cambiar.
/// Solo desde el hilo de mensajes.
juce::Font font(float size, Weight weight = Weight::Regular, bool tabular = false);

/// Fuente para etiquetas en mayúsculas, con algo de espacio entre letras.
juce::Font captionFont();

/// Tipografía por defecto de la interfaz (Inter Regular).
juce::Typeface::Ptr defaultTypeface();

/// Color de una banda: tonos repartidos por la proporción áurea para que
/// bandas consecutivas se distingan bien.
juce::Colour band(int index);

} // namespace esceq::ui::theme
