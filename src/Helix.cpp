#include "Helix.hpp"

namespace Helix {
	// ==== Helix:Constructors ====
	Helix::Helix (std::filesystem::path t_filename, File::OpenFlags t_flags, Flags t_hflags) :
		block_size(t_hflags.block_size), max_block_count(t_hflags.max_block_count),
		mode_info(t_hflags.mode_info),
		file(t_filename, mode_info.getStart(), mode_info.getEnd(), t_flags) {}

	Helix::Helix (std::filesystem::path t_filename, Flags t_hflags) :
		block_size(t_hflags.block_size), max_block_count(t_hflags.max_block_count),
		mode_info(t_hflags.mode_info),
		file(t_filename, mode_info.getStart(), mode_info.getEnd(), File::OpenFlags()) {}


	// ==== Helix:Blocks ====
	Helix::RoundedNatural Helix::getRoundedPosition (Natural position) const {
		return Helix::RoundedNatural(util::getRoundedPosition(position, Natural(block_size)));
	}

	std::optional<size_t> Helix::findBlock (RoundedNatural rounded_position) const {
		return util::find_one(blocks, [rounded_position] (const Block& b, size_t) {
			return b.start_position == rounded_position;
		});
	}

	bool Helix::hasBlock (RoundedNatural rounded_position) const {
		return findBlock(rounded_position).has_value();
	}

	std::optional<size_t> Helix::createBlock (RoundedNatural position) {
		std::vector<std::byte> bytes = file.read(static_cast<Natural>(position), block_size);

		if (bytes.size() == 0) {
			return std::nullopt;
		}

		// TODO: remove badly scoring blocks.

		blocks.push_back(Block(position, std::move(bytes)));

		return blocks.size() - 1;
	}


	// ==== Helix:Other ====
	bool Helix::isWritable () const {
		return file.isWritable();
	}

