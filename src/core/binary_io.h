#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
        std::array<char, sizeof(T)> buffer{};
        std::memcpy(buffer.data(), &value, sizeof(T));
        out_->write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    }

    void write_bytes(const void* data, std::size_t size) {
        out_->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
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
        std::array<char, sizeof(T)> buffer{};
        in_->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::memcpy(&value, buffer.data(), sizeof(T));
    }

    template <typename T>
    [[nodiscard]] T read() {
        T value{};
        read(value);
        return value;
    }

    void read_bytes(void* data, std::size_t size) {
        in_->read(static_cast<char*>(data), static_cast<std::streamsize>(size));
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
