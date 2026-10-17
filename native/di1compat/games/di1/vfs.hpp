// The 1.0 install's zips read in place by engine path (VFS1). core/zip.hpp, choalias. Any open or loader thread.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "../../base/text.hpp"
#include "../../core/zip.hpp"
#include "../../third_party/miniz/miniz.h"
#include "archives.hpp"
#include "choalias.hpp"
#include "config.hpp"

namespace di1::vfs {

// Keeps the keys the reads follow (install10, assetbufs10, root10) and raises the CRT's open-file limit.
inline bool install(const Config& c);
// An engine path's 1.0 member, or its choalias10 alias's: heap bytes (mz_free them) or nullptr. where: "zip:member".
inline void* read10(const char* enginePath, size_t* size, std::string* where);
// read10 into out; false: no such 1.0 file, out untouched.
inline bool read10s(const std::string& path, std::string& out);
// Runs f(archive) under the archives' lock (miniz is not thread-safe); null: rel is no zip of the install.
template <class F>
void withArchive(const std::string& rel, F&& f);
// The archive names looked up so far, opened or not (A-12: failures are kept too).
inline size_t archiveCount();

namespace detail {

constexpr int kOpenFiles = 8192;   // every 1.0 archive used stays open (miniz holds a FILE*); the CRT default is 512

struct State {
    std::string install10;   // set once in install()
    bool assetBufs = false;  // assetbufs10: set once in install()
    bool root = false;       // root10: set once in install()
    std::mutex mx;           // guards zips and each archive's use
    std::map<std::string, std::unique_ptr<mz_zip_archive>> zips;   // by path under assets; nullptr: no such zip
};
inline State g_state;

// The archive rel, opened on first use and kept for the session; failures too (A-12). Caller holds mx.
inline mz_zip_archive* zipFor(const std::string& rel)
{
    State& s = g_state;
    auto it = s.zips.find(rel);
    if (it != s.zips.end()) return it->second.get();
    return (s.zips[rel] = core::openZip(archivePath(s.install10.c_str(), rel))).get();
}

// p's member from the first archive that has it: the candidates of each split, deepest first, then (root10) the roots.
inline void* read10z(const std::string& p, size_t* size, std::string* where)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    const AssetBuffers buffers = s.assetBufs ? AssetBuffers::Search : AssetBuffers::Skip;
    for (size_t cut = p.rfind('/'); cut != std::string::npos && cut > 0; cut = p.rfind('/', cut - 1)) {
        std::string member = p.substr(cut + 1);
        for (const std::string& zr : candidateArchives(p, cut, buffers)) {
            mz_zip_archive* z = zr.empty() ? nullptr : zipFor(zr);
            void* data = z ? core::extractZipMember(z, member.c_str(), size) : nullptr;
            if (!data) continue;
            *where = zr + ":" + member;
            return data;
        }
    }
    if (!s.root) return nullptr;
    for (const char* zr : kRootArchives) {
        mz_zip_archive* z = zipFor(zr);
        void* data = z ? core::extractZipMember(z, p.c_str(), size) : nullptr;
        if (!data) continue;
        *where = std::string(zr) + ":" + p;
        return data;
    }
    return nullptr;
}

}  // namespace detail

inline void* read10(const char* enginePath, size_t* size, std::string* where)
{
    std::string p = text::norm(enginePath);
    void* d = detail::read10z(p, size, where);
    std::string a = d ? "" : choalias::resolve(p);
    return a.empty() ? d : detail::read10z(a, size, where);
}

inline bool read10s(const std::string& path, std::string& out)
{
    size_t n = 0;
    std::string where;
    void* d = read10(path.c_str(), &n, &where);
    if (!d) return false;
    out.assign((const char*)d, n);
    mz_free(d);
    return true;
}

template <class F>
void withArchive(const std::string& rel, F&& f)
{
    std::lock_guard<std::mutex> lock(detail::g_state.mx);
    f(detail::zipFor(rel));
}

inline size_t archiveCount()
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    return s.zips.size();
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.install10 = c.install10;
    s.assetBufs = c.assetBufs10;
    s.root = c.root10;
    _setmaxstdio(detail::kOpenFiles);
    return true;
}

}  // namespace di1::vfs
