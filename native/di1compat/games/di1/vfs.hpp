// The 1.0 install's zips read in place by engine path (VFS1). core/zip, core/log, choalias. Any open or loader thread.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include "../../base/text.hpp"
#include "../../core/log.hpp"
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
// Runs f(archive) under the archives' lock (miniz is not thread-safe); null: rel is no zip, or did not open.
template <class F>
void withArchive(const std::string& rel, F&& f);
// The archive names looked up so far, opened or not (A-12: failures are kept too).
inline size_t archiveCount();

namespace detail {

constexpr int kOpenFiles = 8192;   // every 1.0 archive used stays open (miniz holds a FILE*); the CRT default is 512
// Chosen, not measured: a transient failure (a scanner holding the zip) gets kOpenRetries more tries, kRetryMs apart.
constexpr int kOpenRetries = 3;
constexpr ULONGLONG kRetryMs = 1000;

// One archive name the search asked for.
struct Archive {
    std::unique_ptr<mz_zip_archive> zip;   // null: no such zip, or it did not open
    int retriesLeft = 0;                   // tries left after a transient failure; 0: a null zip stays null
    ULONGLONG retryAt = 0;                 // GetTickCount64 from which the next try may run
};

struct State {
    std::string install10;   // set once in install()
    bool assetBufs = false;  // assetbufs10: set once in install()
    bool root = false;       // root10: set once in install()
    std::mutex mx;           // guards zips and each archive's use
    std::map<std::string, Archive> zips;   // guarded by mx; by path under assets
};
inline State g_state;

// Whether a transient failure's next try is due.
inline bool retryDue(const Archive& a) { return !a.zip && a.retriesLeft > 0 && GetTickCount64() >= a.retryAt; }

// An archive's first failed open, with its reason (E6).
inline void logOpenFailure(const std::string& path, const core::ZipOpen& o)
{
    if (o.failure == core::ZipOpenFailure::Transient)
        core::logf("vfs10: %s not opened: %s; tried again up to %d times, %u ms apart", path.c_str(), o.why.c_str(),
                   kOpenRetries, (unsigned)kRetryMs);
    else
        core::logf("vfs10: %s not opened: %s", path.c_str(), o.why.c_str());
}

// rel's archive, kept all session; caller holds mx. Transient open failures are retried; missing and broken zips stay
// null (A-12).
inline mz_zip_archive* zipFor(const std::string& rel)
{
    State& s = g_state;
    auto it = s.zips.find(rel);
    if (it != s.zips.end() && !retryDue(it->second)) return it->second.zip.get();
    const bool first = it == s.zips.end();
    Archive& a = first ? s.zips[rel] : it->second;
    const std::string path = archivePath(s.install10.c_str(), rel);
    core::ZipOpen o = core::openZip(path);
    a.zip = std::move(o.zip);
    if (o.failure != core::ZipOpenFailure::Transient)
        a.retriesLeft = 0;
    else
        a.retriesLeft = first ? kOpenRetries : a.retriesLeft - 1;
    a.retryAt = GetTickCount64() + kRetryMs;
    const bool failed = o.failure == core::ZipOpenFailure::Transient || o.failure == core::ZipOpenFailure::Broken;
    if (first && failed) logOpenFailure(path, o);
    return a.zip.get();
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
