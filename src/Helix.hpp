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
#include <map>

#include <MlActions.hpp>
#include <AlphaFile.hpp>
#define HELIX_USE_LUA
#define HELIX_USE_LUA_GUI
#ifdef HELIX_USE_LUA

// Ignore warnings from this header

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weverything"

// TODO: let this be disabled
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#pragma GCC diagnostic pop

#endif

#include "util.hpp"

namespace Helix {
    struct BaseAction {
        explicit BaseAction () {}
        virtual ~BaseAction () {}

        /// Returns the byte value (if somehow stored in the action)
        /// or the position before any modifications to it
        virtual std::variant<std::byte, AlphaFile::Natural> reversePosition (AlphaFile::Natural position) {
            // No modifications
            return position;
        }

        /// Get the difference in file size due to this action, used for calculating the end-point file size
        virtual ptrdiff_t getSizeDifference () const {
            return 0;
        }

        virtual void save (AlphaFile::BasicFile& file) = 0;
    };

    // It is somewhat notable that the three basic Actions (Edit, Insertion, Deletion) don't have any custom code for undo/redo as they simply exist for storing data
    // Though they'll of course need custom code for actually saving to the file.
    struct EditAction : public BaseAction {
        AlphaFile::Natural position;
        std::vector<std::byte> data;

        explicit EditAction (AlphaFile::Natural t_position, std::vector<std::byte>&& t_data) : position(t_position), data(t_data) {}

        std::variant<std::byte, AlphaFile::Natural> reversePosition (AlphaFile::Natural read_position) override {
            if (data.size() == 0) {
                return read_position; // just continue
            }

            // is-in-range of [position, position + data.size)
            if (
                read_position >= position &&
                read_position < (position + data.size())
            ) {
                return data.at(static_cast<size_t>(read_position - position));
            }
            // Do nothing
            return read_position;
        }

        void save (AlphaFile::BasicFile& file) override {
            file.edit(position, data);
        }
    };
    struct InsertionAction : public BaseAction {
        static constexpr std::byte insertion_value = std::byte(0x00);
        AlphaFile::Natural position;
        size_t amount;

        explicit InsertionAction (AlphaFile::Natural t_position, size_t t_amount) : position(t_position), amount(t_amount) {}

        std::variant<std::byte, AlphaFile::Natural> reversePosition (AlphaFile::Natural read_position) override {
            if (
                read_position >= position &&
                read_position < (position + amount)
            ) {
                return std::byte(insertion_value);
            }

            if (read_position >= position) {
                return read_position - amount;
            }
            // Do nothing
            return read_position;
        }

        ptrdiff_t getSizeDifference () const override {
            // TODO: make sure amount is within range. Perhaps split large insertions up?
            return static_cast<ptrdiff_t>(amount);
        }

        void save (AlphaFile::BasicFile& file) override {
            // TODO: pass in chunk_size somehow
            file.insertion(position, amount, 120);
        }
    };
    struct DeletionAction : public BaseAction {
        AlphaFile::Natural position;
        size_t amount;

        explicit DeletionAction (AlphaFile::Natural t_position, size_t t_amount) : position(t_position), amount(t_amount) {}

        std::variant<std::byte, AlphaFile::Natural> reversePosition (AlphaFile::Natural read_position) override {
            if (read_position >= position) {
                return read_position + amount;
            }
            // Do nothing
            return read_position;
        }

        ptrdiff_t getSizeDifference () const override {
            // TODO: make sure amount is within range. Perhaps split large deletions up?
            return static_cast<ptrdiff_t>(amount);
        }

        void save (AlphaFile::BasicFile& file) override {
            // TODO: pass in chunk_size somehow
            file.deletion(position, amount, 120);
        }
    };
    struct BundledAction : public BaseAction {
        std::vector<std::unique_ptr<BaseAction>> actions;

        explicit BundledAction (std::vector<std::unique_ptr<BaseAction>>&& t_actions) : actions(std::move(t_actions)) {}

        // TODo: this is slightly annoying, as it's the exact same as the ActionLists readFromStorage function
        std::variant<std::byte, AlphaFile::Natural> reversePosition (AlphaFile::Natural position) override {
            for (auto iterator = actions.rbegin(); iterator != actions.rend(); ++iterator) {
                std::unique_ptr<BaseAction>& action_variant = *iterator;

                std::variant<std::byte, AlphaFile::Natural> result = action_variant->reversePosition(position);

                if (std::holds_alternative<std::byte>(result)) {
                    return std::get<std::byte>(result);
                } else {
                    position = std::get<AlphaFile::Natural>(result);
                }
            }
            return position;
        }

        void save (AlphaFile::BasicFile& file) override {
            for (std::unique_ptr<BaseAction>& action_v : actions) {
                action_v->save(file);
            }
        }
    };

    class ActionListLink : public MlActions::ActionListLink<BaseAction> {
        public:

        explicit ActionListLink (MlActions::ActionList& action_list) : MlActions::ActionListLink<BaseAction>(action_list) {}

