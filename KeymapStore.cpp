#include "KeymapStore.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace
{
constexpr wchar_t kSettingsInstallSubKey[] = L"Software\\Sumire";
constexpr wchar_t kInstallDirValue[] = L"InstallDir";
constexpr wchar_t kDefaultProfileId[] = L"ms-ime";
constexpr wchar_t kDefaultProfileName[] = L"MS-IME";
constexpr wchar_t kAtokProfileId[] = L"atok";
constexpr wchar_t kAtokProfileName[] = L"ATOK";

struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::wstring stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::wstring, JsonValue> objectValue;
};

bool SetError(std::wstring* errorMessage, const std::wstring& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

std::wstring Trim(const std::wstring& value)
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

std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring ToUpper(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towupper(ch));
    });
    return value;
}

std::vector<std::wstring> Split(const std::wstring& value, wchar_t delimiter)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;)
    {
        const size_t pos = value.find(delimiter, start);
        if (pos == std::wstring::npos)
        {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
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

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring MultiByteToWide(const std::string& value, UINT codePage)
{
    if (value.empty())
    {
        return std::wstring();
    }

    const int flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    const int size = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::wstring();
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring BytesToWideText(std::string bytes)
{
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(0, 3);
    }

    std::wstring utf8 = MultiByteToWide(bytes, CP_UTF8);
    if (!utf8.empty() || bytes.empty())
    {
        return utf8;
    }

    return MultiByteToWide(bytes, CP_ACP);
}

bool ReadTextFile(const std::filesystem::path& path, std::wstring* out, std::wstring* errorMessage)
{
    if (out == nullptr)
    {
        return SetError(errorMessage, L"Internal error: output buffer is null.");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return SetError(errorMessage, L"Failed to open file: " + path.wstring());
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    *out = BytesToWideText(buffer.str());
    return true;
}

bool WriteTextFileAtomic(const std::filesystem::path& path, const std::wstring& text, std::wstring* errorMessage)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        return SetError(errorMessage, L"Failed to create directory: " + path.parent_path().wstring());
    }

    const std::filesystem::path temporaryPath = path.wstring() + L".tmp";
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return SetError(errorMessage, L"Failed to write file: " + temporaryPath.wstring());
        }

        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        const std::string bytes = WideToUtf8(text);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporaryPath, ec);
        return SetError(errorMessage, L"Failed to replace file: " + path.wstring());
    }

    return true;
}

class JsonParser
{
public:
    explicit JsonParser(const std::wstring& text) : _text(text) {}

    bool Parse(JsonValue* value)
    {
        SkipWhitespace();
        if (!ParseValue(value))
        {
            return false;
        }
        SkipWhitespace();
        return _pos == _text.size();
    }

private:
    void SkipWhitespace()
    {
        while (_pos < _text.size() && iswspace(_text[_pos]) != 0)
        {
            ++_pos;
        }
    }

    bool Match(wchar_t ch)
    {
        SkipWhitespace();
        if (_pos >= _text.size() || _text[_pos] != ch)
        {
            return false;
        }
        ++_pos;
        return true;
    }

    bool ParseValue(JsonValue* value)
    {
        if (value == nullptr)
        {
            return false;
        }

        SkipWhitespace();
        if (_pos >= _text.size())
        {
            return false;
        }

        const wchar_t ch = _text[_pos];
        if (ch == L'{')
        {
            return ParseObject(value);
        }
        if (ch == L'[')
        {
            return ParseArray(value);
        }
        if (ch == L'"')
        {
            value->type = JsonValue::Type::String;
            return ParseString(&value->stringValue);
        }
        if (ch == L't' && _text.compare(_pos, 4, L"true") == 0)
        {
            _pos += 4;
            value->type = JsonValue::Type::Bool;
            value->boolValue = true;
            return true;
        }
        if (ch == L'f' && _text.compare(_pos, 5, L"false") == 0)
        {
            _pos += 5;
            value->type = JsonValue::Type::Bool;
            value->boolValue = false;
            return true;
        }
        if (ch == L'n' && _text.compare(_pos, 4, L"null") == 0)
        {
            _pos += 4;
            value->type = JsonValue::Type::Null;
            return true;
        }
        if (ch == L'-' || (ch >= L'0' && ch <= L'9'))
        {
            return ParseNumber(value);
        }
        return false;
    }

