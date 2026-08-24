// The save's byte-stream primitives — POD writer/reader with fail-closed
// bounds. Lifted out of save.cpp (2026-08-24) so the world-field registry
// rows (macro/world_fields.h) can serialize themselves: the field table owns
// its bytes, save.cpp owns the order of everything else.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sm::savefmt {

inline constexpr std::uint32_t kMaxStringBytes = 1u << 20;

struct Writer {
    std::vector<std::uint8_t> bytes;
    bool ok = true;

    template <class T>
    void pod(const T& v) {
        if (!ok) return;
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }

    void str(const std::string& s) {
        if (s.size() > kMaxStringBytes) {
            ok = false;
            return;
        }
        const auto n = static_cast<std::uint32_t>(s.size());
        pod(n);
        if (n > 0) bytes.insert(bytes.end(), s.begin(), s.end());
    }

    bool count(std::size_t n, std::uint32_t cap) {
        if (n > cap || n > 0xffffffffull) {
            ok = false;
            return false;
        }
        const auto out = static_cast<std::uint32_t>(n);
        pod(out);
        return true;
    }
};

struct Reader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    bool ok = true;

    template <class T>
    void pod(T& v) {
        if (!ok) return;
        if (size - pos < sizeof(T)) {
            ok = false;
            return;
        }
        std::memcpy(&v, data + pos, sizeof(T));
        pos += sizeof(T);
    }

    void str(std::string& s) {
        std::uint32_t n = 0;
        pod(n);
        if (!ok || n > kMaxStringBytes || size - pos < n) {
            ok = false;
            return;
        }
        s.assign(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
    }
};

inline bool read_count(Reader& r, std::uint32_t& n, std::uint32_t cap) {
    n = 0;
    r.pod(n);
    if (!r.ok || n > cap) {
        r.ok = false;
        return false;
    }
    return true;
}

} // namespace sm::savefmt
