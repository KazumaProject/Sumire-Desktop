#pragma once

enum class InputMode
{
    DirectInput = 0,
    Hiragana = 1,
    FullwidthAlphanumeric = 2,
    HalfwidthKatakana = 3,
    FullwidthKatakana = 4,
};

class InputModeState
{
public:
    InputModeState();

    void SetUserInputMode(InputMode mode);
    InputMode GetUserInputMode() const;

    void SetInputScopeOverride(InputMode mode);
    void ClearInputScopeOverride();
    bool HasInputScopeOverride() const;
    InputMode GetInputScopeOverride() const;

    InputMode GetEffectiveInputMode() const;

private:
    void RecomputeEffectiveMode();

    InputMode _userInputMode;
    InputMode _inputScopeOverride;
    InputMode _effectiveInputMode;
    bool _hasInputScopeOverride;
};
