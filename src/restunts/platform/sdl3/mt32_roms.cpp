#include "mt32_roms.h"
#include <SDL3/SDL.h>
#include <mt32emu/ROMInfo.h>
#include <algorithm>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{

typedef std::basic_string<legacy_char> Text;
typedef std::vector<legacy_u8> Bytes;
typedef std::shared_ptr<Bytes> SharedBytes;
typedef std::vector<SharedBytes> BufferList;
typedef MT32Emu::ROMInfo ROMInfo;
typedef MT32Emu::MachineConfiguration Machine;
static const legacy_u32 MAX_DIRECTORY_DEPTH = 32U;
static const legacy_u32 MAX_DIRECTORY_ENTRIES = 65536U;
static const legacy_u32 JSON_CONTROL_LIMIT = 32U;
static const legacy_char JSON_HEX_DIGITS[] = "0123456789abcdef";

struct Candidate {
	Text digest;
	SharedBytes bytes;
};

struct Selection {
	const ROMInfo *first;
	const ROMInfo *second;
	SharedBytes first_bytes;
	SharedBytes second_bytes;

	Selection() : first(NULL), second(NULL)
	{
	}

	legacy_u8 complete() const
	{
		return first != NULL;
	}
};

struct Set {
	Text id;
	Text label;
	Selection control;
	Selection pcm;
};

struct Inventory {
	std::vector<Candidate> images;
	std::vector<Set> sets;
	Text error;
	legacy_u32 entries;

	Inventory() : entries(0)
	{
	}
};

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
/* SDL's mutex works before SDL_Init and with MinGW's Win32 thread model.
 * Publish it atomically so concurrent first scans can initialise it safely. */
class ROMMutex
{
	public:
	ROMMutex() : pointer(NULL)
	{
	}

	~ROMMutex()
	{
		SDL_DestroyMutex(static_cast<SDL_Mutex *>(pointer));
	}

	SDL_Mutex *get()
	{
		SDL_Mutex *mutex = static_cast<SDL_Mutex *>(SDL_GetAtomicPointer(&pointer));
		if (mutex != NULL) {
			return mutex;
		}
		SDL_Mutex *created = SDL_CreateMutex();
		if (created == NULL) {
			return NULL;
		}
		if (SDL_CompareAndSwapAtomicPointer(&pointer, NULL, created)) {
			return created;
		}
		SDL_DestroyMutex(created);
		return static_cast<SDL_Mutex *>(SDL_GetAtomicPointer(&pointer));
	}

	private:
	void *pointer;
};

static ROMMutex rom_mutex;

class ROMLock
{
	public:
	ROMLock() : mutex(rom_mutex.get())
	{
		if (mutex != NULL) {
			SDL_LockMutex(mutex);
		}
	}

	~ROMLock()
	{
		if (mutex != NULL) {
			SDL_UnlockMutex(mutex);
		}
	}

	legacy_u8 acquired() const
	{
		return mutex != NULL;
	}

	private:
	ROMLock(const ROMLock &) = delete;
	ROMLock &operator=(const ROMLock &) = delete;
	SDL_Mutex *mutex;
};
#else
/* The serial browser build cannot run concurrent scanner calls and disables
 * SDL's thread subsystem. Do not require its dummy mutex implementation. */
class ROMLock
{
	public:
	legacy_u8 acquired() const
	{
		return 1;
	}
};
#endif

/* Munt's metadata is initialised lazily. Serialise scanner entry points as well
 * as the small map retaining buffers borrowed by active C API contexts.
 * In threaded builds this map is destroyed before the mutex at shutdown. */
static std::map<mt32emu_context, BufferList> loaded_buffers;

static Text quoted(const Text &input)
{
	Text output = "\"";
	for (size_t index = 0; index < input.size(); ++index) {
		legacy_u8 value = static_cast<legacy_u8>(input[index]);
		if (value == '\\' || value == '"') {
			output += '\\';
			output += static_cast<legacy_char>(value);
		} else if (value < JSON_CONTROL_LIMIT) {
			output += "\\u00";
			output += JSON_HEX_DIGITS[value >> 4U];
			output += JSON_HEX_DIGITS[value & 15U];
		} else {
			output += static_cast<legacy_char>(value);
		}
	}
	output += '"';
	return output;
}

