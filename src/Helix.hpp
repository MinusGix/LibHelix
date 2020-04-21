#pragma once

#include <cstdint>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <cstring>
#include <utility>
#include <random>
#include <array>
#include <variant>

#include "File.hpp"
#include "types.hpp"
#include <type_safe/strong_typedef.hpp>

enum class UndoStatus {
    // Anythng other than success is some form of failure (or partial failure)
    // Checks should be done like `if (thing.undo() != UndoStatus::Success) { }`
    Success = 0,
    UnknownFailure = 1,
    // There's nothing to undo
    Nothing,
    // The last action can't be undone
    Unnable,
    // Undoing/Redoing failed in such a way that the action now has an invalid state.
    // Bleh.
    InvalidState,
};

enum class RedoStatus {
    Success = 0,
    UnknownFailure = 1,
    Nothing,
    Unnable,
    InvalidState,
};

using ActionStatus = RedoStatus;

struct Action {
    virtual ~Action () {}

    virtual bool canUndo () const {
        return true;
    }
    virtual bool canRedo () const {
        return true;
    }

    virtual UndoStatus undo () {
        return UndoStatus::Success;
    }
    virtual RedoStatus redo () {
        return RedoStatus::Success;
    }

    /// Returns the byte value (if somehow stored in the action)
    /// or the position before any modifications to it
    virtual std::variant<std::byte, Natural> reversePosition (Natural position) {
        // No modifications
        return position;
    }

    /// Get the difference in file size due to this action, used for calculating the end-point file size
    virtual ptrdiff_t getSizeDifference () const {
        return 0;
    }

    virtual void save (FileHelper::File& file) = 0;
};

// It is somewhat notable that the three basic Actions (Edit, Insertion, Deletion) don't have any custom code for undo/redo as they simply exist for storing data
// Though they'll of course need custom code for actually saving to the file.
struct EditAction : public Action {
    Natural position;
    std::vector<std::byte> data;

    explicit EditAction (Natural t_position, std::vector<std::byte>&& t_data) : position(t_position), data(t_data) {}

    std::variant<std::byte, Natural> reversePosition (Natural read_position) override {
        if (data.size() == 0) {
            return read_position; // just continue
        }

        // is-in-range of [position, position + data.size)
        if (
            read_position >= position &&
            read_position < (position + Relative(data.size()))
        ) {
            return data.at(static_cast<size_t>(read_position - position));
        }
        // Do nothing
        return read_position;
    }

    void save (FileHelper::File& file) override {
        file.write(
            static_cast<std::streampos>(static_cast<size_t>(position)),
            data
        );
    }
};
struct InsertionAction : public Action {
    static constexpr std::byte insertion_value = std::byte(0x00);
    Natural position;
    size_t amount;

    explicit InsertionAction (Natural t_position, size_t t_amount) : position(t_position), amount(t_amount) {}

    std::variant<std::byte, Natural> reversePosition (Natural read_position) override {
        if (
            read_position >= position &&
            read_position < (position + Relative(amount))
        ) {
            return std::byte(insertion_value);
        }

        if (read_position >= position) {
            return read_position - Relative(amount);
        }
        // Do nothing
        return read_position;
    }

    ptrdiff_t getSizeDifference () const override {
        // TODO: make sure amount is within range. Perhaps split large insertions up?
        return static_cast<ptrdiff_t>(amount);
    }

    void save (FileHelper::File& file) override {
        file.insertion(
            static_cast<size_t>(5),
            amount,
            // TODO: pass in chunk_size somehow
            120
        );
    }
};
struct DeletionAction : public Action {
    Natural position;
    size_t amount;

    explicit DeletionAction (Natural t_position, size_t t_amount) : position(t_position), amount(t_amount) {}

    std::variant<std::byte, Natural> reversePosition (Natural read_position) override {
        if (read_position >= position) {
            return read_position + Relative(amount);
        }
        // Do nothing
        return read_position;
    }

    ptrdiff_t getSizeDifference () const override {
        // TODO: make sure amount is within range. Perhaps split large deletions up?
        return static_cast<ptrdiff_t>(amount);
    }

    void save (FileHelper::File& file) override {
        file.deletion(
            static_cast<size_t>(5),
            amount,
            // TODO: pass in chunk_size somhow
            120
        );
    }
};
struct BundledAction : public Action {
    std::vector<std::unique_ptr<Action>> actions;