        /// What this function does is applies the Actions in *reverse* until it finds an action which modified the position we're looking for.
        /// Due to the way this works, if we don't find an Action that edited the position, then the new-position is the right place in the file to read!
        std::variant<std::byte, AlphaFile::Natural> readFromStorage (AlphaFile::Natural natural_position) {
            // TODO: have mlaction provide a useful function for this
            for (auto iterator = this->data.rbegin(); iterator != this->data.rend(); ++iterator) {
                std::unique_ptr<BaseAction>& action = *iterator;

                std::variant<std::byte, AlphaFile::Natural> result = action->reversePosition(natural_position);

                if (std::holds_alternative<std::byte>(result)) {
                    return std::get<std::byte>(result);
                } else {
                    natural_position = std::get<AlphaFile::Natural>(result);
                }
            }
            return natural_position;
        }

        size_t getSizeDifference (size_t value) {
            // TODO: possibly make sure this doesn't go under 0 or over max
            // TODO: also possibly make so the value is passed to it instead of merely adding to it
            //       that would work better than returning ptrdiff_t, and allow more complicate size differences
            for (auto& action : this->data) {
                value += action->getSizeDifference();
            }
            return value;
        }

        void save (AlphaFile::BasicFile& file) {
            for (auto& action : data) {
                action->save(file);
            }
            // TODO: undoing past a save would be really nice to have
            list.clear();
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
        std::optional<AlphaFile::Absolute> getStart () const {
            return std::nullopt;
        }
        std::optional<AlphaFile::Absolute> getEnd () const {
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
        std::optional<AlphaFile::Absolute> start;
        std::optional<AlphaFile::Absolute> end;
        explicit PartialFileMode (std::optional<AlphaFile::Absolute> t_start, std::optional<AlphaFile::Absolute> t_end=std::nullopt) : start(t_start), end(t_end) {}

        std::optional<AlphaFile::Absolute> getStart () const {
            return start;
        }
        std::optional<AlphaFile::Absolute> getEnd () const {
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
        std::optional<AlphaFile::Absolute> start;
        explicit OpenPartialFileMode (std::optional<AlphaFile::Absolute> t_start) : start(t_start) {}
        std::optional<AlphaFile::Absolute> getStart () const {
            return start;
        }
    };
    /// Partially editing a file
    /// Does not allow insertion/deletion
    /// Save-as only saves the part we're editing, nothing else
    /// This is meant for 'spotty' files, which are not allowed to read outside of bounds.
    struct JohnFileMode : public FileMode {
        std::optional<AlphaFile::Absolute> start;
        std::optional<AlphaFile::Absolute> end;
        explicit JohnFileMode (std::optional<AlphaFile::Absolute> t_start, std::optional<AlphaFile::Absolute> t_end=std::nullopt) : start(t_start), end(t_end) {}

        std::optional<AlphaFile::Absolute> getStart () const {
            return start;
        }
        std::optional<AlphaFile::Absolute> getEnd () const {
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

        std::optional<AlphaFile::Absolute> getStart () const {
            return std::visit([] (auto&& x) { return x.getStart(); }, mode);
        }

        std::optional<AlphaFile::Absolute> getEnd () const {
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

        using RoundedNatural = size_t;

        struct Block {
            std::vector<std::byte> data;
            RoundedNatural start_position;

            explicit Block (RoundedNatural t_start, std::vector<std::byte>&& t_data) : data(t_data), start_position(t_start) {}
        };

        std::vector<Block> blocks;

        RoundedNatural getRoundedPosition (AlphaFile::Natural position) const;

        std::optional<size_t> findBlock (RoundedNatural rounded_position) const;

        bool hasBlock (RoundedNatural rounded_position) const;

        /// Creates a block at the position, doesn't check if it already exists.
        /// Invalidates all indexes if it returns a value.
        std::optional<size_t> createBlock (RoundedNatural position);

        public:

        ActionListLink actions;

        protected:

        AlphaFile::ConstrainedFile file;

        public:

        explicit Helix (MlActions::ActionList& action_list, std::filesystem::path t_filename, AlphaFile::OpenFlags t_flags=AlphaFile::OpenFlags(), Flags t_hflags=Flags(WholeFileMode()));

        explicit Helix (MlActions::ActionList& action_list, std::filesystem::path t_filename, Flags t_hflags);

        std::optional<size_t> cached_file_size;
        std::optional<size_t> cached_editable_size;

        void clearCaches ();

        /// If the file can be written to.
        /// If this is false, then the temp file (in mem) can be written to, but it can't be saved.
        bool isWritable () const;

        /// Gets file size, UNCACHED
        size_t getSize ();
        /// Gets editable size of the file, UNCACHED
        size_t getEditableSize ();

        /// Gets file size, CACHED
        size_t getCachedSize ();
        /// Gets editable size of the file, CACHED
        size_t getCachedEditableSize ();

        std::optional<std::byte> read (AlphaFile::Natural position);
        std::vector<std::byte> read (AlphaFile::Natural position, size_t amount);

        std::optional<uint8_t> readU8 (AlphaFile::Natural position);
        std::optional<uint16_t> readU16BE (AlphaFile::Natural Position);
        std::optional<uint16_t> readU16LE (AlphaFile::Natural Position);
        std::optional<uint32_t> readU32BE (AlphaFile::Natural Position);
        std::optional<uint32_t> readU32LE (AlphaFile::Natural Position);
        std::optional<uint64_t> readU64BE (AlphaFile::Natural Position);
        std::optional<uint64_t> readU64LE (AlphaFile::Natural Position);
        // TODO: readU128
        std::optional<float> readF32BE (AlphaFile::Natural Position);
        std::optional<float> readF32LE (AlphaFile::Natural Position);
        std::optional<double> readF64BE (AlphaFile::Natural Position);
        std::optional<double> readF64LE (AlphaFile::Natural Position);

        void edit (AlphaFile::Natural position, std::byte value);
        void edit (AlphaFile::Natural position, std::vector<std::byte>&& values);

        void insert (AlphaFile::Natural position, size_t amount, std::byte pattern=InsertionAction::insertion_value);

        void insert (AlphaFile::Natural position, size_t amount, const std::vector<std::byte>& pattern);

        /// Called deletion because delete is a keyword :x
        void deletion (AlphaFile::Natural position, size_t amount);

        SaveStatus save ();

        SaveStatus saveAs (const std::filesystem::path& destination);

        protected:

        static constexpr size_t save_as_write_amount = 512; // bytes at a time
        static constexpr size_t save_max_temp_filename_iteration = 10;


        /// A simple save that directly writes to the file.
        /// Does not allow insertion/deletion and just ignores them if they exist
        /// (Though it should *not* be called if there is insertions/deletions in the first place..)
        SaveStatus save_file_simple ();

        SaveStatus saveAsFile (const std::filesystem::path& initial_destination);
        bool save_hasValidFilename (const std::filesystem::path& file_path);
        size_t save_calculateResultingFileSize (size_t previous_file_size);
        /// generates filenames in the form: [filename].[4 byte hex].tmp
        std::filesystem::path save_generateTempFilename (std::filesystem::path filename);
        std::optional<std::pair<std::filesystem::path, std::filesystem::path>> save_generateTempPath (const std::filesystem::path& destination);


        std::optional<std::byte> readSingleRaw (AlphaFile::Natural pos);
    };



#ifdef HELIX_USE_LUA

    namespace LuaUtil {
        std::vector<std::byte> convertTableToBytes (sol::table table);

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

            explicit Events (sol::table t_keys);

            sol::table getKeys ();

            size_t listen (int32_t key, sol::function func);

            template<typename... Types>
            void triggerTemplate (int32_t key, Types... values) {
                if (std::vector<sol::function>* event_listeners = util::mapFindEntry(listeners, key)) {
                    for (sol::function& func : *event_listeners) {
                        func(values...);
                    }
                }
            }

            void triggerLua (int32_t key, sol::variadic_args va);

            // TODO: remove function to remove a specific listeners

            int32_t createEventType (std::string name);
        };
    };

    class PluginHelix : public Helix {
        struct CurrentFile {
            PluginHelix& helix;
            LuaUtil::Events events;

            explicit CurrentFile (PluginHelix& t_helix);

            LuaUtil::Events& getEvents ();

            bool isWritable () const;

            void edit (size_t natural_position, sol::table table);

            std::vector<std::byte> read (size_t natural_position, size_t amount);

            void insertion (size_t natural_position, size_t amount);

            void deletion (size_t natural_position, size_t amount);

            SaveStatus save ();

            SaveStatus saveAs (std::string filename);
        };
        protected:

        sol::state lua;
        CurrentFile current_file;
        public:

        explicit PluginHelix (MlActions::ActionList& action_list, std::filesystem::path t_filename, AlphaFile::OpenFlags t_flags=AlphaFile::OpenFlags(), Flags t_hflags=Flags(WholeFileMode()));
        explicit PluginHelix (MlActions::ActionList& action_list, std::filesystem::path t_filename, Flags t_hflags);

        sol::state& getLua ();

        protected:

        // ==== LUA Creation ====
        void initLua ();

        void initLua_Enumerations ();

        void initLua_Events ();

        void initLua_CurrentFile ();

        public:

        // ==== LUA Functions/Helper functions ====

        // ====

        void edit (AlphaFile::Natural position, std::byte value);

        // TODO: some utility func to turn a list of parameters into a sol::variadic_args
        // remember to look at docs
        void edit (AlphaFile::Natural position, std::vector<std::byte>&& values);
    };

#ifdef HELIX_USE_LUA_GUI

    class PluginGUIHelix : public PluginHelix {
        public:
        explicit PluginGUIHelix (MlActions::ActionList& action_list, std::filesystem::path t_filename, AlphaFile::OpenFlags t_flags=AlphaFile::OpenFlags(), Flags t_hflags=Flags(WholeFileMode()));
        explicit PluginGUIHelix (MlActions::ActionList& action_list, std::filesystem::path t_filename, Flags t_hflags);

        protected:
        void initGUILua ();
    };

#endif

#endif

} // namespace Helix