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
inline constexpr WindowSize kMinWindow{1120, 540};
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
    Rect meter;    ///< Medidor de salida, a la derecha de la curva; vacío en la vista ampliada.
    Rect panel;    ///< Panel de la banda seleccionada; vacío en la vista ampliada.
    Rect keyboard; ///< Teclado de la escala de notas, bajo la curva; vacío si está oculto.
};

/// Con `pianoKeyboard`, la curva cede por abajo el alto del teclado de notas
/// (kPianoKeyboardHeight, de esceq/ui/notes.h) y una separación, y el teclado
/// va ahí, con el mismo ancho y la misma x que la curva. Sin él, la
/// disposición es la de antes de la escala de notas, píxel a píxel.
///
/// Con `expanded` (vista ampliada del analizador, PLAN.md 5.2), no hay panel
/// ni medidor: la curva, con el teclado si se ve, ocupa toda la ventana menos
/// la barra superior, con el margen de siempre a los lados y por abajo. Sin
/// él, la disposición no cambia.
EditorLayout editorLayout(float width, float height, bool pianoKeyboard = false, bool expanded = false) noexcept;

/// Barra superior, de izquierda a derecha.
struct TopBarLayout
{
    Rect logo;
    Rect presets; ///< Navegador de presets: ocupa el espacio que sobra.
    Rect undo;
    Rect redo;
    Rect compare; ///< Comparar con otras instancias de la sesión (botón con icono).
    Rect options; ///< Menú de opciones (botón con icono): el algoritmo de filtro y el tamaño de la ventana.
    Rect phase;   ///< Modo de fase.
    Rect analyzer;
    Rect expand; ///< Vista ampliada del analizador (botón con icono).
    Rect range;
    Rect output;      ///< Barra de la ganancia de salida.
    Rect outputValue; ///< Su valor, editable.
    Rect autoGain;    ///< Compensación automática de ganancia: "Auto -2.3 dB".
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
    Rect autoTiming; ///< Interruptor de los tiempos automáticos ("Auto").
    Rect spectral;   ///< Interruptor de la dinámica espectral ("Spectral"), a su derecha.
    std::array<Rect, kNumDynamicKnobs> dynamicKnobs;
    Rect dynamicsMeter;
};

PanelLayout panelLayout(Rect bounds) noexcept;

/// Panel Match (igualar a una referencia, PLAN.md 5.6): una tarjeta sobre la
/// esquina superior derecha de la curva, a la izquierda de su escala de dB.
/// Filas de arriba abajo: título y cierre; referencia; Learn/Stop y los
/// segundos útiles; Amount; Smoothing; Max bands; Range (mínimo y máximo);
/// Apply y el estado.
struct MatchPanelLayout
{
    Rect card;
    Rect title;
    Rect close;
    Rect referenceLabel;
    Rect reference;
    Rect learn;
    Rect progress; ///< "3.2 s / 10 s" y su barra.
    Rect amountLabel;
    Rect amount;
    Rect smoothingLabel;
    Rect smoothing;
    Rect bandsLabel;
    Rect bands;
    Rect rangeLabel;
    Rect rangeLow;
    Rect rangeDash; ///< El guion entre el mínimo y el máximo.
    Rect rangeHigh;
    Rect apply;
    Rect status; ///< "Fitting…", "Nothing to match", los avisos o las bandas propuestas.
};

inline constexpr float kMatchCardWidth = 280.0f;
inline constexpr float kMatchCardHeight = 240.0f;
/// Ancho de las etiquetas de la izquierda ("SMOOTHING") y de los botones Learn y Apply.
inline constexpr float kMatchLabelWidth = 72.0f;

/// `curve` es la zona de la curva (EditorLayout::curve), en la vista normal o
/// en la ampliada. La tarjeta queda dentro de ella, separada 8 px de arriba y
/// a la izquierda de la escala de dB (kGainScaleWidth); si la curva es más
/// baja que la tarjeta, la tarjeta se recorta a su alto.
MatchPanelLayout matchPanelLayout(Rect curve) noexcept;

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
