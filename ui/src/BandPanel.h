// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Controls.h"

#include "esceq/core/state.h"
#include "esceq/ui/layout.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Panel de la banda seleccionada, en tres tarjetas:
///  - Banda: color y número, activación, tipo, pendiente y canal.
///  - EQ: frecuencia, ganancia y Q.
///  - Dinámica: activación, fuente del detector, tiempos automáticos, umbral,
///    rango, ataque, relajación y rodilla, y un medidor de la ganancia
///    dinámica. Con los tiempos automáticos, ataque y relajación se atenúan
///    y dicen "Auto".
///
/// Todos los controles se enlazan a parámetros del host. Dónde va cada cosa
/// lo decide esceq/ui/layout.h (testeado sin ventana); aquí solo se pinta.
class BandPanel final : public juce::Component
{
public:
    explicit BandPanel(PluginProcessor& processor);
    ~BandPanel() override;

    /// Banda a mostrar; -1 muestra las instrucciones de uso.
    void setBand(int band);

    /// Actualiza lo que depende del tipo de filtro y del audio (controles
    /// sin efecto, aviso del sidechain, medidor). La llama el temporizador del editor.
    void refresh();

    /// Escala de notas: la afinación, con la que se entienden las notas
    /// escritas en la frecuencia, y si se ve el teclado; entonces la etiqueta
    /// de la frecuencia dice su nota ("A3 +0 ct").
    void setNoteScale(bool visible, double a4Hz);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void paintEmpty(juce::Graphics& g) const;
    void paintMeter(juce::Graphics& g) const;

    PluginProcessor& processor_;
    int band_ = -1;
    PanelLayout layout_{};

    ToggleSwitch enabled_{ToggleSwitch::Style::Switch, {}, "Band on"};
    Dropdown type_{"Filter type"};
    Dropdown slope_{"Slope"};
    Segmented channel_;

    ParamControl frequency_{ParamControl::Style::Knob, "Freq"};
    ParamControl gain_{ParamControl::Style::Knob, "Gain"};
    ParamControl q_{ParamControl::Style::Knob, "Q"};

    ToggleSwitch dynamic_{ToggleSwitch::Style::Switch, "Active", "Dynamics on"};
    Dropdown source_{"Detector source"};
    ToggleSwitch autoTiming_{ToggleSwitch::Style::Switch, "Auto", "Auto timing"};
    ParamControl threshold_{ParamControl::Style::Knob, "Thresh"};
    ParamControl range_{ParamControl::Style::Knob, "Range"};
    ParamControl attack_{ParamControl::Style::Knob, "Attack"};
    ParamControl release_{ParamControl::Style::Knob, "Release"};
    ParamControl knee_{ParamControl::Style::Knob, "Knee"};

    bool notesVisible_ = false;
    double a4Hz_ = core::kDefaultA4Hz;

    // Estado que se pinta; se actualiza en refresh().
    juce::String typeName_;
    bool dynamicsSupported_ = true;
    bool dynamicsActive_ = false;
    bool sidechainMissing_ = false;
    float dynamicGainDb_ = 0.0f;
    float rangeDb_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandPanel)
};

} // namespace esceq::ui
