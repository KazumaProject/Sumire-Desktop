// RomajiKanaConverter.cpp
#include "RomajiKanaConverter.h"

namespace {

    // Kotlin ÇÃ getDefaultMapData Ç∆ìØÇ∂ì‡óeÇÃÉeÅ[ÉuÉã
    std::unordered_map<std::wstring, RomajiKanaConverter::MapEntry> CreateDefaultMap()
    {
        using Entry = RomajiKanaConverter::MapEntry;
        std::unordered_map<std::wstring, Entry> m;

        // punctuation / symbols
        m.emplace(L"-", Entry{ L"Å[", 1 });
        m.emplace(L"~", Entry{ L"?", 1 });
        m.emplace(L".", Entry{ L"ÅB", 1 });
        m.emplace(L",", Entry{ L"ÅA", 1 });
        m.emplace(L"z/", Entry{ L"ÅE", 2 });
        m.emplace(L"z.", Entry{ L"Åc", 2 });
        m.emplace(L"z,", Entry{ L"Åd", 2 });
        m.emplace(L"zh", Entry{ L"Å©", 2 });
        m.emplace(L"zj", Entry{ L"Å´", 2 });
        m.emplace(L"zk", Entry{ L"Å™", 2 });
        m.emplace(L"zl", Entry{ L"Å®", 2 });
        m.emplace(L"z-", Entry{ L"?", 2 });
        m.emplace(L"z[", Entry{ L"Åw", 2 });
        m.emplace(L"z]", Entry{ L"Åx", 2 });
        m.emplace(L"[", Entry{ L"Åu", 1 });
        m.emplace(L"]", Entry{ L"Åv", 1 });

        // v-row
        m.emplace(L"va", Entry{ L"?Çü", 2 });
        m.emplace(L"vi", Entry{ L"?Ç°", 2 });
        m.emplace(L"vu", Entry{ L"?",   2 });
        m.emplace(L"ve", Entry{ L"?Ç•", 2 });
        m.emplace(L"vo", Entry{ L"?Çß", 2 });
        m.emplace(L"vya", Entry{ L"?Ç·", 3 });
        m.emplace(L"vyi", Entry{ L"?Ç°", 3 });
        m.emplace(L"vyu", Entry{ L"?Ç„", 3 });
        m.emplace(L"vye", Entry{ L"?Ç•", 3 });
        m.emplace(L"vyo", Entry{ L"?ÇÂ", 3 });

        // gemination (small tsu + consonant)
        m.emplace(L"qq", Entry{ L"Ç¡", 2 });
        m.emplace(L"vv", Entry{ L"Ç¡", 2 });
        m.emplace(L"ll", Entry{ L"Ç¡", 2 });
        m.emplace(L"xx", Entry{ L"Ç¡", 2 });
        m.emplace(L"kk", Entry{ L"Ç¡", 2 });
        m.emplace(L"gg", Entry{ L"Ç¡", 2 });
        m.emplace(L"ss", Entry{ L"Ç¡", 2 });
        m.emplace(L"zz", Entry{ L"Ç¡", 2 });
        m.emplace(L"jj", Entry{ L"Ç¡", 2 });
        m.emplace(L"tt", Entry{ L"Ç¡", 2 });
        m.emplace(L"tch", Entry{ L"Ç¡", 3 });
        m.emplace(L"dd", Entry{ L"Ç¡", 2 });
        m.emplace(L"hh", Entry{ L"Ç¡", 2 });
        m.emplace(L"ff", Entry{ L"Ç¡", 2 });
        m.emplace(L"bb", Entry{ L"Ç¡", 2 });
        m.emplace(L"pp", Entry{ L"Ç¡", 2 });
        m.emplace(L"mm", Entry{ L"Ç¡", 2 });
        m.emplace(L"yy", Entry{ L"Ç¡", 2 });
        m.emplace(L"rr", Entry{ L"Ç¡", 2 });
        m.emplace(L"ww", Entry{ L"Ç¡", 2 });
        m.emplace(L"www", Entry{ L"www", 3 });
        m.emplace(L"cc", Entry{ L"Ç¡", 2 });

        // k-row youon
        m.emplace(L"kya", Entry{ L"Ç´Ç·", 3 });
        m.emplace(L"kyi", Entry{ L"Ç´Ç°", 3 });
        m.emplace(L"kyu", Entry{ L"Ç´Ç„", 3 });
        m.emplace(L"kye", Entry{ L"Ç´Ç•", 3 });
        m.emplace(L"kyo", Entry{ L"Ç´ÇÂ", 3 });

        // g-row youon
        m.emplace(L"gya", Entry{ L"Ç¨Ç·", 3 });
        m.emplace(L"gyi", Entry{ L"Ç¨Ç°", 3 });
        m.emplace(L"gyu", Entry{ L"Ç¨Ç„", 3 });
        m.emplace(L"gye", Entry{ L"Ç¨Ç•", 3 });
        m.emplace(L"gyo", Entry{ L"Ç¨ÇÂ", 3 });

        // s-row
        m.emplace(L"sya", Entry{ L"ÇµÇ·", 3 });
        m.emplace(L"syi", Entry{ L"ÇµÇ°", 3 });
        m.emplace(L"syu", Entry{ L"ÇµÇ„", 3 });
        m.emplace(L"sye", Entry{ L"ÇµÇ•", 3 });
        m.emplace(L"syo", Entry{ L"ÇµÇÂ", 3 });
        m.emplace(L"sha", Entry{ L"ÇµÇ·", 3 });
        m.emplace(L"shi", Entry{ L"Çµ",   3 });
        m.emplace(L"shu", Entry{ L"ÇµÇ„", 3 });
        m.emplace(L"she", Entry{ L"ÇµÇ•", 3 });
        m.emplace(L"sho", Entry{ L"ÇµÇÂ", 3 });

        // n-row
        m.emplace(L"na", Entry{ L"Ç»", 2 });
        m.emplace(L"ni", Entry{ L"Ç…", 2 });
        m.emplace(L"nu", Entry{ L"Ç ", 2 });
        m.emplace(L"ne", Entry{ L"ÇÀ", 2 });
        m.emplace(L"no", Entry{ L"ÇÃ", 2 });

        // k-row
        m.emplace(L"ca", Entry{ L"Ç©", 2 });
        m.emplace(L"ka", Entry{ L"Ç©", 2 });
        m.emplace(L"ki", Entry{ L"Ç´", 2 });
        m.emplace(L"ku", Entry{ L"Ç≠", 2 });
        m.emplace(L"ke", Entry{ L"ÇØ", 2 });
        m.emplace(L"ko", Entry{ L"Ç±", 2 });

        // s-row basic
        m.emplace(L"sa", Entry{ L"Ç≥", 2 });
        m.emplace(L"si", Entry{ L"Çµ", 2 });
        m.emplace(L"su", Entry{ L"Ç∑", 2 });
        m.emplace(L"se", Entry{ L"Çπ", 2 });
        m.emplace(L"so", Entry{ L"Çª", 2 });

        // g-row basic
        m.emplace(L"ga", Entry{ L"Ç™", 2 });
        m.emplace(L"gi", Entry{ L"Ç¨", 2 });
        m.emplace(L"gu", Entry{ L"ÇÆ", 2 });
        m.emplace(L"ge", Entry{ L"Ç∞", 2 });
        m.emplace(L"go", Entry{ L"Ç≤", 2 });

        // z-row
        m.emplace(L"zya", Entry{ L"Ç∂Ç·", 3 });
        m.emplace(L"zyi", Entry{ L"Ç∂Ç°", 3 });
        m.emplace(L"zyu", Entry{ L"Ç∂Ç„", 3 });
        m.emplace(L"zye", Entry{ L"Ç∂Ç•", 3 });
        m.emplace(L"zyo", Entry{ L"Ç∂ÇÂ", 3 });
        m.emplace(L"za", Entry{ L"Ç¥",  2 });
        m.emplace(L"zi", Entry{ L"Ç∂",  2 });
        m.emplace(L"zu", Entry{ L"Ç∏",  2 });
        m.emplace(L"ze", Entry{ L"Ç∫",  2 });
        m.emplace(L"zo", Entry{ L"Çº",  2 });

        m.emplace(L"jya", Entry{ L"Ç∂Ç·", 3 });
        m.emplace(L"jyi", Entry{ L"Ç∂Ç°", 3 });
        m.emplace(L"jyu", Entry{ L"Ç∂Ç„", 3 });
        m.emplace(L"jye", Entry{ L"Ç∂Ç•", 3 });
        m.emplace(L"jyo", Entry{ L"Ç∂ÇÂ", 3 });
        m.emplace(L"ja", Entry{ L"Ç∂Ç·", 2 });
        m.emplace(L"ji", Entry{ L"Ç∂",   2 });
        m.emplace(L"ju", Entry{ L"Ç∂Ç„", 2 });
        m.emplace(L"je", Entry{ L"Ç∂Ç•", 2 });
        m.emplace(L"jo", Entry{ L"Ç∂ÇÂ", 2 });

        // t-row youon & variants
        m.emplace(L"tya", Entry{ L"ÇøÇ·", 3 });
        m.emplace(L"tyi", Entry{ L"ÇøÇ°", 3 });
        m.emplace(L"tyu", Entry{ L"ÇøÇ„", 3 });
        m.emplace(L"tye", Entry{ L"ÇøÇ•", 3 });
        m.emplace(L"tyo", Entry{ L"ÇøÇÂ", 3 });
        m.emplace(L"cha", Entry{ L"ÇøÇ·", 3 });
        m.emplace(L"chi", Entry{ L"Çø",   3 });
        m.emplace(L"chu", Entry{ L"ÇøÇ„", 3 });
        m.emplace(L"che", Entry{ L"ÇøÇ•", 3 });
        m.emplace(L"cho", Entry{ L"ÇøÇÂ", 3 });
        m.emplace(L"cya", Entry{ L"ÇøÇ·", 3 });
        m.emplace(L"cyi", Entry{ L"ÇøÇ°", 3 });
        m.emplace(L"cyu", Entry{ L"ÇøÇ„", 3 });
        m.emplace(L"cye", Entry{ L"ÇøÇ•", 3 });
        m.emplace(L"cyo", Entry{ L"ÇøÇÂ", 3 });

        m.emplace(L"ta", Entry{ L"ÇΩ", 2 });
        m.emplace(L"ti", Entry{ L"Çø", 2 });
        m.emplace(L"tu", Entry{ L"Ç¬", 2 });
        m.emplace(L"tsu", Entry{ L"Ç¬", 3 });
        m.emplace(L"te", Entry{ L"Çƒ", 2 });
        m.emplace(L"to", Entry{ L"Ç∆", 2 });

        // d-row youon & variants
        m.emplace(L"dya", Entry{ L"Ç¿Ç·", 3 });
        m.emplace(L"dyi", Entry{ L"Ç¿Ç°", 3 });
        m.emplace(L"dyu", Entry{ L"Ç¿Ç„", 3 });
        m.emplace(L"dye", Entry{ L"Ç¿Ç•", 3 });
        m.emplace(L"dyo", Entry{ L"Ç¿ÇÂ", 3 });
        m.emplace(L"da", Entry{ L"Çæ",   2 });
        m.emplace(L"di", Entry{ L"Ç¿",   2 });
        m.emplace(L"du", Entry{ L"Ç√",   2 });
        m.emplace(L"de", Entry{ L"Ç≈",   2 });
        m.emplace(L"do", Entry{ L"Ç«",   2 });

        // de-y variants
        m.emplace(L"dha", Entry{ L"Ç≈Ç·", 3 });
        m.emplace(L"dhi", Entry{ L"Ç≈Ç°", 3 });
        m.emplace(L"d'i", Entry{ L"Ç≈Ç°", 3 });
        m.emplace(L"dhu", Entry{ L"Ç≈Ç„", 3 });
        m.emplace(L"dhe", Entry{ L"Ç≈Ç•", 3 });
        m.emplace(L"dho", Entry{ L"Ç≈ÇÂ", 3 });
        m.emplace(L"d'yu", Entry{ L"Ç≈Ç„", 4 });

        // t-h variants
        m.emplace(L"tha", Entry{ L"ÇƒÇ·", 3 });
        m.emplace(L"thi", Entry{ L"ÇƒÇ°", 3 });
        m.emplace(L"t'i", Entry{ L"ÇƒÇ°", 3 });
        m.emplace(L"thu", Entry{ L"ÇƒÇ„", 3 });
        m.emplace(L"the", Entry{ L"ÇƒÇ•", 3 });
        m.emplace(L"tho", Entry{ L"ÇƒÇÂ", 3 });
        m.emplace(L"t'yu", Entry{ L"ÇƒÇ„", 4 });

        // t-w variants
        m.emplace(L"twa", Entry{ L"Ç∆Çü", 3 });
        m.emplace(L"twi", Entry{ L"Ç∆Ç°", 3 });
        m.emplace(L"twu", Entry{ L"Ç∆Ç£", 3 });
        m.emplace(L"twe", Entry{ L"Ç∆Ç•", 3 });
        m.emplace(L"two", Entry{ L"Ç∆Çß", 3 });
        m.emplace(L"t'u", Entry{ L"Ç∆Ç£", 3 });

        // d-w variants
        m.emplace(L"dwa", Entry{ L"Ç«Çü", 3 });
        m.emplace(L"dwi", Entry{ L"Ç«Ç°", 3 });
        m.emplace(L"dwu", Entry{ L"Ç«Ç£", 3 });
        m.emplace(L"dwe", Entry{ L"Ç«Ç•", 3 });
        m.emplace(L"dwo", Entry{ L"Ç«Çß", 3 });
        m.emplace(L"d'u", Entry{ L"Ç«Ç£", 3 });

        // n-row youon & n variants
        m.emplace(L"nya", Entry{ L"Ç…Ç·", 3 });
        m.emplace(L"nyi", Entry{ L"Ç…Ç°", 3 });
        m.emplace(L"nyu", Entry{ L"Ç…Ç„", 3 });
        m.emplace(L"nye", Entry{ L"Ç…Ç•", 3 });
        m.emplace(L"nyo", Entry{ L"Ç…ÇÂ", 3 });
        m.emplace(L"nn", Entry{ L"ÇÒ",   2 });
        m.emplace(L"xn", Entry{ L"ÇÒ",   2 });

        // h-row youon & variants
        m.emplace(L"hya", Entry{ L"Ç–Ç·", 3 });
        m.emplace(L"hyi", Entry{ L"Ç–Ç°", 3 });
        m.emplace(L"hyu", Entry{ L"Ç–Ç„", 3 });
        m.emplace(L"hye", Entry{ L"Ç–Ç•", 3 });
        m.emplace(L"hyo", Entry{ L"Ç–ÇÂ", 3 });
        m.emplace(L"ha", Entry{ L"ÇÕ",   2 });
        m.emplace(L"hi", Entry{ L"Ç–",   2 });
        m.emplace(L"hu", Entry{ L"Ç”",   2 });
        m.emplace(L"fu", Entry{ L"Ç”",   2 });
        m.emplace(L"he", Entry{ L"Ç÷",   2 });
        m.emplace(L"ho", Entry{ L"ÇŸ",   2 });

        // b-row youon
        m.emplace(L"bya", Entry{ L"Ç—Ç·", 3 });
        m.emplace(L"byi", Entry{ L"Ç—Ç°", 3 });
        m.emplace(L"byu", Entry{ L"Ç—Ç„", 3 });
        m.emplace(L"bye", Entry{ L"Ç—Ç•", 3 });
        m.emplace(L"byo", Entry{ L"Ç—ÇÂ", 3 });
        m.emplace(L"ba", Entry{ L"ÇŒ",   2 });
        m.emplace(L"bi", Entry{ L"Ç—",   2 });
        m.emplace(L"bu", Entry{ L"Ç‘",   2 });
        m.emplace(L"be", Entry{ L"Ç◊",   2 });
        m.emplace(L"bo", Entry{ L"Ç⁄",   2 });

        // p-row youon
        m.emplace(L"pya", Entry{ L"Ç“Ç·", 3 });
        m.emplace(L"pyi", Entry{ L"Ç“Ç°", 3 });
        m.emplace(L"pyu", Entry{ L"Ç“Ç„", 3 });
        m.emplace(L"pye", Entry{ L"Ç“Ç•", 3 });
        m.emplace(L"pyo", Entry{ L"Ç“ÇÂ", 3 });
        m.emplace(L"pa", Entry{ L"Çœ",   2 });
        m.emplace(L"pi", Entry{ L"Ç“",   2 });
        m.emplace(L"pu", Entry{ L"Ç’",   2 });
        m.emplace(L"pe", Entry{ L"Çÿ",   2 });
        m.emplace(L"po", Entry{ L"Ç€",   2 });

        // f-variants & youon
        m.emplace(L"fa", Entry{ L"Ç”Çü", 2 });
        m.emplace(L"fi", Entry{ L"Ç”Ç°", 2 });
        m.emplace(L"fe", Entry{ L"Ç”Ç•", 2 });
        m.emplace(L"fo", Entry{ L"Ç”Çß", 2 });
        m.emplace(L"fya", Entry{ L"Ç”Ç·", 3 });
        m.emplace(L"fyu", Entry{ L"Ç”Ç„", 3 });
        m.emplace(L"fyo", Entry{ L"Ç”ÇÂ", 3 });
        m.emplace(L"hwa", Entry{ L"Ç”Çü", 3 });
        m.emplace(L"hwi", Entry{ L"Ç”Ç°", 3 });
        m.emplace(L"hwe", Entry{ L"Ç”Ç•", 3 });
        m.emplace(L"hwo", Entry{ L"Ç”Çß", 3 });
        m.emplace(L"hwyu", Entry{ L"Ç”Ç„", 4 });

        // m-row youon
        m.emplace(L"mya", Entry{ L"Ç›Ç·", 3 });
        m.emplace(L"myi", Entry{ L"Ç›Ç°", 3 });
        m.emplace(L"myu", Entry{ L"Ç›Ç„", 3 });
        m.emplace(L"mye", Entry{ L"Ç›Ç•", 3 });
        m.emplace(L"myo", Entry{ L"Ç›ÇÂ", 3 });
        m.emplace(L"ma", Entry{ L"Ç‹",   2 });
        m.emplace(L"mi", Entry{ L"Ç›",   2 });
        m.emplace(L"mu", Entry{ L"Çﬁ",   2 });
        m.emplace(L"me", Entry{ L"Çﬂ",   2 });
        m.emplace(L"mo", Entry{ L"Ç‡",   2 });

        // y-row
        m.emplace(L"xya", Entry{ L"Ç·",   3 });
        m.emplace(L"lya", Entry{ L"Ç·",   3 });
        m.emplace(L"ya", Entry{ L"Ç‚",   2 });
        m.emplace(L"wyi", Entry{ L"ÇÓ",   3 });
        m.emplace(L"xyu", Entry{ L"Ç„",   3 });
        m.emplace(L"lyu", Entry{ L"Ç„",   3 });
        m.emplace(L"yu", Entry{ L"Ç‰",   2 });
        m.emplace(L"wye", Entry{ L"ÇÔ",   3 });
        m.emplace(L"xyo", Entry{ L"ÇÂ",   3 });
        m.emplace(L"lyo", Entry{ L"ÇÂ",   3 });
        m.emplace(L"yo", Entry{ L"ÇÊ",   2 });

        // r-row youon
        m.emplace(L"rya", Entry{ L"ÇËÇ·", 3 });
        m.emplace(L"ryi", Entry{ L"ÇËÇ°", 3 });
        m.emplace(L"ryu", Entry{ L"ÇËÇ„", 3 });
        m.emplace(L"rye", Entry{ L"ÇËÇ•", 3 });
        m.emplace(L"ryo", Entry{ L"ÇËÇÂ", 3 });
        m.emplace(L"ra", Entry{ L"ÇÁ",   2 });
        m.emplace(L"ri", Entry{ L"ÇË",   2 });
        m.emplace(L"ru", Entry{ L"ÇÈ",   2 });
        m.emplace(L"re", Entry{ L"ÇÍ",   2 });
        m.emplace(L"ro", Entry{ L"ÇÎ",   2 });

        // w-row & variants
        m.emplace(L"xwa", Entry{ L"ÇÏ",   3 });
        m.emplace(L"lwa", Entry{ L"ÇÏ",   3 });
        m.emplace(L"wa", Entry{ L"ÇÌ",   2 });
        m.emplace(L"wi", Entry{ L"Ç§Ç°", 2 });
        m.emplace(L"we", Entry{ L"Ç§Ç•", 2 });
        m.emplace(L"wo", Entry{ L"Ç",   2 });
        m.emplace(L"wha", Entry{ L"Ç§Çü", 3 });
        m.emplace(L"whi", Entry{ L"Ç§Ç°", 3 });
        m.emplace(L"whu", Entry{ L"Ç§",   3 });
        m.emplace(L"whe", Entry{ L"Ç§Ç•", 3 });
        m.emplace(L"who", Entry{ L"Ç§Çß", 3 });

        // basic vowels
        m.emplace(L"a", Entry{ L"Ç†", 1 });
        m.emplace(L"i", Entry{ L"Ç¢", 1 });
        m.emplace(L"u", Entry{ L"Ç§", 1 });
        m.emplace(L"wu", Entry{ L"Ç§", 2 });
        m.emplace(L"e", Entry{ L"Ç¶", 1 });
        m.emplace(L"o", Entry{ L"Ç®", 1 });

        // small vowels
        m.emplace(L"xa", Entry{ L"Çü", 2 });
        m.emplace(L"xi", Entry{ L"Ç°", 2 });
        m.emplace(L"xu", Entry{ L"Ç£", 2 });
        m.emplace(L"xe", Entry{ L"Ç•", 2 });
        m.emplace(L"xo", Entry{ L"Çß", 2 });
        m.emplace(L"la", Entry{ L"Çü", 2 });
        m.emplace(L"li", Entry{ L"Ç°", 2 });
        m.emplace(L"lu", Entry{ L"Ç£", 2 });
        m.emplace(L"le", Entry{ L"Ç•", 2 });
        m.emplace(L"lo", Entry{ L"Çß", 2 });
        m.emplace(L"lyi", Entry{ L"Ç°", 3 });
        m.emplace(L"xyi", Entry{ L"Ç°", 3 });
        m.emplace(L"lye", Entry{ L"Ç•", 3 });
        m.emplace(L"xye", Entry{ L"Ç•", 3 });
        m.emplace(L"ye", Entry{ L"Ç¢Ç•", 2 });

        // x-row small kana
        m.emplace(L"xka", Entry{ L"Éï", 3 });
        m.emplace(L"xke", Entry{ L"Éñ", 3 });
        m.emplace(L"lka", Entry{ L"Éï", 3 });
        m.emplace(L"lke", Entry{ L"Éñ", 3 });

        // qa/ku-variants
        m.emplace(L"qa", Entry{ L"Ç≠Çü", 2 });
        m.emplace(L"qi", Entry{ L"Ç≠Ç°", 2 });
        m.emplace(L"qu", Entry{ L"Ç≠",   2 });
        m.emplace(L"qe", Entry{ L"Ç≠Ç•", 2 });
        m.emplace(L"qo", Entry{ L"Ç≠Çß", 2 });

        // kw-variants
        m.emplace(L"kwa", Entry{ L"Ç≠Çü", 3 });
        m.emplace(L"kwi", Entry{ L"Ç≠Ç°", 3 });
        m.emplace(L"kwu", Entry{ L"Ç≠Ç£", 3 });
        m.emplace(L"kwe", Entry{ L"Ç≠Ç•", 3 });
        m.emplace(L"kwo", Entry{ L"Ç≠Çß", 3 });

        // gw-variants
        m.emplace(L"gwa", Entry{ L"ÇÆÇü", 3 });
        m.emplace(L"gwi", Entry{ L"ÇÆÇ°", 3 });
        m.emplace(L"gwu", Entry{ L"ÇÆÇ£", 3 });
        m.emplace(L"gwe", Entry{ L"ÇÆÇ•", 3 });
        m.emplace(L"gwo", Entry{ L"ÇÆÇß", 3 });

        // sw-variants
        m.emplace(L"swa", Entry{ L"Ç∑Çü", 3 });
        m.emplace(L"swi", Entry{ L"Ç∑Ç°", 3 });
        m.emplace(L"swu", Entry{ L"Ç∑Ç£", 3 });
        m.emplace(L"swe", Entry{ L"Ç∑Ç•", 3 });
        m.emplace(L"swo", Entry{ L"Ç∑Çß", 3 });

        // zw-variants
        m.emplace(L"zwa", Entry{ L"Ç∏Çü", 3 });
        m.emplace(L"zwi", Entry{ L"Ç∏Ç°", 3 });
        m.emplace(L"zwu", Entry{ L"Ç∏Ç£", 3 });
        m.emplace(L"zwe", Entry{ L"Ç∏Ç•", 3 });
        m.emplace(L"zwo", Entry{ L"Ç∏Çß", 3 });

        // xtsu / ltsu variants
        m.emplace(L"xtu", Entry{ L"Ç¡", 3 });
        m.emplace(L"xtsu", Entry{ L"Ç¡", 4 });
        m.emplace(L"ltu", Entry{ L"Ç¡", 3 });
        m.emplace(L"ltsu", Entry{ L"Ç¡", 4 });

        return m;
    }

} // anonymous namespace

