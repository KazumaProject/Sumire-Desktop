#include <Windows.h>
#include <urlmon.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
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

bool WriteResponse(HANDLE pipe, ZenzProtocol::Status status, const std::wstring& value)
{
    ZenzProtocol::ResponseHeader response;
    response.status = static_cast<std::uint32_t>(status);
    response.resultChars = static_cast<std::uint32_t>(value.size());
    return WriteAll(pipe, &response, sizeof(response)) && WriteWideString(pipe, value);
}

std::atomic_uint64_t g_latestGenerateRequestId = 0;

bool IsLatestGenerateRequest(std::uint64_t requestId)
{
    return requestId == g_latestGenerateRequestId.load(std::memory_order_acquire);
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

bool StartsWithInsensitive(const std::wstring& value, const std::wstring& prefix)
{
    return value.size() >= prefix.size() &&
        CompareStringOrdinal(
            value.c_str(),
            static_cast<int>(prefix.size()),
            prefix.c_str(),
            static_cast<int>(prefix.size()),
            TRUE) == CSTR_EQUAL;
}

bool ContainsInsensitive(const std::wstring& value, const std::wstring& fragment)
{
    if (fragment.empty() || value.size() < fragment.size())
    {
        return false;
    }

    for (size_t index = 0; index + fragment.size() <= value.size(); ++index)
    {
        if (CompareStringOrdinal(
                value.c_str() + index,
                static_cast<int>(fragment.size()),
                fragment.c_str(),
                static_cast<int>(fragment.size()),
                TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }

    return false;
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

std::wstring Utf8ToWideLossy(const std::string& value)
{
    if (value.empty())
    {
        return std::wstring();
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::wstring();
    }

    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), size);
    return wide;
}

std::wstring UrlEncodePathComponent(const std::wstring& value)
{
    static constexpr char kHex[] = "0123456789ABCDEF";

    const std::string utf8 = WideToUtf8(value);
    std::string encoded;
    encoded.reserve(utf8.size() * 3);
    for (unsigned char byte : utf8)
    {
        if ((byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '-' ||
            byte == '_' ||
            byte == '.' ||
            byte == '~')
        {
            encoded.push_back(static_cast<char>(byte));
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(kHex[(byte >> 4) & 0x0F]);
            encoded.push_back(kHex[byte & 0x0F]);
        }
    }

    return Utf8ToWideLossy(encoded);
}

void ReplaceFirstInsensitive(std::wstring* value, const std::wstring& from, const std::wstring& to)
{
    if (value == nullptr || from.empty() || value->size() < from.size())
    {
        return;
    }

    for (size_t index = 0; index + from.size() <= value->size(); ++index)
    {
        if (CompareStringOrdinal(
                value->c_str() + index,
                static_cast<int>(from.size()),
                from.c_str(),
                static_cast<int>(from.size()),
                TRUE) == CSTR_EQUAL)
        {
            value->replace(index, from.size(), to);
            return;
        }
    }
}

std::wstring ResolveModelDownloadUrl(const std::wstring& modelPath, const std::wstring& modelRepo)
{
    std::wstring url = TrimWide(modelRepo);
    if (url.empty())
    {
        return L"";
    }

    const std::wstring fileName = std::filesystem::path(modelPath).filename().wstring();
    if (fileName.empty())
    {
        return L"";
    }

    while (!url.empty() && url.back() == L'/')
    {
        url.pop_back();
    }

    ReplaceFirstInsensitive(&url, L"/blob/", L"/resolve/");
    ReplaceFirstInsensitive(&url, L"/tree/", L"/resolve/");

    if (ContainsInsensitive(url, L".gguf"))
    {
        return url;
    }

    if (ContainsInsensitive(url, L"/resolve/"))
    {
        return url + L"/" + UrlEncodePathComponent(fileName) + L"?download=true";
    }

    if (StartsWithInsensitive(url, L"http://") || StartsWithInsensitive(url, L"https://"))
    {
        return url + L"/resolve/main/" + UrlEncodePathComponent(fileName) + L"?download=true";
    }

    return L"";
}

bool DownloadFileToPath(const std::wstring& url, const std::filesystem::path& destination)
{
    if (url.empty() || destination.empty())
    {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    const std::filesystem::path tempPath = destination.wstring() + L".download";
    std::filesystem::remove(tempPath, ec);
    ec.clear();

    const HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), tempPath.c_str(), 0, nullptr);
    if (FAILED(hr))
    {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    return std::filesystem::exists(destination, ec) && !ec;
}

bool EnsureModelFileExists(const std::wstring& modelPath, const std::wstring& modelRepo)
{
    std::error_code ec;
    const std::filesystem::path path(modelPath);
    if (std::filesystem::exists(path, ec) && !ec)
    {
        return true;
    }

    const std::wstring downloadUrl = ResolveModelDownloadUrl(modelPath, modelRepo);
    if (downloadUrl.empty())
    {
        return false;
    }

    return DownloadFileToPath(downloadUrl, path);
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

std::string BuildPrompt(const std::string& leftContextUtf8, const std::string& inputHiraUtf8)
{
    const std::string inputTag = "\xEE\xB8\x80";
    const std::string outputTag = "\xEE\xB8\x81";
    const std::string contextTag = "\xEE\xB8\x82";

    std::string prompt;
    if (!leftContextUtf8.empty())
    {
        prompt = contextTag + leftContextUtf8 + inputTag + HiraganaToKatakanaUtf8(inputHiraUtf8) + outputTag;
    }
    else
    {
        prompt = inputTag + HiraganaToKatakanaUtf8(inputHiraUtf8) + outputTag;
    }

    return PreprocessText(prompt);
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

    bool EnsureLoaded(const std::wstring& modelPath, const std::wstring&)
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

    std::wstring Generate(
        const std::wstring& modelPath,
        const std::wstring& modelRepo,
        const std::wstring& leftContext,
        const std::wstring& reading,
        std::uint64_t requestId,
        const std::function<bool(const std::wstring&)>& onPartial)
    {
        if (!IsLatestGenerateRequest(requestId))
        {
            return L"";
        }

        if (!EnsureLoaded(modelPath, modelRepo))
        {
            return L"";
        }

        std::lock_guard<std::mutex> lock(mutex);
        if (!IsLatestGenerateRequest(requestId))
        {
            return L"";
        }
        llama_kv_cache_clear(context);

        const std::string prompt = BuildPrompt(WideToUtf8(leftContext), WideToUtf8(reading));
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
        if (!IsLatestGenerateRequest(requestId))
        {
            return L"";
        }

        const llama_token eos = llama_token_eos(model);
        const int32_t nVocab = llama_n_vocab(model);
        const std::unordered_set<std::string> stopPieces = {"、", "。", "！", "？"};

        std::string generated;
        for (int step = 0; step < 24; ++step)
        {
            if (!IsLatestGenerateRequest(requestId))
            {
                return L"";
            }

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
            if (onPartial)
            {
                const std::wstring partial = TrimWide(Utf8ToWide(generated));
                if (!partial.empty() && !onPartial(partial))
                {
                    return L"";
                }
            }

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
    std::wstring leftContext;
    std::wstring modelPath;
    std::wstring modelRepo;
    if (!ReadWideString(pipe, request.readingChars, &reading) ||
        !ReadWideString(pipe, request.leftContextChars, &leftContext) ||
        !ReadWideString(pipe, request.modelPathChars, &modelPath) ||
        !ReadWideString(pipe, request.modelRepoChars, &modelRepo))
    {
        return false;
    }

    const std::wstring trimmedModelPath = TrimWide(modelPath);
    const std::wstring trimmedModelRepo = TrimWide(modelRepo);

    if (request.command == static_cast<std::uint32_t>(ZenzProtocol::Command::WarmUp))
    {
        return WriteResponse(
            pipe,
            GetRuntime().EnsureLoaded(trimmedModelPath, trimmedModelRepo) ? ZenzProtocol::Status::Ok : ZenzProtocol::Status::Error,
            std::wstring());
    }

    const std::uint64_t requestId = g_latestGenerateRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;
    const std::wstring generated = GetRuntime().Generate(
        trimmedModelPath,
        trimmedModelRepo,
        leftContext,
        reading,
        requestId,
        std::function<bool(const std::wstring&)>());
    return WriteResponse(
        pipe,
        generated.empty() ? ZenzProtocol::Status::Error : ZenzProtocol::Status::Ok,
        generated);
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
            PIPE_UNLIMITED_INSTANCES,
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
            std::thread([pipe]()
            {
                HandleClient(pipe);
                FlushFileBuffers(pipe);
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
            }).detach();
            continue;
        }

        CloseHandle(pipe);
    }
}
