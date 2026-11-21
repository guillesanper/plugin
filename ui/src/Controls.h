// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Draw.h"

#include "esceq/core/parameters.h"
#include "esceq/core/state.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

/// Controles propios de esceQ. Cada uno:
///  - se enlaza a un parámetro del host con juce::ParameterAttachment, así
///    que automatización, deshacer y guardado funcionan igual que desde la curva;
///  - delega el comportamiento en esceq/ui/control_logic.h (testeado sin ventana);
///  - se pinta con las funciones de Draw.h;
///  - tiene nombre accesible y se maneja con el teclado.
namespace esceq::ui {

/// Control de un parámetro continuo: potenciómetro con etiqueta y valor, o
/// barra horizontal con el valor a la derecha.
///
/// Gestos: arrastrar (vertical u horizontal), Mayús para ajuste fino, rueda,
/// doble clic o Ctrl/Cmd+clic para volver al valor por defecto, clic en el
/// valor para escribirlo. Teclado: flechas (Mayús = fino), Intro para
/// escribir el valor, Supr o Retroceso para el valor por defecto.
class ParamControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style
    {
        Knob,
        Bar,
        Field, ///< Solo el valor, en una caja: se arrastra o se escribe (el rango del panel Match).
    };

    /// Texto propio del valor, en lugar del de core::formatValue y commitText
    /// (por ejemplo, "50 %"). Lo que `parse` no entiende devuelve nullopt.
    struct TextFormat
    {
        std::function<juce::String(float)> format;
        std::function<std::optional<float>(const juce::String&)> parse;
    };

    ParamControl(Style style, const juce::String& label);
    ~ParamControl() override;

    /// Enlaza el control a un parámetro. `spec` debe ser la entrada de
    /// core::parameters() de ese parámetro (vive mientras viva el programa).
    void attach(juce::RangedAudioParameter& parameter, const core::ParameterSpec& spec);
    void detach();

    /// Sin parámetro del host (los ajustes del panel Match): el control
    /// guarda su valor y llama a `onChange` con cada cambio del usuario,
    /// también durante un arrastre. No hay gestos ni deshacer. `spec` (rango,
    /// escala, valor por defecto y unidad) debe vivir más que el control.
    void bindLocal(const core::ParameterSpec& spec, float value, std::function<void(float)> onChange,
                   TextFormat format = {});
    /// Con bindLocal: pone el valor desde fuera, sin avisar (no durante un arrastre).
    void setLocalValue(float value);

    void setAccent(juce::Colour accent);

    /// Atenúa el control sin impedir editarlo: el parámetro no influye ahora
    /// en el sonido (por ejemplo, la dinámica apagada).
    void setDimmed(bool dimmed);

    /// Texto que se muestra (y lee el lector de pantalla) en lugar del valor,
    /// por ejemplo "Auto" cuando otro ajuste decide el valor; vacío vuelve a
    /// mostrar el valor. El parámetro no cambia y se puede seguir editando.
    void setValueOverride(const juce::String& text);

    /// Cambia la etiqueta que se ve sobre el potenciómetro (por ejemplo, la
    /// nota de la frecuencia). El nombre accesible sigue siendo el del parámetro.
    void setLabel(const juce::String& label);

    /// Afinación (la del A4, en Hz) con la que se entiende una nota escrita
    /// en un parámetro en Hz ("A3"), ver esceq/ui/notes.h.
    void setA4(double a4Hz) noexcept { a4Hz_ = a4Hz; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void enablementChanged() override { repaint(); }

    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // Para el lector de pantalla (y los tests manuales).
    float value() const noexcept { return value_; }
    juce::String valueText() const;
    const core::ParameterSpec* spec() const noexcept { return spec_; }
    void setValueFromUser(float value);
    bool setTextFromUser(const juce::String& text);

private:
    juce::Rectangle<float> controlArea() const;
    juce::Rectangle<float> valueArea() const;
    void showEditor();
    void hideEditor(bool commit);
    void resetToDefault();

    /// Con el parámetro del host, sus gestos; con bindLocal, el valor y su aviso.
    bool bound() const noexcept { return attachment_ != nullptr || onLocalChange_ != nullptr; }
    void beginEdit();
    void editValue(float value);
    void endEdit();
    void setValueComplete(float value);
    void setLocal(float value);

    Style style_;
    juce::String label_;
    juce::Colour accent_ = theme::colour::accent;
    bool dimmed_ = false;
    juce::String valueOverride_;
    double a4Hz_ = core::kDefaultA4Hz;

    const core::ParameterSpec* spec_ = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    std::function<void(float)> onLocalChange_;
    TextFormat format_;
    float value_ = 0.0f;

    bool hover_ = false;
    bool dragging_ = false;
    bool moved_ = false;
    bool keyboardFocus_ = false;
    float dragPosition_ = 0.0f; ///< Posición 0..1 acumulada durante el arrastre.
    juce::Point<float> dragLast_;

    std::unique_ptr<juce::TextEditor> editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamControl)
};