    explicit BundledAction (std::vector<std::unique_ptr<Action>>&& t_actions) : actions(std::move(t_actions)) {}

    bool canUndo () const override {
        for (const std::unique_ptr<Action>& v : actions) {
            bool result = v->canUndo();
            if (!result) {
                // One of the items can't be undone, so none of them can be
                return false;
            }
        }
        return true;
    }

    bool canRedo () const override {
        for (const std::unique_ptr<Action>& v : actions) {
            bool result = v->canRedo();
            if (!result) {
                // One of the items can't be redone, so none of them can be
                return false;
            }
        }
        return true;
    }

    UndoStatus undo () override {
        if (!canUndo()) {
            return UndoStatus::Unnable;
        }

        if (actions.size() == 0) {
            return UndoStatus::Success;
        }

        for (size_t i = actions.size(); i--;) {
            actions.at(i)->undo();
        }

        // For simplicities sake we don't use this.
/*
        for (size_t i = actions.size(); i--;) {
            VariantType& action = actions.at(i);

            UndoStatus result = std::visit([] (auto&& x) { return x.undo(); }, action);

            // One of the actions failed in undoing,
            if (result != UndoStatus::Success) {
                // Forward iterate, re-doing the actions.
                for (size_t j = i + 1; j < actions.size(); j++) {
                    VariantType& r_action = actions.at(j);
                    RedoStatus r_result = std::visit([] (auto&& x) { return x.redo(); }, action);

                    // it failed.. thus we're in a state where we can't return from
                    if (r_result != RedoStatus::Success) {
                        return UndoStatus::InvalidState;
                    }
                }
                return result;
            }f
        }*/

        return UndoStatus::Success;
    }

    RedoStatus redo () override {
        if (!canRedo()) {
            return RedoStatus::Unnable;
        }

        if (actions.size() == 0) {
            return RedoStatus::Success;
        }

        for (size_t i = actions.size(); i--;) {
            actions.at(i)->redo();
        }

        return RedoStatus::Success;
    }

    // TODo: this is slightly annoying, as it's the exact same as the ActionLists readFromStorage function
    std::variant<std::byte, Natural> reversePosition (Natural position) override {
        for (auto iterator = actions.rbegin(); iterator != actions.rend(); ++iterator) {
            std::unique_ptr<Action>& action_variant = *iterator;

            std::variant<std::byte, Natural> result = action_variant->reversePosition(position);

            if (std::holds_alternative<std::byte>(result)) {
                return std::get<std::byte>(result);
            } else {
                position = std::get<Natural>(result);
            }
        }
        return position;
    }

    void save (FileHelper::File& file) override {
        for (std::unique_ptr<Action>& action_v : actions) {
            action_v->save(file);
        }
    }
};

class ActionList {
    public:
    // Should be mainly edited through helper functions
    std::vector<std::unique_ptr<Action>> actions;
    /// This is where we currently are in the edit history.
    /// Everything before it is currently 'applied', everything after is unapplied
    /// Ex: {Alpha, Beta}
    /// with an index of 0, both Alpha and Beta would be unapplied.
    /// With an index of 1, Alpha is applied and Beta is unapplied.
    /// With an index of 2, both are applied.
    size_t index = 0;

    bool hasAppliedEntries () const {
        // We don't bother verifying that index is valid, as it should always be.
        return index > 0;
    }

    bool hasUnappliedEntries () const {
        return index < actions.size();
    }

    bool canUndo () const {
        if (hasAppliedEntries()) {
            return actions.at(index - 1)->canUndo();
        }
        return false;
    }

    bool canRedo () const {
        if (hasUnappliedEntries()) {
            return actions.at(index)->canRedo();
        }
        return false;
    }

    UndoStatus undo () {
        if (!hasAppliedEntries()) {
            return UndoStatus::Nothing;
        }

        if (!canUndo()) {
            return UndoStatus::Unnable;
        }

        // We subtract first for simplicities sake. Since the previous undo is 1 behind index.
        index--;
        return actions.at(index)->undo();
    }

    RedoStatus redo () {
        if (!hasUnappliedEntries()) {
            return RedoStatus::Nothing;
        }

        if (!canRedo()) {
            return RedoStatus::Unnable;
        }

        index++;
        return actions.at(index - 1)->redo();
    }

