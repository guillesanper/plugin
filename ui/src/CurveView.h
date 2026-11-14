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
///  - Doble clic en un hueco: añade una banda (campana) en ese punto, con el
///    resto de ajustes por defecto (sin dinámica, en Stereo).
///  - Arrastrar un nodo: frecuencia y ganancia. Con Mayús, ajuste fino.
///  - Rueda sobre un nodo o con una banda seleccionada: Q.
///  - Doble clic en un nodo: elimina (desactiva) la banda.
///  - Clic derecho en un nodo: menú con el tipo de filtro, el canal, la dinámica y eliminar.
///  - Teclado: izquierda y derecha eligen banda; arriba y abajo, su ganancia;
///    Ctrl/Cmd + izquierda y derecha, su frecuencia; Mayús, ajuste fino; Supr la elimina.
///
/// Al señalar o arrastrar un nodo aparece una etiqueta flotante con su
/// frecuencia, ganancia y Q. Las bandas dinámicas dibujan la curva que suena
/// en cada momento (con la ganancia dinámica), la estática como referencia
/// punteada, y junto al nodo un medidor con el rango y la ganancia dinámica
/// actual. Si la ganancia de una banda no cabe en el rango visible, el nodo
/// se queda en el borde con una flecha y el valor.
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

    /// Banda señalada por el ratón (-1 = ninguna). Lo fija el propio ratón;
    /// es público para la herramienta de capturas.
    void setHoveredBand(int band);

    void setRangeDb(double rangeDb);
    void setAnalyzerMode(AnalyzerMode mode);

    /// Lee parámetros y audio nuevo, y repinta si algo ha cambiado. La llama el temporizador del editor.
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

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

    /// Lee el audio nuevo y actualiza el espectro. true si ha cambiado lo bastante para repintar.
    bool updateAnalyzer();
    void drawGrid(juce::Graphics& g, const CurveMapping& m) const;
    void drawAnalyzer(juce::Graphics& g, const CurveMapping& m) const;
    void drawCurves(juce::Graphics& g, const CurveMapping& m) const;
    void drawNodes(juce::Graphics& g, const CurveMapping& m) const;
    void drawReadout(juce::Graphics& g, const CurveMapping& m) const;

    PluginProcessor& processor_;
    core::ParameterValues values_{};
    std::array<float, core::kNumBands> dynamicGains_{};

    int selected_ = -1;
    int hovered_ = -1;
    int dragging_ = -1;
    bool draggingGain_ = false;
    bool keyboardFocus_ = false;
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
    double lastSampleRate_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveView)
};

} // namespace esceq::ui
