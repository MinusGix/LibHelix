#pragma once

/// This file contains minor utilities.

#include <optional>
#include <functional>
#include <map>
#include <cstddef>

namespace Helix::util {
    namespace optional {
        template<typename T, typename Func>
        /// If opt does have a value, it returns that. If it does not, it returns func.
        /// Note that currently it does not return a reference (Is there a good way to do that here?)
        T value_or (const std::optional<T>& opt, Func&& func) {
            if (opt.has_value()) {
                return opt.value();
            }
            return func();
        }

        template<typename T, typename Func>
        /// Mutating or-value. If opt does not have a value, it replaces it with the value returned by `func`
        /// It then returns a reference to the contained value.
        T& mut_or_value (std::optional<T>& opt, Func&& func) {
            if (!opt.has_value()) {
                opt = func();
            }
            return opt.value();
        }
    } // namespace optional

    // Note that this is really simple observer_ptr
    // and is not implemented to the upcoming standard observer_ptr
    // this is just a wrapper around a ptr.
    template<typename T>
    struct observer_ptr {
        public:
        using type = T;
        using pointer = T*;
        using reference = T&;
        using const_reference = const T&;

        protected:
        pointer value = nullptr;

        public:
        observer_ptr () {}
        observer_ptr (pointer ptr) : value(ptr) {}
        observer_ptr (std::nullptr_t) {}

        const pointer get () const {
            return value;
        }

        pointer get () {
            return value;
        }

        reference operator-> () {
            return *get();
        }

        const_reference operator-> () const {
            return *get();
        }

        void reset (pointer ptr=nullptr) {
            value = ptr;
        }
    };


    template<typename T>
    T getRoundedPosition (T value, T round_to) {
        return value - (value % round_to);
    }

    template<typename T>
    T getChunkedWithRemainder (T amount, T chunk_size) {
        return (amount / chunk_size) +
            ((amount % chunk_size) == 0 ? 0 : 1);
    }

    template<typename T, typename Callable>
    std::optional<size_t> find_one (const std::vector<T>& data, Callable func) {
        for (size_t index = 0; index < data.size(); index++) {
            if (func(data[index], index)) {
                return index;
            }
        }
        return std::nullopt;
    }

    char nibbleToChar (std::byte value);

    std::pair<char, char> byteToString (std::byte value, bool padded);
    std::pair<char, char> byteToString (std::byte value);

    template<typename K, typename V>
    V* mapFindEntry (std::map<K, V>& map, K key) {
        auto iterator = map.find(key);
        if (iterator != map.end()) {
            return &(iterator->second);
        } else {
            return nullptr;
        }
    }
} // namespace Helix::util