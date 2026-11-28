// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "BandPanel.h"
#include "MacroLinks.h"
#include "Controls.h"
#include "CurveView.h"
#include "EsceqLookAndFeel.h"
#include "MatchPanel.h"
#include "OutputDrawer.h"
#include "OutputMeter.h"
#include "PianoKeyboard.h"
#include "PresetBar.h"

#include "esceq/core/parameters.h"
#include "esceq/core/state.h"
#include "esceq/dsp/filter_design.h"
#include "esceq/ui/layout.h"
#include "esceq/ui/match.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Ventana del plugin: barra superior (presets, deshacer, comparar, opciones,
/// modo de fase, analizador, vista ampliada, rango y bypass), curva, cajón
/// de la etapa de salida (saturación, ganancia de salida y compensación
/// automática), medidor de salida y panel de la banda seleccionada.
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
    PianoKeyboard& keyboard() noexcept { return keyboard_; }

    /// Panel Match (igualar a una referencia, PLAN.md 5.6), que se abre desde
    /// el menú del botón de comparar ("Match to reference…"). Mientras está
    /// abierto hay un MatchController, que se refresca con el editor;
    /// cerrarlo es Stop, y lo aprendido se queda en el procesador.
    void openMatch();
    void closeMatch();
    bool matchOpen() const noexcept { return match_ != nullptr; }
    /// El controlador del panel abierto (nulo si está cerrado), para la herramienta de capturas.
    MatchController* matchController() noexcept { return match_.get(); }

private:
    void timerCallback() override;

    /// Aplica las preferencias guardadas con el proyecto (analizador, rango y tamaño).
    void applyUiSettings();
    /// Guarda en el procesador las preferencias actuales de la interfaz.
    void storeUiSettings();
    /// Menú para elegir con qué instancias de la sesión comparar.
    void showCompareMenu();
    /// Menú de opciones: el algoritmo de filtro ("HF accuracy"), el tamaño
    /// de la ventana ("Window size"), la escala de notas ("Notes": el
    /// teclado y la afinación) y los ajustes del analizador ("Analyzer").
    void showOptionsMenu();
    /// Pide la afinación escrita a mano ("Other..." en el menú de opciones).
    void showTuningDialog();
    /// Muestra u oculta el teclado de notas y cambia la afinación, en la
    /// curva, el teclado y el panel, y lo guarda en las preferencias.
    void setNoteScale(bool keyboard, double a4Hz);
    /// Vista ampliada (PLAN.md 5.2): sin panel ni medidor, la curva ocupa la
    /// ventana menos la barra superior. No se guarda con el proyecto.
    void setExpanded(bool expanded);
    /// Pone el botón de la vista ampliada (icono y ayuda) de acuerdo con ella.
    void updateExpandButton();
    /// Pone en la ayuda del botón de opciones el algoritmo que suena.
    void updateOptionsTooltip();
    /// Pone el botón Compare de acuerdo con la selección del procesador.
    void updateCompareButton();
    /// Pone en la ayuda del modo de fase la latencia del modo que suena.
    void updatePhaseTooltip();
    /// Pone en el botón Auto la compensación de la curva actual.
    void updateAutoGain();
    /// Abre o pliega el cajón de la etapa de salida y lo guarda en las preferencias.
    void setDrawerOpen(bool open);
    /// Lo que la instancia sabe ahora, para el controlador de la igualación.
    MatchContext matchContext();
    /// Pone al día el panel Match y la curva con el estado del controlador.
    /// Con `tick` (el refresco de 30 Hz, y solo él), antes avanza el
    /// aprendizaje: el audio del sidechain y MatchController::tick.
    void updateMatch(bool tick);
    void tickMatch(const MatchContext& context);
    /// Apply: crea las bandas propuestas como una sola acción y selecciona la primera.
    void applyMatch();

    PluginProcessor& processor_;
    EsceqLookAndFeel lookAndFeel_;
    /// Las macros en los controles: antes que los controles que la usan.
    MacroLinks links_;
    EditorLayout layout_{};
    TopBarLayout topBar_{};

    CurveView curve_;
    PianoKeyboard keyboard_;
    bool showKeyboard_ = false;
    double a4Hz_ = core::kDefaultA4Hz;
    juce::Component::SafePointer<juce::AlertWindow> tuningDialog_; ///< Abierto con "Other...".
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
    IconButton expand_{draw::Icon::Expand, "Expanded view"};
    bool expanded_ = false;
    Dropdown range_{"Display range"};
    OutputDrawer drawer_;
    /// De lo que depende la compensación que muestra el botón Auto: solo se
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

    MatchPanel matchPanel_;
    std::unique_ptr<MatchController> match_;
    std::vector<core::InstanceSpectrum> matchInstances_; ///< La sesión, en cada refresco.
    core::ParameterValues matchValues_{};                ///< Los valores del refresco (MatchContext::values).
    std::vector<float> sidechainBuffer_;

    int stateRevision_ = 0;
    bool restoring_ = true; ///< Mientras se aplican preferencias guardadas, no se vuelven a guardar.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqEditor)
};

} // namespace esceq::ui