    void clearUnappliedActions () {
        if (!hasUnappliedEntries()) {
            return;
        }

        size_t size = actions.size();
        for (size_t i = index; i < size; i++) {
            actions.pop_back();
        }
    }

    ActionStatus doAction (std::unique_ptr<Action>&& action) {
        clearUnappliedActions();

        actions.push_back(std::move(action));

        index++;

        // We just tell it to 'redo', even though we haven't done it already.
        // If you need to know if it's the first run then simply have a bool on the Action instance for if it's been ran.
        return actions.at(index - 1)->redo();
    }

    /// What this function does is applies the Actions in *reverse* until it finds an action which modified the position we're looking for.
    /// Due to the way this works, if we don't find an Action that edited the position, then the new-position is the right place in the file to read!
    std::variant<std::byte, Natural> readFromStorage (Natural position) {
        // iterate in reverse
        for (auto iterator = actions.rbegin(); iterator != actions.rend(); ++iterator) {
            std::unique_ptr<Action>& action_variant = *iterator;

            std::variant<std::byte, Natural> result = action_variant->reversePosition(position);

            if (std::holds_alternative<std::byte>(result)) {
                return std::get<std::byte>(result);
            } else {
                position = std::get<Natural>(result);
            }
        }
        return position;
    }

    void save (FileHelper::File& file) {
        for (std::unique_ptr<Action>& action : actions) {
            action->save(file);
        }
        // TODO: undoing past a save would be really nice to have
        actions.clear();
    }
};


enum SaveStatus {
    Success = 0,
    /// Filename was ill-formed. Perhaps the filename is a single "." or empty.
    InvalidFilename,
    /// Invalid destination. The path to the place to store the file is invalid.
    InvalidDestination,
    /// We can't write here :(
    InsufficientPermissions,
    /// Went over the iteration limit of looking for a temp filename. May be a sign of a bug.
    TempFileIterationLimit,
    /// Unsupported mode. This is probably a bug in this library.
    InvalidMode,
};


enum class SaveAsMode {
    // Saves the entire file
    Whole = 0,
    // Only saves the currently editing partial values.
    Partial,
};

// The reason there is modes is because some actions can't be done reasonably in certain situations.
struct FileMode {
    std::optional<Absolute> getStart () const {
        return std::nullopt;
    }
    std::optional<Absolute> getEnd () const {
        return std::nullopt;
    }
    bool supportsInsertion () const {
        return true;
    }
    bool supportsDeletion () const {
        return true;
    }
    SaveAsMode getSaveAsMode () const {
        return SaveAsMode::Whole;
    }
};
/// Editing an entire file.
/// Allows insertion/deletion
/// Allows full save-as
struct WholeFileMode : public FileMode {};
/// Partially editing a file
/// Does not allow insertion/deletion
/// Allows full save-as
struct PartialFileMode : public FileMode {
    std::optional<Absolute> start;
    std::optional<Absolute> end;
    explicit PartialFileMode (std::optional<Absolute> t_start, std::optional<Absolute> t_end=std::nullopt) : start(t_start), end(t_end) {}

    std::optional<Absolute> getStart () const {
        return start;
    }
    std::optional<Absolute> getEnd () const {
        return end;
    }
    bool supportsInsertion () const {
        return false;
    }
    bool supportsDeletion () const {
        return false;
    }
};
/// Partially editing a file with an open range on the right end
/// ex: editing [500, end-of-file) so indeletion is allowed
/// Allows insertion/deletion
/// Allows full save-as
struct OpenPartialFileMode : public FileMode {
    std::optional<Absolute> start;
    explicit OpenPartialFileMode (std::optional<Absolute> t_start) : start(t_start) {}
    std::optional<Absolute> getStart () const {
        return start;
    }
};
/// Partially editing a file
/// Does not allow insertion/deletion
/// Save-as only saves the part we're editing, nothing else
/// This is meant for 'spotty' files, which are not allowed to read outside of bounds.
struct JohnFileMode : public FileMode {
    std::optional<Absolute> start;
    std::optional<Absolute> end;
    explicit JohnFileMode (std::optional<Absolute> t_start, std::optional<Absolute> t_end=std::nullopt) : start(t_start), end(t_end) {}