static Text label_for(const Machine &machine)
{
	Text id = machine.getMachineID();
	if (id == "mt32_bluer") {
		return "MT-32 (BlueRidge)";
	}
	const ROMInfo *const *infos = machine.getCompatibleROMInfos();
	for (size_t index = 0; infos[index] != NULL; ++index) {
		const ROMInfo &info = *infos[index];
		if (info.type != ROMInfo::Control || info.pairType != ROMInfo::Full) {
			continue;
		}
		Text description = info.description;
		size_t version = description.find(" Control v");
		if (version == Text::npos) {
			return description;
		}
		Text number = description.substr(version + Text(" Control v").size());
		if (id.compare(0, Text("mt32_1_").size(), "mt32_1_") == 0) {
			return "MT-32 (old, v" + number + ")";
		}
		if (id.compare(0, Text("mt32_2_").size(), "mt32_2_") == 0) {
			return "MT-32 (new, v" + number + ")";
		}
		Text family = description.substr(0, version);
		for (size_t slash = family.find('/'); slash != Text::npos;
			 slash = family.find('/', slash + Text(" / ").size())) {
			family.replace(slash, 1U, " / ");
		}
		return family + " (v" + number + ")";
	}
	return id;
}

static const Candidate *find_image(const Inventory &inventory, const ROMInfo &info)
{
	for (size_t index = 0; index < inventory.images.size(); ++index) {
		if (inventory.images[index].digest == info.sha1Digest) {
			return &inventory.images[index];
		}
	}
	return NULL;
}

static Selection choose_images(const Inventory &inventory, const Machine &machine,
							   ROMInfo::Type type)
{
	Selection selection;
	const ROMInfo *const *infos = machine.getCompatibleROMInfos();
	/* Prefer one complete dump over an equivalent pair of chip dumps. */
	for (size_t index = 0; infos[index] != NULL; ++index) {
		const ROMInfo &info = *infos[index];
		if (info.type == type && info.pairType == ROMInfo::Full) {
			const Candidate *image = find_image(inventory, info);
			if (image != NULL) {
				selection.first = &info;
				selection.first_bytes = image->bytes;
				return selection;
			}
		}
	}
	for (size_t index = 0; infos[index] != NULL; ++index) {
		const ROMInfo &info = *infos[index];
		if (info.type != type || info.pairROMInfo == NULL) {
			continue;
		}
		const Candidate *first = find_image(inventory, info);
		const Candidate *second = find_image(inventory, *info.pairROMInfo);
		if (first != NULL && second != NULL) {
			selection.first = &info;
			selection.second = info.pairROMInfo;
			selection.first_bytes = first->bytes;
			selection.second_bytes = second->bytes;
			return selection;
		}
	}
	return selection;
}

static legacy_u8 has_type(const Inventory &inventory, ROMInfo::Type type)
{
	const ROMInfo *const *infos = ROMInfo::getAllROMInfos();
	for (size_t index = 0; infos[index] != NULL; ++index) {
		if (infos[index]->type == type && find_image(inventory, *infos[index]) != NULL) {
			return 1;
		}
	}
	return 0;
}

static void resolve_sets(Inventory &inventory)
{
	const Machine *const *machines = Machine::getAllMachineConfigurations();
	for (size_t index = 0; machines[index] != NULL; ++index) {
		Set set;
		set.id = machines[index]->getMachineID();
		set.label = label_for(*machines[index]);
		set.control = choose_images(inventory, *machines[index], ROMInfo::Control);
		set.pcm = choose_images(inventory, *machines[index], ROMInfo::PCM);
		if (set.control.complete() && set.pcm.complete()) {
			inventory.sets.push_back(set);
		}
	}
	if (!inventory.sets.empty()) {
		return;
	}
	if (inventory.images.empty()) {
		inventory.error = "No supported MT-32 ROM images were found in this folder.";
	} else if (!has_type(inventory, ROMInfo::Control)) {
		inventory.error = "Compatible control ROM missing; only PCM ROM data was found.";
	} else if (!has_type(inventory, ROMInfo::PCM)) {
		inventory.error = "Compatible PCM ROM missing; only control ROM data was found.";
	} else {
		inventory.error = "No complete compatible ROM set was found. Check that the control and "
						  "PCM ROMs match and both halves of any split image are present.";
	}
}

static legacy_u8 known_size(legacy_u64 size)
{
	const ROMInfo *const *infos = ROMInfo::getAllROMInfos();
	for (size_t index = 0; infos[index] != NULL; ++index) {
		if (infos[index]->type != ROMInfo::Reverb && infos[index]->fileSize == size) {
			return 1;
		}
	}
	return 0;
}

