#include <initguid.h>

#include "InputScopeEvaluator.h"

bool InputScopeEvaluator::Evaluate(ITfContext* pContext, TfEditCookie ec, InputMode* pMode) const
{
    if (pContext == NULL || pMode == NULL)
    {
        return false;
    }

    ITfReadOnlyProperty* pInputScopeProperty = NULL;
    if (pContext->GetAppProperty(GUID_PROP_INPUTSCOPE, &pInputScopeProperty) != S_OK || pInputScopeProperty == NULL)
    {
        return false;
    }

    TF_SELECTION selection;
    ULONG cFetched = 0;
    bool hasOverride = false;

    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &cFetched) == S_OK && cFetched == 1)
    {
        VARIANT var;
        VariantInit(&var);

        if (pInputScopeProperty->GetValue(ec, selection.range, &var) == S_OK)
        {
            if (var.vt == VT_UNKNOWN && var.punkVal != NULL)
            {
                ITfInputScope* pInputScope = NULL;
                if (var.punkVal->QueryInterface(IID_ITfInputScope, (void**)&pInputScope) == S_OK && pInputScope != NULL)
                {
                    InputScope* pScopes = NULL;
                    UINT count = 0;

                    if (pInputScope->GetInputScopes(&pScopes, &count) == S_OK && pScopes != NULL)
                    {
                        for (UINT i = 0; i < count; ++i)
                        {
                            if (EvaluateInputScopeValue(pScopes[i], pMode))
                            {
                                hasOverride = true;
                                break;
                            }
                        }

                        CoTaskMemFree(pScopes);
                    }

                    pInputScope->Release();
                }
            }

            VariantClear(&var);
        }

        selection.range->Release();
    }

    pInputScopeProperty->Release();
    return hasOverride;
}

bool InputScopeEvaluator::EvaluateInputScopeValue(InputScope inputScope, InputMode* pMode) const
{
    switch (inputScope)
    {
    case IS_URL:
    case IS_EMAIL_USERNAME:
    case IS_EMAIL_SMTPEMAILADDRESS:
    case IS_EMAILNAME_OR_ADDRESS:
    case IS_PASSWORD:
    case IS_NUMERIC_PASSWORD:
    case IS_NUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN_SET:
    case IS_DIGITS:
    case IS_NUMBER:
    case IS_ALPHANUMERIC_HALFWIDTH:
        *pMode = InputMode::DirectInput;
        return true;

    case IS_NUMBER_FULLWIDTH:
    case IS_ALPHANUMERIC_FULLWIDTH:
        *pMode = InputMode::FullwidthAlphanumeric;
        return true;

    case IS_HIRAGANA:
        *pMode = InputMode::Hiragana;
        return true;

    case IS_KATAKANA_HALFWIDTH:
        *pMode = InputMode::HalfwidthKatakana;
        return true;

    case IS_KATAKANA_FULLWIDTH:
        *pMode = InputMode::FullwidthKatakana;
        return true;

    default:
        return false;
    }
}
