// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/history.h"

#include <algorithm>

namespace esceq::core {

namespace {

/// Los valores se comparan ya saneados: dos estados que suenan igual son el mismo.
EditState sanitized(EditState state) noexcept
{
    for (std::size_t i = 0; i < kNumParameters; ++i)
        state.parameters[i] = sanitize(parameters()[i], state.parameters[i]);
    return state;
}

} // namespace

UndoHistory::UndoHistory(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1))
{
    states_.reserve(capacity_ + 1);
    states_.push_back(EditState{});
}

void UndoHistory::reset(const EditState& current)
{
    states_.clear();
    states_.push_back(sanitized(current));
    index_ = 0;
}

bool UndoHistory::commit(const EditState& current)
{
    const EditState next = sanitized(current);
    if (next == states_[index_])
        return false;

    states_.resize(index_ + 1); // lo que se podía rehacer deja de existir
    states_.push_back(next);
    ++index_;

    if (states_.size() > capacity_ + 1)
    {
        states_.erase(states_.begin());
        --index_;
    }
    return true;
}

std::optional<EditState> UndoHistory::undo()
{
    if (!canUndo())
        return std::nullopt;
    return states_[--index_];
}

std::optional<EditState> UndoHistory::redo()
{
    if (!canRedo())
        return std::nullopt;
    return states_[++index_];
}

} // namespace esceq::core