	std::optional<std::byte> Helix::read (Natural position) {
		std::variant<std::byte, Natural> data = actions.readFromStorage(position);
		if (std::holds_alternative<std::byte>(data)) {
			return std::get<std::byte>(data);
		} else {
			return readSingleRaw(std::get<Natural>(data));
		}
	}
	std::vector<std::byte> Helix::read (Natural position, size_t amount) {
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

	std::optional<std::byte> Helix::readSingleRaw (Natural pos) {
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

	void Helix::edit (Natural position, std::byte value, File::EditFlags flags) {
		actions.doAction(std::make_unique<EditAction>(position, std::vector<std::byte>{value}));
	}
	void Helix::edit (Natural position, std::vector<std::byte>&& values, File::EditFlags flags) {
		actions.doAction(std::make_unique<EditAction>(position, std::forward<std::vector<std::byte>>(values)));
	}

	void Helix::insert (Natural position, size_t amount, std::byte pattern) {
		if (!mode_info.supportsInsertion()) {
			throw std::runtime_error("Insertion is unsupported in this mode.");
		}

		// We don't bother filling it with the insertion_value since it essentially already does that
		if (pattern == InsertionAction::insertion_value) {
			// TODO: since we don't bother filling.. the parameter should just be an optional.
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

	void Helix::insert (Natural position, size_t amount, const std::vector<std::byte>& pattern) {
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

	void Helix::deletion (Natural position, size_t amount) {
		if (!mode_info.supportsDeletion()) {
			throw std::runtime_error("Deletion is unsupported in this mode.");
		}
		actions.doAction(std::make_unique<DeletionAction>(position, amount));
	}

	// TODO: investigate if this makes sense
	SaveStatus Helix::save () {
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

	SaveStatus Helix::saveAs (const std::filesystem::path& destination) {
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

	// ==== Helix:Save-Internal ====
	SaveStatus Helix::save_file_simple () {
		actions.save(file.file);
		return SaveStatus::Success;
	}

	SaveStatus Helix::saveAsFile (const std::filesystem::path& initial_destination) {
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
	bool Helix::save_hasValidFilename (const std::filesystem::path& file_path) {
		// Check if it has a filename that is remotely valid
		const std::filesystem::path filename = file_path.filename();
		return filename != "" && filename != "." && filename != "..";
	}
	size_t Helix::save_calculateResultingFileSize (size_t previous_file_size) {
		size_t new_file_size = previous_file_size;

		for (size_t i = 0; i < actions.actions.size(); i++) {
			std::unique_ptr<Action>& action_v = actions.actions.at(i);
			new_file_size += action_v->getSizeDifference();
		}
		return new_file_size;
	}
	/// generates filenames in the form: [filename].[4 byte hex].tmp
	std::filesystem::path Helix::save_generateTempFilename (std::filesystem::path filename) {
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
	std::optional<std::pair<std::filesystem::path, std::filesystem::path>> Helix::save_generateTempPath (const std::filesystem::path& destination) {
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

#ifdef HELIX_USE_LUA
	namespace LuaUtil {
		std::vector<std::byte> convertTableToBytes (sol::table table) {
			std::vector<std::byte> data;
			size_t size = table.size();
			data.reserve(size);
			for (size_t i = 1; i <= size; i++) {
				data.push_back(std::byte(table.get<uint8_t>(i)));
			}
			return data;
		}

		Events::Events (sol::table t_keys) : keys(t_keys) {}

		sol::table Events::getKeys () {
			return keys;
		}

		size_t Events::listen (int32_t key, sol::function func) {
			auto& vec = listeners[key];
			vec.push_back(func);
			return vec.size() - 1;
		}

		void Events::triggerLua (int32_t key, sol::variadic_args va)  {
			if (std::vector<sol::function>* event_listeners = util::mapFindEntry(listeners, key)) {
				for (sol::function& func : *event_listeners) {
					func(sol::as_args(va));
				}
			}
		}

		int32_t Events::createEventType (std::string name) {
			const int32_t id = current_id++;
			keys[name] = id;
			return id;
		}
	} // namespace LuaUtil

	// ==== PluginHelix:CurrentFile ====
	PluginHelix::CurrentFile::CurrentFile (PluginHelix& t_helix) : helix(t_helix), events(helix.getLua().create_table()) {
		events.createEventType("Edit");
	}

	LuaUtil::Events& PluginHelix::CurrentFile::getEvents () {
		return events;
	}

	bool PluginHelix::CurrentFile::isWritable () const {
		return helix.isWritable();
	}

	void PluginHelix::CurrentFile::edit (size_t natural_position, sol::table table) {
		helix.edit(Natural(natural_position), LuaUtil::convertTableToBytes(table));
	}

	std::vector<std::byte> PluginHelix::CurrentFile::read (size_t natural_position, size_t amount) {
		return helix.read(Natural(natural_position), amount);
	}

	void PluginHelix::CurrentFile::insertion (size_t natural_position, size_t amount) {
		helix.insert(Natural(natural_position), amount);
	}

	void PluginHelix::CurrentFile::deletion (size_t natural_position, size_t amount) {
		helix.deletion(Natural(natural_position), amount);
	}

	SaveStatus PluginHelix::CurrentFile::save () {
		return helix.save();
	}

	SaveStatus PluginHelix::CurrentFile::saveAs (std::string filename) {
		return helix.saveAs(filename);
	}

	// ==== PluginHelix:Constructors ====
	PluginHelix::PluginHelix (std::filesystem::path t_filename, File::OpenFlags t_flags, Flags t_hflags) :
        Helix(t_filename, t_flags, t_hflags), current_file(*this) {
        initLua();
    }
    PluginHelix::PluginHelix (std::filesystem::path t_filename, Flags t_hflags) :
        Helix(t_filename, t_hflags), current_file(*this) {
        initLua();
    }

	// ==== PluginHelix:Lua ====
    sol::state& PluginHelix::getLua () {
        return lua;
    }

	void PluginHelix::initLua () {
		lua.open_libraries(sol::lib::base, sol::lib::package);
		initLua_Events();
		initLua_Enumerations();
		initLua_CurrentFile();
	}

	void PluginHelix::initLua_Enumerations () {
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

	void PluginHelix::initLua_Events () {
		lua.new_usertype<LuaUtil::Events>("Events_type",
			"listen", &LuaUtil::Events::listen,
			"trigger", &LuaUtil::Events::triggerLua,
			"createEventType", &LuaUtil::Events::createEventType,
			// This is a bit icky
			"Keys", sol::readonly_property(&LuaUtil::Events::getKeys)
		);
	}

	void PluginHelix::initLua_CurrentFile () {
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

	// ==== PluginHelix:Other ====

	void PluginHelix::edit (Natural position, std::byte value, File::EditFlags flags) {
		// TODO: would it be possible to just make it a reference_wrapper (std::ref)?
		// Slightly ugly, but it creates a temporary table to use to store the value.

		sol::table table = lua.create_table();
		table[1] = value;
		current_file.events.triggerTemplate(current_file.events.keys.template get<int32_t>("Edit"), static_cast<size_t>(position), table);
		value = table.get<std::byte>(1);
		Helix::edit(position, value, flags);
    }

    void PluginHelix::edit (Natural position, std::vector<std::byte>&& values, File::EditFlags flags) {
        current_file.events.triggerTemplate(current_file.events.keys.template get<int32_t>("Edit"), static_cast<size_t>(position), std::ref(values));
        Helix::edit(position, std::move(values), flags);
    }



#ifdef HELIX_USE_LUA_GUI

	// ==== PluginGUIHelix:Constructors ====
	PluginGUIHelix::PluginGUIHelix (std::filesystem::path t_filename, File::OpenFlags t_flags, Flags t_hflags) :
        PluginHelix(t_filename, t_flags, t_hflags) {
        initGUILua();
    }
    PluginGUIHelix::PluginGUIHelix (std::filesystem::path t_filename, Flags t_hflags) :
        PluginHelix(t_filename, t_hflags) {
        initGUILua();
    }
	void PluginGUIHelix::initGUILua () {
		
	}

#endif
#endif
} // namespace Helix