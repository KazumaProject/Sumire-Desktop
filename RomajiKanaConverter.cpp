// RomajiKanaConverter.cpp
#include "RomajiKanaConverter.h"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "Globals.h"
#include "SumireSettingsStore.h"

namespace
{
using Map = std::unordered_map<std::wstring, RomajiKanaConverter::MapEntry>;

const char* kDefaultMapTsv = u8R"(# romaji	kana	[pending]
-	ー
~	〜
.	。
,	、
z/	・
z.	…
z,	‥
zh	←
zj	↓
zk	↑
zl	→
z-	〜
z[	『
z]	』
[	「
]	」
va	ゔぁ
vi	ゔぃ
vu	ゔ
ve	ゔぇ
vo	ゔぉ
vya	ゔゃ
vyi	ゔぃ
vyu	ゔゅ
vye	ゔぇ
vyo	ゔょ
qq	っ	q
vv	っ	v
ll	っ	l
xx	っ	x
kk	っ	k
gg	っ	g
ss	っ	s
zz	っ	z
jj	っ	j
tt	っ	t
tch	っ	ch
dd	っ	d
hh	っ	h
ff	っ	f
bb	っ	b
pp	っ	p
mm	っ	m
yy	っ	y
rr	っ	r
ww	っ	w
www	w	ww
cc	っ	c
kya	きゃ
kyi	きぃ
kyu	きゅ
kye	きぇ
kyo	きょ
gya	ぎゃ
gyi	ぎぃ
gyu	ぎゅ
gye	ぎぇ
gyo	ぎょ
sya	しゃ
syi	しぃ
syu	しゅ
sye	しぇ
syo	しょ
sha	しゃ
shi	し
shu	しゅ
she	しぇ
sho	しょ
zya	じゃ
zyi	じぃ
zyu	じゅ
zye	じぇ
zyo	じょ
tya	ちゃ
tyi	ちぃ
tyu	ちゅ
tye	ちぇ
tyo	ちょ
cha	ちゃ
chi	ち
chu	ちゅ
che	ちぇ
cho	ちょ
cya	ちゃ
cyi	ちぃ
cyu	ちゅ
cye	ちぇ
cyo	ちょ
dya	ぢゃ
dyi	ぢぃ
dyu	ぢゅ
dye	ぢぇ
dyo	ぢょ
tsa	つぁ
tsi	つぃ
tse	つぇ
tso	つぉ
tha	てゃ
thi	てぃ
t'i	てぃ
thu	てゅ
the	てぇ
tho	てょ
t'yu	てゅ
dha	でゃ
dhi	でぃ
d'i	でぃ
dhu	でゅ
dhe	でぇ
dho	でょ
d'yu	でゅ
twa	とぁ
twi	とぃ
twu	とぅ
twe	とぇ
two	とぉ
t'u	とぅ
dwa	どぁ
dwi	どぃ
dwu	どぅ
dwe	どぇ
dwo	どぉ
d'u	どぅ
nya	にゃ
nyi	にぃ
nyu	にゅ
nye	にぇ
nyo	にょ
hya	ひゃ
hyi	ひぃ
hyu	ひゅ
hye	ひぇ
hyo	ひょ
bya	びゃ
byi	びぃ
byu	びゅ
bye	びぇ
byo	びょ
pya	ぴゃ
pyi	ぴぃ
pyu	ぴゅ
pye	ぴぇ
pyo	ぴょ
fa	ふぁ
fi	ふぃ
fu	ふ
fe	ふぇ
fo	ふぉ
fya	ふゃ
fyu	ふゅ
fyo	ふょ
hwa	ふぁ
hwi	ふぃ
hwe	ふぇ
hwo	ふぉ
hwyu	ふゅ
mya	みゃ
myi	みぃ
myu	みゅ
mye	みぇ
myo	みょ
rya	りゃ
ryi	りぃ
ryu	りゅ
rye	りぇ
ryo	りょ
n'	ん
nn	ん
n	ん
xn	ん
a	あ
i	い
u	う
wu	う
e	え
o	お
xa	ぁ
xi	ぃ
xu	ぅ
xe	ぇ
xo	ぉ
la	ぁ
li	ぃ
lu	ぅ
le	ぇ
lo	ぉ
lyi	ぃ
xyi	ぃ
lye	ぇ
xye	ぇ
ye	いぇ
ka	か
ki	き
ku	く
ke	け
ko	こ
xka	ヵ
xke	ヶ
lka	ヵ
lke	ヶ
ga	が
gi	ぎ
gu	ぐ
ge	げ
go	ご
sa	さ
si	し
su	す
se	せ
so	そ
ca	か
ci	し
cu	く
ce	せ
co	こ
qa	くぁ
qi	くぃ
qu	く
qe	くぇ
qo	くぉ
kwa	くぁ
kwi	くぃ
kwu	くぅ
kwe	くぇ
kwo	くぉ
gwa	ぐぁ
gwi	ぐぃ
gwu	ぐぅ
gwe	ぐぇ
gwo	ぐぉ
swa	すぁ
swi	すぃ
swu	すぅ
swe	すぇ
swo	すぉ
zwa	ずぁ
zwi	ずぃ
zwu	ずぅ
zwe	ずぇ
zwo	ずぉ
za	ざ
zi	じ
zu	ず
ze	ぜ
zo	ぞ
ja	じゃ
ji	じ
ju	じゅ
je	じぇ
jo	じょ
jya	じゃ
jyi	じぃ
jyu	じゅ
jye	じぇ
jyo	じょ
ta	た
ti	ち
tu	つ
tsu	つ
te	て
to	と
da	だ
di	ぢ
du	づ
de	で
do	ど
xtu	っ
xtsu	っ
ltu	っ
ltsu	っ
na	な
ni	に
nu	ぬ
ne	ね
no	の
ha	は
hi	ひ
hu	ふ
he	へ
ho	ほ
ba	ば
bi	び
bu	ぶ
be	べ
bo	ぼ
pa	ぱ
pi	ぴ
pu	ぷ
pe	ぺ
po	ぽ
ma	ま
mi	み
mu	む
me	め
mo	も
xya	ゃ
lya	ゃ
ya	や
wyi	ゐ
xyu	ゅ
lyu	ゅ
yu	ゆ
wye	ゑ
xyo	ょ
lyo	ょ
yo	よ
ra	ら
ri	り
ru	る
re	れ
ro	ろ
xwa	ゎ
lwa	ゎ
wa	わ
wi	うぃ
we	うぇ
wo	を
wha	うぁ
whi	うぃ
whu	う
whe	うぇ
who	うぉ
)";

