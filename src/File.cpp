#include "File.hpp"

using namespace Helix::File;

/// Errors

OpenError::OpenError (const std::string& str, const std::filesystem::path& t_filename) : std::runtime_error(str), filename(t_filename) {}

FileDoesNotExist::FileDoesNotExist (const std::filesystem::path& t_filename) : OpenError("File Does Not Exist: " + t_filename.string(), t_filename) {}

UnopenableFile::UnopenableFile (const std::string& str, const std::filesystem::path& t_filename) :
    OpenError("File can not be opened (" + t_filename.string() + "): " + str, t_filename) {}

UnknownOpenError::UnknownOpenError (const std::string& str, const std::filesystem::path& t_filename) : OpenError(str, t_filename) {}

RangeError::RangeError (const std::string& str) : std::runtime_error(str) {}

PositionRangeError::PositionRangeError (const std::string& str) : RangeError(str) {}

ReadError::ReadError (const std::string& str) : std::runtime_error(str) {}





/// Constraint

Constraint::Constraint (std::filesystem::path t_filename, std::optional<Absolute> t_start, std::optional<Absolute> t_end, OpenFlags t_flags) :
    filename(t_filename), start(t_start), end(t_end), flags(t_flags) {

    // Disallow completely zero-space. It's useless.
    if (start.has_value() && end.has_value() && start.value() == end.value()) {
        throw RangeError("Invalid range in construction. Both optionals have a value and are equivalent, which is zero-space.");
    }

    if (!std::filesystem::exists(filename)) {
        throw FileDoesNotExist(filename);
    }

    if (std::filesystem::is_directory(filename)) {
        // TODO: should there be a Helix static(?) support function that creates several instances, all for each file in a directory?
        throw UnopenableFile("Can not open directory.", filename);
    }

    if (std::filesystem::is_character_file(filename)) {
        throw UnopenableFile("Cannot open special character file.", filename);
    }

    if (std::filesystem::is_fifo(filename)) {
        throw UnopenableFile("Cannot open fifo.", filename);
    }

    if (std::filesystem::is_socket(filename)) {
        throw UnopenableFile("Cannot open socket.", filename);
    }

    // Appears to work fine with symlinks.
    // Block files probably won't work completely properly? I have no clue how to test this.

    file = FileHelper::File(filename, flags.mode());

    if (file.fail()) {
        throw UnknownOpenError("Failed to open file for an unknown reason.", filename);
    }
}

bool Constraint::isWritable () const {
    return flags.write;
}

void Constraint::clearErrorState () {
    file.clearErrors();
}


std::optional<Helix::Absolute> Constraint::convert_noThrow (Helix::Natural pos) const {
    try {
        Helix::Absolute value = convert(pos);
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

Helix::Absolute Constraint::convert (Helix::Natural pos) const {
    Helix::GeneralPosition i_pos = static_cast<Helix::GeneralPosition>(pos);
    if (start.has_value()) {
        i_pos += static_cast<Helix::GeneralPosition>(start.value());
    }

    if (end.has_value()) {
        if (i_pos >= static_cast<Helix::GeneralPosition>(end.value())) {
            throw PositionRangeError("Natural position was outside of range.");
        }
    }

    return Helix::Absolute(i_pos);
}

bool Constraint::isValidAbsolute (Absolute pos) const {
    if (start.has_value()) {
        if (pos < start.value()) {
            return false;
        }
    }

    if (end.has_value()) {
        if (pos >= end.value()) {
            return false;
        }
    }
    return true;
}



std::vector<std::byte> Constraint::read_internal (Absolute pos, size_t amount) {
    clearErrorState();

    std::vector<std::byte> read_bytes;
    if (amount == 0) {
        return read_bytes;
    }
    read_bytes.resize(amount);

    size_t amount_read = file.read(
        static_cast<GeneralPosition>(pos),
        read_bytes.size(),
        reinterpret_cast<char*>(read_bytes.data())
    );

    if (file.fail() && !file.eof()) {
        // TODO: look to see if there's more info in this error state
        throw ReadError("Failed to read file data.");
    }

    // if fail and eof then we hit the end of the file.
    // That's fine, but we still need to clear error state to continue properly using the functions
    clearErrorState();

    // Abnormality. This should never happen as far as I know.
    if (amount_read > amount) {
        // We forcefully restrict it back down to amount, so that we won't get any possibility of garbage
        // Honestly, this might be a place where we should crash hard.
        amount_read = amount;
    }

    read_bytes.resize(amount_read);

    // Tell the vector to shrink, since most of the time any users will not be modifying the returned vector.
    read_bytes.shrink_to_fit();

    return read_bytes;
}

bool Constraint::canBeConstrained (Natural pos) {
    return convert_noThrow(pos).has_value();
}

std::optional<std::byte> Constraint::read (Natural pos) {
    // TODO: make this use read directly for the slightest performance improvement
    std::vector<std::byte> bytes = read_internal(convert(pos), 1);
    if (bytes.size() == 0) {
        return std::nullopt;
    }
    return bytes[0];
}

/// Reads from position into a vector with at most `amount` entries. May have less.
/// To read from a specific position, remember to `.seek(pos)` first.
std::vector<std::byte> Constraint::read (Natural pos, size_t amount) {
    return read_internal(convert(pos), amount);
}

// TODO: actually handle EditFlags options
void Constraint::edit (Natural pos, std::byte value, EditFlags flags) {
    file.write(
        static_cast<std::streampos>(static_cast<GeneralPosition>(convert(pos))),
        1,
        reinterpret_cast<const char*>(&value)
    );
}
void Constraint::edit (Natural pos, const std::vector<std::byte>& values, EditFlags flags) {
    file.write(
        static_cast<std::streampos>(static_cast<size_t>(convert(pos))),
        values.size(),
        reinterpret_cast<const char*>(values.data())
    );
}

void Constraint::insertion (Natural position, size_t amount, size_t chunk_size) {
    file.insertion(static_cast<size_t>(convert(position)), amount, chunk_size);
}
void Constraint::deletion (Natural position, size_t amount, size_t chunk_size) {
    file.deletion(static_cast<size_t>(convert(position)), amount, chunk_size);
}

size_t Constraint::getSize () {
    return file.getSize();
}

Helix::Absolute Constraint::getConstrainedValue (Natural pos) const {
    return convert(pos);
}