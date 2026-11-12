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

/// Barra horizontal de valor (deslizador de salida).
void bar(juce::Graphics& g, juce::Rectangle<float> bounds, const ValueSpan& span, bool bipolar, juce::Colour accent,
         const ControlState& state);

/// Interruptor de dos posiciones.
void toggle(juce::Graphics& g, juce::Rectangle<float> bounds, bool on, juce::Colour accent, const ControlState& state);

/// Botón de texto que se queda encendido (Bypass) o segmento de un selector.
enum class Corners { All, Left, Middle, Right };
void pill(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& text, bool on, juce::Colour accent,
          const ControlState& state, Corners corners = Corners::All);

/// Cuerpo de un desplegable, con su flecha.
void dropdown(juce::Graphics& g, juce::Rectangle<float> bounds, const ControlState& state);

/// Anillo de foco de teclado alrededor de `bounds`.
void focusRing(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius);

/// Etiqueta en mayúsculas pequeñas.
void caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds, juce::Justification justification,
             juce::Colour colour = theme::colour::textDim);

} // namespace esceq::ui::draw
