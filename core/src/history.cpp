// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/history.h"

#include <algorithm>

namespace esceq::core {

namespace {

/// Los valores se comparan ya saneados: dos estados que suenan igual son el mismo.
ParameterValues sanitized(ParameterValues values) noexcept
{
    for (std::size_t i = 0; i < kNumParameters; ++i)
        values[i] = sanitize(parameters()[i], values[i]);
    return values;
}

} // namespace

UndoHistory::UndoHistory(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1))
{
    states_.reserve(capacity_ + 1);
    states_.push_back(defaultValues());
}

void UndoHistory::reset(const ParameterValues& current)
{
    states_.clear();
    states_.push_back(sanitized(current));
    index_ = 0;
}

bool UndoHistory::commit(const ParameterValues& current)
{
    const ParameterValues next = sanitized(current);
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

std::optional<ParameterValues> UndoHistory::undo()
{
    if (!canUndo())
        return std::nullopt;
    return states_[--index_];
}

std::optional<ParameterValues> UndoHistory::redo()
{
    if (!canRedo())
        return std::nullopt;
    return states_[++index_];
}

} // namespace esceq::core