    std::optional<Absolute> getStart () const {
        return start;
    }
    std::optional<Absolute> getEnd () const {
        return end;
    }
    bool supportsInsertion () const {
        return false;
    }
    bool supportsDeletion () const {
        return false;
    }
    SaveAsMode getSaveAsMode () const {
        return SaveAsMode::Partial;
    }
};

struct FileModeInfo {
    using VariantType = std::variant<WholeFileMode, PartialFileMode, OpenPartialFileMode, JohnFileMode>;
    VariantType mode;

    explicit FileModeInfo (VariantType&& t_mode) : mode(t_mode) {}

    std::optional<Absolute> getStart () const {
        return std::visit([] (auto&& x) { return x.getStart(); }, mode);
    }

    std::optional<Absolute> getEnd () const {
        return std::visit([] (auto&& x) { return x.getEnd(); }, mode);
    }

    bool supportsInsertion () const {
        return std::visit([] (auto&& x) { return x.supportsInsertion(); }, mode);
    }

    bool supportsDeletion () const {
        return std::visit([] (auto&& x) { return x.supportsDeletion(); }, mode);
    }

    SaveAsMode getSaveAsMode () const {
        return std::visit([] (auto&& x) { return x.getSaveAsMode(); }, mode);
    }

    template<typename T>
    bool is () const {
        return std::holds_alternative<T>(mode);
    }
};

struct Flags {
    size_t block_size = 1024;
    size_t max_block_count = 8;
    FileModeInfo mode_info;

    explicit Flags (typename FileModeInfo::VariantType t_mode) : mode_info(std::move(t_mode)) {}
};

class Helix {
    public:

    const size_t block_size;
    const size_t max_block_count;

    FileModeInfo mode_info;

    protected:
    // For easy usage

    struct RoundedNatural :
        ts::strong_typedef<RoundedNatural, Natural>,
        ts::strong_typedef_op::equality_comparison<RoundedNatural> {
        using ts::strong_typedef<RoundedNatural, Natural>::strong_typedef;
    };

    struct Block {
        std::vector<std::byte> data;
        RoundedNatural start_position;

        explicit Block (RoundedNatural t_start, std::vector<std::byte>&& t_data) : data(t_data), start_position(t_start) {}
    };

    std::vector<Block> blocks;

    RoundedNatural getRoundedPosition (Natural position) const {
        return RoundedNatural(util::getRoundedPosition(position, Natural(block_size)));
    }

    std::optional<size_t> findBlock (RoundedNatural rounded_position) const {
        return util::find_one(blocks, [rounded_position] (const Block& b, size_t) {
            return b.start_position == rounded_position;
        });
    }

    bool hasBlock (RoundedNatural rounded_position) const {
        return findBlock(rounded_position).has_value();
    }

    /// Creates a block at the position, doesn't check if it already exists.
    /// Invalidates all indexes if it returns a value.
    std::optional<size_t> createBlock (RoundedNatural position) {
        std::vector<std::byte> bytes = file.read(static_cast<Natural>(position), block_size);

        if (bytes.size() == 0) {
            return std::nullopt;
        }

        // TODO: remove badly scoring blocks.

        blocks.push_back(Block(position, std::move(bytes)));

        return blocks.size() - 1;
    }

    public:

    ActionList actions;

    protected:

    File::Constraint file;

    public:

    explicit Helix (std::filesystem::path t_filename, File::OpenFlags t_flags=File::OpenFlags(), Flags t_hflags=Flags(WholeFileMode())) :
        block_size(t_hflags.block_size), max_block_count(t_hflags.max_block_count),
        mode_info(t_hflags.mode_info),
        file(t_filename, mode_info.getStart(), mode_info.getEnd(), t_flags) {}

    explicit Helix (std::filesystem::path t_filename, Flags t_hflags) :
        block_size(t_hflags.block_size), max_block_count(t_hflags.max_block_count),
        mode_info(t_hflags.mode_info),
        file(t_filename, mode_info.getStart(), mode_info.getEnd(), File::OpenFlags()) {}

    /// If the file can be written to.
    /// If this is false, then the temp file (in mem) can be written to, but it can't be saved.
    bool isWritable () const {
        return file.isWritable();
    }

