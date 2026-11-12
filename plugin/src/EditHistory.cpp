// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EditHistory.h"

#include "PluginProcessor.h"

namespace esceq {

EditHistory::EditHistory(PluginProcessor& processor)
    : processor_(processor)
{
    history_.reset(processor_.currentValues());
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        processor_.parameter(i).addListener(this);
}

EditHistory::~EditHistory()
{
    stopTimer();
    cancelPendingUpdate();
    for (std::size_t i = 0; i < core::kNumParameters; ++i)
        processor_.parameter(i).removeListener(this);
}

bool EditHistory::canUndo() const
{
    const juce::ScopedLock lock(lock_);
    return history_.canUndo() || isTimerRunning();
}

bool EditHistory::canRedo() const
{
    const juce::ScopedLock lock(lock_);
    return history_.canRedo();
}

void EditHistory::parameterGestureChanged(int, bool gestureIsStarting)
{
    if (gestureIsStarting)
    {
        ++activeGestures_;
        return;
    }
    if (--activeGestures_ <= 0)
    {
        activeGestures_ = 0;
        triggerAsyncUpdate();
    }
}

void EditHistory::handleAsyncUpdate()
{
    // Se espera un poco antes de registrar: los gestos seguidos (una ráfaga
    // de rueda, los parámetros de una banda nueva) forman una sola acción.
    startTimer(kCoalesceMs);
}

void EditHistory::timerCallback()
{
    stopTimer();
    if (activeGestures_.load() > 0)
        return; // hay un gesto en curso: se registrará cuando termine
    const juce::ScopedLock lock(lock_);
    history_.commit(processor_.currentValues());
}

void EditHistory::flushPending()
{
    if (!isTimerRunning() && !isUpdatePending())
        return;
    cancelPendingUpdate();
    stopTimer();
    const juce::ScopedLock lock(lock_);
    history_.commit(processor_.currentValues());
}

void EditHistory::commitNow()
{
    cancelPendingUpdate();
    stopTimer();
    const juce::ScopedLock lock(lock_);
    history_.commit(processor_.currentValues());
}

void EditHistory::undo()
{
    flushPending();
    std::optional<core::ParameterValues> values;
    {
        const juce::ScopedLock lock(lock_);
        values = history_.undo();
    }
    if (!values)
        return;
    // Los gestos que abre applyValues() no crean una acción nueva: cuando se
    // registren, el estado será el mismo que ya tiene el historial.
    processor_.applyValues(*values);
}

void EditHistory::redo()
{
    flushPending();
    std::optional<core::ParameterValues> values;
    {
        const juce::ScopedLock lock(lock_);
        values = history_.redo();
    }
    if (!values)
        return;
    // Los gestos que abre applyValues() no crean una acción nueva: cuando se
    // registren, el estado será el mismo que ya tiene el historial.
    processor_.applyValues(*values);
}

void EditHistory::reset()
{
    cancelPendingUpdate();
    stopTimer();
    activeGestures_ = 0;
    const juce::ScopedLock lock(lock_);
    history_.reset(processor_.currentValues());
}

} // namespace esceq
