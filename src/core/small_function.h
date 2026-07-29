#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace sm {

template <class Signature, std::size_t StorageBytes = 64>
class SmallFunction;

template <class R, class... Args, std::size_t StorageBytes>
class SmallFunction<R(Args...), StorageBytes> {
public:
    SmallFunction() = default;

    template <class Fn,
              class = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, SmallFunction>>>
    SmallFunction(Fn&& fn) {
        emplace<std::decay_t<Fn>>(std::forward<Fn>(fn));
    }

    SmallFunction(const SmallFunction& other) {
        copy_from(other);
    }

    SmallFunction& operator=(const SmallFunction& other) {
        if (this != &other) {
            reset();
            copy_from(other);
        }
        return *this;
    }

    SmallFunction(SmallFunction&& other) noexcept {
        move_from(other);
    }

    SmallFunction& operator=(SmallFunction&& other) noexcept {
        if (this != &other) {
            reset();
            move_from(other);
        }
        return *this;
    }

    ~SmallFunction() {
        reset();
    }

    template <class Fn,
              class = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, SmallFunction>>>
    SmallFunction& operator=(Fn&& fn) {
        reset();
        emplace<std::decay_t<Fn>>(std::forward<Fn>(fn));
        return *this;
    }

    explicit operator bool() const {
        return invoke_ != nullptr;
    }

    R operator()(Args... args) const {
        if (!invoke_) {
            if constexpr (!std::is_void_v<R>) {
                return R{};
            } else {
                return;
            }
        }

        if constexpr (std::is_void_v<R>) {
            invoke_(storage_, std::forward<Args>(args)...);
        } else {
            return invoke_(storage_, std::forward<Args>(args)...);
        }
    }

private:
    using InvokeFn = R (*)(const void*, Args&&...);
    using CopyFn = void (*)(void*, const void*);
    using MoveFn = void (*)(void*, void*) noexcept;
    using DestroyFn = void (*)(void*) noexcept;

    template <class Fn>
    void emplace(Fn fn) {
        static_assert(std::is_invocable_r_v<R, const Fn&, Args...>,
                      "SmallFunction target has an incompatible call signature");
        static_assert(sizeof(Fn) <= StorageBytes,
                      "SmallFunction target capture is too large for inline storage");
        static_assert(alignof(Fn) <= alignof(std::max_align_t),
                      "SmallFunction target alignment exceeds inline storage alignment");
        static_assert(std::is_nothrow_move_constructible_v<Fn>,
                      "SmallFunction target must be nothrow-move-constructible");
        static_assert(std::is_copy_constructible_v<Fn>,
                      "SmallFunction target must be copy-constructible");

        new (storage_) Fn(std::move(fn));
        invoke_ = [](const void* ptr, Args&&... args) -> R {
            Fn& target = *static_cast<Fn*>(const_cast<void*>(ptr));
            if constexpr (std::is_void_v<R>) {
                target(std::forward<Args>(args)...);
            } else {
                return target(std::forward<Args>(args)...);
            }
        };
        copy_ = [](void* dst, const void* src) {
            new (dst) Fn(*static_cast<const Fn*>(src));
        };
        move_ = [](void* dst, void* src) noexcept {
            new (dst) Fn(std::move(*static_cast<Fn*>(src)));
            static_cast<Fn*>(src)->~Fn();
        };
        destroy_ = [](void* ptr) noexcept {
            static_cast<Fn*>(ptr)->~Fn();
        };
    }

    void reset() noexcept {
        if (destroy_) {
            destroy_(storage_);
        }
        invoke_ = nullptr;
        copy_ = nullptr;
        move_ = nullptr;
        destroy_ = nullptr;
    }

    void copy_from(const SmallFunction& other) {
        if (!other.copy_) {
            return;
        }
        other.copy_(storage_, other.storage_);
        invoke_ = other.invoke_;
        copy_ = other.copy_;
        move_ = other.move_;
        destroy_ = other.destroy_;
    }

    void move_from(SmallFunction& other) noexcept {
        if (!other.move_) {
            return;
        }
        other.move_(storage_, other.storage_);
        invoke_ = other.invoke_;
        copy_ = other.copy_;
        move_ = other.move_;
        destroy_ = other.destroy_;
        other.invoke_ = nullptr;
        other.copy_ = nullptr;
        other.move_ = nullptr;
        other.destroy_ = nullptr;
    }

    alignas(std::max_align_t) unsigned char storage_[StorageBytes]{};
    InvokeFn invoke_ = nullptr;
    CopyFn copy_ = nullptr;
    MoveFn move_ = nullptr;
    DestroyFn destroy_ = nullptr;
};

} // namespace sm
