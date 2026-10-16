#!/usr/bin/env python3
"""Translate the shared DOS game into native HolyC (requires pycparser 2.22).

Run from any directory.  Original C remains the source of truth.  The C
preprocessor selects RESTUNTS_FULL without SDL3; the AST pass preserves C
operator precedence and lowers unsupported HolyC constructs explicitly.
"""
from __future__ import annotations

import argparse
import ast as python_ast
import copy
import hashlib
import importlib.machinery
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from pycparser import c_ast as A, c_parser

HERE = Path(__file__).resolve().parent.parent
STYLE_LOADER = importlib.machinery.SourceFileLoader("temple_style", str(HERE / "tools/STYLE.PY"))
STYLE_SPEC = importlib.util.spec_from_loader(STYLE_LOADER.name, STYLE_LOADER)
STYLE = importlib.util.module_from_spec(STYLE_SPEC)
STYLE_LOADER.exec_module(STYLE)
ROOT = HERE.parents[3]
C_DIR = ROOT / "src/restunts/c"
GEN = HERE / "generated"
FORMATTER = None
CHECK = False
MISMATCHES = []
NATIVE_PRERENDER_FUNCTIONS = {
    "preRender_default", "preRender_default_alt", "preRender_patterned",
    "preRender_two_color", "preRender_line", "preRender_sphere",
}
RASTER_SOURCE_FUNCTIONS = {
    "shape2d_render_rle", "shape2d_render_rle_clipped",
    "sprite_putimage_at", "sprite_shape_to_1_impl",
}


def native_function(name):
    return name.startswith("video_pages_") or name in NATIVE_PRERENDER_FUNCTIONS


FAKE_HEADERS = {
    "stddef.h": """
#ifndef _STUNT_STDDEF
#define _STUNT_STDDEF
typedef unsigned long size_t;
typedef long ptrdiff_t;
#define NULL 0
#define offsetof(type, member) ((size_t)&(((type *)0)->member))
#endif
""",
    "stdint.h": "#include <stddef.h>\ntypedef unsigned long uintptr_t;\ntypedef long intptr_t;\n",
    "limits.h": """
#define CHAR_BIT 8
#define SCHAR_MAX 127
#define SCHAR_MIN (-128)
#define SHRT_MAX 32767
#define USHRT_MAX 65535U
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U
#define LONG_MAX 9223372036854775807L
#define ULONG_MAX 18446744073709551615UL
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL
""",
    "float.h": """
#define FLT_RADIX 2
#define FLT_MANT_DIG 24
#define FLT_MIN_EXP (-125)
#define FLT_MAX_EXP 128
#define DBL_MANT_DIG 53
#define DBL_MIN_EXP (-1021)
#define DBL_MAX_EXP 1024
""",
    "stdlib.h": """
#include <stddef.h>
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
int abs(int value);
long labs(long value);
int atoi(const char *str);
void exit(int status);
int rand(void);
void srand(unsigned int seed);
void qsort(void *ptr, size_t count, size_t size, int (*compare)(const void *, const void *));
""",
    "string.h": """
#include <stddef.h>
void *memcpy(void *dst, const void *src, size_t count);
void *memmove(void *dst, const void *src, size_t count);
void *memset(void *dst, int value, size_t count);
int memcmp(const void *first, const void *second, size_t count);
size_t strlen(const char *str);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t count);
char *strcat(char *dst, const char *src);
char *strchr(const char *str, int ch);
char *strrchr(const char *str, int ch);
int strcmp(const char *first, const char *second);
int strncmp(const char *first, const char *second, size_t count);
""",
    "stdio.h": """
#include <stddef.h>
typedef void FILE;
extern FILE *stderr;
extern FILE *stdout;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
int fputs(const char *str, FILE *stream);
int sprintf(char *dst, const char *format, ...);
int snprintf(char *dst, size_t size, const char *format, ...);
int printf(const char *format, ...);
""",
    "ctype.h": "int toupper(int ch);\nint tolower(int ch);\nint isspace(int ch);\nint isdigit(int ch);\n",
    "stdarg.h": """
#ifndef _STUNT_STDARG
#define _STUNT_STDARG
typedef unsigned long *va_list;
#define va_start(args, last) ((args) = __temple_argv)
#define va_arg(args, type) ((type)(*(args)++))
#define va_end(args) ((void)0)
#endif
""",
}

BUILTINS = {
    "char": "legacy_char", "signed char": "legacy_s8", "unsigned char": "legacy_u8",
    "short": "legacy_s16", "signed short": "legacy_s16", "unsigned short": "legacy_u16",
    "int": "legacy_int", "signed int": "legacy_int", "unsigned int": "legacy_uint",
    "long": "legacy_s64", "unsigned long": "legacy_u64", "long long": "legacy_s64",
    "unsigned long long": "legacy_u64", "float": "legacy_f64", "double": "legacy_f64",
    "void": "legacy_void", "size_t": "legacy_u64", "ptrdiff_t": "legacy_s64",
    "uintptr_t": "legacy_u64", "intptr_t": "legacy_s64", "FILE": "legacy_void",
    "va_list": "legacy_u64 *",
}

LIBC = {"__temple_argv": "argv"}
INTEGER_TYPES = {
    "legacy_char": (8, True), "legacy_s8": (8, True), "legacy_u8": (8, False),
    "legacy_s16": (16, True), "legacy_u16": (16, False),
    "legacy_int": (32, True), "legacy_uint": (32, False),
    "legacy_s32": (32, True), "legacy_u32": (32, False),
    "legacy_s64": (64, True), "legacy_u64": (64, False),
}

def scalar_type(name):
    return A.TypeDecl(None, [], None, A.IdentifierType([name]))



def source_names():
    makefile = (ROOT / "src/restunts/dos/full-game-objects.mk").read_text()
    # The canonical state serializer is used by the cross-platform replay
    # regression even though the original executable does not link it.
    return list(dict.fromkeys(re.findall(r"\b(\w+)\.obj", makefile) + ["stateio"]))


