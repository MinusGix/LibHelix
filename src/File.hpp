#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include <type_safe/strong_typedef.hpp>
#include "types.hpp"
#include "util.hpp"
#include <cassert>

#include <iostream>

#include <FileHelper.hpp>

struct EditFlags {
    bool partial_write;

    explicit EditFlags (bool t_partial_write=true) : partial_write(t_partial_write) {}
};

namespace File {
    struct OpenFlags {
        bool write;

        explicit constexpr OpenFlags (bool t_write=true) : write(t_write) {}

        constexpr std::ios_base::openmode mode () const {
            std::ios_base::openmode ret_mode = std::ios_base::in | // Always reading
                std::ios_base::binary; // Always binary mode.

            if (write) {
                ret_mode |= std::ios_base::out;
            }

            return ret_mode;
        }
    };


    class OpenError : public std::runtime_error {
        public:
        std::filesystem::path filename;
        explicit OpenError (const std::string& str, const std::filesystem::path& t_filename);
    };

    class FileDoesNotExist : public OpenError {
        public:
        explicit FileDoesNotExist (const std::filesystem::path& t_filename);
    };

    class UnopenableFile : public OpenError {
        public:
        explicit UnopenableFile (const std::string& str, const std::filesystem::path& t_filename);
    };

    class UnknownOpenError : public OpenError {
        public:
        explicit UnknownOpenError (const std::string& str, const std::filesystem::path& t_filename);
    };




    class RangeError : public std::runtime_error {
        public:
        explicit RangeError (const std::string& str);
    };
    class PositionRangeError : public RangeError {
        public:
        explicit PositionRangeError (const std::string& str);
    };



    class ReadError : public std::runtime_error {
        public:
        explicit ReadError (const std::string& str);
    };

    class Constraint {
        public:

        std::filesystem::path filename;

        //std::fstream file;
        FileHelper::File file;

        // [start, end)
        std::optional<Absolute> start;
        std::optional<Absolute> end;

        OpenFlags flags;

        public:

        explicit Constraint (std::filesystem::path t_filename, std::optional<Absolute> t_start=std::nullopt, std::optional<Absolute> t_end=std::nullopt, OpenFlags t_flags=OpenFlags());

        protected:
        void clearErrorState ();

        std::optional<Absolute> convert_noThrow (Natural pos) const;

        Absolute convert (Natural pos) const;

        bool isValidAbsolute (Absolute pos) const;


        std::vector<std::byte> read_internal (Absolute pos, size_t amount);

        public:

        bool isWritable () const;

        bool canBeConstrained (Natural pos);

        Absolute getConstrainedValue (Natural pos) const;

        std::optional<std::byte> read (Natural pos);

        /// Reads from position into a vector with at most `amount` entries. May have less.
        /// To read from a specific position, remember to `.seek(pos)` first.
        std::vector<std::byte> read (Natural pos, size_t amount);

        void edit (Natural pos, std::byte value, EditFlags flags=EditFlags());
        void edit (Natural pos, const std::vector<std::byte>& values, EditFlags flags=EditFlags());

        void insertion (Natural position, size_t amount, size_t chunk_size);
        void deletion (Natural position, size_t amount, size_t chunk_size);

        size_t getSize ();
    };
} // namespace File