std::wstring ReadEnvVar(const wchar_t* name)
{
    size_t required = 0;
    _wgetenv_s(&required, nullptr, 0, name);
    if (required == 0)
    {
        return L"";
    }

    std::wstring value(required, L'\0');
    _wgetenv_s(&required, &value[0], value.size(), name);
    if (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }

    return value;
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

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        return path.lexically_normal();
    }

    return normalized;
}

void AppendMapFileVariants(std::vector<std::filesystem::path>* out, const std::filesystem::path& base)
{
    if (base.empty())
    {
        return;
    }

    std::filesystem::path current = base;
    for (int depth = 0; depth < 5; ++depth)
    {
        out->push_back(current / L"romaji-hiragana.tsv");
        out->push_back(current / L"keymaps" / L"romaji-hiragana.tsv");
        out->push_back(current / L"dictionaries" / L"romaji-hiragana.tsv");

        if (!current.has_parent_path())
        {
            break;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current)
        {
            break;
        }

        current = parent;
    }
}

std::vector<std::filesystem::path> GetRomajiMapFiles()
{
    std::vector<std::filesystem::path> candidates;

    const SumireSettingsStore::Settings settings = SumireSettingsStore::Load();
    if (!settings.romajiMapPath.empty())
    {
        candidates.push_back(std::filesystem::path(settings.romajiMapPath));
    }

    const std::wstring envPath = ReadEnvVar(L"SUMIRE_ROMAJI_MAP_PATH");
    if (!envPath.empty())
    {
        candidates.push_back(std::filesystem::path(envPath));
    }

    AppendMapFileVariants(&candidates, GetModuleDirectory());
    AppendMapFileVariants(&candidates, std::filesystem::current_path());

    std::vector<std::filesystem::path> unique;
    for (const std::filesystem::path& candidate : candidates)
    {
        if (candidate.empty())
        {
            continue;
        }

        const std::filesystem::path normalized = NormalizePath(candidate);
        bool exists = false;
        for (const std::filesystem::path& current : unique)
        {
            if (NormalizePath(current) == normalized)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
        {
            unique.push_back(candidate);
        }
    }

    return unique;
}

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return L"";
    }

    size_t offset = 0;
    if (utf8.size() >= 3 &&
        static_cast<unsigned char>(utf8[0]) == 0xEF &&
        static_cast<unsigned char>(utf8[1]) == 0xBB &&
        static_cast<unsigned char>(utf8[2]) == 0xBF)
    {
        offset = 3;
    }

    const char* data = utf8.data() + offset;
    const int size = static_cast<int>(utf8.size() - offset);
    if (size <= 0)
    {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, data, size, nullptr, 0);
    if (required <= 0)
    {
        return L"";
    }

    std::wstring wide(required, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, size, &wide[0], required);
    return wide;
}