def prepare_sources(folder):
    target = folder / "src/restunts/c"
    target.mkdir(parents=True)
    for path in C_DIR.glob("*.[ch]"):
        text = path.read_text()
        # These branches describe flat-pointer storage rather than SDL features.
        if path.name == "shape2d_internal.h":
            text = text.replace("RESTUNTS_SDL3", "RESTUNTS_TEMPLEOS")
        if path.name == "full_strings.c":
            text = text.replace("audiodriverstring[5] = {'a', 'd', '1', '5', 0}", "audiodriverstring[5] = {'p', 'c', '1', '5', 0}")
        if path.name == "command_line.c":
            help_text = ('Restunts for TempleOS\r\n'
                         'Run Restunts("C:/DATA"); append TRUE to skip the intro.\r\n'
                         'Video: 640 x 400 (8:5), 16 colors. Audio: PC speaker.\r\n'
                         'F12 toggles SuperSight.\r\n')
            replacement = 'static const legacy_s8 command_line_help[] = ' + json.dumps(help_text) + ';'
            text, count = re.subn(r'static const legacy_s8 command_line_help\[\] =.*?COMMAND_LINE_NEWLINE;', lambda match: replacement, text, flags=re.S)
            if count != 1:
                raise ValueError("TempleOS startup help adaptation missing")
        if path.name == "memmgr.c":
            text = text.replace('#include "hires.h"', '')
            text = text.replace("RESTUNTS_SDL3", "RESTUNTS_TEMPLEOS")
            text = text.replace("hires_forget_range", "TempleRasterForgetRange")
        native_functions = {
            "shape2d.c": ["shape2d_get_line_offset", "shape2d_line_pointer", "sprite_set_palette_map", "sprite_draw_dissolve_row"],
            "shape2d_blit.c": ["sprite_make_wnd", "sprite_free_wnd", "sprite_save_context", "sprite_restore_context", "sprite_push_background", "sprite_pop_background"],
            "full_data.c": ["full_initialize_screen_sprite", "full_data_initialize"],
            "audio_resources.c": ["audio_load_dos_driver", "audioresource_compare_chunknames", "audioresource_get_chunk_index", "audio_far_pointer_add_normalized", "audioresource_find", "audio_map_song_instruments", "audioresource_copy_n_bytes"],
            "audio_engine.c": ["audio_copy_engine_definition", "audio_reclaim_driver_context", "audio_set_finish_callback"],
            "fileio.c": ["file_read", "file_write"],
        }.get(path.name, [])
        for function in native_functions:
            match = re.search(r"^.*\b" + function + r"\([^;]*?\n\{", text, re.M)
            if not match:
                raise ValueError(f"Native pointer adaptation missing: {path.name}:{function}")
            end = text.index("\n}", match.end()) + 2
            block = text[match.start():end].replace("RESTUNTS_SDL3", "RESTUNTS_TEMPLEOS")
            if function == "sprite_free_wnd":
                block = block.replace("hires_forget(", "TempleRasterForget(")
            if function == "audio_load_dos_driver":
                block = block.replace("audio_driver_prefix[0] = 'a'", "audio_driver_prefix[0] = 'p'")
                block = block.replace("audio_driver_prefix[1] = 'd'", "audio_driver_prefix[1] = 'c'")
            text = text[:match.start()] + block + text[end:]
        if path.name == "shape2d.c":
            text, count = re.subn(r"video_pages_write_pixel\(bitmap, destination,\s*video_pages_read_pixel\(bitmap, destination\) \^ color_bits\);", "TempleRasterXorPixel(bitmap, destination, color_bits);", text)
            if count != 1:
                raise ValueError("Native XOR raster adaptation missing")
        if path.name == "shape2d_blit.c":
            text, count = re.subn(
                r"#if defined\(RESTUNTS_SDL3\) \|\| defined\(RESTUNTS_AMIGA\)"
                r"(\nstatic struct SPRITE\s+native_background_contexts\b)",
                r"#ifdef RESTUNTS_TEMPLEOS\1", text)
            if count != 1:
                raise ValueError("Native background storage adaptation missing")
            text = text.replace("native_background_contexts", "temple_background_contexts")
            text = text.replace("return farwnd;", "TempleRasterRegisterSprite(farwnd);\n\treturn farwnd;")
            text = text.replace("fmemcpy(&drawing_sprite, target_sprite, sizeof(struct SPRITE));", "fmemcpy(&drawing_sprite, target_sprite, sizeof(struct SPRITE));\n\tTempleRasterSelect(&drawing_sprite);")
            text = text.replace("\tsprite_putimage(mcga_backbuffer_sprite->sprite_bitmapptr);", "\tsprite_putimage(mcga_backbuffer_sprite->sprite_bitmapptr);\n\tTempleVideoPresent();")
        (target / path.name).write_text(text)
    memory = folder / "src/restunts/platform/sdl3"
    memory.mkdir(parents=True)
    shutil.copyfile(ROOT / "src/restunts/platform/sdl3/memory.c", memory / "memory.c")
    return target


def typedef_name(node):
    while isinstance(node, (A.TypeDecl, A.PtrDecl, A.ArrayDecl, A.FuncDecl)):
        node = node.type
    return node


class Model:
    def __init__(self):
        self.units = []
        self.classes = {}
        self.enums = {}
        self.aliases = {}
        self.symbols = {}
        self.functions = {}
        self.global_defs = {}
        self.global_decls = {}
        self.orig_names = {}
        self.external_used = set()
        self.serial = 0
        self.data_initializers = []
        self.name_map = {}

    def unique(self, name="tmp"):
        self.serial += 1
        result = f"temple_{name}_{self.serial}"
        add_name(self.name_map, result, STYLE.snake_case(result))
        return result

    def collect(self, ast, module):
        rename = {}
        for item in ast.ext:
            decl = item.decl if isinstance(item, A.FuncDef) else item
            if isinstance(decl, A.Decl) and "static" in decl.storage and not (decl.name or "").startswith("video_pages_"):
                rename[decl.name] = f"rs_{module}_{decl.name}"
        for item in ast.ext:
            decl = item.decl if isinstance(item, A.FuncDef) else item
            if isinstance(decl, A.Decl) and decl.name:
                name = rename.get(decl.name, decl.name)
                self.symbols[name] = decl.type
                self.orig_names[name] = decl.name
                if isinstance(decl.type, A.FuncDecl):
                    if name not in self.functions or isinstance(item, A.FuncDef):
                        self.functions[name] = (decl, rename, item if isinstance(item, A.FuncDef) else None)
                else:
                    if "/include/" not in str(decl.coord.file):
                        self.global_decls[name] = (decl, rename)
                    if "extern" not in decl.storage:
                        self.global_defs[name] = (decl, rename)
            self.scan_types(item)
        self.units.append((module, ast, rename))

    def scan_types(self, node):
        if isinstance(node, A.Decl) and isinstance(node.type, A.ArrayDecl) and node.type.dim is None and node.init:
            if isinstance(node.init, A.InitList):
                length = len(node.init.exprs)
            elif isinstance(node.init, A.Constant) and node.init.type == "string":
                length = len(python_ast.literal_eval(node.init.value).encode("latin-1")) + 1
            else:
                raise ValueError("Cannot infer array size")
            node.type.dim = A.Constant("int", str(length))
        if isinstance(node, (A.Struct, A.Union)) and node.decls:
            if not node.name:
                node.name = "RsAnon_" + hashlib.sha256((Path(node.coord.file).name + ":" + str(node.coord.line) + ":" + type(node).__name__).encode()).hexdigest()[:12]
            self.classes.setdefault(node.name, node)
        if isinstance(node, A.Enum) and node.values:
            previous = "-1"
            for enum in node.values.enumerators:
                previous = enum.value if enum.value is not None else A.BinaryOp("+", A.Constant("int", previous), A.Constant("int", "1")) if isinstance(previous, str) else A.BinaryOp("+", copy.deepcopy(previous), A.Constant("int", "1"))
                self.enums.setdefault(enum.name, previous)
        if isinstance(node, A.Typedef) and not isinstance(node.type, A.ArrayDecl):
            if node.name not in BUILTINS and not node.name.startswith("legacy_"):
                self.aliases.setdefault(node.name, node.type)
        for _, child in node.children():
            self.scan_types(child)


def add_name(mapping, original, styled):
    previous = mapping.get(original)
    if previous is not None and previous != styled:
        raise ValueError(f"Conflicting HolyC names for {original}: {previous} and {styled}")
    mapping[original] = styled


def check_name_collisions(names, mapping, scope):
    originals = {}
    for name in names:
        styled = mapping.get(name, name)
        previous = originals.setdefault(styled, name)
        if previous != name:
            raise ValueError(f"HolyC name collision in {scope}: {previous} and {name} become {styled}")


def build_name_map(model):
    """Keep C symbols for semantic analysis; name only the emitted HolyC tokens.

    Static symbols already carry their translation-unit prefix and local
    storage carries a unique serial. Validate the remaining shared and member
    namespaces before writing any output, so a new C name cannot silently bind
    to a different HolyC declaration after case/underscore normalization.
    """
    mapping = model.name_map
    # Enum values become preprocessor constants. Reserve their original
    # spelling so a same-named member/type cannot silently rewrite the macro.
    for name in model.enums:
        add_name(mapping, name, name)
    functions = {name for name, (decl, _, definition) in model.functions.items()
                 if definition or "/include/" not in str(decl.coord.file)}
    for name in functions:
        add_name(mapping, name, STYLE.function_name(name))
    for name in model.classes:
        add_name(mapping, name, STYLE.class_name(name))
    for name in set(model.global_decls) | set(model.global_defs):
        add_name(mapping, name, STYLE.snake_case(name))

    def fields(cls):
        names = []
        for field in cls.decls or []:
            if field.name:
                add_name(mapping, field.name, STYLE.snake_case(field.name))
                names.append(field.name)
            elif isinstance(field.type, (A.Struct, A.Union)):
                names += fields(field.type)
        check_name_collisions(names, mapping, "class " + cls.name)
        return names

    for cls in model.classes.values():
        fields(cls)
    for name, (decl, _, _) in model.functions.items():
        names = []
        for arg in decl.type.args.params if decl.type.args else []:
            if isinstance(arg, A.Decl) and arg.name:
                add_name(mapping, arg.name, STYLE.snake_case(arg.name))
                add_name(mapping, "temple_value_" + arg.name,
                         "temple_value_" + STYLE.snake_case(arg.name))
                names.append(arg.name)
        check_name_collisions(names, mapping, "parameters of " + name)
    check_name_collisions(functions | set(model.classes) |
                          set(model.global_decls) | set(model.global_defs) |
                          set(model.enums), mapping, "global scope")
    return mapping


