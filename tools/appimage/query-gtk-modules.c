/* Build with the legacy SDK, then query its matching GTK2 modules at runtime.
 * The generated caches contain this AppImage's current absolute paths. */
#define GDK_PIXBUF_ENABLE_BACKEND
#define PANGO_ENABLE_ENGINE
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gmodule.h>
#include <pango/pango-engine.h>
#include <pango/pango-enum-types.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gdk-pixbuf-io.h>
#include "../../src/restunts/c/legacy.h"

#define MODULE_MODE_INDEX 1
#define MODULE_FIRST_PATH_INDEX 2
#define MODULE_MINIMUM_ARGUMENT_COUNT 3
#define MODULE_QUERY_SUCCESS 0
#define MODULE_QUERY_FAILURE 1
#define MODULE_TRANSLATION_DOMAIN "gtk20"

static void print_escaped(const legacy_char *value)
{
	legacy_char *escaped = g_strescape(value != NULL ? value : "", NULL);
	printf("\"%s\" ", escaped);
	g_free(escaped);
}

static void print_pango_path(const legacy_char *path)
{
	/* Pango's cache accepts quoted UTF-8 with newline, quote and backslash escapes. */
	putchar('"');
	for (; *path != '\0'; path++) {
		if (*path == '\n') {
			fputs("\\n", stdout);
		} else {
			if (*path == '"' || *path == '\\') {
				putchar('\\');
			}
			putchar((legacy_u8)*path);
		}
	}
	putchar('"');
}

static legacy_int query_pango(GModule *module, const legacy_char *path)
{
	void (*list)(PangoEngineInfo **, legacy_int *);
	if (!g_module_symbol(module, "script_engine_list", (gpointer *)&list)) {
		return MODULE_QUERY_FAILURE;
	}
	PangoEngineInfo *engines;
	legacy_int count;
	list(&engines, &count);
	GEnumClass *scripts = g_type_class_ref(PANGO_TYPE_SCRIPT);
	for (legacy_int index = 0; index < count; index++) {
		PangoEngineInfo *engine = &engines[index];
		print_pango_path(path);
		printf(" %s %s %s", engine->id, engine->engine_type, engine->render_type);
		for (legacy_int script = 0; script < engine->n_scripts; script++) {
			GEnumValue *value = g_enum_get_value(scripts, engine->scripts[script].script);
			if (value == NULL) {
				g_type_class_unref(scripts);
				return MODULE_QUERY_FAILURE;
			}
			printf(" %s:%s", value->value_nick, engine->scripts[script].langs);
		}
		putchar('\n');
	}
	g_type_class_unref(scripts);
	return MODULE_QUERY_SUCCESS;
}

static legacy_int query_pixbuf(GModule *module, const legacy_char *path)
{
	void (*fill)(GdkPixbufFormat *);
	if (!g_module_symbol(module, "fill_info", (gpointer *)&fill)) {
		return MODULE_QUERY_FAILURE;
	}
	GdkPixbufFormat info = {0};
	fill(&info);
	if (info.signature == NULL || info.mime_types == NULL || info.extensions == NULL) {
		return MODULE_QUERY_FAILURE;
	}
	print_escaped(path);
	putchar('\n');
	print_escaped(info.name);
	printf("%u ", info.flags);
	print_escaped(info.domain != NULL ? info.domain : MODULE_TRANSLATION_DOMAIN);
	print_escaped(info.description);
	print_escaped(info.license);
	putchar('\n');
	for (legacy_char **mime = info.mime_types; *mime != NULL; mime++) {
		print_escaped(*mime);
	}
	fputs("\"\"\n", stdout);
	for (legacy_char **extension = info.extensions; *extension != NULL; extension++) {
		print_escaped(*extension);
	}
	fputs("\"\"\n", stdout);
	for (GdkPixbufModulePattern *pattern = info.signature; pattern->prefix != NULL; pattern++) {
		print_escaped(pattern->prefix);
		print_escaped(pattern->mask);
		printf("%d\n", pattern->relevance);
	}
	putchar('\n');
	return MODULE_QUERY_SUCCESS;
}

legacy_int main(legacy_int argc, legacy_char **argv)
{
	if (argc < MODULE_MINIMUM_ARGUMENT_COUNT || (strcmp(argv[MODULE_MODE_INDEX], "pango") != 0 &&
												 strcmp(argv[MODULE_MODE_INDEX], "pixbuf") != 0)) {
		fputs("Usage: restunts-gtk-modules pango|pixbuf MODULE...\n", stderr);
		return MODULE_QUERY_FAILURE;
	}
	g_type_init();
	for (legacy_int index = MODULE_FIRST_PATH_INDEX; index < argc; index++) {
		GModule *module = g_module_open(argv[index], G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
		if (module == NULL) {
			fprintf(stderr, "Cannot load GTK module %s: %s\n", argv[index], g_module_error());
			return MODULE_QUERY_FAILURE;
		}
		legacy_int result = strcmp(argv[MODULE_MODE_INDEX], "pango") == 0
								? query_pango(module, argv[index])
								: query_pixbuf(module, argv[index]);
		g_module_close(module);
		if (result != MODULE_QUERY_SUCCESS) {
			fprintf(stderr, "Cannot query GTK module %s\n", argv[index]);
			return result;
		}
	}
	return fflush(stdout) == 0 ? MODULE_QUERY_SUCCESS : MODULE_QUERY_FAILURE;
}
