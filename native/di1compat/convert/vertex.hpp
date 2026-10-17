// Vertices converted by a render-table plan, bit-exact with vtxf.py; a primitive's indices made local. Any thread.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "../base/bytes.hpp"
#include "../formats/rentable.hpp"
#include "../formats/vtxf.hpp"

namespace vertex {

// IEEE binary16: sign bit 15, 5 exponent bits (bias 15), 10 mantissa bits
constexpr int kHalfSign = 15, kHalfMantissa = 10, kHalfBias = 15, kHalfExpMax = 31;
constexpr uint16_t kHalfInfinity = 0x7c00, kHalfNan = 0x7e00;

// Vdata (FORMATS.md 4.3): [streams, vertices, (vertex buffer, offset, stride) per stream]
constexpr size_t kVdataHead = 2, kVdataStream = 3;
inline int32_t vdataOffset(const std::vector<int32_t>& vda, size_t s) { return vda[kVdataHead + kVdataStream * s + 1]; }
inline int32_t vdataStride(const std::vector<int32_t>& vda, size_t s) { return vda[kVdataHead + kVdataStream * s + 2]; }

// vda holds the triples of its first `streams` streams
inline bool vdataHolds(const std::vector<int32_t>& vda, size_t streams)
{
    return vda.size() >= kVdataHead && streams <= (vda.size() - kVdataHead) / kVdataStream;
}

// Idata: [index buffer, first index, base vertex, count]
constexpr size_t kIdataFirst = 1, kIdataCount = 3;
constexpr uint32_t kIndexValues16 = 0x10000;   // every value a 16-bit index takes
constexpr size_t kIndexBytes = sizeof(uint16_t);

inline double halfToDouble(uint16_t h)
{
    const int s = h >> kHalfSign, e = (h >> kHalfMantissa) & kHalfExpMax, m = h & ((1 << kHalfMantissa) - 1);
    const int shift = kHalfBias + kHalfMantissa;   // 25: a normal value is (m + 1024) * 2^(e - 25)
    double v = e == 0 ? std::ldexp(m, 1 - shift) : e == kHalfExpMax ? (m ? NAN : INFINITY)
                                                                     : std::ldexp(m + (1 << kHalfMantissa), e - shift);
    return s ? -v : v;
}

// CPython PyFloat_Pack2 (struct 'e'): round half to even straight from the double.
inline uint16_t doubleToHalf(double x)
{
    const int sign = std::signbit(x) ? 1 : 0;
    if (x == 0.0) return (uint16_t)(sign << kHalfSign);
    if (std::isinf(x)) return (uint16_t)((sign << kHalfSign) | kHalfInfinity);
    if (std::isnan(x)) return (uint16_t)((sign << kHalfSign) | kHalfNan);
    int e;
    double f = std::frexp(std::fabs(x), &e);
    f *= 2.0;
    e--;
    if (e > kHalfBias) return (uint16_t)((sign << kHalfSign) | kHalfInfinity);   // overflow: CPython raises
    if (e < -kHalfBias - kHalfMantissa) {
        f = 0.0;
        e = 0;
    } else if (e < 1 - kHalfBias) {   // subnormal
        f = std::ldexp(f, kHalfBias - 1 + e);
        e = 0;
    } else {
        e += kHalfBias;
        f -= 1.0;
    }
    f *= 1 << kHalfMantissa;
    unsigned bits = (unsigned)f;
    const bool above = f - bits > 0.5;
    const bool tieToEven = f - bits == 0.5 && bits % 2 == 1;
    if (above || tieToEven) {
        ++bits;
        if (bits == 1u << kHalfMantissa) {
            bits = 0;
            ++e;
        }
    }
    return (uint16_t)(bits | (e << kHalfMantissa) | (sign << kHalfSign));
}

namespace detail {

// One component of struct code `code` at p
inline double component(char code, const uint8_t* p)
{
    switch (code) {
    case vtxf::kFloat32: {
        float f;
        memcpy(&f, p, sizeof f);
        return f;
    }
    case vtxf::kFloat16: return halfToDouble(bytes::u16At(p));
    case vtxf::kUint8: return p[0];
    case vtxf::kInt16: return (int16_t)bytes::u16At(p);
    default: return bytes::u16At(p);
    }
}

// x (rounded and clamped already) as one component of struct code `code` at out -> past it
inline char* putComponent(char code, double x, char* out)
{
    switch (code) {
    case vtxf::kFloat32: {
        const float f = (float)x;
        memcpy(out, &f, sizeof f);
        return out + sizeof f;
    }
    case vtxf::kFloat16: {
        const uint16_t h = doubleToHalf(x);
        memcpy(out, &h, sizeof h);
        return out + sizeof h;
    }
    case vtxf::kUint8:
        *out = (char)(int)x;
        return out + 1;
    default: {
        const int16_t s = (int16_t)(int)x;
        memcpy(out, &s, sizeof s);
        return out + sizeof s;
    }
    }
}

// Python's rounding: normalised types round half to even after clamping; integers truncate and clamp
inline double rounded(const vtxf::ElementType& v, double x)
{
    if (v.norm) {
        const double lo = v.code == vtxf::kUint8 ? 0.0 : -1.0;
        return std::nearbyint((x < lo ? lo : x > 1.0 ? 1.0 : x) * v.norm);
    }
    if (v.code == vtxf::kFloat32 || v.code == vtxf::kFloat16) return x;
    const double hi = v.code == vtxf::kUint8 ? 255.0 : 65535.0;
    x = std::trunc(x);
    return x < 0 ? 0 : x > hi ? hi : x;
}

}  // namespace detail

// One element of type v at b -> out[0..v.n)
inline void decode(const vtxf::ElementType& v, const uint8_t* b, double* out)
{
    for (int i = 0; i < v.n; ++i) {
        double x = detail::component(v.code, b + i * v.csize);
        if (v.norm) {
            x /= v.norm;
            if (x < -1.0) x = -1.0;
        }
        out[i] = x;
    }
}

// vals[0..v.n) as one element of type v at out (v.n * v.csize bytes)
inline void encode(const vtxf::ElementType& v, const double* vals, char* out)
{
    for (int i = 0; i < v.n; ++i) out = detail::putComponent(v.code, detail::rounded(v, vals[i]), out);
}

// One vertex through plan p into its slot of the destination streams per[0..p.ds.size()), zeroed already
inline void convertVertex(const std::vector<const uint8_t*>& src, const rentable::Plan& p,
                          std::vector<std::string>& per, size_t slot)
{
    for (const rentable::Op& o : p.ops) {
        char* d = &per[o.dsi][slot * p.ds[o.dsi] + o.doff];
        if (o.kind == rentable::kConstant) {
            memcpy(d, o.bytes.data(), o.size < (int)o.bytes.size() ? o.size : o.bytes.size());
            continue;
        }
        if (o.kind == rentable::kCopy) {
            memcpy(d, src[o.ssi] + o.soff, o.size);
            continue;
        }
        double vals[rentable::kMaxComponents] = {0, 0, 0, 0};   // source values, then defaults, then zeros
        decode(o.sv, src[o.ssi] + o.soff, vals);
        for (int i = o.sv.n; i < o.dv.n; ++i) vals[i] = i < (int)o.dflt.size() ? o.dflt[i] : 0.0;
        char e[rentable::kMaxComponents * sizeof(float)];
        encode(o.dv, vals, e);
        const int n = o.dv.n * o.dv.csize;
        memcpy(d, e, o.size < n ? o.size : n);
    }
}

// 16-bit index -> local index, stamped per primitive: a flat table, not a hash map per index (in_city: ~4M indices)
struct Remap { std::vector<uint32_t> stamp, to; uint32_t gen = 0; };

// The primitive's indices made local: order = the vertices it uses, first use first; sub = its local indices
inline const char* remapIndices(const std::vector<int32_t>& ida, const std::string& ibuf, Remap& m,
                                std::vector<uint32_t>& order, std::vector<uint32_t>& sub)
{
    const int32_t first = ida[kIdataFirst], count = ida[kIdataCount];
    const size_t at = kIndexBytes * (size_t)first, len = kIndexBytes * (size_t)count;
    const bool inside = first >= 0 && bytes::fits(ibuf.size(), at, len);
    if (count > 0 && !inside) return "index past the ibuf";
    const uint8_t* ib = (const uint8_t*)ibuf.data();
    sub.reserve(count > 0 ? (size_t)count : 0);
    ++m.gen;
    for (int32_t q = 0; q < count; ++q) {
        const uint32_t v = bytes::u16At(ib + kIndexBytes * ((size_t)first + q));
        if (m.stamp[v] != m.gen) {
            m.stamp[v] = m.gen;
            m.to[v] = (uint32_t)order.size();
            order.push_back(v);
        }
        sub.push_back(m.to[v]);
    }
    return nullptr;
}

// [off, off + (last + 1) * stride) lies inside the vertex buffer: every vertex up to index last
inline bool vertexFits(int32_t off, int32_t stride, uint32_t last, size_t size)
{
    if (off < 0 || stride < 0) return false;
    const uint64_t span = ((uint64_t)last + 1) * (uint32_t)stride;
    return span <= size && bytes::fits(size, (size_t)off, (size_t)span);
}

// Every vertex of order through plan into per (one string per destination stream); strides and ranges checked first
inline const char* convertPrim(const rentable::Plan& plan, const std::vector<int32_t>& vda,
                               const std::vector<uint32_t>& order, const std::string& vbuf,
                               std::vector<std::string>& per)
{
    for (int s = 0; s < vda[0]; ++s)
        if (s >= (int)plan.ss.size() || vdataStride(vda, s) != plan.ss[s]) return "stride mismatch";
    if (!vdataHolds(vda, plan.ss.size())) return "Vdata shorter than the plan";
    const uint32_t last = order.empty() ? 0 : *std::max_element(order.begin(), order.end());
    for (size_t s = 0; s < plan.ss.size() && !order.empty(); ++s)
        if (!vertexFits(vdataOffset(vda, s), plan.ss[s], last, vbuf.size())) return "vertex past the vbuf";
    per.assign(plan.ds.size(), std::string());
    for (size_t s = 0; s < plan.ds.size(); ++s) per[s].assign(order.size() * (size_t)plan.ds[s], '\0');
    if (order.empty()) return nullptr;   // the offsets were checked only for a primitive with vertices
    std::vector<const uint8_t*> streams(plan.ss.size()), srcp(plan.ss.size());
    for (size_t s = 0; s < plan.ss.size(); ++s) streams[s] = (const uint8_t*)vbuf.data() + vdataOffset(vda, s);
    for (size_t i = 0; i < order.size(); ++i) {
        for (size_t s = 0; s < plan.ss.size(); ++s) srcp[s] = streams[s] + (size_t)order[i] * plan.ss[s];
        convertVertex(srcp, plan, per, i);
    }
    return nullptr;
}

}  // namespace vertex
