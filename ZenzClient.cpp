#include "ZenzClient.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "SumireSettingsStore.h"
#include "ZenzProtocol.h"

namespace
{
bool WriteAll(HANDLE handle, const void* data, size_t byteCount)
{
    const BYTE* current = static_cast<const BYTE*>(data);
    size_t remaining = byteCount;
    while (remaining > 0)
    {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(0x7fffffff)));
        if (!WriteFile(handle, current, chunk, &written, nullptr))
        {
            return false;
        }

        current += written;
        remaining -= written;
    }

    return true;
}

bool ReadAll(HANDLE handle, void* data, size_t byteCount)
{
    BYTE* current = static_cast<BYTE*>(data);
    size_t remaining = byteCount;
    while (remaining > 0)
    {
        DWORD read = 0;
        const DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(0x7fffffff)));
        if (!ReadFile(handle, current, chunk, &read, nullptr) || read == 0)
        {
            return false;
        }

        current += read;
        remaining -= read;
    }

    return true;
}

bool WriteWideString(HANDLE handle, const std::wstring& value)
{
    if (value.empty())
    {
        return true;
    }

    return WriteAll(handle, value.data(), value.size() * sizeof(wchar_t));
}

bool ReadWideString(HANDLE handle, std::uint32_t charCount, std::wstring* value)
{
    value->clear();
    if (charCount == 0)
    {
        return true;
    }

    value->resize(charCount);
    return ReadAll(handle, value->data(), static_cast<size_t>(charCount) * sizeof(wchar_t));
}

std::filesystem::path GetCurrentModuleDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetCurrentModuleDirectory),
            &module))
    {
        return std::filesystem::current_path();
    }

    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return std::filesystem::current_path();
        }

        if (length < path.size() - 1)
        {
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }

        path.resize(path.size() * 2);
    }
}

bool IsServiceEnabledInSettings()
{
    return SumireSettingsStore::Load().zenzServiceEnabled;
}

bool SendRequestHeader(HANDLE pipe, const ZenzProtocol::RequestHeader& request)
{
    return WriteAll(pipe, &request, sizeof(request));
}
}

ZenzClient::ZenzClient(Config config)
    : _config(std::move(config))
{
}

std::wstring ZenzClient::Generate(
    const std::wstring& reading,
    const std::wstring& leftContext,
    DWORD timeoutMs,
    const std::function<bool()>& shouldCancel,
    const std::function<void(const std::wstring&)>& onPartial) const
{
    const std::wstring modelPath = ResolveModelPath();
    if (!_config.enabled || !IsServiceEnabledInSettings() || reading.empty() || modelPath.empty())
    {
        return L"";
    }

    if (shouldCancel && shouldCancel())
    {
        return L"";
    }

    if (!EnsureServiceRunning(timeoutMs))
    {
        return L"";
    }

    if (shouldCancel && shouldCancel())
    {
        return L"";
    }

    HANDLE pipe = CreateFileW(
        ZenzProtocol::kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return L"";
    }

    DWORD readMode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &readMode, nullptr, nullptr);

    ZenzProtocol::RequestHeader request;
    request.readingChars = static_cast<std::uint32_t>(reading.size());
    request.leftContextChars = static_cast<std::uint32_t>(leftContext.size());
    request.modelPathChars = static_cast<std::uint32_t>(modelPath.size());
    request.modelRepoChars = static_cast<std::uint32_t>(_config.modelRepo.size());
    request.timeoutMs = timeoutMs;

    bool ok = WriteAll(pipe, &request, sizeof(request)) &&
        WriteWideString(pipe, reading) &&
        WriteWideString(pipe, leftContext) &&
        WriteWideString(pipe, modelPath) &&
        WriteWideString(pipe, _config.modelRepo);
    if (ok)
    {
        FlushFileBuffers(pipe);
    }

    std::wstring generated;
    if (ok)
    {
        for (;;)
        {
            if (shouldCancel && shouldCancel())
            {
                ok = false;
                break;
            }

            ZenzProtocol::ResponseHeader response;
            if (!ReadAll(pipe, &response, sizeof(response)) || response.version != ZenzProtocol::kVersion)
            {
                ok = false;
                break;
            }

            std::wstring chunk;
            if (!ReadWideString(pipe, response.resultChars, &chunk))
            {
                ok = false;
                break;
            }

            if (response.status == static_cast<std::uint32_t>(ZenzProtocol::Status::Partial))
            {
                generated = std::move(chunk);
                if (onPartial)
                {
                    onPartial(generated);
                }
                continue;
            }

            ok = response.status == static_cast<std::uint32_t>(ZenzProtocol::Status::Ok);
            if (ok)
            {
                generated = std::move(chunk);
            }
            break;
        }
    }

    CloseHandle(pipe);
    return ok ? generated : L"";
}