/// Interruptor enlazado a un parámetro Toggle. Como botón de JUCE, hereda el
/// manejo con teclado (espacio, Intro) y la accesibilidad.
class ToggleSwitch final : public juce::Button
{
public:
    enum class Style
    {
        Switch, ///< Interruptor deslizante, con el texto (si lo hay) a su derecha.
        Pill,   ///< Botón de texto que se queda encendido.
    };

    ToggleSwitch(Style style, const juce::String& label, const juce::String& accessibleName);
    ~ToggleSwitch() override;

    void attach(juce::RangedAudioParameter& parameter);
    void detach();
    void setAccent(juce::Colour accent);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    Style style_;
    juce::Colour accent_ = theme::colour::accent;
    bool keyboardFocus_ = false;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSwitch)
};

/// Selector de una opción entre unas pocas, todas a la vista ("ST L R M S").
/// Puede enlazarse a un parámetro Choice o usarse suelto con onSelect.
class Segmented final : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Option
    {
        juce::String text;           ///< Lo que se ve.
        juce::String accessibleName; ///< Lo que lee el lector de pantalla.
    };

    Segmented(std::vector<Option> options, const juce::String& accessibleName);
    ~Segmented() override;

    void attach(juce::RangedAudioParameter& parameter);
    void detach();
    void setAccent(juce::Colour accent);

    /// Opción elegida (-1 = ninguna), sin avisar a onSelect.
    void setSelected(int index);
    int selected() const noexcept { return selected_; }

    /// Se llama cuando el usuario elige una opción (no al cambiar por código).
    std::function<void(int)> onSelect;

    /// La ayuda va en cada segmento: son ellos los que quedan bajo el ratón.
    void setTooltip(const juce::String& tooltip) override;

    void resized() override;
    void enablementChanged() override;

private:
    class Segment;
    void choose(int index);

    std::vector<std::unique_ptr<Segment>> segments_;
    juce::Colour accent_ = theme::colour::accent;
    int selected_ = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Segmented)
};

/// Botón con un icono (deshacer, rehacer, preset anterior y siguiente).
class IconButton final : public juce::Button
{
public:
    IconButton(draw::Icon icon, const juce::String& accessibleName, draw::Corners corners = draw::Corners::All);

    /// Encendido: el icono va en el color de acento (por ejemplo, Compare con algo seleccionado).
    void setOn(bool on);
    /// Texto corto en una pastilla sobre la esquina (un número), o vacío.
    void setBadge(const juce::String& badge);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    draw::Icon icon_;
    draw::Corners corners_;
    bool keyboardFocus_ = false;
    bool on_ = false;
    juce::String badge_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconButton)
};

/// Desplegable enlazado a un parámetro Choice. Es un juce::ComboBox pintado
/// por EsceqLookAndFeel.
class Dropdown final : public juce::ComboBox
{
public:
    explicit Dropdown(const juce::String& accessibleName);
    ~Dropdown() override;

    /// Rellena las opciones a partir de `spec.choices` y enlaza el parámetro.
    void attach(juce::RangedAudioParameter& parameter, const core::ParameterSpec& spec);
    void detach();

    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    std::unique_ptr<juce::ComboBoxParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dropdown)
};

} // namespace esceq::ui
