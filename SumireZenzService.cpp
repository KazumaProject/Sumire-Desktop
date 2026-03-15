#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "third_party/llama.cpp/llama.h"
#include "ZenzProtocol.h"

namespace
{
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

bool WriteWideString(HANDLE handle, const std::wstring& value)
{
    if (value.empty())
    {
        return true;
    }

    return WriteAll(handle, value.data(), value.size() * sizeof(wchar_t));
}

std::wstring TrimWide(const std::wstring& value)
{
    size_t start = 0;
    while (start < value.size() && iswspace(value[start]) != 0)
    {
        ++start;
    }

    size_t end = value.size();
    while (end > start && iswspace(value[end - 1]) != 0)
    {
        --end;
    }

    return value.substr(start, end - start);
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return std::string();
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return std::string();
    }

    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return std::wstring();
    }

    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::wstring();
    }

    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), wide.data(), size);
    return wide;
}

std::string PreprocessText(std::string text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text)
    {
        if (ch == ' ')
        {
            out.append("\xE3\x80\x80");
        }
        else if (ch != '\r' && ch != '\n')
        {
            out.push_back(ch);
        }
    }

    return out;
}

std::string HiraganaToKatakanaUtf8(const std::string& utf8)
{
    std::string out;
    out.reserve(utf8.size());

    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8.data());
    const size_t n = utf8.size();
    size_t i = 0;

    auto appendUtf8 = [&](uint32_t cp)
    {
        if (cp <= 0x7F)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };

    auto decodeOne = [&](uint32_t& cp, size_t& adv) -> bool
    {
        adv = 0;
        if (i >= n)
        {
            return false;
        }

        const unsigned char c0 = p[i];
        if (c0 < 0x80)
        {
            cp = c0;
            adv = 1;
            return true;
        }
        if ((c0 & 0xE0) == 0xC0 && i + 1 < n)
        {
            cp = ((c0 & 0x1F) << 6) | (p[i + 1] & 0x3F);
            adv = 2;
            return true;
        }
        if ((c0 & 0xF0) == 0xE0 && i + 2 < n)
        {
            cp = ((c0 & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            adv = 3;
            return true;
        }
        if ((c0 & 0xF8) == 0xF0 && i + 3 < n)
        {
            cp = ((c0 & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F);
            adv = 4;
            return true;
        }
        return false;
    };

    while (i < n)
    {
        uint32_t cp = 0;
        size_t adv = 0;
        if (!decodeOne(cp, adv) || adv == 0)
        {
            out.push_back(static_cast<char>(p[i]));
            ++i;
            continue;
        }

        if (0x3041 <= cp && cp <= 0x3096)
        {
            cp += 0x60;
        }

        appendUtf8(cp);
        i += adv;
    }

    return out;
}

std::string BuildPrompt(const std::string& inputHiraUtf8)
{
    const std::string inputTag = "\xEE\xB8\x80";
    const std::string outputTag = "\xEE\xB8\x81";
    return PreprocessText(inputTag + HiraganaToKatakanaUtf8(inputHiraUtf8) + outputTag);
}

std::vector<llama_token> Tokenize(const llama_model* model, const std::string& text, bool addSpecial)
{
    int32_t capacity = static_cast<int32_t>(text.size()) + 32;
    if (capacity < 64)
    {
        capacity = 64;
    }

    std::vector<llama_token> tokens(static_cast<size_t>(capacity));
    int32_t count = llama_tokenize(
        model,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        tokens.data(),
        capacity,
        addSpecial,
        false);
    if (count < 0)
    {
        tokens.resize(static_cast<size_t>(-count));
        count = llama_tokenize(
            model,
            text.c_str(),
            static_cast<int32_t>(text.size()),
            tokens.data(),
            static_cast<int32_t>(tokens.size()),
            addSpecial,
            false);
    }

    if (count <= 0)
    {
        return std::vector<llama_token>();
    }

    tokens.resize(static_cast<size_t>(count));
    return tokens;
}

std::string TokenToPiece(const llama_model* model, llama_token token)
{
    char buffer[16] = {};
    int32_t count = llama_token_to_piece(model, token, buffer, static_cast<int32_t>(sizeof(buffer)));
    if (count < 0)
    {
        std::vector<char> larger(static_cast<size_t>(-count));
        count = llama_token_to_piece(model, token, larger.data(), static_cast<int32_t>(larger.size()));
        if (count <= 0)
        {
            return std::string();
        }

        return std::string(larger.data(), larger.data() + count);
    }

    return std::string(buffer, buffer + count);
}

bool DecodeTokens(llama_context* context, std::vector<llama_token>& tokens, int* nPast)
{
    if (context == nullptr || nPast == nullptr || tokens.empty())
    {
        return false;
    }

    llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()), *nPast, 0);
    const int result = llama_decode(context, batch);
    if (result < 0)
    {
        return false;
    }

    *nPast += static_cast<int>(tokens.size());
    return true;
}

