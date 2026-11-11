// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/ui/curve_mapping.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

enum class AnalyzerMode
{
    Off,
    Post,
    PreAndPost,
};

/// Vista principal: rejilla, analizador, curva de respuesta y nodos de las bandas.
///
/// Interacción:
///  - Doble clic en un hueco: añade una banda (campana) en ese punto.
///  - Arrastrar un nodo: frecuencia y ganancia. Con Mayús, ajuste fino.
///  - Rueda sobre un nodo o con una banda seleccionada: Q.
///  - Doble clic en un nodo: elimina (desactiva) la banda.
///  - Clic derecho en un nodo: menú con el tipo de filtro, la dinámica y eliminar.
///
/// Las bandas dinámicas dibujan la curva que suena en cada momento (con la
/// ganancia dinámica), la estática como referencia punteada, y junto al nodo
/// una barra con el rango y una marca con la ganancia dinámica actual.
///
/// Toda la geometría está en esceq/ui/curve_mapping.h; aquí solo se pinta
/// y se traducen los gestos del ratón a parámetros del host.
class CurveView final : public juce::Component
{
public:
    explicit CurveView(PluginProcessor& processor);
    ~CurveView() override;

    /// Se llama cuando cambia la banda seleccionada (-1 = ninguna).
    std::function<void(int)> onSelectionChanged;

    int selectedBand() const noexcept { return selected_; }
    void setSelectedBand(int band);

    void setRangeDb(double rangeDb);
    void setAnalyzerMode(AnalyzerMode mode);

    /// Lee parámetros y audio nuevo y repinta. La llama el temporizador del editor.
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    CurveMapping mapping() const;

    /// Ajustes de la banda tal y como suenan ahora (con la ganancia dinámica).
    dsp::BandSettings soundingSettings(int band) const;
    std::optional<int> bandAt(juce::Point<float> position) const;

    void setParameter(int band, core::BandParam param, float value);
    void beginGesture(int band, core::BandParam param);
    void endGesture(int band, core::BandParam param);
    void addBandAt(juce::Point<float> position);
    void removeBand(int band);
    void showBandMenu(int band);

    void updateAnalyzer();
    void drawGrid(juce::Graphics& g, const CurveMapping& m) const;
    void drawAnalyzer(juce::Graphics& g, const CurveMapping& m) const;
    void drawCurves(juce::Graphics& g, const CurveMapping& m) const;
    void drawNodes(juce::Graphics& g, const CurveMapping& m) const;

    PluginProcessor& processor_;
    core::ParameterValues values_{};
    std::array<float, core::kNumBands> dynamicGains_{};

    int selected_ = -1;
    int hovered_ = -1;
    int dragging_ = -1;
    bool draggingGain_ = false;
    juce::Point<float> dragLast_;

    double rangeDb_ = 18.0;
    AnalyzerMode analyzerMode_ = AnalyzerMode::Post;

    std::unique_ptr<dsp::SpectrumAnalyzer> preAnalyzer_;
    std::unique_ptr<dsp::SpectrumAnalyzer> postAnalyzer_;
    std::vector<double> analyzerFrequencies_;
    std::vector<float> preLevels_;
    std::vector<float> postLevels_;
    std::vector<float> scratch_;
    std::vector<float> readBuffer_;
    double lastAnalyzerUpdateMs_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveView)
};

} // namespace esceq::ui