static void read_candidate(Inventory &inventory, const Text &path)
{
	std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> file(SDL_IOFromFile(path.c_str(), "rb"),
															   SDL_CloseIO);
	if (file.get() == NULL) {
		inventory.error = "Cannot read ROM file: " + path;
		return;
	}
	Sint64 size = SDL_GetIOSize(file.get());
	if (size <= 0 || !known_size(static_cast<legacy_u64>(size))) {
		return;
	}
	SharedBytes bytes(new Bytes(static_cast<size_t>(size)));
	size_t count = SDL_ReadIO(file.get(), bytes->data(), bytes->size());
	file.reset();
	if (count != bytes->size()) {
		inventory.error = "Cannot read complete ROM file: " + path;
		return;
	}
	MT32Emu::ArrayFile image(bytes->data(), bytes->size());
	const ROMInfo *info = ROMInfo::getROMInfo(&image);
	if (info == NULL || info->type == ROMInfo::Reverb || find_image(inventory, *info) != NULL) {
		return;
	}
	Candidate candidate;
	candidate.digest = info->sha1Digest;
	candidate.bytes = bytes;
	inventory.images.push_back(candidate);
}

struct DirectoryScan {
	Inventory &inventory;
	legacy_u32 depth;
};

static SDL_EnumerationResult SDLCALL scan_entry(void *userdata, const legacy_char *directory,
												const legacy_char *name)
{
	DirectoryScan &scan = *static_cast<DirectoryScan *>(userdata);
	if (SDL_strcmp(name, ".") == 0 || SDL_strcmp(name, "..") == 0) {
		return SDL_ENUM_CONTINUE;
	}
	if (++scan.inventory.entries > MAX_DIRECTORY_ENTRIES) {
		scan.inventory.error = "The ROM folder contains too many entries. Choose a smaller folder.";
		return SDL_ENUM_FAILURE;
	}
	Text path = Text(directory) + name;
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(path.c_str(), &info)) {
		scan.inventory.error = "Cannot inspect ROM folder entry: " + path;
		return SDL_ENUM_FAILURE;
	}
	if (info.type == SDL_PATHTYPE_DIRECTORY) {
		if (scan.depth >= MAX_DIRECTORY_DEPTH) {
			scan.inventory.error =
				"The ROM folder is nested too deeply or contains a directory loop.";
			return SDL_ENUM_FAILURE;
		}
		DirectoryScan child = {scan.inventory, scan.depth + 1U};
		if (!SDL_EnumerateDirectory(path.c_str(), scan_entry, &child)) {
			if (scan.inventory.error.empty()) {
				scan.inventory.error = "Cannot read ROM subfolder: " + path;
			}
			return SDL_ENUM_FAILURE;
		}
	} else if (info.type == SDL_PATHTYPE_FILE && known_size(info.size)) {
		read_candidate(scan.inventory, path);
		if (!scan.inventory.error.empty()) {
			return SDL_ENUM_FAILURE;
		}
	}
	return SDL_ENUM_CONTINUE;
}

static Inventory scan_directory(const legacy_char *directory)
{
	Inventory inventory;
	SDL_PathInfo info;
	if (directory == NULL || *directory == 0) {
		inventory.error = "Choose your MT-32 ROM folder path.";
		return inventory;
	}
	if (!SDL_GetPathInfo(directory, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
		inventory.error = "Cannot open MT-32 ROM folder: " + Text(directory);
		return inventory;
	}
	DirectoryScan scan = {inventory, 0};
	if (!SDL_EnumerateDirectory(directory, scan_entry, &scan)) {
		if (inventory.error.empty()) {
			inventory.error = "Cannot read MT-32 ROM folder: " + Text(directory);
		}
		return inventory;
	}
	resolve_sets(inventory);
	return inventory;
}

static Text inventory_json(const Inventory &inventory)
{
	Text json = "{\"sets\":[";
	for (size_t index = 0; index < inventory.sets.size(); ++index) {
		if (index != 0) {
			json += ',';
		}
		json += "{\"id\":" + quoted(inventory.sets[index].id) +
				",\"label\":" + quoted(inventory.sets[index].label) + '}';
	}
	json += "],\"error\":" + quoted(inventory.error) + '}';
	return json;
}

static const Set *choose_set(const Inventory &inventory, const legacy_char *model, Text &error)
{
	if (!inventory.error.empty()) {
		error = inventory.error;
		return NULL;
	}
	if (model != NULL && *model != 0) {
		for (size_t index = 0; index < inventory.sets.size(); ++index) {
			if (inventory.sets[index].id == model) {
				return &inventory.sets[index];
			}
		}
		error = "Selected MT-32 ROM set '" + Text(model) + "' is unavailable or incomplete.";
		return NULL;
	}
	if (inventory.sets.size() == 1U) {
		return &inventory.sets[0];
	}
	error = "Multiple MT-32 ROM sets found; specify --mt-32-model. Available sets:";
	for (size_t index = 0; index < inventory.sets.size(); ++index) {
		error += " " + inventory.sets[index].id;
	}
	return NULL;
}

static legacy_u8 add_selection(mt32emu_context context, const Selection &selection)
{
	if (selection.second == NULL) {
		return mt32emu_add_rom_data(context, selection.first_bytes->data(),
									selection.first_bytes->size(), NULL) > MT32EMU_RC_OK;
	}
	return mt32emu_merge_and_add_rom_data(context, selection.first_bytes->data(),
										  selection.first_bytes->size(), NULL,
										  selection.second_bytes->data(),
										  selection.second_bytes->size(), NULL) > MT32EMU_RC_OK;
}

static void copy_error(legacy_char *destination, size_t capacity, const Text &message)
{
	if (destination != NULL && capacity != 0) {
		SDL_strlcpy(destination, message.c_str(), capacity);
	}
}

} // namespace

