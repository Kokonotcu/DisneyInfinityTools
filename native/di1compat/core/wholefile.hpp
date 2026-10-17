// A whole file read into memory. CRT stdio only; any thread.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace core {

// The file's bytes into out; false when it is missing, empty or short. A file that opened always resizes out.
inline bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* fp = nullptr;
    const errno_t err = fopen_s(&fp, path.c_str(), "rb");
    if (err || !fp) return false;
    fseek(fp, 0, SEEK_END);
    const long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const bool ok = n > 0 && fread(out.data(), 1, out.size(), fp) == out.size();
    fclose(fp);
    return ok;
}

}  // namespace core
