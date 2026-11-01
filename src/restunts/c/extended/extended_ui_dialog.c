#include "../ui/ui_dialog_internal.h"
#include "extended_ui_dialog.h"
#ifdef RESTUNTS_SDL3
#include "../ui/ui_file_dialog_internal.h"
#include "../fileio.h"
#include "../ui/ui_text.h"
#include "../externs.h"
#define FILE_DIALOG_EXTENDED_TEXT_COLOR 4
legacy_s16 extended_ui_dialog_append_name(struct FILE_DIALOG *dialog, const legacy_s8 *path,
										  legacy_u8 extended)
{
	if (dialog->format.merge_formats) {
		const legacy_s8 *stem = path;
		for (const legacy_s8 *cursor = path; *cursor != 0; cursor++) {
			if (*cursor == '/' || *cursor == '\\' || *cursor == ':') {
				stem = cursor + 1;
			}
		}
		legacy_u16 length = 0;
		while (stem[length] != 0 && stem[length] != '.') {
			length++;
		}
		/* GAMEINFO retains the original eight-character track-name field. */
		if (length == 0 || length > 8U || stem[length] != '.') {
			return -1;
		}
		memcpy(dialog->filenames[dialog->file_count], stem, length);
		dialog->filenames[dialog->file_count][length] = 0;
	} else {
		dialog->format.extended[dialog->file_count] = extended;
		return 0;
	}
	dialog->format.extended[dialog->file_count] = extended;

	return 1;
}
void extended_ui_dialog_swap(struct FILE_DIALOG *dialog, legacy_u16 first, legacy_u16 second)
{
	legacy_u8 format = dialog->format.extended[first];
	dialog->format.extended[first] = dialog->format.extended[second];
	dialog->format.extended[second] = format;
}
static const legacy_s8 *extended_ui_dialog_suffix(const legacy_s8 *legacy)
{
	return strcmp(legacy, ".trk") == 0 ? ".cstrk" : ".csrpl";
}
void extended_ui_dialog_label(struct FILE_DIALOG *dialog, legacy_s16 candidate, legacy_s8 *text)
{
	if (!dialog->format.merge_formats) {
		return;
	}
	const legacy_s8 *extension = dialog->format.legacy_extension;
	if (dialog->format.extended[(legacy_u8)candidate]) {
		extension = extended_ui_dialog_suffix(extension);
		font_set_colors(FILE_DIALOG_EXTENDED_TEXT_COLOR, candidate == dialog->selected
															 ? dialog_fnt_colour
															 : dialog_background_color);
	}
	strcat(text, extension);
}
void extended_ui_dialog_selected_extension(struct FILE_DIALOG *dialog, legacy_s8 *out)
{
	if (dialog->format.merge_formats && dialog->format.extended[(legacy_u8)dialog->selected]) {
		strcpy(out, extended_ui_dialog_suffix(dialog->format.legacy_extension));
	}
}
void extended_ui_dialog_initialize(struct FILE_DIALOG *dialog, legacy_u8 merge,
								   const legacy_s8 *extension)
{
	dialog->format.merge_formats =
		merge && (strcmp(extension, ".trk") == 0 || strcmp(extension, ".rpl") == 0);
	dialog->format.legacy_extension = extension;
}
void extended_ui_dialog_collect(struct FILE_DIALOG *dialog, const legacy_s8 *directory,
								void (*collect)(struct FILE_DIALOG *, const legacy_s8 *, legacy_u8))
{
	if (dialog->format.merge_formats) {
		const legacy_s8 *found = file_combine_and_find(
			directory, "*", extended_ui_dialog_suffix(dialog->format.legacy_extension));
		if (found != 0) {
			collect(dialog, found, 1);
		}
	}
}
#endif
