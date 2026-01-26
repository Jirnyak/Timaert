#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

class BinaryWriter {
public:
    explicit BinaryWriter(std::ostream& out) : out_(&out) {}

    template <typename T>
    void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        out_->write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    }

    void write_bytes(const void* data, std::size_t size) {
        out_->write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    void write_string(std::string_view value) {
        const auto len = static_cast<std::uint32_t>(value.size());
        write(len);
        if (len > 0) {
            write_bytes(value.data(), len);
        }
    }

    [[nodiscard]] bool ok() const {
        return static_cast<bool>(*out_);
    }

private:
    std::ostream* out_;
};

class BinaryReader {
public:
    explicit BinaryReader(std::istream& in) : in_(&in) {}

    template <typename T>
    void read(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        in_->read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    }

    template <typename T>
    [[nodiscard]] T read() {
        T value{};
        read(value);
        return value;
    }

    void read_bytes(void* data, std::size_t size) {
        in_->read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    }

    void read_string(std::string& value) {
        const auto len = read<std::uint32_t>();
        value.resize(len);
        if (len > 0) {
            in_->read(value.data(), static_cast<std::streamsize>(len));
        }
    }

    [[nodiscard]] bool ok() const {
        return static_cast<bool>(*in_);
    }

private:
    std::istream* in_;
};
