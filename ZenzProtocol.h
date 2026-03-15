#ifndef SUMIRE_ZENZ_PROTOCOL_H
#define SUMIRE_ZENZ_PROTOCOL_H

#include <cstdint>

namespace ZenzProtocol
{
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\SumireZenzService";
constexpr std::uint32_t kVersion = 1;

enum class Command : std::uint32_t
{
    Generate = 1,
};

enum class Status : std::uint32_t
{
    Ok = 0,
    Disabled = 1,
    Error = 2,
};

#pragma pack(push, 1)
struct RequestHeader
{
    std::uint32_t version = kVersion;
    std::uint32_t command = static_cast<std::uint32_t>(Command::Generate);
    std::uint32_t readingChars = 0;
    std::uint32_t modelPathChars = 0;
    std::uint32_t modelRepoChars = 0;
    std::uint32_t timeoutMs = 0;
};

struct ResponseHeader
{
    std::uint32_t version = kVersion;
    std::uint32_t status = static_cast<std::uint32_t>(Status::Error);
    std::uint32_t resultChars = 0;
};
#pragma pack(pop)
}

#endif // SUMIRE_ZENZ_PROTOCOL_H
