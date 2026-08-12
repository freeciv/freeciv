"""Transcribe the _derive_native_schema_id canonical structure into TypeScript.

One-shot codegen for spike S2.  The emitted file contains *no* Python-produced
JSON bytes: every Python int becomes a TS bigint literal, every str a TS string
literal, every dict a TS object literal in Python insertion order (so the TS
canonical writer's own key sorting is exercised, not Python's).
"""
import inspect
import json
import sys

from agent_eval import v2_control as V


def canonical_structure():
    src = inspect.getsource(V._derive_native_schema_id)
    body = src.split('"""\n', 2)[-1].rsplit("    encoded = json.dumps", 1)[0]
    ns = dict(vars(V))
    exec("if True:\n" + body, ns)  # noqa: S102
    return ns["canonical"]


def emit(value, indent):
    pad = "  " * indent
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return f"{value}n"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=True)
    if isinstance(value, (list, tuple)):
        if not value:
            return "[]"
        items = ",\n".join(f"{pad}  {emit(v, indent + 1)}" for v in value)
        return "[\n" + items + "\n" + pad + "]"
    if isinstance(value, dict):
        if not value:
            return "{}"
        items = ",\n".join(
            f"{pad}  {json.dumps(k, ensure_ascii=True)}: {emit(v, indent + 1)}"
            for k, v in value.items()
        )
        return "{\n" + items + "\n" + pad + "}"
    raise TypeError(f"unsupported {type(value)!r}")


def main() -> None:
    canonical = canonical_structure()
    header = (
        "/**\n"
        " * Spike S2 fixture: the structure `_derive_native_schema_id` hashes,\n"
        " * transcribed from agent_eval/v2_control.py into TypeScript values.\n"
        " *\n"
        " * Mechanically generated once (see the spike report) — the real port\n"
        " * hand-writes this from the row/action tables.  Python ints are bigints\n"
        " * here, including the FNV-1a-64 offset basis 14695981039346656037 which\n"
        " * is > 2**53 and cannot survive a JS number.  Object keys are in Python\n"
        " * insertion order on purpose: the canonical writer must sort them.\n"
        " */\n"
        "import type { CanonValue } from './s2-canon-impl.ts';\n\n"
        "export const NATIVE_SCHEMA_CANONICAL: CanonValue =\n"
    )
    with open(sys.argv[1], "w", encoding="utf-8") as handle:
        handle.write(header + "  " + emit(canonical, 1) + ";\n")


main()