RomajiKanaConverter::RomajiKanaConverter()
{
    m_romajiToKana = CreateDefaultMap();

    m_maxKeyLength = 1;
    for (const auto& kv : m_romajiToKana)
    {
        int len = static_cast<int>(kv.first.size());
        if (len > m_maxKeyLength)
            m_maxKeyLength = len;
    }
}

wchar_t RomajiKanaConverter::ToHalfWidth(wchar_t ch)
{
    // ëSäpâpéö Å® îºäpâpéöÅiè¨ï∂éöÇ…ëµÇ¶ÇÈÅj
    if (ch >= L'ÇÅ' && ch <= L'Çö')
    {
        return L'a' + (ch - L'ÇÅ');
    }
    if (ch >= L'Ç`' && ch <= L'Çy')
    {
        return L'a' + (ch - L'Ç`');
    }

    // ëSäpêîéö Å® îºäpêîéö
    if (ch >= L'ÇO' && ch <= L'ÇX')
    {
        return L'0' + (ch - L'ÇO');
    }

    // ëSäp [ ] Å® îºäp
    if (ch == L'Åm') return L'[';
    if (ch == L'Ån') return L']';

    // ÇªÇÃëºÇÕÇªÇÃÇ‹Ç‹
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

// RawText(ëSäp) -> SurfaceText(Ç©Ç») Ç…ïœä∑
std::wstring RomajiKanaConverter::ConvertFromRaw(const std::wstring& raw) const
{
    // Ç‹Ç∏ëSäpâpêîÇîºäpÇ…ê≥ãKâª
    std::wstring text = FullWidthToHalfWidth(raw);

    std::wstring result;
    size_t i = 0;

    while (i < text.size())
    {
        wchar_t current = text[i];

        // 1. '[' / ']' ÇÕïœä∑ÇπÇ∏ÇªÇÃÇ‹Ç‹
        if (current == L'[' || current == L']')
        {
            result.push_back(current);
            ++i;
            continue;
        }

        // 2. ë£âπÅiÇ¡ÅjÇÃîªíË: éqâπÇÃèdÇÀÅinn ÇèúÇ≠Åj
        if (i + 1 < text.size() &&
            current == text[i + 1])
        {
            const std::wstring sokuonConsonants = L"kstcpbdfghljmqrvwxyz";
            if (sokuonConsonants.find(current) != std::wstring::npos)
            {
                result.push_back(L'Ç¡');
                ++i; // 1ï∂éöÇæÇØè¡îÔ
                continue;
            }
        }

        // 3. ÅunÅvÇÃì¡ï ÉãÅ[Éã
        if (current == L'n' && i + 1 < text.size())
        {
            wchar_t next = text[i + 1];
            const std::wstring vowels = L"aiueoyn";
            if (vowels.find(next) == std::wstring::npos)
            {
                result.push_back(L'ÇÒ');
                ++i;
                continue;
            }
        }

        // 4. ç≈í∑àÍívÇ≈ romajiToKana Çà¯Ç≠
        bool matched = false;
        for (int len = m_maxKeyLength; len >= 1; --len)
        {
            if (i + len > text.size())
                continue;

            std::wstring segment = text.substr(i, len);
            auto it = m_romajiToKana.find(segment);
            if (it != m_romajiToKana.end())
            {
                const MapEntry& entry = it->second;
                result.append(entry.kana);
                i += entry.consume;
                matched = true;
                break;
            }
        }

        // 5. É}ÉbÉ`ÇµÇ»Ç©Ç¡ÇΩï∂éöÇÕÇªÇÃÇ‹Ç‹
        if (!matched)
        {
            result.push_back(text[i]);
            ++i;
        }
    }

    return result;
}
