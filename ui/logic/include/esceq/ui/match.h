// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/match.h"
#include "esceq/core/parameters.h"
#include "esceq/core/spectrum_session.h"
#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/dsp/spectrum_collision.h"
#include "esceq/ui/curve_mapping.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Igualar a una referencia, sin ventana (PLAN.md 5.6, ADR 0023): qué
/// referencias se ofrecen, el aprendizaje (MatchController), cuándo se pide
/// un ajuste, el estado del panel con los motivos de Apply desactivado, y los
/// cambios de los parámetros que crean las bandas. Lo aprendido, los ajustes y
/// el hilo del ajuste viven en el procesador (core/match.h); esto, en la
/// interfaz, que es la que mide la pista. Todo, en el hilo de mensajes.
namespace esceq::ui
{

/// Una entrada del desplegable de referencias.
struct MatchReferenceOption
{
    core::MatchReference reference;
    std::string name;     ///< El de la instancia ("esceQ 2", o el de la pista), o "Sidechain".
    bool enabled = true;  ///< El sidechain, solo si el host lo conecta.
    bool silent = false;  ///< Una instancia a la que no le llega audio.

    /// Texto de la entrada: el nombre, con " (silent)" o " (not connected)".
    std::string text() const;

    friend bool operator==(const MatchReferenceOption&, const MatchReferenceOption&) = default;
};

/// Las otras instancias de la sesión, en su orden, y al final el sidechain
/// (desactivado si no está conectado). La propia (`self`) no aparece.
std::vector<MatchReferenceOption> matchReferenceOptions(std::span<const core::InstanceSpectrum> instances,
                                                        core::InstanceId self, bool sidechainConnected);

/// La referencia por defecto: la primera instancia con señal; si no, la
/// primera instancia; si no hay, el sidechain si está conectado; si no, ninguna.
core::MatchReference defaultMatchReference(std::span<const MatchReferenceOption> options);

/// true si se puede aprender de `reference`: es una de `options` y está activada.
bool canLearnFrom(const core::MatchReference& reference, std::span<const MatchReferenceOption> options);

/// Por qué Learn o Apply están desactivados.
enum class MatchBlock
{
    None,
    NoReference,    ///< "No reference": ni otras instancias ni sidechain conectado.
    LearnFirst,     ///< "Learn 10 s first".
    NoFreeBands,    ///< "No free bands".
    Fitting,        ///< "Fitting…": se está calculando la propuesta.
    NothingToMatch, ///< "Nothing to match": la propuesta está vacía (ya se parecen).
};

/// Texto del motivo, en la ayuda del botón (vacío con MatchBlock::None).
std::string matchBlockText(MatchBlock block);

/// Avisos del panel al pararse el aprendizaje solo.
enum class MatchNotice
{
    None,
    ReferenceRemoved,      ///< "Reference removed": se ha borrado la instancia de referencia.
    SidechainDisconnected, ///< "Sidechain disconnected".
};

std::string matchNoticeText(MatchNotice notice);

/// Indicador de los segundos útiles: "3.2 s / 10 s" (redondeado a décimas hacia abajo, para no
/// mostrar "10.0 s / 10 s" antes de tiempo).
std::string matchProgressText(double usefulSeconds);

/// Lo que la instancia sabe en un refresco de la interfaz.
struct MatchContext
{
    /// Copia de la sesión (SpectrumSession::snapshot) y esta instancia.
    std::span<const core::InstanceSpectrum> instances;
    core::InstanceId self = 0;
    bool sidechainConnected = false;
    /// La instancia: frecuencia de muestreo, canales y algoritmo de filtro.
    /// Las curvas de la propuesta se calculan con ellos.
    double sampleRate = 48000.0;
    int numChannels = 2;
    dsp::FilterAlgorithm algorithm = dsp::FilterAlgorithm::Classic;
    /// Valores actuales de los parámetros (las bandas que ya hay).
    const core::ParameterValues* values = nullptr;
};

/// Estado del panel.
struct MatchStatus
{
    std::vector<MatchReferenceOption> options;
    core::MatchReference reference;
    bool learning = false;
    bool canLearn = false;
    MatchBlock learnBlock = MatchBlock::None; ///< NoReference si no se puede aprender.
    double usefulSeconds = 0.0;
    std::string progress;                     ///< matchProgressText().
    /// Con 10 s útiles se dibujan los espectros, el objetivo y la propuesta.
    bool showCurves = false;
    bool canApply = false;
    MatchBlock applyBlock = MatchBlock::LearnFirst;
    MatchNotice notice = MatchNotice::None;
};

/// Bandas libres (apagadas).
int freeBandCount(const core::ParameterValues& values) noexcept;

/// Lo que hace Apply: los cambios que crean `bands` en las bandas libres, de
/// la de menor número en adelante (las que no caben, no se crean), y la
/// primera creada, que se selecciona. Cada banda: su tipo (campana o shelf),
/// frecuencia, ganancia y Q, en Stereo, sin dinámica ni tiempos automáticos,
/// y lo demás por defecto (newBandChanges).
struct MatchApply
{
    std::vector<ParameterChange> changes;
    std::optional<int> firstBand;
};
MatchApply matchApplyChanges(const core::ParameterValues& values, std::span<const dsp::FilterParams> bands);

/// `values` con `changes` aplicados.
core::ParameterValues withChanges(core::ParameterValues values, std::span<const ParameterChange> changes) noexcept;

/// El aprendizaje y las peticiones de ajuste de una instancia, mientras la
/// interfaz está abierta: mide la pista y el sidechain con las mismas piezas
/// que la sesión (dsp::SpectrumAnalyzer de 8192 puntos con su inclinación y
/// dsp::SpectrumAverager de 0,4 s), suma lo aprendido en el MatchLearner del
/// procesador cuando la referencia publica un espectro y las dos tienen señal,
/// y pide la propuesta al MatchWorker del procesador cuando cambia algo.
///
/// La interfaz le da el audio que lee de las colas ("pre" y la del sidechain)
/// y lo refresca a 30 Hz con tick(). Destruirlo (cerrar el panel o la
/// ventana) es Stop: lo aprendido se conserva. Solo en el hilo de mensajes.
class MatchController
{
public:
    /// Tras cambiar un ajuste o una banda, la propuesta se pide esto después del último cambio.
    static constexpr double kRefitDelaySeconds = 0.15;
    /// Mientras se aprende, la propuesta se vuelve a pedir cada vez que se
    /// suma esto de segundos útiles.
    static constexpr double kLearningRefitSeconds = 1.0;

