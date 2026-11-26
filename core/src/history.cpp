// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/history.h"

#include <algorithm>
#include <utility>

namespace esceq::core {

EditAction EditAction::inverse() const
{
    EditAction result;
    result.parameters.reserve(parameters.size());
    for (const auto& change : parameters)
        result.parameters.push_back({change.index, change.after, change.before});
    result.algorithmBefore = algorithmAfter;
    result.algorithmAfter = algorithmBefore;
    result.macrosBefore = macrosAfter;
    result.macrosAfter = macrosBefore;
    return result;
}

void PendingAction::touch(std::size_t index, float value) noexcept
{
    if (index >= kNumParameters || touched_[index])
        return;
    touched_[index] = true;
    before_[index] = value;
    any_ = true;
}

void PendingAction::touchAlgorithm(dsp::FilterAlgorithm current) noexcept
{
    if (!algorithmBefore_)
        algorithmBefore_ = current;
    any_ = true;
}

void PendingAction::touchMacros(const MacroAssignments& current) noexcept
{
    if (!macrosBefore_)
        macrosBefore_ = current;
    any_ = true;
}

EditAction PendingAction::finish(const EditState& current)
{
    EditAction action;
    for (std::size_t i = 0; i < kNumParameters; ++i)
    {
        if (!touched_[i])
            continue;
        // Saneados: dos valores que suenan igual son el mismo.
        const float before = sanitize(parameters()[i], before_[i]);
        const float after = sanitize(parameters()[i], current.parameters[i]);
        if (before < after || before > after)
            action.parameters.push_back({i, before, after});
    }
    if (algorithmBefore_ && *algorithmBefore_ != current.filterAlgorithm)
    {
        action.algorithmBefore = algorithmBefore_;
        action.algorithmAfter = current.filterAlgorithm;
    }
    if (macrosBefore_ && !(*macrosBefore_ == current.macros))
    {
        action.macrosBefore = macrosBefore_;
        action.macrosAfter = current.macros;
    }
    clear();
    return action;
}

void PendingAction::clear() noexcept
{
    touched_.fill(false);
    algorithmBefore_.reset();
    macrosBefore_.reset();
    any_ = false;
}

UndoHistory::UndoHistory(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1))
{
}

void UndoHistory::clear()
{
    actions_.clear();
    index_ = 0;
}

bool UndoHistory::commit(EditAction action)
{
    if (action.empty())
        return false;

    actions_.resize(index_); // lo que se podía rehacer deja de existir
    actions_.push_back(std::move(action));
    ++index_;

    if (actions_.size() > capacity_)
    {
        actions_.erase(actions_.begin());
        --index_;
    }
    return true;
}

std::optional<EditAction> UndoHistory::undo()
{
    if (!canUndo())
        return std::nullopt;
    return actions_[--index_].inverse();
}

std::optional<EditAction> UndoHistory::redo()
{
    if (!canRedo())
        return std::nullopt;
    return actions_[index_++];
}

} // namespace esceq::core