    bool ParseObject(JsonValue* value)
    {
        if (!Match(L'{'))
        {
            return false;
        }

        value->type = JsonValue::Type::Object;
        SkipWhitespace();
        if (Match(L'}'))
        {
            return true;
        }

        for (;;)
        {
            std::wstring key;
            if (!ParseString(&key) || !Match(L':'))
            {
                return false;
            }

            JsonValue child;
            if (!ParseValue(&child))
            {
                return false;
            }
            value->objectValue[key] = std::move(child);

            SkipWhitespace();
            if (Match(L'}'))
            {
                return true;
            }
            if (!Match(L','))
            {
                return false;
            }
        }
    }

    bool ParseArray(JsonValue* value)
    {
        if (!Match(L'['))
        {
            return false;
        }

        value->type = JsonValue::Type::Array;
        SkipWhitespace();
        if (Match(L']'))
        {
            return true;
        }

        for (;;)
        {
            JsonValue child;
            if (!ParseValue(&child))
            {
                return false;
            }
            value->arrayValue.push_back(std::move(child));

            SkipWhitespace();
            if (Match(L']'))
            {
                return true;
            }
            if (!Match(L','))
            {
                return false;
            }
        }
    }

    bool ParseString(std::wstring* value)
    {
        SkipWhitespace();
        if (_pos >= _text.size() || _text[_pos] != L'"')
        {
            return false;
        }
        ++_pos;

        std::wstring result;
        while (_pos < _text.size())
        {
            wchar_t ch = _text[_pos++];
            if (ch == L'"')
            {
                *value = std::move(result);
                return true;
            }
            if (ch != L'\\')
            {
                result.push_back(ch);
                continue;
            }
            if (_pos >= _text.size())
            {
                return false;
            }

            const wchar_t escaped = _text[_pos++];
            switch (escaped)
            {
            case L'"': result.push_back(L'"'); break;
            case L'\\': result.push_back(L'\\'); break;
            case L'/': result.push_back(L'/'); break;
            case L'b': result.push_back(L'\b'); break;
            case L'f': result.push_back(L'\f'); break;
            case L'n': result.push_back(L'\n'); break;
            case L'r': result.push_back(L'\r'); break;
            case L't': result.push_back(L'\t'); break;
            case L'u':
                {
                    if (_pos + 4 > _text.size())
                    {
                        return false;
                    }
                    std::wstring hex = _text.substr(_pos, 4);
                    wchar_t* end = nullptr;
                    const unsigned long code = wcstoul(hex.c_str(), &end, 16);
                    if (end == nullptr || *end != L'\0')
                    {
                        return false;
                    }
                    result.push_back(static_cast<wchar_t>(code));
                    _pos += 4;
                }
                break;
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseNumber(JsonValue* value)
    {
        const size_t start = _pos;
        if (_text[_pos] == L'-')
        {
            ++_pos;
        }
        while (_pos < _text.size() && iswdigit(_text[_pos]) != 0)
        {
            ++_pos;
        }
        if (_pos < _text.size() && _text[_pos] == L'.')
        {
            ++_pos;
            while (_pos < _text.size() && iswdigit(_text[_pos]) != 0)
            {
                ++_pos;
            }
        }

        value->type = JsonValue::Type::Number;
        value->numberValue = wcstod(_text.substr(start, _pos - start).c_str(), nullptr);
        return true;
    }

    const std::wstring& _text;
    size_t _pos = 0;
};

std::wstring JsonEscape(const std::wstring& value)
{
    std::wstring result;
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'"': result += L"\\\""; break;
        case L'\\': result += L"\\\\"; break;
        case L'\b': result += L"\\b"; break;
        case L'\f': result += L"\\f"; break;
        case L'\n': result += L"\\n"; break;
        case L'\r': result += L"\\r"; break;
        case L'\t': result += L"\\t"; break;
        default:
            if (ch < 0x20)
            {
                wchar_t buffer[8] = {};
                swprintf_s(buffer, L"\\u%04x", static_cast<unsigned int>(ch));
                result += buffer;
            }
            else
            {
                result.push_back(ch);
            }
            break;
        }
    }
    return result;
}

const JsonValue* GetObjectValue(const JsonValue& value, const wchar_t* name)
{
    if (value.type != JsonValue::Type::Object)
    {
        return nullptr;
    }
    auto it = value.objectValue.find(name);
    return it == value.objectValue.end() ? nullptr : &it->second;
}

std::wstring GetJsonString(const JsonValue& value, const wchar_t* name, const std::wstring& fallback = L"")
{
    const JsonValue* child = GetObjectValue(value, name);
    return child != nullptr && child->type == JsonValue::Type::String ? child->stringValue : fallback;
}

bool GetJsonBool(const JsonValue& value, const wchar_t* name, bool fallback)
{
    const JsonValue* child = GetObjectValue(value, name);
    return child != nullptr && child->type == JsonValue::Type::Bool ? child->boolValue : fallback;
}

int GetJsonInt(const JsonValue& value, const wchar_t* name, int fallback)
{
    const JsonValue* child = GetObjectValue(value, name);
    return child != nullptr && child->type == JsonValue::Type::Number ? static_cast<int>(child->numberValue) : fallback;
}

std::wstring GetTimestamp()
{
    SYSTEMTIME time = {};
    GetSystemTime(&time);
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

std::wstring MakeBackupTimestamp()
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

std::filesystem::path GetModuleDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
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

std::filesystem::path GetInstallDirectoryFromRegistry()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsInstallSubKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return std::filesystem::path();
    }

