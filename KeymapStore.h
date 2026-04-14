#ifndef SUMIRE_KEYMAP_STORE_H
#define SUMIRE_KEYMAP_STORE_H

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace SumireKeymap
{
struct Binding
{
    std::wstring status;
    std::wstring key;
    std::wstring command;
    bool enabled = true;
};

struct Profile
{
    std::wstring id;
    std::wstring name;
    std::wstring sourceFormat = L"tsv";
    std::wstring sourcePath;
    bool isBuiltin = false;
    std::wstring updatedAt;
    std::vector<Binding> bindings;
};

struct Database
{
    int version = 1;
    std::wstring activeProfileId = L"ms-ime";
    std::vector<Profile> profiles;
};

struct RuntimeKeymap
{
    std::wstring activeProfileId;
    std::vector<Binding> bindings;
    bool loaded = false;
};

std::filesystem::path GetKeymapDirectory();
std::filesystem::path GetKeymapJsonPath();
std::filesystem::path GetBuiltinKeymapDirectory();
std::filesystem::path GetDefaultMsImeTsvPath();
std::filesystem::path GetDefaultAtokTsvPath();

bool EnsureInitialized(std::wstring* errorMessage = nullptr);
bool LoadDatabase(Database* database, std::wstring* errorMessage = nullptr);
bool SaveDatabase(const Database& database, std::wstring* errorMessage = nullptr);
bool LoadRuntimeKeymap(RuntimeKeymap* keymap, std::wstring* errorMessage = nullptr);

bool ImportTsvFileAsProfile(
    const std::filesystem::path& path,
    Database* database,
    const std::wstring& profileId,
    const std::wstring& profileName,
    bool activate,
    std::wstring* errorMessage = nullptr);

bool ExportProfileToTsv(
    const Database& database,
    const std::wstring& profileId,
    const std::filesystem::path& path,
    std::wstring* errorMessage = nullptr);

bool BackupDatabase(std::filesystem::path* backupPath = nullptr, std::wstring* errorMessage = nullptr);

std::wstring NormalizeKeyText(const std::wstring& key);
std::wstring NormalizeKeyStroke(WPARAM wParam, LPARAM lParam, bool* printableAscii = nullptr);
bool FindCommand(
    const RuntimeKeymap& keymap,
    const std::wstring& status,
    const std::wstring& key,
    bool printableAscii,
    std::wstring* command);
bool IsSupportedCommand(const std::wstring& command);
}

#endif // SUMIRE_KEYMAP_STORE_H
