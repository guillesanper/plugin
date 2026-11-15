// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/ui/curve_mapping.h"

#include <array>
#include <optional>

/// Disposición de la ventana, sin dependencias gráficas: dónde va cada zona
/// y cada control para un tamaño dado. Vive aquí para poder comprobar con
/// tests que nada se solapa ni se queda sin sitio en ningún tamaño de ventana.
/// Las medidas siguen la rejilla de 4 px del sistema de diseño (docs/ui.md).
namespace esceq::ui {

struct WindowSize
{
    int width = 0;
    int height = 0;

    friend bool operator==(const WindowSize&, const WindowSize&) = default;
};

/// El ancho mínimo es el que necesita la barra superior entera, con el
/// navegador de presets en su ancho mínimo (ver docs/ui.md).
inline constexpr WindowSize kMinWindow{1080, 540};
inline constexpr WindowSize kMaxWindow{2400, 1400};

/// Tamaños predefinidos de la ventana. CONTRATO: el índice se guarda en el
/// estado (preferencias de la interfaz); los nuevos se añaden al final.
enum class SizePreset
{
    Small,
    Medium,
    Large,
};

inline constexpr int kNumSizePresets = 3;
inline constexpr SizePreset kDefaultSizePreset = SizePreset::Medium;

WindowSize windowSize(SizePreset preset) noexcept;

/// Preset cuyo tamaño coincide exactamente, o nullopt si la ventana se ha
/// redimensionado a mano.
std::optional<SizePreset> matchingPreset(WindowSize size) noexcept;

/// Ajusta un tamaño a los límites de la ventana.
WindowSize clampWindow(WindowSize size) noexcept;

/// Zonas de la ventana.
struct EditorLayout
{
    Rect topBar;
    Rect curve;
    Rect meter; ///< Medidor de salida, a la derecha de la curva.
    Rect panel; ///< Panel de la banda seleccionada.
};

EditorLayout editorLayout(float width, float height) noexcept;

/// Barra superior, de izquierda a derecha.
struct TopBarLayout
{
    Rect logo;
    Rect presets; ///< Navegador de presets: ocupa el espacio que sobra.
    Rect undo;
    Rect redo;
    Rect compare; ///< Comparar con otras instancias de la sesión (botón con icono).
    Rect phase;   ///< Modo de fase.
    Rect analyzer;
    Rect range;
    Rect size;
    Rect output;      ///< Barra de la ganancia de salida.
    Rect outputValue; ///< Su valor, editable.
    Rect bypass;
};

TopBarLayout topBarLayout(Rect bounds) noexcept;

inline constexpr int kNumEqKnobs = 3;
inline constexpr int kNumDynamicKnobs = 5;

/// Panel de la banda: tres tarjetas (banda, EQ y dinámica).
struct PanelLayout
{
    Rect bandCard;
    Rect bandTitle;
    Rect enabled;
    Rect type;
    Rect slope;
    Rect channel;

    Rect eqCard;
    Rect eqTitle;
    std::array<Rect, kNumEqKnobs> eqKnobs; ///< Celdas: etiqueta, potenciómetro y valor.

    Rect dynamicsCard;
    Rect dynamicsTitle;
    Rect dynamicsEnabled;
    Rect source;
    Rect sourceWarning;
    std::array<Rect, kNumDynamicKnobs> dynamicKnobs;
    Rect dynamicsMeter;
};

PanelLayout panelLayout(Rect bounds) noexcept;

/// Reparto vertical de la celda de un potenciómetro.
struct KnobCellLayout
{
    Rect label;
    Rect knob; ///< Cuadrado centrado.
    Rect value;
};

inline constexpr float kKnobLabelHeight = 14.0f;
inline constexpr float kKnobValueHeight = 20.0f;
inline constexpr float kMinKnobDiameter = 32.0f;
inline constexpr float kMaxKnobDiameter = 56.0f;
/// Ancho mínimo de una celda para que quepan sin cortarse la etiqueta más
/// larga ("RELEASE") y el valor más largo ("-48.0 dB", "12.5 kHz").
inline constexpr float kMinKnobCellWidth = 66.0f;

KnobCellLayout knobCellLayout(Rect cell) noexcept;

} // namespace esceq::ui