    DWORD type = 0;
    DWORD size = 0;
    std::filesystem::path path;
    if (RegQueryValueExW(key, kInstallDirValue, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) &&
        size > 0)
    {
        std::wstring value(size / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(key, kInstallDirValue, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &size) == ERROR_SUCCESS)
        {
            while (!value.empty() && value.back() == L'\0')
            {
                value.pop_back();
            }
            path = value;
        }
    }

    RegCloseKey(key);
    return path;
}

std::filesystem::path GetLocalAppDataDirectory()
{
    std::wstring value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size())
    {
        return GetModuleDirectory();
    }
    value.resize(length);
    return std::filesystem::path(value);
}

std::filesystem::path FindBuiltinTsv(const wchar_t* fileName)
{
    std::vector<std::filesystem::path> roots;
    const std::filesystem::path installDir = GetInstallDirectoryFromRegistry();
    if (!installDir.empty())
    {
        roots.push_back(installDir);
    }
    roots.push_back(GetModuleDirectory());
    roots.push_back(std::filesystem::current_path());

    for (const auto& root : roots)
    {
        std::filesystem::path current = root;
        for (int depth = 0; depth < 5 && !current.empty(); ++depth)
        {
            const std::filesystem::path keymapsPath = current / L"keymaps" / fileName;
            if (std::filesystem::exists(keymapsPath))
            {
                return keymapsPath;
            }
            const std::filesystem::path directPath = current / fileName;
            if (std::filesystem::exists(directPath))
            {
                return directPath;
            }
            current = current.parent_path();
        }
    }

    return std::filesystem::path();
}

std::wstring NormalizeProfileId(std::wstring value)
{
    value = ToLower(Trim(value));
    for (wchar_t& ch : value)
    {
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_'))
        {
            ch = L'-';
        }
    }
    return value.empty() ? L"user" : value;
}

SumireKeymap::Profile* FindProfile(SumireKeymap::Database* database, const std::wstring& profileId)
{
    if (database == nullptr)
    {
        return nullptr;
    }
    for (SumireKeymap::Profile& profile : database->profiles)
    {
        if (profile.id == profileId)
        {
            return &profile;
        }
    }
    return nullptr;
}

const SumireKeymap::Profile* FindProfile(const SumireKeymap::Database& database, const std::wstring& profileId)
{
    for (const SumireKeymap::Profile& profile : database.profiles)
    {
        if (profile.id == profileId)
        {
            return &profile;
        }
    }
    return nullptr;
}

