// Config files of "key = value" lines, read into a struct through a table of keys: each row names a key, its kind
// and the field it sets. Standard library only. Read once, before any hook is installed (CLAUDE.md S4).
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace core {

enum class ConfigKind {
    Flag,   // atoi(value) != 0: "true" and "yes" read as false
    Int,    // atoi(value)
    Text,   // the value as written; the field holds at least 260 bytes
};

// One key: its exact spelling and the field it sets (offsetof; sizeof the field for Text).
struct ConfigKey {
    const char* key;
    ConfigKind kind;
    size_t offset;
    size_t size = 0;
};

namespace detail {
template <class Cfg>
void setConfigField(Cfg& cfg, const ConfigKey& k, const char* value)
{
    char* field = reinterpret_cast<char*>(&cfg) + k.offset;
    switch (k.kind) {
    case ConfigKind::Flag: *reinterpret_cast<bool*>(field) = atoi(value) != 0; break;
    case ConfigKind::Int: *reinterpret_cast<int*>(field) = atoi(value); break;
    case ConfigKind::Text: strcpy_s(field, k.size, value); break;
    }
}
}  // namespace detail

// Sets the field of each line's key. Lines are " key = value": key up to 63 characters, value up to 259 (cut there,
// trailing spaces and '#' kept). Lines that start with '#', other lines and unknown keys are skipped; keys match
// exactly, case too; the last line of a key wins. A line over 599 bytes is read as two.
template <class Cfg, size_t N>
void readConfig(FILE* f, Cfg& cfg, const ConfigKey (&keys)[N])
{
    char line[600];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[260];   // the format's widths plus the NUL
        if (line[0] == '#' || sscanf_s(line, " %63[^= ] = %259[^\r\n]", key, (unsigned)sizeof(key), val, (unsigned)sizeof(val)) != 2)
            continue;
        for (const ConfigKey& k : keys) {
            if (strcmp(key, k.key)) continue;
            detail::setConfigField(cfg, k, val);
            break;
        }
    }
}

}  // namespace core
