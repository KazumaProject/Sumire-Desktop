#pragma once

#include <Windows.h>

#include <functional>
#include <memory>
#include <string>

class ZenzClient
{
public:
    struct Config
    {
        bool enabled = true;
        std::wstring modelPreset;
        std::wstring modelPath;
        std::wstring modelRepo;
    };

    explicit ZenzClient(Config config);

    std::wstring Generate(
        const std::wstring& reading,
        DWORD timeoutMs,
        const std::function<bool()>& shouldCancel) const;

    bool IsEnabled() const;

private:
    bool EnsureServiceRunning(DWORD timeoutMs) const;
    std::wstring GetServiceExecutablePath() const;
    std::wstring ResolveModelPath() const;

    Config _config;
};