std::wstring Trim(const std::wstring& value)
{
    size_t start = 0;
    while (start < value.size() && iswspace(value[start]))
    {
        ++start;
    }

    size_t end = value.size();
    while (end > start && iswspace(value[end - 1]))
    {
        --end;
    }

    return value.substr(start, end - start);
}

std::vector<std::wstring> SplitTsvLine(const std::wstring& line)
{
    std::vector<std::wstring> columns;
    size_t start = 0;
    for (size_t index = 0; index <= line.size(); ++index)
    {
        if (index == line.size() || line[index] == L'\t')
        {
            columns.push_back(Trim(line.substr(start, index - start)));
            start = index + 1;
        }
    }
    return columns;
}

bool TryParsePositiveInt(const std::wstring& value, int* parsed)
{
    if (value.empty())
    {
        return false;
    }

    for (wchar_t ch : value)
    {
        if (!iswdigit(ch))
        {
            return false;
        }
    }

    *parsed = _wtoi(value.c_str());
    return *parsed > 0;
}

bool EndsWith(const std::wstring& value, const std::wstring& suffix)
{
    if (suffix.size() > value.size())
    {
        return false;
    }

    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

bool ContainsNonAscii(const std::wstring& value)
{
    for (wchar_t ch : value)
    {
        if (ch > 0x7F)
        {
            return true;
        }
    }

    return false;
}

bool ContainsAsciiLetter(const std::wstring& value)
{
    for (wchar_t ch : value)
    {
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z'))
        {
            return true;
        }
    }

    return false;
}

bool TryAppendMapKana(const Map& map, const std::wstring& key, std::wstring* out)
{
    const auto it = map.find(key);
    if (it == map.end() || it->second.kana.empty())
    {
        return false;
    }

    out->append(it->second.kana);
    return true;
}

bool TrySynthesizeYoon(const Map& map, const std::wstring& segment, std::wstring* kana)
{
    if (kana == nullptr)
    {
        return false;
    }

    std::wstring baseKey;
    std::wstring smallKey;

    if (segment == L"kya" || segment == L"kyu" || segment == L"kyo") baseKey = L"ki";
    else if (segment == L"gya" || segment == L"gyu" || segment == L"gyo") baseKey = L"gi";
    else if (segment == L"sya" || segment == L"syu" || segment == L"syo") baseKey = L"si";
    else if (segment == L"sha" || segment == L"shu" || segment == L"sho") baseKey = L"si";
    else if (segment == L"zya" || segment == L"zyu" || segment == L"zyo") baseKey = L"zi";
    else if (segment == L"jya" || segment == L"jyu" || segment == L"jyo") baseKey = L"zi";
    else if (segment == L"ja" || segment == L"ju" || segment == L"jo") baseKey = L"zi";
    else if (segment == L"tya" || segment == L"tyu" || segment == L"tyo") baseKey = L"ti";
    else if (segment == L"cha" || segment == L"chu" || segment == L"cho") baseKey = L"ti";
    else if (segment == L"cya" || segment == L"cyu" || segment == L"cyo") baseKey = L"ti";
    else if (segment == L"dya" || segment == L"dyu" || segment == L"dyo") baseKey = L"di";
    else if (segment == L"nya" || segment == L"nyu" || segment == L"nyo") baseKey = L"ni";
    else if (segment == L"hya" || segment == L"hyu" || segment == L"hyo") baseKey = L"hi";
    else if (segment == L"bya" || segment == L"byu" || segment == L"byo") baseKey = L"bi";
    else if (segment == L"pya" || segment == L"pyu" || segment == L"pyo") baseKey = L"pi";
    else if (segment == L"mya" || segment == L"myu" || segment == L"myo") baseKey = L"mi";
    else if (segment == L"rya" || segment == L"ryu" || segment == L"ryo") baseKey = L"ri";
    else if (segment == L"vya" || segment == L"vyu" || segment == L"vyo") baseKey = L"vi";
    else if (segment == L"fya" || segment == L"fyu" || segment == L"fyo") baseKey = L"fu";
    else
    {
        return false;
    }

    if (segment.back() == L'a')
    {
        smallKey = L"xya";
    }
    else if (segment.back() == L'u')
    {
        smallKey = L"xyu";
    }
    else if (segment.back() == L'o')
    {
        smallKey = L"xyo";
    }
    else
    {
        return false;
    }

    std::wstring synthesized;
    if (!TryAppendMapKana(map, baseKey, &synthesized) ||
        !TryAppendMapKana(map, smallKey, &synthesized))
    {
        return false;
    }

    *kana = std::move(synthesized);
    return true;
}

bool ParseMapData(const std::wstring& content, Map* out)
{
    if (out == nullptr)
    {
        return false;
    }

    out->clear();

    std::wistringstream stream(content);
    std::wstring line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }

        const std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L'#')
        {
            continue;
        }

        const std::vector<std::wstring> columns = SplitTsvLine(trimmed);
        if (columns.size() < 2)
        {
            continue;
        }

        const std::wstring& key = columns[0];
        const std::wstring& kana = columns[1];
        if (key.empty() || kana.empty())
        {
            continue;
        }

        RomajiKanaConverter::MapEntry entry;
        entry.kana = kana;
        entry.consume = static_cast<int>(key.size());

        for (size_t index = 2; index < columns.size(); ++index)
        {
            if (columns[index].empty())
            {
                continue;
            }

            int parsed = 0;
            if (TryParsePositiveInt(columns[index], &parsed))
            {
                entry.consume = parsed;
                continue;
            }

            if (entry.pending.empty())
            {
                entry.pending = columns[index];
            }
        }

        if (entry.consume <= 0)
        {
            entry.consume = static_cast<int>(key.size());
        }

        (*out)[key] = std::move(entry);
    }

    return !out->empty();
}

