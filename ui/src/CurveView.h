// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/core/state.h"
#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/ui/curve_mapping.h"
#include "esceq/ui/match.h"
#include "esceq/ui/meter_logic.h"
#include "esceq/ui/session_overlay.h"
#include "esceq/ui/spectrum_peaks.h"

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
///  - Arrastrar un nodo: frecuencia y ganancia. Con Mayús, ajuste fino; con
///    Alt (Option en macOS), la frecuencia salta a la nota temperada más cercana.
///  - Rueda sobre un nodo o con una banda seleccionada: Q.
///  - Doble clic en un nodo: elimina (desactiva) la banda.
///  - Clic derecho en un nodo: menú con el tipo de filtro, el canal, la dinámica y eliminar.
///  - Teclado: izquierda y derecha eligen banda; arriba y abajo, su ganancia;
///    Ctrl/Cmd + izquierda y derecha, su frecuencia, de semitono en semitono
///    (con Mayús, en pasos finos); Supr la elimina. F amplía o reduce la vista
///    (onExpandToggled) y Esc, en la vista ampliada, vuelve.
///  - Clic en la escala de ganancia (a la derecha): borra los picos mantenidos
///    del analizador.
///  - Con el ratón en un hueco, se marca el pico del espectro más cercano
///    (PLAN.md 5.3, esceq/ui/spectrum_peaks.h). Arrastrar desde ahí crea una
///    campana en el pico, con su Q, cuya ganancia sigue al arrastre vertical;
///    con Alt al soltar, queda dinámica. Todo el gesto es una acción de
///    deshacer, y un clic sin arrastre no crea nada.
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

    /// Posición del ratón sobre la curva (nullopt: fuera). La fija el propio
    /// ratón; es pública para la herramienta de capturas. Decide la banda
    /// señalada y el pico marcado.
    void setPointer(std::optional<juce::Point<float>> position);

    /// Escala de notas (esceq/ui/notes.h): la afinación, con la que se ajusta
    /// a semitonos, y si se ve el teclado; entonces la etiqueta flotante dice
    /// también la nota de la frecuencia.
    void setNoteScale(bool visible, double a4Hz);

    /// Nota que se marca con una línea vertical (la tecla señalada en el
    /// teclado de notas), o nullopt.
    void setMarkedNote(std::optional<int> midi);

    void setRangeDb(double rangeDb);
    void setAnalyzerMode(AnalyzerMode mode);

    /// Ajustes del analizador (esceq/ui/meter_logic.h). Cambiar la resolución
    /// rehace los analizadores (asigna memoria: solo en el hilo de la
    /// interfaz); cambiar la resolución o la inclinación borra los picos.
    void setAnalyzerView(const AnalyzerView& view);
    const AnalyzerView& analyzerView() const noexcept { return analyzerView_; }

    /// Vista ampliada (PLAN.md 5.2): la etiqueta flotante dice también el
    /// canal y la dinámica de la banda, porque no se ve el panel.
    void setExpanded(bool expanded);

    /// Se llama al pulsar F, o Esc en la vista ampliada, con el foco en la curva.
    std::function<void()> onExpandToggled;

    /// Lee parámetros y audio nuevo, y repinta si algo ha cambiado. La llama el temporizador del editor.
    void refresh();

    /// La selección de instancias a comparar del procesador ha cambiado: se
    /// empieza o se deja de mirar la sesión, y se repinta.
    void compareChanged();

    /// Igualación (PLAN.md 5.6): mientras el panel Match está abierto, el
    /// audio de la cola "pre", que solo lee esta vista, va también a
    /// `controller` (MatchController::pushTrack), con el analizador encendido
    /// o no. nullptr al cerrarlo.
    void setMatch(MatchController* controller);

    /// Tras cada refresco del controlador: con 10 s útiles, se dibujan los
    /// espectros aprendidos (la referencia con `referenceColour`), la curva
    /// objetivo y la propuesta con sus nodos en fantasma. Repinta si cambia algo.
    void refreshMatch(juce::Colour referenceColour);

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
    /// Cambia un parámetro en un gesto propio (una acción de deshacer) y repinta.
    void setParameterAsGesture(std::size_t index, float value);
    void beginGesture(int band, core::BandParam param);
    void endGesture(int band, core::BandParam param);
    /// Manda al host los cambios de una banda, cada uno en su gesto, salvo
    /// el del parámetro `openGesture`, que ya tiene uno abierto.
    void applyChanges(const std::vector<ParameterChange>& changes,
                      std::optional<std::size_t> openGesture = std::nullopt);
    void addBandAt(juce::Point<float> position);
    /// Crea la banda del pico pulsado y empieza a arrastrar su ganancia.
    void createPeakBand(const SpectrumPeak& peak);
    /// Busca el pico que se marca bajo el ratón (si el espectro ha cambiado,
    /// rehace la lista). true si el marcador ha cambiado.
    bool updatePeakMarker();
    void updateCursor();
    void removeBand(int band);
    void showBandMenu(int band);

    /// Lee el audio nuevo y actualiza el espectro. true si ha cambiado lo bastante para repintar.
    bool updateAnalyzer();
    void drawGrid(juce::Graphics& g, const CurveMapping& m) const;
    void drawAnalyzer(juce::Graphics& g, const CurveMapping& m) const;
    /// Rehace la curva de las bandas que han cambiado (ver CurveCache).
    void updateCurveCache(const CurveMapping& m);
    void drawCurves(juce::Graphics& g, const CurveMapping& m) const;
    void drawNodes(juce::Graphics& g, const CurveMapping& m) const;
    void drawReadout(juce::Graphics& g, const CurveMapping& m) const;
    void drawMarkedNote(juce::Graphics& g, const CurveMapping& m) const;
    /// El pico marcado y su etiqueta, o "No free bands" junto al ratón.
    void drawPeakMarker(juce::Graphics& g, const CurveMapping& m) const;
    /// Aviso de que el analizador está congelado, arriba a la izquierda.
    void drawFrozen(juce::Graphics& g, const CurveMapping& m) const;

    /// Lee la sesión y rehace los espectros superpuestos y las colisiones.
    /// true si hay que repintar.
    bool updateSession(bool force);
    void drawSession(juce::Graphics& g, const CurveMapping& m) const;
    /// Igualación: los espectros aprendidos (bajo la rejilla), el objetivo y
    /// la propuesta (sobre las curvas), los nodos en fantasma (bajo los
    /// nodos) y la leyenda (arriba a la izquierda).
    void drawMatchSpectra(juce::Graphics& g, const CurveMapping& m) const;
    void drawMatchCurves(juce::Graphics& g, const CurveMapping& m) const;
    void drawMatchNodes(juce::Graphics& g, const CurveMapping& m) const;
    void drawMatchLegend(juce::Graphics& g, const CurveMapping& m) const;
    /// Velo y franja de las colisiones (debajo de la rejilla).
    void drawCollisions(juce::Graphics& g, const CurveMapping& m) const;
    /// Etiquetas de las zonas de colisión (encima de curvas y nodos).
    void drawCollisionLabels(juce::Graphics& g, const CurveMapping& m) const;

    PluginProcessor& processor_;
    core::ParameterValues values_{};
    dsp::FilterAlgorithm algorithm_ = dsp::FilterAlgorithm::Classic; ///< El de la instancia, leído en refresh().
    std::array<float, core::kNumBands> dynamicGains_{};

    int selected_ = -1;
    int hovered_ = -1;
    int dragging_ = -1;
    bool draggingGain_ = false;
    bool keyboardFocus_ = false;
    juce::Point<float> dragLast_;
    /// Frecuencia del arrastre sin ajustar a semitonos: con Alt, la banda
    /// suena en la nota más cercana a ella, y el ratón no se queda pegado.
    double dragFrequencyHz_ = 0.0;

    bool notesVisible_ = false;
    double a4Hz_ = core::kDefaultA4Hz;
    std::optional<int> markedNote_;

    double rangeDb_ = 18.0;
    AnalyzerMode analyzerMode_ = AnalyzerMode::Post;

    AnalyzerView analyzerView_;
    bool expanded_ = false;

    std::unique_ptr<dsp::SpectrumAnalyzer> preAnalyzer_;
    std::unique_ptr<dsp::SpectrumAnalyzer> postAnalyzer_;
    std::vector<double> analyzerFrequencies_;
    SpectrumBallistics preLevels_;
    SpectrumBallistics postLevels_;
    SpectrumPeakHold postPeaks_; ///< Picos mantenidos del espectro post.
    /// Bandas desde el espectro (PLAN.md 5.3, ADR 0019): el promedio del
    /// espectro en el que se buscan los picos (el post, o el pre en pre +
    /// post), sus picos y si hay que volver a buscarlos.
    SpectrumAverage peakSpectrum_;
    std::vector<SpectrumPeak> peaks_;
    bool peaksStale_ = true;
    /// Ratón sobre la curva (para elegir el pico), el pico marcado y si hay
    /// una banda libre para tomarlo.
    std::optional<juce::Point<float>> pointer_;
    std::optional<SpectrumPeak> peakMarker_;
    bool freeBand_ = true;
    /// Pico pulsado: un arrastre desde él crea su banda. Sigue puesto
    /// mientras se arrastra esa banda (entonces solo cambia la ganancia).
    std::optional<SpectrumPeak> pressedPeak_;
    std::vector<float> scratch_;
    std::vector<float> readBuffer_;
    double lastAnalyzerUpdateMs_ = 0.0;
    double lastSampleRate_ = 0.0;

    /// Curva de cada banda ya calculada: dB por columna y los trazados.
    /// Solo se rehace la de una banda si cambia lo que suena (frecuencia,
    /// ganancia con la dinámica, Q...) o su referencia estática; todas, si
    /// cambia la geometría de la vista o la frecuencia de muestreo. Al
    /// arrastrar un nodo, las demás bandas no se recalculan.
    struct BandCurve
    {
        bool valid = false;
        bool dynamic = false;
        dsp::FilterParams sounding;  ///< Lo que suena, con la ganancia dinámica.
        dsp::FilterParams reference; ///< El ajuste estático (solo si es dinámica).
        std::vector<double> db;      ///< Magnitud en cada columna.
        juce::Path line;
        juce::Path fill;
        juce::Path dashed;           ///< Referencia estática punteada, ya convertida en contorno.
    };
    struct CurveCache
    {
        Rect area;
        double rangeDb = 0.0;
        double sampleRate = 0.0;
        std::vector<float> xs;
        std::vector<double> tangents; ///< tan(ω/2) de cada columna, común a todas las bandas.
        std::array<BandCurve, core::kNumBands> bands;
    };
    CurveCache curveCache_;

    /// Comparación con otras instancias (ver esceq/ui/session_overlay.h).
    /// Mientras haya alguna seleccionada, la vista "mira" la sesión: solo
    /// entonces las instancias calculan y publican su espectro.
    struct SessionOverlay
    {
        core::InstanceId id = 0;
        std::string name;
        juce::Colour colour;
        bool hasSignal = false;
        std::vector<float> levels;
    };
    core::SpectrumSession::Viewer sessionViewer_;
    std::vector<core::InstanceSpectrum> sessionSnapshot_;
    std::uint64_t sessionRevision_ = 0;
    std::vector<SessionOverlay> overlays_;
    CollisionOverlay collisions_;
    bool showCollisions_ = false;

    /// Igualación: el controlador del panel abierto y lo que se dibuja.
    MatchController* match_ = nullptr;
    struct MatchDrawing
    {
        bool shown = false;            ///< Con 10 s útiles.
        std::uint64_t revision = 0;    ///< La de lo aprendido en `spectra`.
        MatchSpectraDisplay spectra;
        juce::Colour referenceColour;
        /// La última propuesta, y si es la de lo que hay ahora (si no, se
        /// atenúa hasta que llegue la nueva: así no parpadea al recalcular).
        std::shared_ptr<const core::MatchProposal> proposal;
        bool current = false;
        std::vector<double> target;
        std::vector<MatchRun> runs;
    };
    MatchDrawing matchDrawing_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveView)
};

} // namespace esceq::ui