std::wstring CanonicalKeyName(const std::wstring& keyName)
{
    const std::wstring trimmed = Trim(keyName);
    const std::wstring lower = ToLower(trimmed);
    if (lower == L"esc" || lower == L"escape")
    {
        return L"ESC";
    }
    if (lower == L"return" || lower == L"enter")
    {
        return L"Enter";
    }
    if (lower == L"backspace")
    {
        return L"Backspace";
    }
    if (lower == L"delete" || lower == L"del")
    {
        return L"Delete";
    }
    if (lower == L"space")
    {
        return L"Space";
    }
    if (lower == L"tab")
    {
        return L"Tab";
    }
    if (lower == L"left" || lower == L"right" || lower == L"up" || lower == L"down" ||
        lower == L"home" || lower == L"end" || lower == L"insert")
    {
        std::wstring result = lower;
        result[0] = static_cast<wchar_t>(towupper(result[0]));
        return result;
    }
    if (lower == L"pageup")
    {
        return L"PageUp";
    }
    if (lower == L"pagedown")
    {
        return L"PageDown";
    }
    if (lower == L"hankaku/zenkaku")
    {
        return L"Hankaku/Zenkaku";
    }
    if (lower == L"kanji" || lower == L"henkan" || lower == L"muhenkan" ||
        lower == L"hiragana" || lower == L"katakana" || lower == L"kana" ||
        lower == L"eisu" || lower == L"on" || lower == L"off")
    {
        std::wstring result = lower;
        result[0] = static_cast<wchar_t>(towupper(result[0]));
        return result;
    }
    if (lower == L"ascii")
    {
        return L"ASCII";
    }
    if (lower.size() >= 2 && lower[0] == L'f')
    {
        bool allDigits = true;
        for (size_t i = 1; i < lower.size(); ++i)
        {
            allDigits = allDigits && iswdigit(lower[i]) != 0;
        }
        if (allDigits)
        {
            return ToUpper(lower);
        }
    }
    if (trimmed.size() == 1)
    {
        return ToLower(trimmed);
    }
    return trimmed;
}

std::wstring SerializeDatabase(const SumireKeymap::Database& database)
{
    std::wostringstream out;
    out << L"{\n";
    out << L"  \"version\": " << database.version << L",\n";
    out << L"  \"activeProfileId\": \"" << JsonEscape(database.activeProfileId) << L"\",\n";
    out << L"  \"profiles\": [\n";
    for (size_t i = 0; i < database.profiles.size(); ++i)
    {
        const SumireKeymap::Profile& profile = database.profiles[i];
        out << L"    {\n";
        out << L"      \"id\": \"" << JsonEscape(profile.id) << L"\",\n";
        out << L"      \"name\": \"" << JsonEscape(profile.name) << L"\",\n";
        out << L"      \"sourceFormat\": \"" << JsonEscape(profile.sourceFormat) << L"\",\n";
        out << L"      \"sourcePath\": \"" << JsonEscape(profile.sourcePath) << L"\",\n";
        out << L"      \"isBuiltin\": " << (profile.isBuiltin ? L"true" : L"false") << L",\n";
        out << L"      \"updatedAt\": \"" << JsonEscape(profile.updatedAt) << L"\",\n";
        out << L"      \"bindings\": [\n";
        for (size_t j = 0; j < profile.bindings.size(); ++j)
        {
            const SumireKeymap::Binding& binding = profile.bindings[j];
            out << L"        { \"status\": \"" << JsonEscape(binding.status)
                << L"\", \"key\": \"" << JsonEscape(binding.key)
                << L"\", \"command\": \"" << JsonEscape(binding.command)
                << L"\", \"enabled\": " << (binding.enabled ? L"true" : L"false") << L" }";
            if (j + 1 < profile.bindings.size())
            {
                out << L",";
            }
            out << L"\n";
        }
        out << L"      ]\n";
        out << L"    }";
        if (i + 1 < database.profiles.size())
        {
            out << L",";
        }
        out << L"\n";
    }
    out << L"  ]\n";
    out << L"}\n";
    return out.str();
}