extern "C" legacy_char *mt32_rom_inventory_json(const legacy_char *directory)
{
	ROMLock lock;
	if (!lock.acquired()) {
		return SDL_strdup("{\"sets\":[],\"error\":\"Cannot allocate MT-32 ROM library mutex.\"}");
	}
	try {
		return SDL_strdup(inventory_json(scan_directory(directory)).c_str());
	} catch (const std::bad_alloc &) {
		return SDL_strdup("{\"sets\":[],\"error\":\"Not enough memory to inspect MT-32 ROMs.\"}");
	}
}

extern "C" void mt32_rom_inventory_free(legacy_char *inventory)
{
	SDL_free(inventory);
}

extern "C" legacy_int mt32_rom_load(mt32emu_context context, const legacy_char *directory,
									const legacy_char *model, legacy_char *error, size_t error_size)
{
	ROMLock lock;
	if (!lock.acquired()) {
		copy_error(error, error_size, "Cannot allocate MT-32 ROM library mutex.");
		return 0;
	}
	try {
		if (context == NULL || mt32emu_is_open(context)) {
			copy_error(error, error_size, "MT-32 ROM loading requires an unopened synthesizer.");
			return 0;
		}
		Inventory inventory = scan_directory(directory);
		Text message;
		const Set *set = choose_set(inventory, model, message);
		if (set == NULL) {
			copy_error(error, error_size, message);
			return 0;
		}
		/* Full-image loading borrows these bytes. Keep them until the caller
		 * destroys its context, even if a later step of opening the synth fails. */
		BufferList &buffers = loaded_buffers[context];
		buffers.push_back(set->control.first_bytes);
		buffers.push_back(set->control.second_bytes);
		buffers.push_back(set->pcm.first_bytes);
		buffers.push_back(set->pcm.second_bytes);
		if (!add_selection(context, set->control) || !add_selection(context, set->pcm)) {
			copy_error(error, error_size, "Munt could not load the selected compatible ROM set.");
			return 0;
		}
		copy_error(error, error_size, "");
		return 1;
	} catch (const std::bad_alloc &) {
		copy_error(error, error_size, "Not enough memory to load MT-32 ROMs.");
		return 0;
	}
}

extern "C" void mt32_rom_release(mt32emu_context context)
{
	ROMLock lock;
	if (lock.acquired()) {
		loaded_buffers.erase(context);
	}
}

#ifdef RESTUNTS_MT32_ROM_TESTS
/* Exercise resolution with the real Munt metadata, without distributing any
 * copyrighted ROM bytes. The production scanner supplies the same identities
 * only after its size and SHA1 checks succeed. */
extern "C" legacy_char *mt32_rom_test_inventory(const legacy_char *const *rom_ids, size_t count,
												const legacy_char *model, legacy_char *error,
												size_t error_size)
{
	ROMLock lock;
	if (!lock.acquired()) {
		copy_error(error, error_size, "Cannot allocate MT-32 ROM library mutex.");
		return NULL;
	}
	Inventory inventory;
	const ROMInfo *const *infos = ROMInfo::getAllROMInfos();
	for (size_t index = 0; index < count; ++index) {
		for (size_t info_index = 0; infos[info_index] != NULL; ++info_index) {
			if (SDL_strcmp(rom_ids[index], infos[info_index]->shortName) == 0) {
				Candidate image;
				image.digest = infos[info_index]->sha1Digest;
				image.bytes = SharedBytes(new Bytes);
				inventory.images.push_back(image);
				break;
			}
		}
	}
	resolve_sets(inventory);
	Text message;
	choose_set(inventory, model, message);
	copy_error(error, error_size, message);
	return SDL_strdup(inventory_json(inventory).c_str());
}
#endif
