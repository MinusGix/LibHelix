#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <cstddef>

namespace FileHelper {
    class File {
        protected:

        std::fstream file;
        std::filesystem::path filename;
        std::ios_base::openmode open_mode;

        public:

        explicit File () {}
        explicit File (const std::filesystem::path& t_filename, std::ios_base::openmode t_open_mode) : filename(std::filesystem::canonical(std::filesystem::absolute(t_filename))), open_mode(t_open_mode) {
            file.open(filename, open_mode);

            if (file.fail()) {
                throw std::runtime_error("Failed to open file.");
            }
        }

        bool fail () const {
            return file.fail();
        }
        bool bad () const {
            return file.bad();
        }
        bool eof () const {
            return file.eof();
        }
        bool good () const {
            return file.good();
        }
        void clearErrors () {
            file.clear();
        }

        std::vector<std::byte> read (size_t absolute_position, size_t amount) {
            std::vector<std::byte> bytes;
            bytes.reserve(amount);
            read(absolute_position, amount, reinterpret_cast<char*>(bytes.data()));
            return bytes;
        }

        size_t read (size_t absolute_position, size_t amount, char* into) {
            file.seekg(absolute_position);
            file.read(into, amount);
            return file.gcount();
        }

        void write (size_t absolute_position, const std::vector<std::byte>& data) {
            write(absolute_position, data.size(), reinterpret_cast<const char*>(data.data()));
        }

        void write (size_t absolute_position, size_t amount, const char* data) {
            file.seekp(absolute_position);
            file.write(data, amount);
        }

        void insertionNoOverwrite (size_t absolute_position, size_t amount, size_t chunk_size) {
            // This function only inserts null bytes. Replacing them is an action which can be done afterwards
            // TODO: this could potentially be done in this function if we were given a vector of bytes, and could be a separate function pretty easily.

            // Imagine (each character being an individual byte):
            //  data               = {a b c d e f g h i j k l m n o p q r s t u v w x y z}
            // We insert one byte (0) at position 0 (position is zero-indexed) into data
            //  insert(data, 0, 1) = {0 a b c d e f g h i j k l m n o p q r s t u v w x y z}
            // If we were to insert one byte (0) at position 5 into data
            //  insert(data, 5, 1) = {a b c d e 0 f g h i j k l m n o p q r s t u v w x y z}
            // Simple, but should explain how we get there.
            // Let's use [chunk_size] as the size of the chunks we should make.
            //  We'll assume [chunk_size] is 4.
            // insert(data, 5, 1) would be done as:
            //  {a b c d e f g h i j k l m n o p q r s t u v w x y z}
            //            ^ We insert here, since that is where position five is. This would be before the 'f' byte.
            // Divide it into chunks of [chunk_size], but only the parts *after* where we are inserting. (Thus, everything before the insertion point is untouched)
            //   There may be 0 chunks, and the last chunk does not have to be equivalent. All of the chunk's sizes are <= chunk_size
            //  {a b c d e {f g h i} {j k l m} {n o p q} {r s t u} {v w x y} {z}}
            //            ^
            // We then need to add new bytes to the end of the file, since we are *expanding* it.
            // We wouold need to add [amount] bytes to the end of the file, their value does not matter.
            //  {a b c d e {f g h i} {j k l m} {n o p q} {r s t u} {v w x y} {z} 0}
            // Since we want to shift everything forward/rightward, we need to start at the end and work our way backwards.
            //   (If you're confused, then realize that if we attempted to shift {f g h i} forward, it would overwrite part of the chunk directly after it.
            //    We could keep track of overwritten data, but I believe it's easier to simply do it in reverse order. That way there's less temp data.)
            //  So we start with {z}, and we move it forward by [amount] bytes.
            //  {a b c d e {f g h i} {j k l m} {n o p q} {r s t u} {v w x y} z | z}
            // You will have noticed that it has 'duplicated', since we don't bother overwriting the *old* {z}.
            //  z is no longer in a chunk like before since it's now just a value to overwrite. The symbol | is for the actual file data that we won't touch again.
            // Then we shift {v w x y} forward by [amount] (1)
            //  {a b c d e {f g h i} {j k l m} {n o p q} {r s t u} v | v w x y z}
            // As you can see, we keep the old start of it, since we don't bother overwriting what we left behind, and then we continue this pattern.
            // Following, we get:
            //  {a b c d e {f g h i} j | j k l m n o p q r s t u v w x y z}
            // Then we do the last chunk of {f g h i }
            //  {a b c d e f | f g h i j k l m n o p q r s t u v w x y z}
            //            ^ our insertion point is now simply an 'overwrite' point, since we now have an extra byte in that position.
            // So we simply edit that byte with the value (0)
            //  {a b c d e 0 f g h i j k l m n o p q r s t u v w x y z} after
            //  {a b c d e f g h i j k l m n o p q r s t u v w x y z} before
            //


            // We start shifting data at the end of the file.
            // we call it end to avoid confusion, even if it's technically the start.
            const size_t shift_end = getSize();

            // The amount of bytes we have to move.
            const size_t shift_amount = shift_end - absolute_position;
            // The amount of chunks we have (and thus the amount of shifts we must make).
            const size_t shift_iterations = (shift_amount / chunk_size) + (shift_amount % chunk_size == 0 ? 0 : 1);

            // The amount in the first slice (if one exists). Either chunk_size (file is divided into equal chunks) or the remainder.
            const size_t first_slice_amount = (shift_end % chunk_size) == 0 ? chunk_size : (shift_end % chunk_size);

            // statically sized as [chunk_size]
            std::vector<std::byte> transpose_data;
            transpose_data.resize(chunk_size);
            for (size_t i = 0; i < shift_iterations; i++) {
                // First iteration is on the last chunk, so then it could be a size <= chunk_size, while all other chunks are == chunk_size
                const size_t slice_start = shift_end - first_slice_amount - (i * chunk_size);
                const size_t slice_amount = i == 0 ? first_slice_amount : chunk_size;
                const size_t slice_destination = slice_start + amount;

                const size_t slice_read_amount = read(slice_start, slice_amount, reinterpret_cast<char*>(transpose_data.data()));
                if (slice_read_amount != slice_amount) {
                    // TODO: should this be a hard error?
                    std::cout << "[insertion] slice_read_amount (" << slice_read_amount << ") is not equivalent to slice_amount (" << slice_amount << ")\n";
                }

                write(slice_destination, slice_read_amount, reinterpret_cast<const char*>(transpose_data.data()));
            }
        }