bool LoadMapFile(const std::filesystem::path& path, Map* out)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return false;
    }

    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty())
    {
        return false;
    }

    return ParseMapData(Utf8ToWide(bytes), out);
}

Map CreateDefaultMap()
{
    Map map;
    ParseMapData(Utf8ToWide(kDefaultMapTsv), &map);
    return map;
}

int ComputeMaxKeyLength(const Map& map)
{
    int maxKeyLength = 1;
    for (const auto& kv : map)
    {
        const int length = static_cast<int>(kv.first.size());
        if (length > maxKeyLength)
        {
            maxKeyLength = length;
        }
    }

    return maxKeyLength;
}

int EffectiveConsumeLength(const std::wstring& key, const RomajiKanaConverter::MapEntry& entry)
{
    int consume = entry.consume;
    if (consume <= 0)
    {
        consume = static_cast<int>(key.size());
    }

    // Optional pending text is reserved for future editable keymap extensions.
    // For kana outputs like "っ", keep the suffix available so subsequent input
    // can still form a longer syllable such as "tcha" -> "っちゃ".
    if (!entry.pending.empty() &&
        ContainsNonAscii(entry.kana) &&
        EndsWith(key, entry.pending) &&
        entry.pending.size() < key.size())
    {
        consume = (std::min)(consume, static_cast<int>(key.size() - entry.pending.size()));
    }

    if (consume < 1)
    {
        consume = 1;
    }
    if (consume > static_cast<int>(key.size()))
    {
        consume = static_cast<int>(key.size());
    }

    return consume;
}