    std::optional<std::byte> read (Natural position) {
        std::variant<std::byte, Natural> data = actions.readFromStorage(position);
        if (std::holds_alternative<std::byte>(data)) {
            return std::get<std::byte>(data);
        } else {
            return readSingleRaw(std::get<Natural>(data));
        }
    }
    std::vector<std::byte> read (Natural position, size_t amount) {
        // This is bleh, it'd be nice to have an optimized method for this that doesn't call the function a ton of times
        std::vector<std::byte> data;
        data.reserve(amount);
        for (size_t i = 0; i < amount; i++) {
            std::optional<std::byte> byte_opt = read(position + Relative(i));
            if (!byte_opt.has_value()) {
                break;
            }
            data.push_back(byte_opt.value());
        }
        return data;
    }

    void edit (Natural position, std::byte value, EditFlags flags=EditFlags()) {
        actions.doAction(std::make_unique<EditAction>(position, std::vector<std::byte>{value}));
    }
    void edit (Natural position, std::vector<std::byte>&& values, EditFlags flags=EditFlags()) {
        actions.doAction(std::make_unique<EditAction>(position, std::forward<std::vector<std::byte>>(values)));
    }

    void insert (Natural position, size_t amount, std::byte pattern=InsertionAction::insertion_value) {
        if (!mode_info.supportsInsertion()) {
            throw std::runtime_error("Insertion is unsupported in this mode.");
        }

        // We don't bother filling it with the insertion_value since it essentially already does that
        if (pattern == InsertionAction::insertion_value) {
            actions.doAction(std::make_unique<InsertionAction>(position, amount));
        } else {
            std::vector<std::byte> data;
            data.resize(amount);
            std::fill(data.begin(), data.end(), pattern);

            std::vector<std::unique_ptr<Action>> bundled_list;
            bundled_list.push_back(std::unique_ptr<Action>(new InsertionAction(position, amount)));
            bundled_list.push_back(std::unique_ptr<Action>(new EditAction(position, std::move(data))));

            actions.doAction(std::unique_ptr<Action>(new BundledAction(std::move(bundled_list))));
        }
    }

    void insert (Natural position, size_t amount, const std::vector<std::byte>& pattern) {
        if (!mode_info.supportsInsertion()) {
            throw std::runtime_error("Insertion is unsupported in this mode.");
        }

        std::vector<std::byte> data;
        data.reserve(amount);

        for (size_t i = 0; i < amount; i++) {
            data.push_back(pattern.at(i % pattern.size()));
        }

        std::vector<std::unique_ptr<Action>> bundled_actions;
        bundled_actions.push_back(std::unique_ptr<Action>(new InsertionAction(position, amount)));
        bundled_actions.push_back(std::unique_ptr<Action>(new EditAction(position, std::move(data))));

        actions.doAction(std::make_unique<BundledAction>(std::move(bundled_actions)));
    }

    /// Called deletion because delete is a keyword :x
    void deletion (Natural position, size_t amount) {
        if (!mode_info.supportsDeletion()) {
            throw std::runtime_error("Deletion is unsupported in this mode.");
        }
        actions.doAction(std::make_unique<DeletionAction>(position, amount));
    }

    // TODO: investigate if this makes sense
    SaveStatus save () {
        // TODO: check if it's writable
        SaveAsMode save_as_mode = mode_info.getSaveAsMode();
        if (save_as_mode == SaveAsMode::Whole) {
            return saveAsFile(file.filename);
        } else if (save_as_mode == SaveAsMode::Partial) {
            return save_file_simple();
        } else {
            return SaveStatus::InvalidMode;
        }
    }

    SaveStatus saveAs (const std::filesystem::path& destination) {
        // TODO: check if it's writable.
        SaveAsMode save_as_mode = mode_info.getSaveAsMode();
        if (save_as_mode == SaveAsMode::Whole) {
            return saveAsFile(destination);
        } else if (save_as_mode == SaveAsMode::Partial) {
            return SaveStatus::Success; // TODO: partial saving. This would presumably not be able to do saveas? Check the sources of Partial-mode
        } else {
            return SaveStatus::InvalidMode;
        }
    }

    protected:

    static constexpr size_t save_as_write_amount = 512; // bytes at a time
    static constexpr size_t save_max_temp_filename_iteration = 10;


