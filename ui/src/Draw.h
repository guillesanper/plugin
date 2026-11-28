// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Theme.h"

#include "esceq/ui/control_logic.h"

#include <juce_gui_basics/juce_gui_basics.h>

/// Dibujo de las piezas de la interfaz. Son funciones sueltas, sin estado:
/// reciben qué pintar y en qué estado, y no saben nada de parámetros ni de
/// ratón. Los componentes (Controls.h) deciden el estado y llaman aquí; la
/// herramienta de capturas las usa para la galería de estados de docs/ui.md.
namespace esceq::ui::draw {

/// Estado visual de un control.
struct ControlState
{
    bool hover = false;
    bool pressed = false;
    bool focused = false; ///< Foco de teclado: se dibuja un anillo.
    bool enabled = true;
    bool dimmed = false;  ///< Editable, pero ahora mismo no influye en el sonido.
};

/// Tarjeta: superficie redondeada con borde fino.
void card(juce::Graphics& g, juce::Rectangle<float> bounds);

/// Potenciómetro en el cuadrado `bounds`.
void knob(juce::Graphics& g, juce::Rectangle<float> bounds, const ValueArc& arc, bool bipolar, juce::Colour accent,
          const ControlState& state);

/// Anillo de modulación de un potenciómetro (las macros, PLAN.md 5.9): un
/// arco fino del color de la curva total, por dentro del arco de valor, de
/// la posición del valor (`from`) a la de lo que suena (`to`), con un punto
/// en lo que suena. Sin modulación (`from` == `to`), solo el punto: el
/// potenciómetro está enlazado a una macro. `bounds`, los del potenciómetro.
void modulationRing(juce::Graphics& g, juce::Rectangle<float> bounds, float from, float to, const ControlState& state);

/// Pestaña de la cabecera de una tarjeta ("OUTPUT", "MACROS"): el título y,
/// elegida, una raya del color de acento debajo.
void tab(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, bool selected,
         const ControlState& state);

/// Barra horizontal de valor (deslizador de salida).
void bar(juce::Graphics& g, juce::Rectangle<float> bounds, const ValueSpan& span, bool bipolar, juce::Colour accent,
         const ControlState& state);

/// Interruptor de dos posiciones.
void toggle(juce::Graphics& g, juce::Rectangle<float> bounds, bool on, juce::Colour accent, const ControlState& state);

/// Botón de texto que se queda encendido (Bypass) o segmento de un selector.
enum class Corners { All, Left, Middle, Right };
void pill(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& text, bool on, juce::Colour accent,
          const ControlState& state, Corners corners = Corners::All);

/// Iconos de los botones de la barra superior.
enum class Icon { Undo, Redo, Previous, Next, Compare, Options, Expand, Close };

/// Botón cuadrado con un icono. Encendido (`on`), el icono va en el color de
/// acento; `badge` (por ejemplo, un número) se dibuja en una pastilla en la
/// esquina superior derecha. Expand, encendido, dibuja las esquinas hacia
/// dentro (volver de la vista ampliada).
void iconButton(juce::Graphics& g, juce::Rectangle<float> bounds, Icon icon, const ControlState& state,
                Corners corners = Corners::All, bool on = false, const juce::String& badge = {});

/// Lo que abre y pliega el cajón de la etapa de salida (ADR 0027). Abierto,
/// la cabecera: el título y una flecha hacia la derecha (plegar). Plegado, la
/// pestaña: una tira con la flecha hacia la izquierda arriba, el título en
/// vertical y, si algo del cajón cambia el sonido (`active`), un punto del
/// color de acento abajo.
void drawerToggle(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, bool open, bool active,
                  const ControlState& state);

/// Cuerpo de un campo de valor (el mínimo y el máximo del rango del panel
/// Match): como un desplegable, sin flecha. El texto lo pinta quien lo usa.
void field(juce::Graphics& g, juce::Rectangle<float> bounds, const ControlState& state);

/// Barra fina de progreso (los segundos útiles del panel Match): `fraction` de 0 a 1.
void progress(juce::Graphics& g, juce::Rectangle<float> bounds, double fraction, juce::Colour accent,
              const ControlState& state);

/// Cuerpo de un desplegable, con su flecha.
void dropdown(juce::Graphics& g, juce::Rectangle<float> bounds, const ControlState& state);

/// Anillo de foco de teclado alrededor de `bounds`.
void focusRing(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius);

/// Etiqueta en mayúsculas pequeñas.
void caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds, juce::Justification justification,
             juce::Colour colour = theme::colour::textDim);

} // namespace esceq::ui::draw