bool ParseDatabase(const std::wstring& text, SumireKeymap::Database* database, std::wstring* errorMessage)
{
    if (database == nullptr)
    {
        return SetError(errorMessage, L"Internal error: database buffer is null.");
    }

    JsonValue root;
    JsonParser parser(text);
    if (!parser.Parse(&root) || root.type != JsonValue::Type::Object)
    {
        return SetError(errorMessage, L"Invalid keymap JSON.");
    }

    SumireKeymap::Database parsed;
    parsed.version = GetJsonInt(root, L"version", 1);
    parsed.activeProfileId = GetJsonString(root, L"activeProfileId", kDefaultProfileId);

    const JsonValue* profiles = GetObjectValue(root, L"profiles");
    if (profiles == nullptr || profiles->type != JsonValue::Type::Array)
    {
        return SetError(errorMessage, L"Invalid keymap JSON: profiles is missing.");
    }

    for (const JsonValue& profileValue : profiles->arrayValue)
    {
        if (profileValue.type != JsonValue::Type::Object)
        {
            continue;
        }

        SumireKeymap::Profile profile;
        profile.id = NormalizeProfileId(GetJsonString(profileValue, L"id"));
        profile.name = GetJsonString(profileValue, L"name", profile.id);
        profile.sourceFormat = GetJsonString(profileValue, L"sourceFormat", L"tsv");
        profile.sourcePath = GetJsonString(profileValue, L"sourcePath");
        profile.isBuiltin = GetJsonBool(profileValue, L"isBuiltin", false);
        profile.updatedAt = GetJsonString(profileValue, L"updatedAt");

        const JsonValue* bindings = GetObjectValue(profileValue, L"bindings");
        if (bindings != nullptr && bindings->type == JsonValue::Type::Array)
        {
            for (const JsonValue& bindingValue : bindings->arrayValue)
            {
                if (bindingValue.type != JsonValue::Type::Object)
                {
                    continue;
                }

                SumireKeymap::Binding binding;
                binding.status = Trim(GetJsonString(bindingValue, L"status"));
                binding.key = SumireKeymap::NormalizeKeyText(GetJsonString(bindingValue, L"key"));
                binding.command = Trim(GetJsonString(bindingValue, L"command"));
                binding.enabled = GetJsonBool(bindingValue, L"enabled", true);
                if (!binding.status.empty() && !binding.key.empty() && !binding.command.empty())
                {
                    profile.bindings.push_back(std::move(binding));
                }
            }
        }

        if (!profile.id.empty())
        {
            parsed.profiles.push_back(std::move(profile));
        }
    }

    if (parsed.profiles.empty())
    {
        return SetError(errorMessage, L"Invalid keymap JSON: no profiles found.");
    }
    if (FindProfile(parsed, parsed.activeProfileId) == nullptr)
    {
        parsed.activeProfileId = parsed.profiles.front().id;
    }

    *database = std::move(parsed);
    return true;
}

bool LoadTsvBindings(const std::filesystem::path& path, std::vector<SumireKeymap::Binding>* bindings, std::wstring* errorMessage)
{
    if (bindings == nullptr)
    {
        return SetError(errorMessage, L"Internal error: binding buffer is null.");
    }

    std::wstring text;
    if (!ReadTextFile(path, &text, errorMessage))
    {
        return false;
    }

    bindings->clear();
    std::wstringstream stream(text);
    std::wstring line;
    bool firstLine = true;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        if (Trim(line).empty())
        {
            continue;
        }

        const std::vector<std::wstring> columns = Split(line, L'\t');
        if (columns.size() < 3)
        {
            continue;
        }

        if (firstLine)
        {
            firstLine = false;
            if (ToLower(Trim(columns[0])) == L"status" &&
                ToLower(Trim(columns[1])) == L"key" &&
                ToLower(Trim(columns[2])) == L"command")
            {
                continue;
            }
        }

        SumireKeymap::Binding binding;
        binding.status = Trim(columns[0]);
        binding.key = SumireKeymap::NormalizeKeyText(columns[1]);
        binding.command = Trim(columns[2]);
        binding.enabled = true;
        if (!binding.status.empty() && !binding.key.empty() && !binding.command.empty())
        {
            bindings->push_back(std::move(binding));
        }
    }

    if (bindings->empty())
    {
        return SetError(errorMessage, L"No keymap entries were found in: " + path.wstring());
    }
    return true;
}

bool AddOrReplaceTsvProfile(
    const std::filesystem::path& path,
    SumireKeymap::Database* database,
    const std::wstring& profileId,
    const std::wstring& profileName,
    bool activate,
    bool isBuiltin,
    const std::wstring& sourcePath,
    std::wstring* errorMessage)
{
    std::vector<SumireKeymap::Binding> bindings;
    if (!LoadTsvBindings(path, &bindings, errorMessage))
    {
        return false;
    }

    const std::wstring normalizedProfileId = NormalizeProfileId(profileId);
    SumireKeymap::Profile profile;
    profile.id = normalizedProfileId;
    profile.name = profileName.empty() ? normalizedProfileId : profileName;
    profile.sourceFormat = L"tsv";
    profile.sourcePath = sourcePath.empty() ? path.wstring() : sourcePath;
    profile.isBuiltin = isBuiltin;
    profile.updatedAt = GetTimestamp();
    profile.bindings = std::move(bindings);

    SumireKeymap::Profile* existing = FindProfile(database, normalizedProfileId);
    if (existing != nullptr)
    {
        *existing = std::move(profile);
    }
    else
    {
        database->profiles.push_back(std::move(profile));
    }

    if (activate || database->activeProfileId.empty())
    {
        database->activeProfileId = normalizedProfileId;
    }

    return true;
}

