// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "BandPanel.h"
#include "Controls.h"
#include "CurveView.h"
#include "EsceqLookAndFeel.h"
#include "OutputMeter.h"
#include "PresetBar.h"

#include "esceq/core/parameters.h"
#include "esceq/dsp/filter_design.h"
#include "esceq/ui/layout.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Ventana del plugin: barra superior (presets, deshacer, comparar, opciones,
/// modo de fase, analizador, rango, salida, compensación automática de
/// ganancia y bypass), curva, medidor de salida y panel de la banda
/// seleccionada.
/// La disposición sale de esceq/ui/layout.h.
class EqEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EqEditor(PluginProcessor& processor);
    ~EqEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    CurveView& curve() noexcept { return curve_; }

private:
    void timerCallback() override;

    /// Aplica las preferencias guardadas con el proyecto (analizador, rango y tamaño).
    void applyUiSettings();
    /// Guarda en el procesador las preferencias actuales de la interfaz.
    void storeUiSettings();
    /// Menú para elegir con qué instancias de la sesión comparar.
    void showCompareMenu();
    /// Menú de opciones: el algoritmo de filtro ("HF accuracy") y el tamaño
    /// de la ventana ("Window size").
    void showOptionsMenu();
    /// Pone en la ayuda del botón de opciones el algoritmo que suena.
    void updateOptionsTooltip();
    /// Pone el botón Compare de acuerdo con la selección del procesador.
    void updateCompareButton();
    /// Pone en la ayuda del modo de fase la latencia del modo que suena.
    void updatePhaseTooltip();
    /// Pone en el botón Auto la compensación de la curva actual.
    void updateAutoGain();

    PluginProcessor& processor_;
    EsceqLookAndFeel lookAndFeel_;
    EditorLayout layout_{};
    TopBarLayout topBar_{};

    CurveView curve_;
    OutputMeter meter_;
    BandPanel panel_;

    PresetBar presets_;
    IconButton undo_{draw::Icon::Undo, "Undo", draw::Corners::Left};
    IconButton redo_{draw::Icon::Redo, "Redo", draw::Corners::Right};
    IconButton compare_{draw::Icon::Compare, "Compare with other tracks"};
    IconButton options_{draw::Icon::Options, "Options"};
    std::optional<dsp::FilterAlgorithm> shownAlgorithm_; ///< El que dice la ayuda de options_.
    juce::TooltipWindow tooltips_{this, 700};
    Dropdown phase_{"Phase mode"};
    int shownLatency_ = -1;
    Dropdown analyzer_{"Analyzer"};
    Dropdown range_{"Display range"};
    ParamControl output_{ParamControl::Style::Bar, "Output"};
    ToggleSwitch autoGain_{ToggleSwitch::Style::Pill, "Auto", "Auto gain"};
    /// De lo que depende la compensación que muestra autoGain_: solo se
    /// recalcula si cambia (tarda de 13 µs a unos ms, ver dsp/auto_gain.h).
    struct AutoGainInputs
    {
        core::ParameterValues values{};
        dsp::FilterAlgorithm algorithm{};
        double sampleRate = 0.0;
        int channels = 0;

        /// Igualdad exacta a propósito: cualquier cambio obliga a recalcular.
        // NOLINTNEXTLINE(clang-diagnostic-float-equal)
        friend bool operator==(const AutoGainInputs&, const AutoGainInputs&) = default;
    };
    std::optional<AutoGainInputs> autoGainInputs_;
    ToggleSwitch bypass_{ToggleSwitch::Style::Pill, "Bypass", "Bypass"};

    int stateRevision_ = 0;
    bool restoring_ = true; ///< Mientras se aplican preferencias guardadas, no se vuelven a guardar.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqEditor)
};

} // namespace esceq::ui
