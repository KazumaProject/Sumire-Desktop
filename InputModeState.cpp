#include "InputModeState.h"

InputModeState::InputModeState()
{
    _userInputMode = InputMode::Hiragana;
    _inputScopeOverride = InputMode::Hiragana;
    _effectiveInputMode = InputMode::Hiragana;
    _hasInputScopeOverride = false;
}

void InputModeState::SetUserInputMode(InputMode mode)
{
    _userInputMode = mode;
    RecomputeEffectiveMode();
}

InputMode InputModeState::GetUserInputMode() const
{
    return _userInputMode;
}

void InputModeState::SetInputScopeOverride(InputMode mode)
{
    _inputScopeOverride = mode;
    _hasInputScopeOverride = true;
    RecomputeEffectiveMode();
}

void InputModeState::ClearInputScopeOverride()
{
    _hasInputScopeOverride = false;
    RecomputeEffectiveMode();
}

bool InputModeState::HasInputScopeOverride() const
{
    return _hasInputScopeOverride;
}

InputMode InputModeState::GetInputScopeOverride() const
{
    return _inputScopeOverride;
}

InputMode InputModeState::GetEffectiveInputMode() const
{
    return _effectiveInputMode;
}

void InputModeState::RecomputeEffectiveMode()
{
    _effectiveInputMode = _hasInputScopeOverride ? _inputScopeOverride : _userInputMode;
}