struct ModelRuntime
{
    std::wstring currentModelPath;
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    std::mutex mutex;

    ~ModelRuntime()
    {
        Reset();
        llama_backend_free();
    }

    void Reset()
    {
        if (context != nullptr)
        {
            llama_free(context);
            context = nullptr;
        }
        if (model != nullptr)
        {
            llama_free_model(model);
            model = nullptr;
        }
        currentModelPath.clear();
    }

    bool EnsureLoaded(const std::wstring& modelPath)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (model != nullptr && context != nullptr && currentModelPath == modelPath)
        {
            return true;
        }

        Reset();

        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(modelPath), ec) || ec)
        {
            return false;
        }

        static std::once_flag backendInit;
        std::call_once(backendInit, []()
        {
            llama_backend_init();
        });

        const std::string modelPathUtf8 = WideToUtf8(modelPath);
        llama_model_params modelParams = llama_model_default_params();
        model = llama_load_model_from_file(modelPathUtf8.c_str(), modelParams);
        if (model == nullptr)
        {
            return false;
        }

        llama_context_params contextParams = llama_context_default_params();
        contextParams.n_ctx = 512;
        contextParams.n_batch = 512;
        contextParams.n_ubatch = 512;
        context = llama_new_context_with_model(model, contextParams);
        if (context == nullptr)
        {
            Reset();
            return false;
        }

        const DWORD threadCount = (std::max)(static_cast<DWORD>(1), GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        llama_set_n_threads(context, threadCount, threadCount);
        currentModelPath = modelPath;
        return true;
    }

    std::wstring Generate(const std::wstring& modelPath, const std::wstring& reading)
    {
        if (!EnsureLoaded(modelPath))
        {
            return L"";
        }

        std::lock_guard<std::mutex> lock(mutex);
        llama_kv_cache_clear(context);

        const std::string prompt = BuildPrompt(WideToUtf8(reading));
        std::vector<llama_token> promptTokens = Tokenize(model, prompt, true);
        if (promptTokens.empty())
        {
            return L"";
        }

        int nPast = 0;
        if (!DecodeTokens(context, promptTokens, &nPast))
        {
            return L"";
        }

        const llama_token eos = llama_token_eos(model);
        const int32_t nVocab = llama_n_vocab(model);
        const std::unordered_set<std::string> stopPieces = {"、", "。", "！", "？"};

        std::string generated;
        for (int step = 0; step < 24; ++step)
        {
            float* logits = llama_get_logits_ith(context, -1);
            if (logits == nullptr)
            {
                break;
            }

            int32_t bestIndex = 0;
            float bestLogit = logits[0];
            for (int32_t index = 1; index < nVocab; ++index)
            {
                if (logits[index] > bestLogit)
                {
                    bestLogit = logits[index];
                    bestIndex = index;
                }
            }

            const llama_token token = static_cast<llama_token>(bestIndex);
            if (token == eos)
            {
                break;
            }

            const std::string piece = TokenToPiece(model, token);
            if (!generated.empty() && stopPieces.find(piece) != stopPieces.end())
            {
                break;
            }

            generated += piece;

            std::vector<llama_token> stepTokens(1, token);
            if (!DecodeTokens(context, stepTokens, &nPast))
            {
                break;
            }
        }

        return TrimWide(Utf8ToWide(generated));
    }
};

ModelRuntime& GetRuntime()
{
    static ModelRuntime runtime;
    return runtime;
}

bool HandleClient(HANDLE pipe)
{
    ZenzProtocol::RequestHeader request = {};
    if (!ReadAll(pipe, &request, sizeof(request)) || request.version != ZenzProtocol::kVersion)
    {
        return false;
    }

    std::wstring reading;
    std::wstring modelPath;
    std::wstring modelRepo;
    if (!ReadWideString(pipe, request.readingChars, &reading) ||
        !ReadWideString(pipe, request.modelPathChars, &modelPath) ||
        !ReadWideString(pipe, request.modelRepoChars, &modelRepo))
    {
        return false;
    }

    UNREFERENCED_PARAMETER(modelRepo);

    const std::wstring generated = GetRuntime().Generate(TrimWide(modelPath), reading);

    ZenzProtocol::ResponseHeader response;
    response.status = static_cast<std::uint32_t>(
        generated.empty() ? ZenzProtocol::Status::Error : ZenzProtocol::Status::Ok);
    response.resultChars = static_cast<std::uint32_t>(generated.size());
    return WriteAll(pipe, &response, sizeof(response)) && WriteWideString(pipe, generated);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    for (;;)
    {
        HANDLE pipe = CreateNamedPipeW(
            ZenzProtocol::kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            32768,
            32768,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return 1;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected)
        {
            HandleClient(pipe);
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}
