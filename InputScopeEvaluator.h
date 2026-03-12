#pragma once

#include <Windows.h>
#include <InputScope.h>
#include <msctf.h>

#include "InputModeState.h"

class InputScopeEvaluator
{
public:
    bool Evaluate(ITfContext* pContext, TfEditCookie ec, InputMode* pMode) const;

private:
    bool EvaluateInputScopeValue(InputScope inputScope, InputMode* pMode) const;
};