std::wstring KeyNameFromVirtualKey(WPARAM wParam)
{
    if (wParam >= VK_F1 && wParam <= VK_F24)
    {
        return L"F" + std::to_wstring(static_cast<int>(wParam - VK_F1 + 1));
    }

    switch (wParam)
    {
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"ESC";
    case VK_SPACE: return L"Space";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_END: return L"End";
    case VK_HOME: return L"Home";
    case VK_LEFT: return L"Left";
    case VK_UP: return L"Up";
    case VK_RIGHT: return L"Right";
    case VK_DOWN: return L"Down";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_CONVERT: return L"Henkan";
    case VK_NONCONVERT: return L"Muhenkan";
    case VK_KANA: return L"Kana";
    case VK_KANJI: return L"Kanji";
    case VK_OEM_3: return L"Hankaku/Zenkaku";
    case VK_OEM_4: return L"[";
    case VK_OEM_6: return L"]";
    case VK_OEM_5: return L"\\";
    case VK_OEM_1: return L";";
    case VK_OEM_7: return L"'";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_PLUS: return L"=";
    case VK_OEM_2: return L"/";
    default:
        break;
    }

    if (wParam >= L'A' && wParam <= L'Z')
    {
        return std::wstring(1, static_cast<wchar_t>(towlower(static_cast<wchar_t>(wParam))));
    }
    if (wParam >= L'0' && wParam <= L'9')
    {
        return std::wstring(1, static_cast<wchar_t>(wParam));
    }
    return std::wstring();
}
}