class Generator:
    def __init__(self, model, rename=None):
        self.m = model
        self.rename = dict(rename or {})
        self.scopes = [{}]
        self.types = dict(model.symbols)
        self.local_decls = []
        self.extra_globals = []
        self.loop_labels = []
        self.in_function = False
        self.current_function = ""
        self.return_type = None
        self.param_arrays = {}

    def lookup(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        return self.rename.get(name, LIBC.get(name, name))

    def type_text(self, node):
        if isinstance(node, A.TypeDecl):
            return self.type_text(node.type)
        if isinstance(node, A.IdentifierType):
            value = " ".join(node.names)
            if value in self.m.aliases:
                return self.type_text(self.m.aliases[value])
            return BUILTINS.get(value, value)
        if isinstance(node, (A.Struct, A.Union)):
            return node.name
        if isinstance(node, A.Enum):
            return "legacy_int"
        if isinstance(node, A.PtrDecl):
            if isinstance(node.type, A.FuncDecl):
                return "legacy_u8 *"
            pointee = self.type_text(node.type)
            return ("legacy_u8" if pointee == "legacy_void" else pointee) + " *"
        if isinstance(node, A.ArrayDecl):
            pointee = self.type_text(node.type)
            return ("legacy_u8" if pointee == "legacy_void" else pointee) + " *"
        if isinstance(node, A.FuncDecl):
            return self.type_text(node.type)
        if isinstance(node, A.Typename):
            return self.type_text(node.type)
        raise NotImplementedError(f"type {type(node).__name__}")

    def class_fields(self, cls):
        result = []
        for d in cls.decls:
            if d.name is None and isinstance(d.type, (A.Struct, A.Union)):
                result += ["union {" if isinstance(d.type, A.Union) else "class {"]
                result += indent(self.class_fields(d.type)) + ["};"]
            else:
                result += [self.decl(d.type, d.name) + ";"]
        return result

    def decl(self, node, name):
        if isinstance(node, A.TypeDecl) and isinstance(node.type, A.IdentifierType):
            alias = " ".join(node.type.names)
            if alias in self.m.aliases:
                return self.decl(self.m.aliases[alias], name)
        if isinstance(node, A.ArrayDecl):
            active = self.in_function
            self.in_function = False
            _, dim = self.expr(node.dim) if node.dim else ([], "")
            self.in_function = active
            return self.decl(node.type, name + f"[{dim}]")
        if isinstance(node, A.PtrDecl):
            if isinstance(node.type, A.FuncDecl):
                return "legacy_u8 *" + name
            if isinstance(node.type, A.ArrayDecl):
                return self.decl(node.type, "(*" + name + ")")
            if self.type_text(node.type) == "legacy_void":
                return "legacy_u8 *" + name
            return self.decl(node.type, "*" + name)
        if isinstance(node, A.FuncDecl):
            return self.prototype(node, name)
        return self.type_text(node) + " " + name

    def prototype(self, node, name):
        args = []
        if node.args:
            for index, arg in enumerate(node.args.params):
                if isinstance(arg, A.EllipsisParam):
                    args.append("...")
                elif self.type_text(arg.type) != "legacy_void":
                    typ = arg.type
                    if isinstance(typ, A.ArrayDecl):
                        while isinstance(typ, A.ArrayDecl):
                            typ = typ.type
                        typ = A.PtrDecl([], typ)
                    name_arg = arg.name or f"arg{index}"
                    if self.class_type_from_type(typ):
                        args.append(self.decl(A.PtrDecl([], typ), "temple_value_" + name_arg))
                    else:
                        args.append(self.decl(typ, name_arg))
        result = self.class_type_from_type(node.type)
        if result:
            args.append(result + " *temple_return_value")
        return ("legacy_void" if result else self.type_text(node.type)) + " " + name + "(" + ", ".join(args) + ")"

    def local_decl(self, node, name):
        # HolyC widens register variables to 64 bits. Keep narrow automatic
        # storage at its declared C width, including when its address is used.
        # Parameters already occupy full register-sized slots in HolyC.
        info = self.integer_info(node)
        if (isinstance(self.resolve_type(node), (A.TypeDecl, A.Enum)) and
                info and info[0] < INTEGER_TYPES["legacy_s64"][0]):
            name = "noreg " + name
        return self.decl(node, name)

    def resolve_type(self, typ):
        if isinstance(typ, A.TypeDecl) and isinstance(typ.type, A.IdentifierType):
            alias = " ".join(typ.type.names)
            if alias in self.m.aliases:
                return self.resolve_type(self.m.aliases[alias])
        return typ

    def integer_info(self, typ):
        if typ is None:
            return None
        return INTEGER_TYPES.get(self.type_text(typ))

    def normalize(self, expr, typ):
        if not self.in_function:
            return expr
        info = self.integer_info(typ)
        if info and info[0] < 64:
            return f"TempleCast{'S' if info[1] else 'U'}{info[0]}({expr})"
        return expr

    def convert(self, expr, source, target):
        if target and self.type_text(target) == "legacy_f64":
            return expr if source and self.type_text(source) == "legacy_f64" else f"ToF64({expr})"
        source_info, target_info = self.integer_info(source), self.integer_info(target)
        if not source_info or not target_info:
            return expr
        if target_info[0] == 64:
            if source_info[1] != target_info[1]:
                return f"TempleCast{'S' if target_info[1] else 'U'}64({expr})"
            return expr
        if source_info[0] > target_info[0] or source_info[1] and not target_info[1]:
            return self.normalize(expr, target)
        return expr

    def promote(self, typ):
        info = self.integer_info(typ)
        if info and info[0] < 32:
            return scalar_type("legacy_s32")
        return typ

    def common_type(self, left, right):
        if isinstance(left, (A.PtrDecl, A.ArrayDecl)):
            return left
        if isinstance(right, (A.PtrDecl, A.ArrayDecl)):
            return right
        if any(self.type_text(typ) == "legacy_f64" for typ in (left, right) if typ):
            return scalar_type("legacy_f64")
        left, right = self.promote(left), self.promote(right)
        a, b = self.integer_info(left), self.integer_info(right)
        if a and b:
            width = max(a[0], b[0])
            signed = (a[1] and b[1]) or (a[0] != b[0] and (a if a[0] > b[0] else b)[1])
            return scalar_type(f"legacy_{'s' if signed else 'u'}{width}")
        return left or right

    def member_type(self, cls, name):
        for field in cls.decls or []:
            if field.name == name:
                return field.type
            if field.name is None:
                base = typedef_name(field.type)
                if isinstance(base, (A.Struct, A.Union)):
                    nested = self.m.classes.get(base.name, base)
                    found = self.member_type(nested, name)
                    if found is not None:
                        return found
        return None

    def division(self, left, right, operation, typ):
        info = self.integer_info(typ)
        if self.in_function and info and operation in ("/", "%"):
            # HolyC rewrites signed division by constant powers of two to
            # shifts, rounding negative values down instead of toward zero.
            # A function parameter keeps the divisor out of that optimizer.
            name = "TempleDivide" if operation == "/" else "TempleRemainder"
            return name + ("S64" if info[1] else "U64") + f"({left}, {right})"
        return f"({left} {operation} {right})"

    def node_type(self, n):
        if isinstance(n, A.Constant):
            if n.type == "string":
                return A.PtrDecl([], scalar_type("legacy_char"))
            if n.type in ("float", "double"):
                return scalar_type("legacy_f64")
            if n.type == "char":
                return scalar_type("legacy_s32")
            text = n.value.lower()
            raw = re.sub(r"[ul]+$", "", text)
            number = int(raw, 0) if not (len(raw) > 1 and raw[0] == "0" and raw[1].isdigit()) else int(raw, 8)
            wide = "ll" in text or number > 4294967295 or (number > 2147483647 and not raw.startswith("0") and "u" not in text)
            unsigned = "u" in text or number > 9223372036854775807 or (not wide and number > 2147483647)
            return scalar_type(f"legacy_{'u' if unsigned else 's'}{64 if wide else 32}")
        if isinstance(n, A.ID):
            if n.name in self.m.enums:
                return scalar_type("legacy_s32")
            return self.resolve_type(self.types.get(self.lookup(n.name)))
        if isinstance(n, A.Cast):
            return n.to_type.type
        if isinstance(n, A.CompoundLiteral):
            return n.type.type
        if isinstance(n, A.UnaryOp):
            if n.op == "sizeof":
                return scalar_type("legacy_u64")
            if n.op == "!":
                return scalar_type("legacy_s32")
            t = self.node_type(n.expr)
            if n.op == "&":
                return A.PtrDecl([], t) if t else None
            if n.op == "*" and isinstance(t, (A.PtrDecl, A.ArrayDecl)):
                return t.type
            return self.promote(t) if n.op in ("+", "-", "~") else t
        if isinstance(n, A.ArrayRef):
            t = self.node_type(n.name)
            return t.type if isinstance(t, (A.PtrDecl, A.ArrayDecl)) else None
        if isinstance(n, A.StructRef):
            t = self.node_type(n.name)
            if n.type == "->" and isinstance(t, (A.PtrDecl, A.ArrayDecl)):
                t = t.type
            base = typedef_name(t)
            if isinstance(base, (A.Struct, A.Union)):
                struct = self.m.classes.get(base.name)
                if struct:
                    return self.member_type(struct, n.field.name)
        if isinstance(n, A.FuncCall):
            t = self.node_type(n.name)
            if isinstance(t, A.PtrDecl):
                t = t.type
            return t.type if isinstance(t, A.FuncDecl) else None
        if isinstance(n, A.TernaryOp):
            return self.common_type(self.node_type(n.iftrue), self.node_type(n.iffalse))
        if isinstance(n, A.BinaryOp):
            if n.op == "-" and all(isinstance(self.node_type(part), (A.PtrDecl, A.ArrayDecl)) for part in (n.left, n.right)):
                return scalar_type("legacy_s64")
            if n.op in ("==", "!=", "<", ">", "<=", ">=", "&&", "||"):
                return scalar_type("legacy_s32")
            if n.op in ("<<", ">>"):
                return self.promote(self.node_type(n.left))
            return self.common_type(self.node_type(n.left), self.node_type(n.right))
        if isinstance(n, A.Assignment):
            return self.node_type(n.lvalue)
        if isinstance(n, A.ExprList):
            return self.node_type(n.exprs[-1])
        return None

    def class_type_from_type(self, typ):
        typ = self.resolve_type(typ)
        base = typedef_name(typ)
        return base.name if isinstance(typ, A.TypeDecl) and isinstance(base, (A.Struct, A.Union)) else None

    def class_type(self, node):
        return self.class_type_from_type(self.node_type(node))

    def temp(self, node=None):
        name = self.m.unique()
        typ = self.node_type(node) if node else None
        if isinstance(typ, A.ArrayDecl):
            typ = A.PtrDecl([], typ.type)
        text = self.local_decl(typ, name) if typ else "legacy_s64 " + name
        self.local_decls.append(text + ";")
        self.types[name] = typ
        return name

    def expr(self, n, direct_call=False):
        if n is None:
            return [], ""
        if isinstance(n, A.ID):
            name = self.lookup(n.name)
            if name in self.m.functions:
                # Fake libc declarations are not emitted unless used. Their
                # unused names can also be valid class members (for example
                # the track interval's exit field).
                add_name(self.m.name_map, name, STYLE.function_name(name))
                if not direct_call:
                    name = "&" + name
            return [], name
        if isinstance(n, A.Constant):
            v = n.value
            if n.type == "string":
                v = v.replace("$", "$$")
            elif n.type not in ("char", "float", "double"):
                v = re.sub(r"[uUlL]+$", "", v)
                if re.match(r"^0[0-7]+$", v):
                    v = str(int(v, 8))
            return [], v
        if isinstance(n, A.BinaryOp):
            a, x = self.expr(n.left)
            b, y = self.expr(n.right)
            if self.in_function and n.op in ("&&", "||"):
                # HolyC only short-circuits boolean expressions in an if.
                # C also requires this in assignments, returns and arguments,
                # even when the right operand is an ordinary function call.
                temp = self.temp(n)
                test = temp if n.op == "&&" else "!" + temp
                # Materialize an integer truth value in branches. HolyC's
                # repeated logical negation of floating comparisons can keep
                # a floating value whose narrow integer store becomes zero.
                left = [f"{temp} = 0;", f"if ({x}) {{", f"  {temp} = 1;", "}"]
                right = [f"{temp} = 0;", f"if ({y}) {{", f"  {temp} = 1;", "}"]
                return a + left + [f"if ({test}) {{"] + indent(b + right) + ["}"], temp
            if self.in_function and n.op not in ("&&", "||", "<<", ">>"):
                common = self.common_type(self.node_type(n.left), self.node_type(n.right))
                x = self.convert(x, self.node_type(n.left), common)
                y = self.convert(y, self.node_type(n.right), common)
            expr = self.division(x, y, n.op, self.node_type(n))
            if (self.in_function and n.op in ("==", "!=", "<", ">", "<=", ">=") and common and
                    self.type_text(common) == "legacy_f64"):
                temp = self.temp(n)
                return a + b + [f"{temp} = 0;", f"if ({expr}) {{", f"  {temp} = 1;", "}"], temp
            if n.op not in ("==", "!=", "<", ">", "<=", ">=", "&&", "||"):
                expr = self.normalize(expr, self.node_type(n))
            return a + b, expr
        if isinstance(n, A.Cast):
            a, x = self.expr(n.expr)
            typ = self.type_text(n.to_type)
            if typ == "legacy_void":
                return a, x
            if typ == "legacy_f64":
                return a, self.convert(x, self.node_type(n.expr), n.to_type.type)
            if self.integer_info(n.to_type.type):
                if self.node_type(n.expr) and self.type_text(self.node_type(n.expr)) == "legacy_f64":
                    x = f"ToI64({x})"
                if self.integer_info(n.to_type.type)[0] == 64:
                    return a, f"TempleCast{'S' if self.integer_info(n.to_type.type)[1] else 'U'}64({x})"
                return a, self.normalize(x, n.to_type.type)
            return a, f"({x})({typ})"
        if isinstance(n, A.UnaryOp):
            if n.op == "&" and isinstance(n.expr, A.StructRef) and n.expr.type == "->" and isinstance(n.expr.name, A.Cast):
                cast = n.expr.name
                if isinstance(cast.expr, A.Constant) and cast.expr.value == "0":
                    typ = cast.to_type.type
                    if isinstance(typ, A.PtrDecl):
                        return [], "offset(" + self.type_text(typ.type) + "." + n.expr.field.name + ")"
            if n.op == "sizeof":
                if isinstance(n.expr, A.Constant) and n.expr.type == "string":
                    return [], str(len(python_ast.literal_eval(n.expr.value).encode("latin-1")) + 1)
                if isinstance(n.expr, A.Typename):
                    return [], "sizeof(" + self.type_text(n.expr) + ")"
                typ = self.node_type(n.expr)
                if typ is not None:
                    if isinstance(typ, A.ArrayDecl):
                        count = []
                        while isinstance(typ, A.ArrayDecl):
                            _, dim = self.expr(typ.dim)
                            count.append(dim)
                            typ = typ.type
                        return [], "(" + " * ".join(count + ["sizeof(" + self.type_text(typ) + ")"]) + ")"
                    return [], "sizeof(" + self.type_text(typ) + ")"
            a, x = self.expr(n.expr)
            if n.op == "&" and x.startswith("&"):
                return a, x
            if n.op in ("p++", "p--", "++", "--") and self.integer_info(self.node_type(n.expr)):
                typ = self.node_type(n.expr)
                pointer = self.temp()
                # Evaluate an indexed/indirect lvalue exactly once.
                pointer_type = A.PtrDecl([], typ)
                self.local_decls[-1] = self.decl(pointer_type, pointer) + ";"
                result = self.temp(n.expr)
                operation = "+" if "+" in n.op else "-"
                a += [f"{pointer} = &({x});", f"{result} = *{pointer};"]
                a += [f"*{pointer} = " + self.normalize(f"({result} {operation} 1)", typ) + ";"]
                return a, result if n.op.startswith("p") else "(*" + pointer + ")"
            if n.op.startswith("p"):
                return a, "(" + x + n.op[1:] + ")"
            expr = "(" + n.op + "(" + x + "))"
            if (self.in_function and n.op == "!" and self.node_type(n.expr) and
                    self.type_text(self.node_type(n.expr)) == "legacy_f64"):
                temp = self.temp(n)
                return a + [f"{temp} = 0;", f"if ({expr}) {{", f"  {temp} = 1;", "}"], temp
            if n.op in ("-", "+", "~"):
                expr = self.normalize(expr, self.node_type(n))
            return a, expr
        if isinstance(n, A.ArrayRef):
            indices = []
            base = n
            while isinstance(base, A.ArrayRef):
                indices.insert(0, base.subscript)
                base = base.name
            if isinstance(base, A.ID) and base.name in self.param_arrays:
                typ = self.param_arrays[base.name]
                a, terms = [], []
                for index in indices:
                    b, value = self.expr(index)
                    a += b
                    typ = typ.type
                    dimension = typ
                    factors = []
                    while isinstance(dimension, A.ArrayDecl):
                        factors.append(self.expr(dimension.dim)[1])
                        dimension = dimension.type
                    terms.append("(" + " * ".join([value] + factors) + ")")
                subscript = "(" + " + ".join(terms) + ")"
                value = self.lookup(base.name) + "[" + subscript + "]"
                return a, "(&(" + value + "))" if isinstance(typ, A.ArrayDecl) else value
            a, x = self.expr(n.name)
            b, y = self.expr(n.subscript)
            if isinstance(n.name, A.Constant) and n.name.type == "string":
                name = self.temp(n.name)
                a.append(f"{name} = {x};")
                x = name
            return a + b, f"{x}[{y}]"
        if isinstance(n, A.StructRef):
            if n.type == "->" and isinstance(n.name, A.UnaryOp) and n.name.op == "&":
                a, x = self.expr(n.name.expr)
                return a, f"{x}.{n.field.name}"
            a, x = self.expr(n.name)
            if n.type == "->" and isinstance(self.node_type(n.name), A.ArrayDecl):
                # C decays an array before ->; HolyC's member lookup does not.
                return a, f"{x}[0].{n.field.name}"
            if n.type == "->" and not isinstance(n.name, (A.ID, A.ArrayRef, A.StructRef)):
                # Materialize compound pointers so the member access has an
                # explicit type, including pointers returned by functions.
                name = self.temp(n.name)
                a.append(f"{name} = {x};")
                x = name
            return a, f"{x}{n.type}{n.field.name}"
        if isinstance(n, A.FuncCall):
            a, x = self.expr(n.name, direct_call=True)
            args = []
            signature = self.node_type(n.name)
            if isinstance(signature, A.PtrDecl):
                signature = signature.type
            indirect = not (isinstance(n.name, A.ID) and (self.lookup(n.name.name) in self.m.functions or signature is None))
            if indirect and isinstance(signature, A.FuncDecl):
                target = self.m.unique("callee")
                self.local_decls.append(self.prototype(signature, "(*" + target + ")") + ";")
                a += [target + " = " + x + ";"]
                x = target
            params = signature.args.params if isinstance(signature, A.FuncDecl) and signature.args else []
            for index, arg in enumerate(n.args.exprs if n.args else []):
                b, y = self.expr(arg)
                if index < len(params) and isinstance(params[index], (A.Decl, A.Typename)):
                    if self.class_type_from_type(params[index].type):
                        y = "&(" + y + ")"
                    else:
                        y = self.normalize(y, params[index].type)
                a += b
                args.append(y)
            if isinstance(n.name, A.ID):
                name = self.lookup(n.name.name)
                if not indirect and (name not in self.m.functions or self.m.functions[name][2] is None or native_function(name)):
                    self.m.external_used.add(name)
            result = self.class_type(n)
            if result:
                temp = self.temp(n)
                args.append("&" + temp)
                a.append(x + "(" + ", ".join(args) + ");")
                return a, temp
            return a, x + "(" + ", ".join(args) + ")"
        if isinstance(n, A.Assignment):
            a, x = self.expr(n.lvalue)
            b, y = self.expr(n.rvalue)
            cls = self.class_type(n.lvalue)
            if cls:
                return a + b, f"MemCpy(&({x}), &({y}), sizeof({cls}))"
            typ = self.node_type(n.lvalue)
            if n.op != "=" and self.integer_info(typ):
                pointer = self.temp()
                self.local_decls[-1] = self.decl(A.PtrDecl([], typ), pointer) + ";"
                a += [f"{pointer} = &({x});"]
                x = "(*" + pointer + ")"
                operation = n.op[:-1]
                common = self.promote(typ) if operation in ("<<", ">>") else self.common_type(typ, self.node_type(n.rvalue))
                left_value = self.convert(x, typ, common)
                right_value = y if operation in ("<<", ">>") else self.convert(y, self.node_type(n.rvalue), common)
                y = self.division(left_value, right_value, operation, common)
                return a + b, f"({x} = {self.normalize(y, typ)})"
            return a + b, f"({x} {n.op} {self.normalize(y, typ)})"
        if isinstance(n, A.TernaryOp):
            a, cond = self.expr(n.cond)
            b, x = self.expr(n.iftrue)
            c, y = self.expr(n.iffalse)
            temp = self.temp(n)
            typ = self.types[temp]
            if self.aggregate(typ):
                true_store = f"MemCpy(&{temp}, &({x}), sizeof({temp}));"
                false_store = f"MemCpy(&{temp}, &({y}), sizeof({temp}));"
            else:
                true_store = f"{temp} = {self.normalize(x, typ)};"
                false_store = f"{temp} = {self.normalize(y, typ)};"
            return a + [f"if ({cond}) {{"] + indent(b + [true_store]) + ["} else {"] + indent(c + [false_store]) + ["}"], temp
        if isinstance(n, A.ExprList):
            a = []
            for i, part in enumerate(n.exprs):
                b, x = self.expr(part)
                a += b
                if i + 1 < len(n.exprs):
                    a.append(x + ";")
            return a, x
        if isinstance(n, A.InitList):
            args = []
            for part in n.exprs:
                a, x = self.expr(part)
                if a:
                    raise ValueError("Nonconstant brace initializer")
                args.append(x)
            return [], "{" + ", ".join(args) + "}"
        if isinstance(n, A.CompoundLiteral):
            name = self.m.unique("literal")
            self.local_decls.append(self.local_decl(n.type.type, name) + ";")
            return self.initialize(name, n.type.type, n.init), name
        if isinstance(n, A.Typename):
            return [], self.type_text(n)
        raise NotImplementedError(f"expression {type(n).__name__} at {n.coord}")

    def const_value(self, node):
        if isinstance(node, A.Constant):
            if node.type == "char":
                return ord(python_ast.literal_eval(node.value))
            value = re.sub(r"[uUlL]+$", "", node.value)
            return int(value, 0) if not (len(value) > 1 and value[0] == "0" and value[1].isdigit()) else int(value, 8)
        if isinstance(node, A.ID):
            return self.const_value(self.m.enums[node.name])
        if isinstance(node, A.Cast):
            value = self.const_value(node.expr)
            info = self.integer_info(node.to_type.type)
            if info:
                width, signed = info
                value &= (1 << width) - 1
                if signed and value >= (1 << (width - 1)):
                    value -= 1 << width
            return value
        if isinstance(node, A.UnaryOp):
            value = self.const_value(node.expr)
            return {"-": lambda: -value, "+": lambda: value, "~": lambda: ~value, "!": lambda: not value}[node.op]()
        if isinstance(node, A.BinaryOp):
            left, right = self.const_value(node.left), self.const_value(node.right)
            return {"+": lambda: left + right, "-": lambda: left - right, "*": lambda: left * right,
                    "/": lambda: (abs(left) // abs(right)) * (-1 if (left < 0) != (right < 0) else 1),
                    "%": lambda: left - (abs(left) // abs(right)) * (-1 if (left < 0) != (right < 0) else 1) * right,
                    "<<": lambda: left << right,
                    ">>": lambda: left >> right, "&": lambda: left & right, "|": lambda: left | right,
                    "^": lambda: left ^ right}[node.op]()
        raise ValueError("Nonconstant array dimension " + str(node))

    def aggregate(self, typ):
        return isinstance(typ, A.ArrayDecl) or isinstance(typ, A.TypeDecl) and isinstance(typedef_name(typ), (A.Struct, A.Union)) or isinstance(typ, (A.Struct, A.Union))

    def initializer_fields(self, name, typ):
        if isinstance(typ, A.ArrayDecl):
            return [(name + f"[{i}]", typ.type) for i in range(self.const_value(typ.dim))]
        cls = self.m.classes[typedef_name(typ).name]
        fields = cls.decls[:1] if isinstance(cls, A.Union) else cls.decls
        return [(name + ("." + field.name if field.name else ""), field.type) for field in fields]

    def consume_initializer(self, name, typ, values):
        if not values:
            return []
        first = values[0]
        if isinstance(typ, A.ArrayDecl) and isinstance(first, A.Constant) and first.type == "string":
            values.pop(0)
            _, text = self.expr(first)
            length = min(self.const_value(typ.dim), len(python_ast.literal_eval(first.value).encode("latin-1")) + 1)
            return [f"MemCpy({name}, {text}, {length});"]
        if self.aggregate(typ):
            nested = list(values.pop(0).exprs) if isinstance(first, A.InitList) else values
            result = []
            for field_name, field_type in self.initializer_fields(name, typ):
                result += self.consume_initializer(field_name, field_type, nested)
            return result
        value = values.pop(0)
        while isinstance(value, A.InitList):
            value = value.exprs[0]
        if isinstance(value, A.Constant) and value.type != "string" and value.value.rstrip("ULul") == "0":
            return []
        a, expr = self.expr(value)
        return a + [f"{name} = {self.normalize(expr, typ)};"]

    def initialize(self, name, typ, init):
        if isinstance(init, A.InitList):
            return [f"MemSet(&{name}, 0, sizeof({name}));"] + self.consume_initializer(name, typ, [init])
        a, x = self.expr(init)
        base = typedef_name(typ)
        if isinstance(typ, A.TypeDecl) and isinstance(base, (A.Struct, A.Union)):
            return a + [f"MemCpy(&{name}, &({x}), sizeof({base.name}));"]
        if isinstance(typ, A.ArrayDecl) and isinstance(init, A.Constant) and init.type == "string":
            return [f"MemSet(&{name}, 0, sizeof({name}));"] + self.consume_initializer(name, typ, [init])
        return a + [f"{name} = {self.normalize(x, typ)};"]

    def global_storage(self, decl, name):
        declaration = self.decl(decl.type, name)
        if self.aggregate(decl.type):
            lines = [declaration + ";", f"MemSet(&{name}, 0, sizeof({name}));"]
        else:
            lines = [declaration + " = 0;"]
        if decl.init:
            active = self.in_function
            self.in_function = False
            self.m.data_initializers += self.initialize(name, decl.type, decl.init)
            self.in_function = active
        return lines

    def stmt(self, n):
        if n is None:
            return []
        if isinstance(n, A.Compound):
            self.scopes.append({})
            result = []
            for child in n.block_items or []:
                result += self.stmt(child)
            self.scopes.pop()
            return result
        if isinstance(n, A.Decl):
            if not n.name:
                return []
            name = self.m.unique(n.name)
            self.scopes[-1][n.name] = name
            self.types[name] = n.type
            if "static" in n.storage:
                self.extra_globals += self.global_storage(n, name)
                return []
            self.local_decls.append(self.local_decl(n.type, name) + ";")
            return self.initialize(name, n.type, n.init) if n.init else []
        if isinstance(n, A.DeclList):
            return sum((self.stmt(d) for d in n.decls), [])
        if isinstance(n, A.Return):
            a, x = self.expr(n.expr)
            result = self.class_type_from_type(self.return_type)
            if result:
                return a + [f"MemCpy(temple_return_value, &({x}), sizeof({result}));", "return;"]
            if x:
                x = self.normalize(x, self.return_type)
            if self.current_function in ("file_load_shape2d", "file_load_shape2d_res") and x:
                hook = "TempleRasterResourceParsed" if self.current_function.endswith("_res") else "TempleRasterResource"
                self.m.external_used.add(hook)
                x = hook + "(" + x + ")"
            if self.current_function in RASTER_SOURCE_FUNCTIONS:
                self.m.external_used.add("TempleRasterSource")
                a += ["TempleRasterSource(0);"]
            return a + ["return" + (" " + x if x else "") + ";"]
        if isinstance(n, A.If):
            a, x = self.expr(n.cond)
            result = a + [f"if ({x}) {{"] + indent(self.stmt(n.iftrue)) + ["}"]
            if n.iffalse:
                result[-1] += " else {"
                result += indent(self.stmt(n.iffalse)) + ["}"]
            return result
        if isinstance(n, (A.For, A.While, A.DoWhile)):
            self.scopes.append({})
            label = self.m.unique("next")
            self.loop_labels.append(label)
            init = self.stmt(n.init) if isinstance(n, A.For) else []
            a, cond = self.expr(n.cond) if n.cond else ([], "1")
            body = self.stmt(n.stmt)
            if any("goto " + label + ";" in line for line in body):
                body += [label + ":;"]
            if isinstance(n, A.For) and n.next:
                b, x = self.expr(n.next)
                body += b + [x + ";"]
            if isinstance(n, A.DoWhile):
                body += a + [f"if (!({cond})) {{", "\tbreak;", "}"]
            else:
                body = a + [f"if (!({cond})) {{", "\tbreak;", "}"] + body
            self.loop_labels.pop()
            self.scopes.pop()
            return init + ["while (1) {"] + indent(body) + ["}"]
        if isinstance(n, A.Continue):
            return ["goto " + self.loop_labels[-1] + ";"]
        if isinstance(n, A.Break):
            return ["break;"]
        if isinstance(n, A.Goto):
            return ["goto " + n.name + ";"]
        if isinstance(n, A.Label):
            return [n.name + ":;"] + self.stmt(n.stmt)
        if isinstance(n, A.Switch):
            a, x = self.expr(n.cond)
            return a + [f"switch ({x}) {{"] + indent(self.stmt(n.stmt)) + ["}"]
        if isinstance(n, (A.Case, A.Default)):
            # Case labels require integral constants. Keep runtime narrowing
            # helpers out of the compiler's constant-expression evaluator.
            result = ["case " + str(self.const_value(n.expr)) + ":" if isinstance(n, A.Case) else "default:"]
            for child in n.stmts or []:
                result += indent(self.stmt(child))
            return result
        if isinstance(n, (A.EmptyStatement, A.Pragma, A.Typedef)):
            return []
        a, x = self.expr(n)
        return a + ([x + ";"] if x else [])

    def function(self, node, name):
        self.in_function = True
        self.current_function = node.decl.name
        self.scopes = [{}]
        typ = node.decl.type
        self.return_type = typ.type
        argument_copies = []
        for arg in typ.args.params if typ.args else []:
            if isinstance(arg, A.Decl) and arg.name:
                self.scopes[0][arg.name] = arg.name
                self.types[arg.name] = arg.type
                result = self.class_type_from_type(arg.type)
                if result:
                    self.local_decls.append(self.decl(arg.type, arg.name) + ";")
                    argument_copies.append(f"MemCpy(&{arg.name}, temple_value_{arg.name}, sizeof({result}));")
                if isinstance(arg.type, A.ArrayDecl):
                    self.param_arrays[arg.name] = arg.type
        if self.current_function == "shape2d_write_raster":
            # The native raster handles both bitmap layouts. Do not translate
            # the replaced C body: its temporaries have no remaining users.
            body = ["planar;", "video_pages_raster_span(bitmap, destination, &value, 0, 1, operation, 0);"]
        else:
            body = argument_copies + self.stmt(node.body)
        if self.current_function == "mmgr_release":
            self.m.external_used.add("TempleRasterForget")
            body = ["TempleRasterForget(ptr);"] + body
        if self.current_function == "sprite_restore_context":
            self.m.external_used.add("TempleRasterSelect")
            body += ["TempleRasterSelect(&drawing_sprite);"]
        if self.current_function in RASTER_SOURCE_FUNCTIONS:
            self.m.external_used.add("TempleRasterSource")
            self.m.external_used.add("TempleRasterSourceAt")
            body = ["TempleRasterSourceAt(shape, y);"] + body + ["TempleRasterSource(0);"]
        if any(isinstance(arg, A.EllipsisParam) for arg in (typ.args.params if typ.args else [])):
            # HolyC adds argc/argv to every variadic function. C va_list may
            # consume argv, but does not require either implicit ABI parameter.
            references = {token[0] for token in STYLE.TOKEN.finditer("\n".join(self.local_decls + body))
                          if token.lastgroup == "word"}
            unused = [argument for argument in ("argc", "argv") if argument not in references]
            if unused:
                body.insert(0, "no_warn " + ", ".join(unused) + ";")
        return self.extra_globals + [self.prototype(typ, name), "{"] + indent(self.local_decls + body) + ["}", ""]


def indent(lines):
    return ["\t" + line if line else line for line in lines]


def output_text(lines, name, name_map=None, formatter=None):
    data = "\n".join(lines).rstrip() + "\n"
    if name_map:
        data = STYLE.rename_identifiers(data, name_map)
    if name.endswith((".HC", ".HH")):
        data = STYLE.format_source(data, formatter, GEN / name)
    return data


def write(name, lines, name_map=None):
    data = output_text(lines, name, name_map, FORMATTER).replace("\r\n", "\n")
    if name.endswith((".HC", ".HH", ".TXT")):
        data = data.replace("\n", "\r\n")
    encoded = data.encode()
    destination = GEN / name
    if CHECK:
        if not destination.exists() or destination.read_bytes() != encoded:
            MISMATCHES.append(name)
    else:
        destination.write_bytes(encoded)


def referenced_defines(definitions, sources):
    """Keep constants used by emitted/native code and their dependencies."""
    def identifiers(source):
        return {token[0] for token in STYLE.TOKEN.finditer(source)
                if token.lastgroup == "word"}

    pending = set().union(*(identifiers(source) for source in sources)) & definitions.keys()
    used = set()
    while pending:
        name = pending.pop()
        if name not in used:
            used.add(name)
            pending.update((identifiers(definitions[name]) & definitions.keys()) - used)
    return used


def required_forwards(names, source):
    """Find symbols referenced before a declaration in HolyC source order.

    Generated candidates are omitted from source during this pass. Existing
    native externs still introduce their symbols, so they do not acquire a
    second generated header. Declarator names, members and local shadows are
    not global references. A class/function introduces its own name before
    its body, which also avoids needless forwards for self-reference.
    """
    source = re.sub(r"^[ \t]*#[^\n]*", "", source.replace("\\\n", ""), flags=re.M)
    tokens = [match[0] for match in STYLE.TOKEN.finditer(source)
              if match.lastgroup not in ("comment", "string")]
    pairs = {}
    stack = []
    for index, token in enumerate(tokens):
        if token in ("(", "[", "{"):
            stack.append(index)
        elif token in (")", "]", "}"):
            if not stack or tokens[stack[-1]] != {")": "(", "]": "[", "}": "{"}[token]:
                raise ValueError("Unbalanced HolyC source while finding forward declarations")
            first = stack.pop()
            pairs[first] = index
    if stack:
        raise ValueError("Unbalanced HolyC source while finding forward declarations")

    declared = {}
    functions = []
    class_bodies = []
    depth = 0
    depths = []
    for index, token in enumerate(tokens):
        depths.append(depth)
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
        if token in ("class", "union") and index + 2 < len(tokens):
            name, following = tokens[index + 1:index + 3]
            if following in ("{", ";"):
                declared[index + 1] = "global"
                if following == "{":
                    class_bodies.append((index + 2, pairs[index + 2]))

    def is_type(token):
        return token.startswith("legacy_") or bool(re.fullmatch(r"C[A-Z][A-Za-z_0-9]*", token))

    # First identify function headers, including multiline native signatures.
    for index, token in enumerate(tokens):
        if depths[index] or not is_type(token):
            continue
        name_index = index + 1
        while name_index < len(tokens) and tokens[name_index] in ("*", "noreg", "reg"):
            name_index += 1
        opening = name_index + 1
        if opening >= len(tokens) or tokens[opening] != "(" or opening not in pairs:
            continue
        end = pairs[opening]
        if end + 1 < len(tokens) and tokens[end + 1] in ("{", ";"):
            declared[name_index] = "global"
            functions.append((opening, pairs[end + 1] if tokens[end + 1] == "{" else end))

    scope_at = [None] * len(tokens)
    for first, last in functions:
        for index in range(first, last + 1):
            scope_at[index] = first

    # Parse only declarator boundaries. Initializer expressions remain in the
    # token stream, including function addresses and references to later data.
    for index, token in enumerate(tokens):
        if not is_type(token):
            continue
        cursor = index + 1
        while cursor < len(tokens):
            parenthesized = tokens[cursor] == "(" and cursor + 1 < len(tokens) and tokens[cursor + 1] == "*"
            if parenthesized:
                cursor += 1
            while cursor < len(tokens) and tokens[cursor] in ("*", "noreg", "reg"):
                cursor += 1
            if cursor >= len(tokens) or not re.fullmatch(r"[A-Za-z_$][A-Za-z_0-9$]*", tokens[cursor]):
                break
            if cursor in declared:
                break
            following = cursor + 1
            if parenthesized:
                if following >= len(tokens) or tokens[following] != ")":
                    break
                following += 1
                if following >= len(tokens) or tokens[following] != "(":
                    break
                following = pairs[following] + 1
            if following >= len(tokens) or tokens[following] not in ("[", "=", ",", ";", ")"):
                break
            if scope_at[cursor] is not None:
                declared[cursor] = "local"
            elif any(first < cursor < last for first, last in class_bodies):
                declared[cursor] = "member"
            elif not depths[cursor]:
                declared[cursor] = "global"
            while following < len(tokens) and tokens[following] not in (",", ";", ")"):
                if tokens[following] in ("(", "[", "{"):
                    following = pairs[following]
                following += 1
            if following >= len(tokens) or tokens[following] != ",":
                break
            cursor = following + 1
            if cursor < len(tokens) and is_type(tokens[cursor]):
                break

    available = set()
    needed = set()
    local_names = {}
    for index, token in enumerate(tokens):
        scope = scope_at[index]
        role = declared.get(index)
        if role == "global":
            available.add(token)
        elif role == "local":
            # HolyC locals remain in the function scope after a nested block.
            local_names.setdefault(scope, set()).add(token)
        elif (token in names and role != "member" and token not in available and
              token not in local_names.get(scope, ()) and
              (not index or tokens[index - 1] not in (".", "->"))):
            needed.add(token)
    return needed


def compilation_source(entry, generated):
    """Expand the port's include order using the current generated buffers."""
    visited = set()

    def expand(path):
        path = path.resolve()
        if path in visited:
            return ""
        visited.add(path)
        source = generated[path] if path in generated else path.read_text()

        def include(match):
            name = match[1]
            child = HERE / name
            if child.resolve() not in generated and not child.exists():
                child = path.parent / name
            return expand(child)

        return re.sub(r'^[ \t]*#include[ \t]+"([^"]+)"[^\n]*', include, source, flags=re.M)

    return expand(entry)


def translate(cpp):
    model = Model()
    with tempfile.TemporaryDirectory(prefix="restunts-holyc-") as tmp:
        tmp = Path(tmp)
        sources = prepare_sources(tmp)
        headers = tmp / "include"
        headers.mkdir()
        for name, text in FAKE_HEADERS.items():
            (headers / name).write_text(text)
        for module in source_names() + ["memory"]:
            if module == "dosstart":
                continue
            source = sources / (module + ".c") if module != "memory" else tmp / "src/restunts/platform/sdl3/memory.c"
            command = [cpp, "-E", "-nostdinc", "-I" + str(headers), "-I" + str(sources),
                       "-DRESTUNTS_FULL", "-DRESTUNTS_TEMPLEOS", "-DRESTUNTS_GIT_HASH=\"TempleOS\"",
                       '-D__DATE__="Oct 03 2026"', str(source)]
            proc = subprocess.run(command, text=True, capture_output=True)
            if proc.returncode:
                raise RuntimeError(proc.stderr)
            ast = c_parser.CParser().parse(proc.stdout, filename=str(source))
            model.collect(ast, module)
            print("Parsed", module, flush=True)
    GEN.mkdir(parents=True, exist_ok=True)
    name_map = build_name_map(model)
    header = ["// Generated by tools/TRANSLATE.PY. Edit the C source or translator.", ""]
    g = Generator(model)
    generated_sources = []
    generated_files = {}

    def emit(name, lines, names=None):
        generated_sources.append("\n".join(lines))
        generated_files[(GEN / name).resolve()] = STYLE.rename_identifiers("\n".join(lines), names or {})
        write(name, lines, names)

    types = list(header)
    enum_definitions = {}
    for name, value in model.enums.items():
        a, text = g.expr(value)
        if a:
            raise ValueError("Nonconstant enum " + name)
        enum_definitions[name] = text
    emitted = set()
    def emit_class(name):
        if name in emitted:
            return
        cls = model.classes[name]
        for field in cls.decls:
            typ = field.type
            while isinstance(typ, (A.ArrayDecl, A.TypeDecl)):
                typ = typ.type
            if isinstance(typ, (A.Struct, A.Union)) and typ.name in model.classes:
                emit_class(typ.name)
        emitted.add(name)
        types.extend([f"{'union' if isinstance(cls, A.Union) else 'class'} {name}", "{"])
        types.extend(indent(g.class_fields(cls)) + ["};", ""])
    for name in model.classes:
        emit_class(name)
    class_names = {name_map.get(name, name) for name in model.classes}
    class_forwards = required_forwards(class_names, STYLE.rename_identifiers("\n".join(types), name_map))
    types[len(header):len(header)] = [f"extern class {name};" for name in model.classes
                                     if name_map.get(name, name) in class_forwards]
    generated_sources.append("\n".join(types))
    generated_files[(GEN / "TYPES.HH").resolve()] = STYLE.rename_identifiers("\n".join(types), name_map)
    casts = list(header)
    cast_prototypes = []
    for width in (8, 16, 32):
        mask, sign = (1 << width) - 1, 1 << (width - 1)
        for signed in (False, True):
            name = f"TempleCast{'S' if signed else 'U'}{width}"
            signature = f"legacy_s64 {name}(legacy_u64 value)"
            cast_prototypes.append("extern " + signature + ";")
            casts += [signature, "{", f"\tvalue &= {mask};"]
            if signed:
                casts += [f"\tif (value & {sign}) {{", f"\t\treturn value - {mask + 1};", "\t}"]
            casts += ["\treturn value;", "}", ""]
    for signed in (False, True):
        typ = "legacy_s64" if signed else "legacy_u64"
        signature = f"{typ} TempleCast{'S' if signed else 'U'}64({typ} value)"
        cast_prototypes.append("extern " + signature + ";")
        # A HolyC postfix cast can change the width of an lvalue memory load.
        # Passing through a typed function widens its value instead of reading
        # adjacent bytes from a narrow global or packed class member.
        casts += [signature, "{", "\treturn value;", "}", ""]
        for name, operator in (("TempleDivide", "/"), ("TempleRemainder", "%")):
            signature = f"{typ} {name}{'S' if signed else 'U'}64({typ} dividend, {typ} divisor)"
            cast_prototypes.append("extern " + signature + ";")
            casts += [signature, "{", f"\treturn dividend {operator} divisor;", "}", ""]
    emit("CASTS.HC", casts, name_map)
    declarations = {re.search(r"\b(Temple\w+)\(", declaration)[1]: declaration
                    for declaration in cast_prototypes}
    for name, (decl, rename, definition) in model.functions.items():
        if definition or "/include/" not in str(decl.coord.file):
            declarations[name] = "extern " + Generator(model, rename).prototype(decl.type, name) + ";"
    for name, (decl, rename) in model.global_decls.items():
        declarations[name] = "extern " + Generator(model, rename).decl(decl.type, name) + ";"
    generated_files[(GEN / "DECLS.HH").resolve()] = ""
    data = list(header)
    for name, (decl, rename) in model.global_defs.items():
        data += Generator(model, rename).global_storage(decl, name)
    emit("DATA.HC", data, name_map)
    includes = list(header) + ['#include "generated/CASTS.HC"']
    for module, ast, rename in model.units:
        lines = list(header)
        for item in ast.ext:
            if isinstance(item, A.FuncDef):
                if native_function(item.decl.name):
                    continue
                name = rename.get(item.decl.name, item.decl.name)
                lines += Generator(model, rename).function(item, name)
        emit(module.upper() + ".HC", lines, name_map)
        includes.append(f'#include "generated/{module.upper()}.HC"')
    includes.append('#include "generated/DATA_INIT.HC"')
    emit("DATA_INIT.HC", header + model.data_initializers, name_map)
    emit("ALL.HC", includes)
    declaration_names = {name_map.get(name, name) for name in declarations}
    needed = required_forwards(declaration_names, compilation_source(HERE / "LOAD.HC", generated_files))
    emit("DECLS.HH", header + [declaration for name, declaration in declarations.items()
                               if name_map.get(name, name) in needed], name_map)
    external = []
    for name in sorted(model.external_used):
        item = model.functions.get(name)
        external.append(Generator(model).prototype(item[0].type, LIBC.get(name, name)) + ";" if item else "// Native hook: " + name)
    emit("EXTERNALS.TXT", header + external, name_map)
    emit("NAMES.JSON", [json.dumps({name: styled for name, styled in sorted(name_map.items())
                                     if name != styled}, indent=2)])
    native_sources = [path.read_text() for path in HERE.rglob("*")
                      if path.suffix in (".HC", ".HH") and GEN not in path.parents]
    used_enums = referenced_defines(enum_definitions, generated_sources + native_sources)
    enum_lines = [f"#define {name} {value}" for name, value in enum_definitions.items()
                  if name in used_enums]
    write("TYPES.HH", header + enum_lines + [""] + types[len(header):], name_map)
    native_source = "\n".join(path.read_text() for path in HERE.glob("*.HC")
                              if not path.name.startswith("TEST_"))
    missing = [name for name in sorted(model.external_used)
               if not re.search(r"\b" + re.escape(name_map.get(name, LIBC.get(name, name))) +
                                r"\s*\([^;{}]*\)\s*\{", native_source)]
    if missing:
        raise ValueError("Missing native function implementations: " + ", ".join(missing))
    manifest = {"modules": [x[0] for x in model.units], "functions": sum(1 for x in model.functions.values() if x[2]), "classes": len(model.classes), "external": sorted(name_map.get(name, name) for name in model.external_used)}
    emit("MANIFEST.JSON", [json.dumps(manifest, indent=2)])
    print(json.dumps({k:v for k,v in manifest.items() if k != "external"}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    global FORMATTER, CHECK
    parser.add_argument("--formatter", help="Path to clang-format 18.1.8")
    parser.add_argument("--check", action="store_true", help="Verify generated output without writing")
    parser.add_argument("--cpp", default="gcc", help="C preprocessor executable")
    args = parser.parse_args()
    FORMATTER, CHECK = args.formatter, args.check
    translate(args.cpp)
    if MISMATCHES:
        raise SystemExit("Generated output differs: " + ", ".join(MISMATCHES))


if __name__ == "__main__":
    main()