void ZenzClient::WarmUpAsync() const
{
    const std::wstring modelPath = ResolveModelPath();
    if (!_config.enabled || !IsServiceEnabledInSettings() || modelPath.empty())
    {
        return;
    }

    static std::mutex warmUpMutex;
    static std::unordered_set<std::wstring> warmingModels;
    {
        std::lock_guard<std::mutex> lock(warmUpMutex);
        if (warmingModels.find(modelPath) != warmingModels.end())
        {
            return;
        }
        warmingModels.insert(modelPath);
    }

    const Config config = _config;
    std::thread([modelPath, config]()
    {
        ZenzClient client(config);
        bool keepMarked = true;
        if (client.EnsureServiceRunning(1500))
        {
            HANDLE pipe = CreateFileW(
                ZenzProtocol::kPipeName,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                ZenzProtocol::RequestHeader request;
                request.command = static_cast<std::uint32_t>(ZenzProtocol::Command::WarmUp);
                request.modelPathChars = static_cast<std::uint32_t>(modelPath.size());
                request.modelRepoChars = static_cast<std::uint32_t>(config.modelRepo.size());

                const bool ok = SendRequestHeader(pipe, request) &&
                    WriteWideString(pipe, std::wstring()) &&
                    WriteWideString(pipe, std::wstring()) &&
                    WriteWideString(pipe, modelPath) &&
                    WriteWideString(pipe, config.modelRepo);
                if (ok)
                {
                    FlushFileBuffers(pipe);
                    ZenzProtocol::ResponseHeader response;
                    keepMarked =
                        ReadAll(pipe, &response, sizeof(response)) &&
                        response.version == ZenzProtocol::kVersion &&
                        response.status == static_cast<std::uint32_t>(ZenzProtocol::Status::Ok);
                }
                else
                {
                    keepMarked = false;
                }

                CloseHandle(pipe);
            }
            else
            {
                keepMarked = false;
            }
        }
        else
        {
            keepMarked = false;
        }

        if (!keepMarked)
        {
            std::lock_guard<std::mutex> lock(warmUpMutex);
            warmingModels.erase(modelPath);
        }
    }).detach();
}

bool ZenzClient::IsEnabled() const
{
    return _config.enabled && IsServiceEnabledInSettings();
}

bool ZenzClient::EnsureServiceRunning(DWORD timeoutMs) const
{
    if (WaitNamedPipeW(ZenzProtocol::kPipeName, timeoutMs) != 0)
    {
        return true;
    }

    const std::wstring servicePath = GetServiceExecutablePath();
    if (servicePath.empty())
    {
        return false;
    }

    static std::mutex startMutex;
    std::lock_guard<std::mutex> lock(startMutex);
    if (WaitNamedPipeW(ZenzProtocol::kPipeName, 50) != 0)
    {
        return true;
    }

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};

    std::wstring commandLine = L"\"";
    commandLine += servicePath;
    commandLine += L"\"";

    if (!CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo))
    {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return WaitNamedPipeW(ZenzProtocol::kPipeName, timeoutMs) != 0;
}

std::wstring ZenzClient::GetServiceExecutablePath() const
{
    const std::filesystem::path moduleDirectory = GetCurrentModuleDirectory();
    const std::filesystem::path candidate = moduleDirectory / L"SumireZenzService.exe";
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec)
    {
        return candidate.wstring();
    }

    return L"";
}

std::wstring ZenzClient::ResolveModelPath() const
{
    if (!_config.modelPath.empty())
    {
        return _config.modelPath;
    }

    const std::filesystem::path moduleDirectory = GetCurrentModuleDirectory();
    if (_config.modelPreset == L"small")
    {
        return (moduleDirectory / L"models" / L"zenz-v3.1-small-gguf" / L"ggml-model-Q5_K_M.gguf").wstring();
    }

    if (_config.modelPreset == L"custom")
    {
        return L"";
    }

    return (moduleDirectory / L"models" / L"zenz-v3.1-xsmall-gguf" / L"ggml-model-Q5_K_M.gguf").wstring();
}