namespace SumireKeymap
{
std::filesystem::path GetKeymapDirectory()
{
    return GetLocalAppDataDirectory() / L"SumireIME" / L"keymaps";
}

std::filesystem::path GetKeymapJsonPath()
{
    return GetKeymapDirectory() / L"keymaps.json";
}

std::filesystem::path GetBuiltinKeymapDirectory()
{
    const std::filesystem::path installDir = GetInstallDirectoryFromRegistry();
    if (!installDir.empty())
    {
        return installDir / L"keymaps";
    }
    return GetModuleDirectory() / L"keymaps";
}

std::filesystem::path GetDefaultMsImeTsvPath()
{
    return FindBuiltinTsv(L"ms-ime.tsv");
}

std::filesystem::path GetDefaultAtokTsvPath()
{
    return FindBuiltinTsv(L"atok.tsv");
}

bool EnsureInitialized(std::wstring* errorMessage)
{
    const std::filesystem::path jsonPath = GetKeymapJsonPath();
    std::error_code ec;
    std::filesystem::create_directories(jsonPath.parent_path(), ec);
    if (ec)
    {
        return SetError(errorMessage, L"Failed to create keymap directory: " + jsonPath.parent_path().wstring());
    }

    if (std::filesystem::exists(jsonPath, ec) && !ec)
    {
        return true;
    }

    Database database;
    database.version = 1;
    database.activeProfileId = kDefaultProfileId;

    const std::filesystem::path msImePath = GetDefaultMsImeTsvPath();
    if (msImePath.empty())
    {
        return SetError(errorMessage, L"Default MS-IME keymap was not found.");
    }

    if (!AddOrReplaceTsvProfile(msImePath, &database, kDefaultProfileId, kDefaultProfileName, true, true, L"builtin:ms-ime.tsv", errorMessage))
    {
        return false;
    }

    const std::filesystem::path atokPath = GetDefaultAtokTsvPath();
    if (!atokPath.empty())
    {
        AddOrReplaceTsvProfile(atokPath, &database, kAtokProfileId, kAtokProfileName, false, true, L"builtin:atok.tsv", nullptr);
    }

    return SaveDatabase(database, errorMessage);
}

bool LoadDatabase(Database* database, std::wstring* errorMessage)
{
    if (!EnsureInitialized(errorMessage))
    {
        return false;
    }

    std::wstring text;
    if (!ReadTextFile(GetKeymapJsonPath(), &text, errorMessage))
    {
        return false;
    }
    return ParseDatabase(text, database, errorMessage);
}

bool SaveDatabase(const Database& database, std::wstring* errorMessage)
{
    Database normalized = database;
    if (normalized.version <= 0)
    {
        normalized.version = 1;
    }
    for (Profile& profile : normalized.profiles)
    {
        profile.id = NormalizeProfileId(profile.id);
        if (profile.name.empty())
        {
            profile.name = profile.id;
        }
        if (profile.updatedAt.empty())
        {
            profile.updatedAt = GetTimestamp();
        }
        for (Binding& binding : profile.bindings)
        {
            binding.status = Trim(binding.status);
            binding.key = NormalizeKeyText(binding.key);
            binding.command = Trim(binding.command);
        }
    }
    if (FindProfile(normalized, normalized.activeProfileId) == nullptr && !normalized.profiles.empty())
    {
        normalized.activeProfileId = normalized.profiles.front().id;
    }

    return WriteTextFileAtomic(GetKeymapJsonPath(), SerializeDatabase(normalized), errorMessage);
}

bool LoadRuntimeKeymap(RuntimeKeymap* keymap, std::wstring* errorMessage)
{
    if (keymap == nullptr)
    {
        return SetError(errorMessage, L"Internal error: runtime keymap buffer is null.");
    }

    Database database;
    if (!LoadDatabase(&database, errorMessage))
    {
        keymap->bindings.clear();
        keymap->loaded = false;
        return false;
    }

    const Profile* profile = FindProfile(database, database.activeProfileId);
    if (profile == nullptr)
    {
        keymap->bindings.clear();
        keymap->loaded = false;
        return SetError(errorMessage, L"Active keymap profile was not found.");
    }

    keymap->activeProfileId = profile->id;
    keymap->bindings = profile->bindings;
    keymap->loaded = true;
    return true;
}

bool ImportTsvFileAsProfile(
    const std::filesystem::path& path,
    Database* database,
    const std::wstring& profileId,
    const std::wstring& profileName,
    bool activate,
    std::wstring* errorMessage)
{
    if (database == nullptr)
    {
        return SetError(errorMessage, L"Internal error: database buffer is null.");
    }
    return AddOrReplaceTsvProfile(path, database, profileId, profileName, activate, false, path.wstring(), errorMessage);
}

bool ExportProfileToTsv(
    const Database& database,
    const std::wstring& profileId,
    const std::filesystem::path& path,
    std::wstring* errorMessage)
{
    const Profile* profile = FindProfile(database, profileId);
    if (profile == nullptr)
    {
        return SetError(errorMessage, L"Keymap profile was not found.");
    }

    std::wostringstream out;
    out << L"status\tkey\tcommand\r\n";
    for (const Binding& binding : profile->bindings)
    {
        if (!binding.enabled)
        {
            continue;
        }
        out << binding.status << L"\t" << binding.key << L"\t" << binding.command << L"\r\n";
    }

    return WriteTextFileAtomic(path, out.str(), errorMessage);
}

bool BackupDatabase(std::filesystem::path* backupPath, std::wstring* errorMessage)
{
    if (!EnsureInitialized(errorMessage))
    {
        return false;
    }

    const std::filesystem::path source = GetKeymapJsonPath();
    const std::filesystem::path targetDirectory = GetKeymapDirectory() / L"backups";
    std::error_code ec;
    std::filesystem::create_directories(targetDirectory, ec);
    if (ec)
    {
        return SetError(errorMessage, L"Failed to create backup directory: " + targetDirectory.wstring());
    }

    const std::filesystem::path target = targetDirectory / (L"keymaps-" + MakeBackupTimestamp() + L".json");
    if (!CopyFileW(source.c_str(), target.c_str(), FALSE))
    {
        return SetError(errorMessage, L"Failed to create backup: " + target.wstring());
    }

    if (backupPath != nullptr)
    {
        *backupPath = target;
    }
    return true;
}

std::wstring NormalizeKeyText(const std::wstring& key)
{
    std::vector<std::wstring> tokens;
    for (const std::wstring& rawToken : Split(Trim(key), L' '))
    {
        const std::wstring token = Trim(rawToken);
        if (!token.empty())
        {
            tokens.push_back(token);
        }
    }
    if (tokens.empty())
    {
        return L"";
    }

    bool hasCtrl = false;
    bool hasShift = false;
    bool hasAlt = false;
    std::vector<std::wstring> keyParts;
    for (const std::wstring& token : tokens)
    {
        const std::wstring lower = ToLower(token);
        if (lower == L"ctrl" || lower == L"control")
        {
            hasCtrl = true;
        }
        else if (lower == L"shift")
        {
            hasShift = true;
        }
        else if (lower == L"alt")
        {
            hasAlt = true;
        }
        else
        {
            keyParts.push_back(token);
        }
    }

    std::wstring keyName;
    for (size_t i = 0; i < keyParts.size(); ++i)
    {
        if (i != 0)
        {
            keyName.push_back(L' ');
        }
        keyName += keyParts[i];
    }
    keyName = CanonicalKeyName(keyName);

    std::vector<std::wstring> result;
    if (hasCtrl)
    {
        result.push_back(L"Ctrl");
    }
    if (hasShift)
    {
        result.push_back(L"Shift");
    }
    if (hasAlt)
    {
        result.push_back(L"Alt");
    }
    if (!keyName.empty())
    {
        result.push_back(keyName);
    }

    std::wstring text;
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (i != 0)
        {
            text.push_back(L' ');
        }
        text += result[i];
    }
    return text;
}

