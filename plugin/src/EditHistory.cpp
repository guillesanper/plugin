// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EditHistory.h"

#include "PluginProcessor.h"

namespace esceq {

EditHistory::EditHistory(PluginProcessor& processor)
    : processor_(processor)
{
    history_.reset(processor_.currentEditState());
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

void EditHistory::parameterGestureChanged(int parameterIndex, bool gestureIsStarting)
{
    // Los gestos de deshacer y rehacer no son acciones del usuario.
    if (applying_.load())
        return;
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(core::kNumParameters))
        return;

    bool allClosed = false;
    {
        const juce::ScopedLock lock(lock_);
        bool& open = openGestures_[static_cast<std::size_t>(parameterIndex)];
        if (gestureIsStarting == open)
            return; // un principio repetido o un final sin principio no cambian nada
        open = gestureIsStarting;
        numOpenGestures_ += gestureIsStarting ? 1 : -1;
        allClosed = numOpenGestures_ == 0;
    }
    if (allClosed)
        triggerAsyncUpdate();
}

void EditHistory::abandonOpenGestures()
{
    {
        const juce::ScopedLock lock(lock_);
        if (numOpenGestures_ == 0)
            return;
        openGestures_.fill(false);
        numOpenGestures_ = 0;
    }
    triggerAsyncUpdate();
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
    const juce::ScopedLock lock(lock_);
    if (numOpenGestures_ > 0)
        return; // hay un gesto en curso: se registrará cuando termine
    history_.commit(processor_.currentEditState());
}

void EditHistory::flushPending()
{
    if (!isTimerRunning() && !isUpdatePending())
        return;
    cancelPendingUpdate();
    stopTimer();
    const juce::ScopedLock lock(lock_);
    history_.commit(processor_.currentEditState());
}

void EditHistory::commitNow()
{
    cancelPendingUpdate();
    stopTimer();
    const juce::ScopedLock lock(lock_);
    history_.commit(processor_.currentEditState());
}

void EditHistory::apply(const core::EditState& state)
{
    applying_.store(true);
    processor_.applyEditState(state);
    applying_.store(false);
}

void EditHistory::undo()
{
    flushPending();
    std::optional<core::EditState> state;
    {
        const juce::ScopedLock lock(lock_);
        state = history_.undo();
    }
    if (state)
        apply(*state);
}

void EditHistory::redo()
{
    flushPending();
    std::optional<core::EditState> state;
    {
        const juce::ScopedLock lock(lock_);
        state = history_.redo();
    }
    if (state)
        apply(*state);
}

void EditHistory::reset()
{
    cancelPendingUpdate();
    stopTimer();
    const juce::ScopedLock lock(lock_);
    openGestures_.fill(false);
    numOpenGestures_ = 0;
    history_.reset(processor_.currentEditState());
}

} // namespace esceq
