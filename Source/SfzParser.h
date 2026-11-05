#pragma once
// ---------------------------------------------------------------------------------------------
// Minimal SFZ parser: turns an .sfz text file into a flat list of playable regions.
//
// Honoured:   <control> default_path, <global>/<master>/<group>/<region> inheritance,
//             sample, key, lokey, hikey, pitch_keycenter, lovel, hivel, #define, #include,
//             // and /* */ comments, note names (c4, f#3, db2), '\' or '/' path separators.
// Skipped:    regions that only play on release (trigger=release / release_key), regions with
//             a '*generator' sample, and regions disabled with hikey=-1.
// Ignored:    everything else (loops, offsets, envelopes, filters, keyswitches, round-robin ...).
//             Regions still load; those opcodes just don't change how they play.
// ---------------------------------------------------------------------------------------------
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sfz
{
struct Region
{
    std::string sample; // path relative to the .sfz file's folder (default_path already applied)
    int lokey = 0, hikey = 127, root = 60;
    int lovel = 0, hivel = 127;
};

struct Result
{
    std::vector<Region> regions;
    int skipped = 0; // regions deliberately not loaded (release triggers, generators, disabled)
};

namespace detail
{
using Opcodes = std::map<std::string, std::string>;

inline std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

inline std::string lower (std::string s)
{
    for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

// Remove // line comments and /* */ block comments (outside nothing else is quoted in SFZ).
inline std::string stripComments (const std::string& in)
{
    std::string out;
    out.reserve (in.size());
    for (size_t i = 0; i < in.size();)
    {
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/')
        {
            while (i < in.size() && in[i] != '\n') ++i;
        }
        else if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < in.size() && ! (in[i] == '*' && in[i + 1] == '/')) { if (in[i] == '\n') out += '\n'; ++i; }
            i += 2;
        }
        else
            out += in[i++];
    }
    return out;
}

// "c4", "F#3", "db2", "-1", "60" -> MIDI note (SFZ convention: c4 = 60). Returns false if invalid.
inline bool parseNote (const std::string& raw, int& out)
{
    const auto s = trim (raw);
    if (s.empty()) return false;

    char* end = nullptr;
    const long n = std::strtol (s.c_str(), &end, 10);
    if (end != s.c_str() && *end == '\0') { out = (int) n; return true; }

    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    const char l = (char) std::tolower ((unsigned char) s[0]);
    if (l < 'a' || l > 'g') return false;
    int semi = semis[l - 'a'];
    size_t i = 1;
    if (i < s.size() && (s[i] == '#' || s[i] == '+')) { ++semi; ++i; }
    else if (i + 1 < s.size() && s[i] == 'b' && (std::isdigit ((unsigned char) s[i + 1]) || s[i + 1] == '-')) { --semi; ++i; }
    if (i >= s.size()) return false;
    const long oct = std::strtol (s.c_str() + i, &end, 10);
    if (end == s.c_str() + i || *end != '\0') return false;
    out = (int) ((oct + 1) * 12 + semi);
    return true;
}

inline std::string replaceAll (std::string s, const std::string& from, const std::string& to)
{
    if (from.empty()) return s;
    for (size_t p = 0; (p = s.find (from, p)) != std::string::npos; p += to.size())
        s.replace (p, from.size(), to);
    return s;
}

struct Scanner
{
    std::function<std::string (const std::string&)> includeReader;
    std::vector<std::pair<std::string, std::string>> defines; // longest names first on lookup
    int includeDepth = 0;

    std::string expand (std::string s) const
    {
        // Substitute longest names first so $KEY2 isn't clobbered by $KEY.
        auto sorted = defines;
        std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
        for (auto& d : sorted) s = replaceAll (s, d.first, d.second);
        return s;
    }

    // Handles #define / #include lines, returns the remaining text with includes inlined.
    std::string preprocess (const std::string& text)
    {
        std::string out;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find ('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            const std::string line = text.substr (pos, eol - pos);
            const auto t = trim (line);

            if (t.rfind ("#define", 0) == 0)
            {
                std::string rest = trim (t.substr (7));
                const size_t sp = rest.find_first_of (" \t");
                if (sp != std::string::npos)
                    defines.emplace_back (rest.substr (0, sp), trim (rest.substr (sp)));
            }
            else if (t.rfind ("#include", 0) == 0 && includeReader && includeDepth < 8)
            {
                const size_t a = t.find ('"'), b = t.rfind ('"');
                if (a != std::string::npos && b > a)
                {
                    ++includeDepth;
                    out += preprocess (stripComments (includeReader (t.substr (a + 1, b - a - 1))));
                    --includeDepth;
                    out += '\n';
                }
            }
            else
            {
                out += expand (line);
                out += '\n';
            }
            pos = eol + 1;
        }
        return out;
    }
};

inline bool isIdent (char c) { return std::isalnum ((unsigned char) c) || c == '_' || c == '$'; }
} // namespace detail