    /// Todo lo de los argumentos debe vivir más que este objeto (lo tiene el procesador).
    MatchController(core::MatchState& state, core::MatchWorker& worker, core::SpectrumSession& session,
                    core::MatchSidechainTap& sidechain);
    /// Stop: deja de mirar la sesión y de escribir la cola del sidechain.
    ~MatchController();

    MatchController(const MatchController&) = delete;
    MatchController& operator=(const MatchController&) = delete;

    /// Audio mono de la pista antes del ecualizador (la cola "pre") y del sidechain (su cola).
    void pushTrack(const float* samples, int numSamples);
    void pushSidechain(const float* samples, int numSamples);

    /// Elige la referencia. Si cambia, se para y se olvida lo aprendido.
    void selectReference(const core::MatchReference& reference);
    /// Cambia los ajustes (saneados). La propuesta se vuelve a pedir.
    void setSettings(const core::MatchSettings& settings);

    /// Learn: empieza a aprender desde cero de la referencia elegida (si no
    /// hay ninguna, de la de por defecto). false si no se puede.
    bool learn(const MatchContext& context);
    /// Stop: deja de aprender y conserva lo aprendido.
    void stop();

    /// Un refresco: analiza el audio recibido, suma lo aprendido, para el
    /// aprendizaje si la referencia desaparece, y pide la propuesta si hace
    /// falta. `dtSeconds`: el tiempo desde el refresco anterior.
    void tick(double dtSeconds, const MatchContext& context);

    /// Estado del panel tras el último tick().
    MatchStatus status(const MatchContext& context) const;

    /// La propuesta, si corresponde a lo que hay ahora (no hay otra pedida ni
    /// calculándose, y nada ha cambiado desde que se pidió). Nula si no.
    std::shared_ptr<const core::MatchProposal> proposal() const;

    /// Los cambios de Apply con la propuesta actual, o nullopt si Apply está
    /// desactivado. Quien los aplica lo hace como una sola acción
    /// (PluginProcessor::applyAction).
    std::optional<MatchApply> applyChanges(const MatchContext& context) const;

    /// Espectros medios de la pista y de la referencia del procesador
    /// (MatchLearner), para dibujarlos.
    const core::MatchState& state() const noexcept { return state_; }

private:
    /// Lo que se mide de una señal, igual que en la sesión.
    struct Meter
    {
        Meter();
        /// Lee lo recibido y actualiza el promedio. true si ha llegado audio.
        bool update(double dtSeconds, double sampleRate);
        void clear();

        dsp::SpectrumAnalyzer analyzer;
        dsp::SpectrumAverager averager;
        std::vector<float> levels;
        int received = 0;
        double inaudibleSeconds;
        bool hasSignal() const noexcept;
    };

    /// Lo que determina la propuesta, salvo lo aprendido.
    struct FitKey
    {
        core::MatchSettings settings;
        std::vector<dsp::BandSettings> bands;
        double sampleRate = 0.0;
        int numChannels = 0;
        dsp::FilterAlgorithm algorithm = dsp::FilterAlgorithm::Classic;
        friend bool operator==(const FitKey&, const FitKey&) = default;
    };
    static FitKey keyOf(const core::MatchState& state, const MatchContext& context);

    void stopLearning(MatchNotice notice);
    void learnStep(double dtSeconds, const MatchContext& context);
    void scheduleFit(double dtSeconds, const MatchContext& context);
    /// true si lo pedido es lo que hay ahora.
    bool upToDate(const MatchContext& context) const;

    core::MatchState& state_;
    core::MatchWorker& worker_;
    core::SpectrumSession& session_;
    core::MatchSidechainTap& sidechainTap_;

    Meter track_;
    Meter sidechain_;
    core::SpectrumSession::Viewer viewer_;
    /// El reloj de audio de la instancia de referencia en la última suma.
    double referenceClock_ = 0.0;
    MatchNotice notice_ = MatchNotice::None;

    // Peticiones.
    std::optional<FitKey> lastKey_;     ///< La del tick anterior.
    double sinceKeyChange_ = 0.0;       ///< Segundos desde que cambió.
    std::optional<FitKey> submittedKey_;
    std::uint64_t submittedRevision_ = 0;
    double submittedSeconds_ = 0.0;
    std::uint64_t submittedId_ = 0;
};

} // namespace esceq::ui