    /// A simple save that directly writes to the file.
    /// Does not allow insertion/deletion and just ignores them if they exist
    /// (Though it should *not* be called if there is insertions/deletions in the first place..)
    SaveStatus save_file_simple () {
        actions.save(file.file);
        return SaveStatus::Success;
    }

    SaveStatus saveAsFile (const std::filesystem::path& initial_destination) {
        struct FileSizeInfo {
            const size_t previous;
            const size_t result;

            size_t largest () const {
                return std::max(previous, result);
            }
        };

        // Make the path more 'normal'
        std::filesystem::path destination = initial_destination.lexically_normal();

        if (destination == "" || !save_hasValidFilename(destination)) {
            return SaveStatus::InvalidFilename;
        }

        // Make sure there is a parent folder
        // Not sure if we can actually get a blank parent_path
        if (destination.parent_path() == "") {
            destination = file.filename.parent_path() / destination;
        }

        // Make sure that the folder it's in exists.
        // TODO: perhaps we should automatically create folders to that position?
        if (!std::filesystem::exists(destination.parent_path())) {
            return SaveStatus::InvalidDestination;
        }


        const size_t previous_file_size = file.getSize();
        FileSizeInfo file_size{previous_file_size, save_calculateResultingFileSize(previous_file_size)};

        // TODO: provide an option to store the temp file in the OS temp folder using filesystem::temp_directory_path
        const std::optional<std::pair<std::filesystem::path, std::filesystem::path>> paths = save_generateTempPath(destination);
        if (!paths.has_value()) {
            return SaveStatus::TempFileIterationLimit;
        }

        const auto& [temp_filename, temp_file_path] = paths.value();

        // We simply copy the file as the temp file that we're modifying.
        std::filesystem::copy_file(file.filename, temp_file_path);

        // TODO: this may not be needed?
        // Resize to the size of the largest file (src, src-after-modifications)
        // we'll cut off any remaining bytes.
        std::filesystem::resize_file(temp_file_path, file_size.largest());

        FileHelper::File temp_file(temp_file_path, std::ios::out | std::ios::binary | std::ios::in);

        // Write all the actions to the newly created temporary file
        actions.save(temp_file);

        // Resize the file to the appropriate size after all the insertions/deletions.
        temp_file.resize(file_size.result);

        // Close the file before we rename it, just in case.
        temp_file.close();

        // Rename it to the destination.
        std::filesystem::rename(temp_file_path, destination);

        return SaveStatus::Success;
    }
    bool save_hasValidFilename (const std::filesystem::path& file_path) {
        // Check if it has a filename that is remotely valid
        const std::filesystem::path filename = file_path.filename();
        return filename != "" && filename != "." && filename != "..";
    }
    size_t save_calculateResultingFileSize (size_t previous_file_size) {
        size_t new_file_size = previous_file_size;

        for (size_t i = 0; i < actions.actions.size(); i++) {
            std::unique_ptr<Action>& action_v = actions.actions.at(i);
            new_file_size += action_v->getSizeDifference();
        }
        return new_file_size;
    }
    /// generates filenames in the form: [filename].[4 byte hex].tmp
    std::filesystem::path save_generateTempFilename (std::filesystem::path filename) {
        std::random_device rd;
        std::mt19937 eng(rd());
        std::uniform_int_distribution<uint32_t> dist(0);
        uint32_t result = dist(eng);

        std::stringstream sstream;
        sstream << std::hex << result;
        std::string hex_digits = sstream.str();

        filename += ".";
        filename += hex_digits;
        filename += ".tmp";

        return filename;
    }
    std::optional<std::pair<std::filesystem::path, std::filesystem::path>> save_generateTempPath (const std::filesystem::path& destination) {
        std::filesystem::path temp_filename;
        std::filesystem::path temp_file_path;
        size_t iteration_count = 0;

        do {
            // Generate the filename and the path it is at
            temp_filename = save_generateTempFilename(destination.filename());
            temp_file_path = destination.parent_path() / temp_filename;

            // We have a limited amount of time it will try finding a valid temp filename.
            iteration_count++;
            if (iteration_count > save_max_temp_filename_iteration) {
                return std::nullopt;
            }
        } while (std::filesystem::exists(temp_file_path));

        return std::make_pair(temp_filename, temp_file_path);
    }


