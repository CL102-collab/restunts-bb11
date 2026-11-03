#!/usr/bin/env python3
"""Convert TASM syntax to deterministic WASM inputs without changing the sources.

The adapter preserves 8086 instruction encodings where TASM's accumulator
selection or SMART state differs from WASM. Unsupported new syntax is rejected
instead of silently losing type/layout information. The generated inputs are
build artifacts; the source directory remains authoritative.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path


class AdapterError(ValueError):
    pass


@dataclass
class Layout:
    size: int
    members: dict[str, tuple[int, str]] = field(default_factory=dict)
    union: bool = False


PRIMITIVES = {"byte": 1, "word": 2, "dword": 4, "qword": 8, "tbyte": 10}
DATA_TYPES = {"db": "byte", "dw": "word", "dd": "dword", "dq": "qword", "dt": "tbyte"}
WORD_REGISTERS = {name: index for index, name in enumerate(("ax", "cx", "dx", "bx", "sp", "bp", "si", "di"))}
BYTE_REGISTERS = {name: index for index, name in enumerate(("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"))}
ACCUMULATOR_OPCODES = {"add": 5, "or": 13, "adc": 21, "sbb": 29, "and": 37, "sub": 45, "xor": 53, "cmp": 61, "test": 169}
LOGICAL_DIGITS = {"or": 1, "and": 4, "xor": 6}
REPEAT_PREFIXES = {"repne": 0xF2, "repnz": 0xF2, "repe": 0xF3, "repz": 0xF3}
FAR_TRANSFER_OPCODES = {"call": 0x9A, "jmp": 0xEA}
REGISTER_BYTE_OPCODES = {"sbb": 0x1A, "xchg": 0x86}
REGISTER_WORD_OPCODES = {"sbb": 0x1B, "xchg": 0x87}
REGISTER_MODE_BITS = 0xC0
MODRM_REGISTER_STRIDE = 8
WORD_IMMEDIATE_GROUP_OPCODE = 0x81
IDENTIFIER = r"[A-Za-z_@$?][A-Za-z_0-9@$?]*"
QUALIFIED_NAME = re.compile(r"\b" + IDENTIFIER + r"(?:\." + IDENTIFIER + r")*")
ASSIGNMENT = re.compile(r"^(\s*)(" + IDENTIFIER + r")\s*=\s*(" + IDENTIFIER + r")\s+ptr\s+(.+)$", re.I)


def split_comment(line: str) -> tuple[str, str]:
    quote = None
    index = 0
    while index < len(line):
        char = line[index]
        if quote:
            if char == quote:
                if index + 1 < len(line) and line[index + 1] == quote:
                    index += 1
                else:
                    quote = None
        elif char in "\"'":
            quote = char
        elif char == ";":
            return line[:index], line[index:]
        index += 1
    return line, ""


def lines_of(data: bytes) -> list[str]:
    # splitlines() also splits original DOS comment bytes such as 85h (NEL).
    text = data.decode("latin1").replace("\r\n", "\n")
    if "\r" in text:
        raise AdapterError("Bare CR is not a supported source line ending")
    return text.removesuffix("\n").split("\n")


def numeric_value(expression: str, constants: dict[str, int]) -> int | None:
    def number(match: re.Match[str]) -> str:
        token = match[0]
        suffix = token[-1].lower()
        if suffix in "hboqd":
            base = {"h": 16, "b": 2, "o": 8, "q": 8, "d": 10}[suffix]
            return str(int(token[:-1], base))
        return token

    try:
        expression = re.sub(r"\b[0-9][0-9A-Fa-f]*[HhBbOoQqDd]?\b", number, expression)
        tree = ast.parse(expression, mode="eval")

        def evaluate(node: ast.AST) -> int:
            if isinstance(node, ast.Constant) and isinstance(node.value, int):
                return node.value
            if isinstance(node, ast.Name) and node.id.lower() in constants:
                return constants[node.id.lower()]
            if isinstance(node, ast.UnaryOp):
                value = evaluate(node.operand)
                if isinstance(node.op, ast.USub):
                    return -value
                if isinstance(node.op, ast.UAdd):
                    return value
                if isinstance(node.op, ast.Invert):
                    return ~value
            if isinstance(node, ast.BinOp):
                left, right = evaluate(node.left), evaluate(node.right)
                operations = {ast.Add: lambda: left + right, ast.Sub: lambda: left - right,
                              ast.Mult: lambda: left * right, ast.LShift: lambda: left << right,
                              ast.RShift: lambda: left >> right, ast.BitAnd: lambda: left & right,
                              ast.BitOr: lambda: left | right, ast.BitXor: lambda: left ^ right}
                if type(node.op) in operations:
                    return operations[type(node.op)]()
            raise ValueError("Not a constant integer expression")

        return evaluate(tree.body)
    except (ValueError, SyntaxError, KeyError):
        return None


def collect_layouts(sources: dict[str, bytes]) -> dict[str, Layout]:
    layouts = {name: Layout(size) for name, size in PRIMITIVES.items()}
    for filename, data in sources.items():
        current = None
        for line_number, line in enumerate(lines_of(data), 1):
            code = split_comment(line)[0].strip()
            start = re.fullmatch(r"(" + IDENTIFIER + r")\s+(struc|union)", code, re.I)
            if start:
                if current:
                    raise AdapterError(f"{filename}:{line_number}: nested structure definition")
                current = start[1].lower()
                if current in layouts:
                    raise AdapterError(f"{filename}:{line_number}: duplicate type {current}")
                layouts[current] = Layout(0, union=start[2].lower() == "union")
                continue
            if not current:
                continue
            end = re.fullmatch(r"(" + IDENTIFIER + r")\s+ends", code, re.I)
            if end:
                if end[1].lower() != current:
                    raise AdapterError(f"{filename}:{line_number}: mismatched structure end")
                current = None
                continue
            if not code:
                continue
            member = re.fullmatch(r"(" + IDENTIFIER + r")\s+(" + IDENTIFIER + r")\s+(.+)", code)
            if not member:
                raise AdapterError(f"{filename}:{line_number}: unsupported structure member")
            kind = DATA_TYPES.get(member[2].lower(), member[2].lower())
            if kind not in layouts:
                raise AdapterError(f"{filename}:{line_number}: unknown structure member type {kind}")
            repeated = re.fullmatch(r"(\d+)\s+dup\s*\(\s*(?:\?|<>)\s*\)", member[3], re.I)
            if repeated:
                count = int(repeated[1])
            elif member[3].strip() in ("?", "<>"):
                count = 1
            else:
                raise AdapterError(f"{filename}:{line_number}: unsupported structure initializer")
            layout = layouts[current]
            offset = 0 if layout.union else layout.size
            layout.members[member[1].lower()] = (offset, kind)
            member_size = layouts[kind].size * count
            layout.size = max(layout.size, member_size) if layout.union else layout.size + member_size
        if current:
            raise AdapterError(f"{filename}: unterminated structure {current}")
    return layouts


def lower_source(filename: str, data: bytes, layouts: dict[str, Layout]) -> tuple[bytes, dict[str, int]]:
    aliases: dict[str, str] = {}
    constants: dict[str, int] = {}
    counts: dict[str, int] = {}
    output: list[str] = []
    union_name = None
    smart = False
    local_far_procedures = {
        match[1].lower()
        for source_line in lines_of(data)
        if (match := re.match(r"\s*(" + IDENTIFIER + r")\s+proc\s+far\b", split_comment(source_line)[0], re.I))
    }

    local_labels = {
        match[1].lower()
        for source_line in lines_of(data)
        if (match := re.fullmatch(r"\s*(" + IDENTIFIER + r"):\s*", split_comment(source_line)[0]))
    }

    def changed(kind: str) -> None:
        counts[kind] = counts.get(kind, 0) + 1

    for line_number, line in enumerate(lines_of(data), 1):
        code, comment = split_comment(line)
        stripped = code.strip()
        union = re.fullmatch(r"(" + IDENTIFIER + r")\s+union", stripped, re.I)
        if union:
            union_name = union[1]
            size = layouts[union_name.lower()].size
            output.extend([f"{union_name} struc", f"__wasm_storage_{union_name} db {size} dup (?)"])
            changed("union_layout")
            continue
        if union_name:
            if re.fullmatch(re.escape(union_name) + r"\s+ends", stripped, re.I):
                output.append(f"{union_name} ends")
                union_name = None
            continue
        if stripped.lower() in ("smart", "nosmart"):
            smart = stripped.lower() == "smart"
            output.append("; WASM adapter: " + stripped + (" " + comment if comment else ""))
            changed("smart_directive")
            continue
        if re.match(r"\s*" + IDENTIFIER + r"\s+proc\b", code, re.I):
            aliases.clear()
            constants.clear()
        assignment = ASSIGNMENT.fullmatch(code)
        if assignment:
            name, kind, expression = assignment[2], assignment[3].lower(), assignment[4]
            if kind not in layouts:
                raise AdapterError(f"{filename}:{line_number}: unknown stack type {kind}")
            aliases[name.lower()] = kind
            value = numeric_value(expression, constants)
            if value is None:
                raise AdapterError(f"{filename}:{line_number}: stack displacement is not constant")
            constants[name.lower()] = value
            code = f"{assignment[1]}{name} = {expression}"
            changed("typed_stack_alias")
        else:
            def memory_operand(match: re.Match[str]) -> str:
                widths = []

                def member(token: re.Match[str]) -> str:
                    parts = token[0].split(".")
                    kind = aliases.get(parts[0].lower())
                    type_constant = kind is None and len(parts) > 1 and parts[0].lower() in layouts
                    if type_constant:
                        kind = parts[0].lower()
                    if kind is None:
                        return token[0]
                    offset = 0
                    for part in parts[1:]:
                        try:
                            addition, kind = layouts[kind].members[part.lower()]
                        except KeyError as exc:
                            raise AdapterError(f"{filename}:{line_number}: unknown stack member {token[0]}") from exc
                        offset += addition
                    if kind in PRIMITIVES:
                        widths.append(kind)
                    return str(offset) if type_constant else parts[0] + (f"+{offset}" if offset else "")

                expression = QUALIFIED_NAME.sub(member, match[0])
                if widths and not re.search(r"\bptr\s*$", code[:match.start()], re.I):
                    expression = widths[0] + " ptr " + expression
                if expression != match[0]:
                    changed("stack_operand")
                return expression

            code = re.sub(r"\[[^\]]+\]", memory_operand, code)
        instruction = re.fullmatch(r"(\s*)([A-Za-z]+)\s+(.+)", code)
        if instruction:
            indent, opcode, operands = instruction[1], instruction[2].lower(), instruction[3]
            repeat = re.fullmatch(r"(movs[bw]?|stos[bw]?|lods[bw]?)(.*)", operands, re.I)
            if opcode in ("repne", "repnz", "repe", "repz") and repeat:
                prefix = REPEAT_PREFIXES[opcode]
                code = f"{indent}db {prefix}\n{indent}{repeat[1]}{repeat[2]}"
                changed("string_prefix")
            elif opcode == "jmp" and not smart and operands.strip().lower() in local_labels:
                # The preserved disassembly marks short branches explicitly. A
                # conservative near JMP also avoids WASM's forward-branch phase
                # instability on 8086. This is not a universal TASM relaxation rule.
                code = f"{indent}jmp near ptr {operands.strip()}"
                changed("local_near_jump")
            elif opcode in ("call", "jmp") and (operands.strip().lower() in local_far_procedures or re.fullmatch(r"far\s+ptr\s+" + IDENTIFIER, operands.strip(), re.I)):
                target = re.sub(r"^far\s+ptr\s+", "", operands.strip(), flags=re.I)
                far_opcode = FAR_TRANSFER_OPCODES[opcode]
                code = f"{indent}db {far_opcode}\n{indent}dw offset {target}, seg {target}"
                changed("local_far_transfer")
            elif opcode in ("sbb", "xchg") and "," in operands:
                left, right = (part.strip().lower() for part in operands.split(",", 1))
                registers = WORD_REGISTERS if left in WORD_REGISTERS else BYTE_REGISTERS
                if left in registers and right in registers and not (opcode == "xchg" and "ax" in (left, right)):
                    byte_opcode = (REGISTER_WORD_OPCODES if registers is WORD_REGISTERS else REGISTER_BYTE_OPCODES)[opcode]
                    modrm = REGISTER_MODE_BITS + registers[left] * MODRM_REGISTER_STRIDE + registers[right]
                    code = f"{indent}db {byte_opcode}, {modrm}"
                    changed("register_direction")
            if opcode in ACCUMULATOR_OPCODES and "," in operands and "\n" not in code:
                left, right = (part.strip() for part in operands.split(",", 1))
                def type_member_constant(match: re.Match[str]) -> str:
                    parts = match[0].split(".")
                    if len(parts) < 2 or parts[0].lower() not in layouts:
                        return match[0]
                    kind, offset = parts[0].lower(), 0
                    for part in parts[1:]:
                        addition, kind = layouts[kind].members[part.lower()]
                        offset += addition
                    return str(offset)

                size_expression = QUALIFIED_NAME.sub(type_member_constant, right)
                size_expression = re.sub(r"\bsize\s+(" + IDENTIFIER + r")", lambda match: str(layouts[match[1].lower()].size), size_expression, flags=re.I)
                immediate = numeric_value(size_expression, constants) is not None or bool(re.match(r"(?:offset|seg)\s+", right, re.I))
                if left.lower() == "ax" and immediate:
                    code = f"{indent}db {ACCUMULATOR_OPCODES[opcode]}\n{indent}dw {right}"
                    changed("accumulator_immediate")
                elif opcode in LOGICAL_DIGITS and not smart and immediate and left.lower() in WORD_REGISTERS:
                    modrm = REGISTER_MODE_BITS + LOGICAL_DIGITS[opcode] * MODRM_REGISTER_STRIDE + WORD_REGISTERS[left.lower()]
                    code = f"{indent}db {WORD_IMMEDIATE_GROUP_OPCODE}, {modrm}\n{indent}dw {right}"
                    changed("nosmart_logical_immediate")
        output.append(code + comment)
    return ("\n".join(output) + "\n").replace("\n", "\r\n").encode("latin1"), counts


def write_generated(path: Path, data: bytes) -> None:
    # Grouped make targets require every output to be refreshed together.
    path.write_bytes(data)


def prepare(source_dir: Path, output_dir: Path) -> dict:
    source_dir, output_dir = source_dir.resolve(), output_dir.resolve()
    if source_dir == output_dir:
        raise AdapterError("Generated output directory must differ from source directory")
    sources = {p.name: p.read_bytes() for p in sorted(source_dir.iterdir()) if p.is_file() and p.suffix.lower() in (".asm", ".inc")}
    if not sources:
        raise AdapterError("Source directory contains no assembly inputs")
    # Shared type definitions must be known before translating any use site.
    ordered = dict(sorted(sources.items(), key=lambda item: (item[0].lower() != "structs.inc", item[0])))
    layouts = collect_layouts(ordered)
    generated = {name: lower_source(name, data, layouts) for name, data in sources.items()}
    manifest = {"format": 1, "adapter_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "files": {}}
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, (data, counts) in generated.items():
        write_generated(output_dir / name, data)
        manifest["files"][name] = {"source_sha256": hashlib.sha256(sources[name]).hexdigest(), "generated_sha256": hashlib.sha256(data).hexdigest(), "transformations": counts}
    write_generated(output_dir / "manifest.json", (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode())
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = prepare(args.source_dir, args.output_dir)
    except (AdapterError, OSError) as exc:
        parser.error(str(exc))
    print(f"Prepared {len(manifest['files'])} original-assembly inputs for WASM")


if __name__ == "__main__":
    main()