        /// Insert's bytes into the file.
        /// Technically resizes the file since it writes more bytes out than was originally in the file
        /// So .resize is not needed (though can be done without harm)
        void insertion (size_t absolute_position, size_t amount, size_t chunk_size) {
            insertionNoOverwrite(absolute_position, amount, chunk_size);

            // Writes out the series in chunk_size chunks.

            // An array of 0's to write.
            std::vector<std::byte> data;
            data.resize(chunk_size);
            for (size_t i = 0; i < data.size(); i++) {
                data[i] = std::byte(0x00);
            }

            const size_t amount_end = absolute_position + amount;
            const size_t amount_iterations = (amount / chunk_size) + (amount % chunk_size == 0 ? 0 : 1);
            for (size_t i = 0; i < amount_iterations; i++) {
                const size_t slice_start = absolute_position + (i * chunk_size);
                const size_t slice_end = std::min(slice_start + chunk_size, amount_end);
                const size_t slice_amount = slice_end - slice_start;

                write(slice_start, slice_amount, reinterpret_cast<const char*>(data.data()));
            }
        }

        /// Insert's bytes into the file.
        /// .resize is not needed.
        /// [data] is the data which should be inserted into the file at [absolute_position]
        void insertion (size_t absolute_position, const std::vector<std::byte>& data, size_t chunk_size) {
            insertionNoOverwrite(absolute_position, data.size(), chunk_size);

            write(absolute_position, data.size(), reinterpret_cast<const char*>(data.data()));
        }

        /// Delete's bytes from the file
        /// Note that it does NOT resize the file. It is up to the caller to call .resize with the appropriate size.
        void deletion (size_t absolute_position, size_t amount, size_t chunk_size) {
            // We only want to shift what's after the deletion.
            const size_t shift_start = absolute_position + amount;
            // Obviously we want to stop at the end of the file.
            const size_t shift_end = getSize();
            // How many bytes will be shifted over.
            const size_t shift_amount = shift_end - shift_start;
            // The amount of shifts we'll have to do.
            const size_t shift_iterations = (shift_amount / chunk_size) + (shift_amount % chunk_size == 0 ? 0 : 1);
            // Used to store the data that we are moving.
            std::vector<std::byte> transpose_data;
            transpose_data.resize(chunk_size);
            for (size_t i = 0; i < shift_iterations; i++) {
                // Where we start reading from, the data we're going to move
                const size_t slice_start = shift_start + (i * chunk_size);
                // Where we're gonna end at. Either after [chunk_size] bytes or at the end of the data we're shifting (aka end of file)
                const size_t slice_end = std::min(slice_start + chunk_size, shift_end);
                // The amount of bytes between the end and the start
                const size_t slice_amount = slice_end - slice_start;

                // Read the data into a variable, and also get how many bytes were read
                const size_t slice_read_amount = read(slice_start, slice_amount, reinterpret_cast<char*>(transpose_data.data()));
                // Check if the bytes we read were equivalent to the amount given
                if (slice_read_amount != slice_amount) {
                    // TODO: should this be a hard error?
                    std::cout << "[deletion] slice_read_amount (" << slice_read_amount << ") is not equivalent to slice_amount (" << slice_amount << ")\n";
                }
                // Write the data back, shifted forward?
                write(slice_start - amount, slice_read_amount, reinterpret_cast<const char*>(transpose_data.data()));
            }

            // TODO: return the amount we deleted?
        }

        void resize (size_t amount) {
            file.close();
            std::filesystem::resize_file(filename, amount);
            file.open(filename, open_mode);
        }

        void close () {
            file.close();
        }

        size_t getSize () {
            // TODO: I don't like having to close and reopen the file just to get the file size in bytes...
            file.close();
            size_t length = static_cast<size_t>(std::filesystem::file_size(filename));
            file.open(filename, open_mode);

            return length;
        }
    };
}