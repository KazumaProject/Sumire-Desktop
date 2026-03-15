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
        const std::wstring& leftContext,
        DWORD timeoutMs,
        const std::function<bool()>& shouldCancel,
        const std::function<void(const std::wstring&)>& onPartial = std::function<void(const std::wstring&)>()) const;
    void WarmUpAsync() const;

    bool IsEnabled() const;

private:
    bool EnsureServiceRunning(DWORD timeoutMs) const;
    std::wstring GetServiceExecutablePath() const;
    std::wstring ResolveModelPath() const;

    Config _config;
};