    std::optional<std::byte> readSingleRaw (Natural pos) {
        const RoundedNatural rounded_position = getRoundedPosition(pos);

        std::optional<size_t> block_index = findBlock(rounded_position);

        // Create block if it couldn't be found
        if (!block_index.has_value()) {
            block_index = createBlock(rounded_position);

            // Couldn't construct the block, so tell them we failed to get it
            if (!block_index.has_value()) {
                return std::nullopt;
            }
        }

        assert(static_cast<Natural>(rounded_position) <= pos);
        size_t block_pos = static_cast<size_t>(pos - static_cast<Natural>(rounded_position));

        return blocks[block_index.value()].data.at(block_pos);
    }
};



#ifdef HELIX_USE_LUA

#include <map>

// Ignore warnings from this header

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weverything"

// TODO: let this be disabled
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#pragma GCC diagnostic pop

namespace LuaUtil {
    static std::vector<std::byte> convertTableToBytes (sol::table table) {
        std::vector<std::byte> data;
        size_t size = table.size();
        data.reserve(size);
        for (size_t i = 1; i <= size; i++) {
            data.push_back(std::byte(table.get<uint8_t>(i)));
        }
        return data;
    }

    template<typename T>
    void addArgument (std::vector<sol::object>& objects, T value) {
        objects.push_back(static_cast<sol::object>(value));
    }
    template<typename T, typename... Types>
    void addArgument (std::vector<sol::object>& objects, T value, Types... values) {
        addArgument<T>(objects, value);
        addArgument<Types...>(objects, values...);
    }

    template<typename... Types>
    std::vector<sol::object> createArguments (Types... values) {
        std::vector<sol::object> objects;
        addArgument<Types...>(objects, values...);
        return objects;
    }

    struct Events {
        std::map<int32_t, std::vector<sol::function>> listeners;

        int32_t current_id = 0;
        // The currently created events
        sol::table keys;

        explicit Events (sol::table t_keys) : keys(t_keys) {}

        sol::table getKeys () {
            return keys;
        }

        size_t listen (int32_t key, sol::function func) {
            auto& vec = listeners[key];
            vec.push_back(func);
            return vec.size() - 1;
        }

        template<typename... Types>
        void triggerTemplate (int32_t key, Types... values) {
            if (std::vector<sol::function>* event_listeners = util::mapFindEntry(listeners, key)) {
                for (sol::function& func : *event_listeners) {
                    func(values...);
                }
            }
        }

        void triggerLua (int32_t key, sol::variadic_args va) {
            if (std::vector<sol::function>* event_listeners = util::mapFindEntry(listeners, key)) {
                for (sol::function& func : *event_listeners) {
                    func(sol::as_args(va));
                }
            }
        }

        // TODO: remove function to remove a specific listeners

        int32_t createEventType (std::string name) {
            const int32_t id = current_id++;
            keys[name] = id;
            return id;
        }
    };
};

class PluginHelix : public Helix {
    struct CurrentFile {
        PluginHelix& helix;
        LuaUtil::Events events;

        explicit CurrentFile (PluginHelix& t_helix) : helix(t_helix), events(helix.getLua().create_table()) {
            events.createEventType("Edit");
        }

        LuaUtil::Events& getEvents () {
            return events;
        }

        bool isWritable () const {
            return helix.isWritable();
        }

        void edit (size_t natural_position, sol::table table) {
            helix.edit(Natural(natural_position), LuaUtil::convertTableToBytes(table));
        }

        std::vector<std::byte> read (size_t natural_position, size_t amount) {
            return helix.read(Natural(natural_position), amount);
        }

        void insertion (size_t natural_position, size_t amount) {
            helix.insert(Natural(natural_position), amount);
        }

        void deletion (size_t natural_position, size_t amount) {
            helix.deletion(Natural(natural_position), amount);
        }

        SaveStatus save () {
            return helix.save();
        }

        SaveStatus saveAs (std::string filename) {
            return helix.saveAs(filename);
        }
    };
    protected:

    sol::state lua;
    CurrentFile current_file;
    public:

    explicit PluginHelix (std::filesystem::path t_filename, File::OpenFlags t_flags=File::OpenFlags(), Flags t_hflags=Flags(WholeFileMode())) :
        Helix(t_filename, t_flags, t_hflags), current_file(*this) {
        initLua();
    }
    explicit PluginHelix (std::filesystem::path t_filename, Flags t_hflags) :
        Helix(t_filename, t_hflags), current_file(*this) {
        initLua();
    }

