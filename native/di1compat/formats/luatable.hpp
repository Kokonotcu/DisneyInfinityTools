// A Lua table constructor read as data: numbers, strings, booleans, nested tables. std only; any thread.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace luatable {

enum class Kind { Nil, Number, String, Boolean, Table };

// One value; a table keeps 'name = value' fields in kv and positional ones in arr.
struct Value {
    Kind t = Kind::Nil;
    double n = 0;   // a number, or a boolean as 0 / 1
    std::string s;
    std::map<std::string, Value, std::less<>> kv;
    std::vector<Value> arr;
    const Value* get(const char* k) const
    {
        auto it = kv.find(k);
        return it == kv.end() ? nullptr : &it->second;
    }
    double num(const char* k, double d) const
    {
        const Value* v = get(k);
        const bool numeric = v && (v->t == Kind::Number || v->t == Kind::Boolean);
        return numeric ? v->n : d;
    }
    std::string str(const char* k) const
    {
        const Value* v = get(k);
        return v && v->t == Kind::String ? v->s : "";
    }
};

// Reads one value from [p, e). Line comments are skipped; long brackets and bare names fail (AU-34).
class Parser {
public:
    Parser(const char* begin, const char* end) : p(begin), e(end) {}
    bool value(Value& v, int depth = 0)
    {
        constexpr int kMaxDepth = 32;
        ws();
        if (p >= e || depth > kMaxDepth) return false;
        if (*p == '{') return table(v, depth);
        if (*p == '"' || *p == '\'') return quoted(v);
        if (word("true")) {
            v.t = Kind::Boolean;
            v.n = 1;
            return true;
        }
        if (word("false")) {
            v.t = Kind::Boolean;
            v.n = 0;
            return true;
        }
        if (word("nil")) return true;
        return number(v);
    }

private:
    const char* p;
    const char* e;

    static bool isIdent(char c) { return isalnum((uint8_t)c) || c == '_'; }
    static bool isNumberChar(char c)
    {
        const bool mark = c != '\0' && strchr(".-+", c) != nullptr;
        return isalnum((uint8_t)c) || mark;
    }

    void ws()
    {
        for (;;) {
            while (p < e && isspace((uint8_t)*p)) ++p;
            if (e - p < 2 || p[0] != '-' || p[1] != '-') return;
            while (p < e && *p != '\n') ++p;
        }
    }

    bool word(const char* w)
    {
        const size_t n = strlen(w);
        if ((size_t)(e - p) < n || memcmp(p, w, n)) return false;
        const bool longer = p + n < e && isIdent(p[n]);
        if (longer) return false;
        p += n;
        return true;
    }

    bool table(Value& v, int depth)
    {
        ++p;
        v.t = Kind::Table;
        for (;;) {
            ws();
            if (p >= e) return false;
            if (*p == '}') {
                ++p;
                return true;
            }
            if (!field(v, depth)) return false;
            ws();
            if (p < e && (*p == ',' || *p == ';')) ++p;
        }
    }

    // 'name = value' (not '==') into kv, anything else into arr
    bool field(Value& v, int depth)
    {
        const char* at = p;
        std::string name;
        while (p < e && isIdent(*p)) name += *p++;
        ws();
        const bool named = !name.empty() && !isdigit((uint8_t)name[0]);
        const bool assign = p < e && *p == '=' && (p + 1 >= e || p[1] != '=');
        if (named && assign) {
            ++p;
            return value(v.kv[name], depth + 1);
        }
        p = at;
        v.arr.emplace_back();
        return value(v.arr.back(), depth + 1);
    }

    bool quoted(Value& v)
    {
        const char q = *p++;
        v.t = Kind::String;
        while (p < e && *p != q) {
            if (*p == '\\' && p + 1 < e) ++p;
            v.s += *p++;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }

    bool number(Value& v)
    {
        std::string num;
        while (p < e && isNumberChar(*p)) num += *p++;
        char* end = nullptr;
        v.n = strtod(num.c_str(), &end);
        v.t = Kind::Number;
        return !num.empty() && end && !*end;
    }
};

}  // namespace luatable