std::vector<RomajiKanaConverter::ConversionSpan> BuildConversionSpansImpl(
    const std::wstring& raw,
    const Map& map,
    int maxKeyLength)
{
    std::vector<RomajiKanaConverter::ConversionSpan> spans;
    spans.reserve(raw.size());

    size_t i = 0;
    while (i < raw.size())
    {
        const wchar_t current = raw[i];

        bool matched = false;
        for (int len = maxKeyLength; len >= 1; --len)
        {
            if (i + static_cast<size_t>(len) > raw.size())
            {
                continue;
            }

            const std::wstring segment = raw.substr(i, static_cast<size_t>(len));
            const auto it = map.find(segment);
            if (it == map.end())
            {
                std::wstring synthesizedKana;
                if (TrySynthesizeYoon(map, segment, &synthesizedKana))
                {
                    RomajiKanaConverter::ConversionSpan span;
                    span.rawStart = i;
                    span.rawEnd = i + segment.size();
                    span.text = std::move(synthesizedKana);
                    spans.push_back(std::move(span));
                    i += segment.size();
                    matched = true;
                    break;
                }

                continue;
            }

            std::wstring kana = it->second.kana;
            int consumeLength = EffectiveConsumeLength(segment, it->second);
            if (ContainsAsciiLetter(kana))
            {
                std::wstring synthesizedKana;
                if (TrySynthesizeYoon(map, segment, &synthesizedKana))
                {
                    kana = std::move(synthesizedKana);
                    consumeLength = static_cast<int>(segment.size());
                }
            }

            RomajiKanaConverter::ConversionSpan span;
            span.rawStart = i;
            span.rawEnd = i + static_cast<size_t>(consumeLength);
            span.text = std::move(kana);
            spans.push_back(std::move(span));
            i += static_cast<size_t>(consumeLength);
            matched = true;
            break;
        }

        if (!matched && i + 1 < raw.size() && current == raw[i + 1])
        {
            const std::wstring sokuonConsonants = L"kstcpbdfghljmqrvwxyz";
            if (sokuonConsonants.find(current) != std::wstring::npos)
            {
                RomajiKanaConverter::ConversionSpan span;
                span.rawStart = i;
                span.rawEnd = i + 1;
                span.text = L"っ";
                spans.push_back(std::move(span));
                ++i;
                continue;
            }
        }

        if (!matched)
        {
            RomajiKanaConverter::ConversionSpan span;
            span.rawStart = i;
            span.rawEnd = i + 1;
            span.text.assign(1, raw[i]);
            spans.push_back(std::move(span));
            ++i;
        }
    }

    return spans;
}
} // namespace

RomajiKanaConverter::RomajiKanaConverter()
    : m_maxKeyLength(1)
{
    ReloadFromSettings();
}

void RomajiKanaConverter::ReloadFromSettings()
{
    m_romajiToKana.clear();
    m_loadedMapPath.clear();

    for (const std::filesystem::path& candidate : GetRomajiMapFiles())
    {
        Map loadedMap;
        if (LoadMapFile(candidate, &loadedMap))
        {
            m_romajiToKana = std::move(loadedMap);
            m_loadedMapPath = candidate;
            break;
        }
    }

    if (m_romajiToKana.empty())
    {
        m_romajiToKana = CreateDefaultMap();
    }

    m_maxKeyLength = ComputeMaxKeyLength(m_romajiToKana);

    if (!m_loadedMapPath.empty())
    {
        DebugLog(L"RomajiKanaConverter: loaded map from %s\r\n", m_loadedMapPath.c_str());
    }
    else
    {
        DebugLog(L"RomajiKanaConverter: using embedded fallback map\r\n");
    }
}

wchar_t RomajiKanaConverter::ToHalfWidth(wchar_t ch)
{
    if (ch >= L'A' && ch <= L'Z')
    {
        return L'a' + (ch - L'A');
    }
    if (ch >= L'a' && ch <= L'z')
    {
        return ch;
    }

    if (ch >= L'ａ' && ch <= L'ｚ')
    {
        return L'a' + (ch - L'ａ');
    }
    if (ch >= L'Ａ' && ch <= L'Ｚ')
    {
        return L'a' + (ch - L'Ａ');
    }

    if (ch >= L'０' && ch <= L'９')
    {
        return L'0' + (ch - L'０');
    }

    if (ch == L'［') return L'[';
    if (ch == L'］') return L']';

    return ch;
}

std::wstring RomajiKanaConverter::FullWidthToHalfWidth(const std::wstring& src)
{
    std::wstring dst;
    dst.reserve(src.size());
    for (wchar_t ch : src)
    {
        dst.push_back(ToHalfWidth(ch));
    }
    return dst;
}

std::wstring RomajiKanaConverter::ConvertFromRaw(const std::wstring& raw) const
{
    std::wstring result;
    for (const ConversionSpan& span : BuildConversionSpans(raw))
    {
        result.append(span.text);
    }

    return result;
}

std::vector<RomajiKanaConverter::ConversionSpan> RomajiKanaConverter::BuildConversionSpans(
    const std::wstring& raw) const
{
    return BuildConversionSpansImpl(FullWidthToHalfWidth(raw), m_romajiToKana, m_maxKeyLength);
}