    sol::state& getLua () {
        return lua;
    }

    protected:

    // ==== LUA Creation ====
    void initLua () {
        lua.open_libraries(sol::lib::base, sol::lib::package);
        initLua_Events();
        initLua_Enumerations();
        initLua_CurrentFile();
    }

    void initLua_Enumerations () {
        lua.new_enum(
            "UndoStatus",
            "Success", UndoStatus::Success,
            "UnknownFailure", UndoStatus::UnknownFailure,
            "Nothing", UndoStatus::Nothing,
            "Unnable", UndoStatus::Unnable,
            "InvalidState", UndoStatus::InvalidState
        );
        lua.new_enum(
            "RedoStatus",
            "Success", RedoStatus::Success,
            "UnknownFailure", RedoStatus::UnknownFailure,
            "Nothing", RedoStatus::Nothing,
            "Unnable", RedoStatus::Unnable,
            "InvalidState", RedoStatus::InvalidState
        );

        lua.new_enum(
            "SaveStatus",
            "Success", SaveStatus::Success,
            "InvalidFilename", SaveStatus::InvalidFilename,
            "InvalidDestination", SaveStatus::InvalidDestination,
            "InsufficientPermissions", SaveStatus::InsufficientPermissions,
            "TempFileIterationLimit", SaveStatus::TempFileIterationLimit,
            "InvalidMode", SaveStatus::InvalidMode
        );

        lua.new_enum(
            "SaveAsMode",
            "Whole", SaveAsMode::Whole,
            "Partial", SaveAsMode::Partial
        );
    }

    void initLua_Events () {
        lua.new_usertype<LuaUtil::Events>("Events_type",
            "listen", &LuaUtil::Events::listen,
            "trigger", &LuaUtil::Events::triggerLua,
            "createEventType", &LuaUtil::Events::createEventType,
            // This is a bit icky
            "Keys", sol::readonly_property(&LuaUtil::Events::getKeys)
        );
    }

    void initLua_CurrentFile () {
        lua.new_usertype<CurrentFile>("CurrentFile_type",
            "isWritable", &CurrentFile::isWritable,
            "edit", &CurrentFile::edit,
            "read", &CurrentFile::read,
            "insertion", &CurrentFile::insertion,
            "deletion", &CurrentFile::deletion,
            "save", &CurrentFile::save,
            "saveAs", &CurrentFile::saveAs,
            // This is a bit icky
            "Events", sol::readonly_property(&CurrentFile::getEvents)
        );

        lua["CurrentFile"] = std::ref(current_file);
    }

    public:

    // ==== LUA Functions/Helper functions ====

    // ====

    void edit (Natural position, std::byte value, EditFlags flags=EditFlags()) {
        // TODO: would it be possible to just make it a reference_wrapper (std::ref)?
        // Slightly ugly, but it creates a temporary table to use to store the value.

        sol::table table = lua.create_table();
        table[1] = value;
        current_file.events.triggerTemplate(current_file.events.keys.template get<int32_t>("Edit"), static_cast<size_t>(position), table);
        value = table.get<std::byte>(1);
        Helix::edit(position, value, flags);
    }

    // TODO: some utility func to turn a list of parameters into a sol::variadic_args
    // remember to look at docs
    void edit (Natural position, std::vector<std::byte>&& values, EditFlags flags=EditFlags()) {
        current_file.events.triggerTemplate(current_file.events.keys.template get<int32_t>("Edit"), static_cast<size_t>(position), std::ref(values));
        Helix::edit(position, std::move(values), flags);
    }
};

#ifdef HELIX_USE_LUA_GUI

class PluginGUIHelix : public PluginHelix {
    public:
    explicit PluginGUIHelix (std::filesystem::path t_filename, File::OpenFlags t_flags=File::OpenFlags(), Flags t_hflags=Flags(WholeFileMode())) :
        PluginHelix(t_filename, t_flags, t_hflags) {
        initGUILua();
    }
    explicit PluginGUIHelix (std::filesystem::path t_filename, Flags t_hflags) :
        PluginHelix(t_filename, t_hflags) {
        initGUILua();
    }

    protected:
    void initGUILua () {
        lua.create_table();
    }
};

#endif

#endif