std::wstring NormalizeKeyStroke(WPARAM wParam, LPARAM lParam, bool* printableAscii)
{
    UNREFERENCED_PARAMETER(lParam);
    if (printableAscii != nullptr)
    {
        *printableAscii = false;
    }

    const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;

    std::wstring keyName = KeyNameFromVirtualKey(wParam);
    if (keyName.empty())
    {
        return L"";
    }

    const bool namedNonAscii = keyName.size() > 1;
    if (printableAscii != nullptr &&
        !ctrlDown &&
        !altDown &&
        !namedNonAscii &&
        keyName[0] >= 0x20 &&
        keyName[0] < 0x7f)
    {
        *printableAscii = true;
    }

    std::vector<std::wstring> parts;
    if (ctrlDown)
    {
        parts.push_back(L"Ctrl");
    }
    if (shiftDown && (ctrlDown || altDown || wParam < L'A' || wParam > L'Z'))
    {
        parts.push_back(L"Shift");
    }
    if (altDown)
    {
        parts.push_back(L"Alt");
    }
    parts.push_back(keyName);

    std::wstring text;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i != 0)
        {
            text.push_back(L' ');
        }
        text += parts[i];
    }
    return NormalizeKeyText(text);
}

bool FindCommand(
    const RuntimeKeymap& keymap,
    const std::wstring& status,
    const std::wstring& key,
    bool printableAscii,
    std::wstring* command)
{
    if (command != nullptr)
    {
        command->clear();
    }
    if (!keymap.loaded)
    {
        return false;
    }

    const std::wstring normalizedKey = NormalizeKeyText(key);
    const auto findExact = [&](const std::wstring& keyToFind, std::wstring* outCommand) -> bool {
        for (const Binding& binding : keymap.bindings)
        {
            if (!binding.enabled)
            {
                continue;
            }
            if (binding.status == status && NormalizeKeyText(binding.key) == keyToFind)
            {
                if (outCommand != nullptr)
                {
                    *outCommand = binding.command;
                }
                return true;
            }
        }
        return false;
    };

    if (!normalizedKey.empty() && findExact(normalizedKey, command))
    {
        return true;
    }
    if (printableAscii && findExact(L"ASCII", command))
    {
        return true;
    }
    return false;
}

bool IsSupportedCommand(const std::wstring& command)
{
    static const wchar_t* kSupportedCommands[] = {
        L"InsertCharacter",
        L"Backspace",
        L"Delete",
        L"Cancel",
        L"Commit",
        L"Convert",
        L"ConvertNext",
        L"ConvertPrev",
        L"ConvertNextPage",
        L"ConvertPrevPage",
        L"MoveCursorLeft",
        L"MoveCursorRight",
        L"MoveCursorToBeginning",
        L"MoveCursorToEnd",
        L"SegmentFocusLeft",
        L"SegmentFocusRight",
        L"SegmentFocusFirst",
        L"SegmentFocusLast",
        L"SegmentWidthShrink",
        L"SegmentWidthExpand",
        L"ToggleAlphanumericMode",
        L"IMEOn",
        L"IMEOff",
        L"CancelAndIMEOff",
    };

    for (const wchar_t* supported : kSupportedCommands)
    {
        if (command == supported)
        {
            return true;
        }
    }
    return false;
}
}