// `readInclude` (optional) returns the text of an #include'd file given its relative path.
inline Result parse (const std::string& fileText,
                     const std::function<std::string (const std::string&)>& readInclude = {})
{
    using namespace detail;

    Scanner sc;
    sc.includeReader = readInclude;
    const std::string text = sc.preprocess (stripComments (fileText));

    Result result;
    Opcodes control, global, master, group, region;
    enum class Scope { None, Control, Global, Master, Group, Region, Ignored } scope = Scope::None;
    std::string defaultPath;
    bool haveRegion = false;

    auto flushRegion = [&]
    {
        if (! haveRegion) return;
        haveRegion = false;

        Opcodes o = global;
        for (auto* layer : { &master, &group, &region })
            for (auto& kv : *layer) o[kv.first] = kv.second;

        auto get = [&o] (const char* k) -> const std::string* { auto it = o.find (k); return it == o.end() ? nullptr : &it->second; };

        Region r;
        const auto* sample = get ("sample");
        if (sample == nullptr || trim (*sample).empty()) return;
        if ((*sample)[0] == '*') { ++result.skipped; return; } // built-in oscillator

        if (const auto* trig = get ("trigger"))
            if (lower (trim (*trig)) == "release" || lower (trim (*trig)) == "release_key") { ++result.skipped; return; }

        int v = 0;
        if (const auto* k = get ("key"); k && parseNote (*k, v)) r.lokey = r.hikey = r.root = v;
        if (const auto* k = get ("lokey"); k && parseNote (*k, v)) r.lokey = v;
        if (const auto* k = get ("hikey"); k && parseNote (*k, v)) r.hikey = v;
        if (const auto* k = get ("pitch_keycenter"); k && parseNote (*k, v)) r.root = v;
        if (const auto* k = get ("lovel"); k && parseNote (*k, v)) r.lovel = v;
        if (const auto* k = get ("hivel"); k && parseNote (*k, v)) r.hivel = v;

        if (r.hikey < 0 || r.hivel < 0) { ++result.skipped; return; } // disabled region
        r.lokey = std::max (0, std::min (127, r.lokey));
        r.hikey = std::max (0, std::min (127, r.hikey));
        r.lovel = std::max (0, std::min (127, r.lovel));
        r.hivel = std::max (0, std::min (127, r.hivel));
        if (r.lokey > r.hikey || r.lovel > r.hivel) { ++result.skipped; return; }

        std::string path = trim (*sample);
        std::string dp = trim (defaultPath);
        for (auto& c : path) if (c == '\\') c = '/';
        for (auto& c : dp) if (c == '\\') c = '/';
        if (! dp.empty() && dp.back() != '/') dp += '/';
        r.sample = dp + path;
        result.regions.push_back (std::move (r));
    };

    auto store = [&] (const std::string& name, const std::string& value)
    {
        const auto key = lower (name);
        switch (scope)
        {
            case Scope::Control: if (key == "default_path") defaultPath = value; break;
            case Scope::Global:  global[key] = value; break;
            case Scope::Master:  master[key] = value; break;
            case Scope::Group:   group[key] = value; break;
            case Scope::Region:  region[key] = value; break;
            case Scope::None:
            case Scope::Ignored: break;
        }
    };

    const size_t n = text.size();
    size_t i = 0;
    while (i < n)
    {
        if (std::isspace ((unsigned char) text[i])) { ++i; continue; }

        if (text[i] == '<')
        {
            const size_t close = text.find ('>', i);
            if (close == std::string::npos) break;
            const auto name = lower (trim (text.substr (i + 1, close - i - 1)));
            i = close + 1;

            flushRegion();
            if (name == "control")      { scope = Scope::Control; control.clear(); }
            else if (name == "global")  { scope = Scope::Global; global.clear(); master.clear(); group.clear(); }
            else if (name == "master")  { scope = Scope::Master; master.clear(); group.clear(); }
            else if (name == "group")   { scope = Scope::Group; group.clear(); }
            else if (name == "region")  { scope = Scope::Region; region.clear(); haveRegion = true; }
            else                        { scope = Scope::Ignored; } // <curve>, <effect>, <midi>, ...
            continue;
        }

        // opcode=value
        size_t nameEnd = i;
        while (nameEnd < n && isIdent (text[nameEnd])) ++nameEnd;
        if (nameEnd >= n || text[nameEnd] != '=' || nameEnd == i)
        {
            while (i < n && ! std::isspace ((unsigned char) text[i])) ++i; // junk token
            continue;
        }

        const std::string name = text.substr (i, nameEnd - i);
        const size_t vStart = nameEnd + 1;
        size_t vEnd = vStart;
        // The value runs to end of line, or to the next " opcode=" / "<header>" on the same line.
        while (vEnd < n && text[vEnd] != '\n' && text[vEnd] != '<')
        {
            if (std::isspace ((unsigned char) text[vEnd]))
            {
                size_t k = vEnd;
                while (k < n && text[k] != '\n' && std::isspace ((unsigned char) text[k])) ++k;
                size_t m = k;
                while (m < n && isIdent (text[m])) ++m;
                if (m > k && m < n && text[m] == '=') break; // next opcode starts here
            }
            ++vEnd;
        }
        store (name, trim (text.substr (vStart, vEnd - vStart)));
        i = vEnd;
    }
    flushRegion();
    return result;
}
} // namespace sfz
