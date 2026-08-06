"""Attribution for projector rejections, without touching ``v2_control``.

The turn-52 wedge was not merely a wrong invariant; it was a wrong invariant
that could not say what it was about.  ``V2ControlError("internal_error")``
carries no detail by design (the public envelope must stay detail-free), so an
operator staring at a bricked seat had nothing to attribute the failure to.

This module recovers attribution from outside the projector, two independent
ways:

``static``
    Walk the ``_ObservationError`` traceback to the deepest ``v2_control``
    frame, then read the row kinds named by the enclosing ``if``/``for``
    headers in the source (``buckets["city_worker_task"]``,
    ``kind == "city"``, ...) plus the live ``kind`` local.  This is what a
    supervisor could log in-process today.

``differential``
    Re-project the bundle with one row reverted at a time and report the rows
    whose reversion makes the bundle acceptable again.  This needs no source
    knowledge at all, which makes it the ground truth the static reader is
    checked against.

Neither path is allowed to modify ``v2_control``; both are read-only.
"""

from __future__ import annotations

import ast
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
import functools
import inspect
import textwrap
from types import TracebackType

import agent_eval.v2_control as v2_control
from agent_eval.v2_control import V2ControlError, V2SeatControl

def _bucket_kinds() -> frozenset[str]:
    """The exact bucket names ``_parse_rows`` allocates."""
    source = textwrap.dedent(
        inspect.getsource(v2_control.V2SeatControl._parse_rows)
    )
    tree = ast.parse(source)
    for node in ast.walk(tree):
        targets: tuple[ast.expr, ...]
        if isinstance(node, ast.Assign):
            targets = tuple(node.targets)
        elif isinstance(node, ast.AnnAssign):
            targets = (node.target,)
        else:
            continue
        if node.value is not None and any(
            isinstance(target, ast.Name) and target.id == "buckets"
            for target in targets
        ):
            names = {
                value.value
                for value in ast.walk(node.value)
                if isinstance(value, ast.Constant) and isinstance(value.value, str)
            }
            return frozenset(names)
    raise AssertionError("could not locate the _parse_rows bucket allocation")


BUCKET_ROW_KINDS: frozenset[str] = _bucket_kinds()


@dataclass(frozen=True)
class Rejection:
    """One projector rejection and everything we could attribute it to."""

    code: str
    function: str
    lineno: int
    source_line: str
    row_kinds: frozenset[str]
    frames: tuple[tuple[str, int], ...]

    @property
    def attributed(self) -> bool:
        return bool(self.row_kinds)

    def describe(self) -> str:
        return (
            f"{self.code} at {self.function}:{self.lineno} "
            f"-> {sorted(self.row_kinds) or '<unattributed>'}\n"
            f"    {self.source_line.strip()}"
        )


@functools.lru_cache(maxsize=1)
def _module_index() -> tuple[tuple[str, ...], dict[int, tuple[ast.AST, ...]]]:
    """Map every source line to its chain of enclosing AST nodes."""
    path = v2_control.__file__
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()
    lines = tuple(text.splitlines())
    tree = ast.parse(text)
    chains: dict[int, list[ast.AST]] = {}

    def visit(node: ast.AST, ancestors: tuple[ast.AST, ...]) -> None:
        start = getattr(node, "lineno", None)
        end = getattr(node, "end_lineno", None)
        chain = ancestors + (node,)
        if start is not None and end is not None:
            for line in range(start, end + 1):
                current = chains.get(line)
                if current is None or len(chain) > len(current):
                    chains[line] = list(chain)
        for child in ast.iter_child_nodes(node):
            visit(child, chain)

    for child in ast.iter_child_nodes(tree):
        visit(child, ())
    return lines, {line: tuple(chain) for line, chain in chains.items()}


def _literal_kinds(node: ast.AST | None) -> set[str]:
    if node is None:
        return set()
    return {
        child.value
        for child in ast.walk(node)
        if isinstance(child, ast.Constant)
        and isinstance(child.value, str)
        and child.value in BUCKET_ROW_KINDS
    }


def _names(node: ast.AST | None) -> set[str]:
    if node is None:
        return set()
    return {
        child.id for child in ast.walk(node) if isinstance(child, ast.Name)
    }


def _bound_names(target: ast.AST) -> set[str]:
    return {
        child.id
        for child in ast.walk(target)
        if isinstance(child, ast.Name)
    }


def _resolve(node: ast.AST | None, scope: Mapping[str, frozenset[str]]) -> set[str]:
    """Row kinds one expression is about, under a variable scope.

    Comprehension targets are bound from their own iterable first so that a
    generic ``item`` inside ``unique([item["x"] for item in tiles])`` resolves
    to ``tile`` rather than to whatever ``item`` last meant.
    """
    if node is None:
        return set()
    overlay: dict[str, frozenset[str]] = dict(scope)
    for child in ast.walk(node):
        if isinstance(child, ast.comprehension):
            kinds = frozenset(_resolve_flat(child.iter, overlay))
            for name in _bound_names(child.target):
                overlay[name] = kinds
    return _resolve_flat(node, overlay)


def _resolve_flat(
    node: ast.AST | None, scope: Mapping[str, frozenset[str]],
) -> set[str]:
    found = _literal_kinds(node)
    for name in _names(node):
        found |= set(scope.get(name, ()))
    return found


@functools.lru_cache(maxsize=64)
def _line_scopes(function: str) -> Mapping[int, Mapping[str, frozenset[str]]]:
    """Snapshot, per source line, which locals hold which row kinds.

    The walk follows source order with a single mutable scope, mirroring
    Python's own function scoping: inside ``for item in buckets["city_tile"]``
    the name ``item`` means a citizen tile, and it means something else after
    the next loop rebinds it.  That flow sensitivity is what keeps attribution
    from degenerating to "every row kind in the function".
    """
    _, chains = _module_index()
    definition: ast.FunctionDef | None = None
    for chain in chains.values():
        for node in chain:
            if isinstance(node, ast.FunctionDef) and node.name == function:
                if definition is None or node.lineno < definition.lineno:
                    definition = node
    # Variables simply named after their row kind are their own origin; that is
    # how ``_validate_cross_links``'s ``player`` parameter is attributed.
    scope: dict[str, frozenset[str]] = {
        name: frozenset({name}) for name in BUCKET_ROW_KINDS
    }
    snapshots: dict[int, Mapping[str, frozenset[str]]] = {}
    if definition is None:
        return {}

    def bind(target: ast.AST, kinds: set[str], *, merge: bool = False) -> None:
        if not kinds and not merge:
            for name in _bound_names(target):
                scope.pop(name, None)
            return
        for name in _bound_names(target):
            previous = scope.get(name, frozenset()) if merge else frozenset()
            scope[name] = previous | frozenset(kinds)

    def walk(node: ast.AST) -> None:
        for child in ast.iter_child_nodes(node):
            line = getattr(child, "lineno", None)
            if isinstance(child, (ast.For, ast.AsyncFor)):
                bind(child.target, _resolve(child.iter, scope))
                if line is not None:
                    snapshots[line] = dict(scope)
                walk(child)
                continue
            if isinstance(child, ast.Assign):
                kinds = _resolve(child.value, scope)
                for target in child.targets:
                    bind(target, kinds)
            elif isinstance(child, ast.AnnAssign) and child.value is not None:
                bind(child.target, _resolve(child.value, scope))
            elif isinstance(child, ast.Call) and isinstance(
                child.func, ast.Attribute,
            ) and child.func.attr in {"append", "add", "extend", "update"}:
                kinds = set()
                for argument in child.args:
                    kinds |= _resolve(argument, scope)
                if kinds:
                    bind(child.func.value, kinds, merge=True)
            if line is not None:
                snapshots[line] = dict(scope)
            walk(child)

    walk(definition)
    return snapshots


def _scope_at(function: str, lineno: int) -> Mapping[str, frozenset[str]]:
    snapshots = _line_scopes(function)
    if not snapshots:
        return {}
    candidates = [line for line in snapshots if line <= lineno]
    if not candidates:
        return {}
    return snapshots[max(candidates)]


def _static_kinds(function: str, lineno: int) -> set[str]:
    _, chains = _module_index()
    chain = chains.get(lineno, ())
    scope = _scope_at(function, lineno)
    for node in reversed(chain):
        found: set[str] = set()
        if isinstance(node, ast.If):
            found = _resolve(node.test, scope)
        elif isinstance(node, (ast.For, ast.AsyncFor)):
            found = _resolve(node.iter, scope)
        elif isinstance(node, ast.While):
            found = _resolve(node.test, scope)
        elif isinstance(node, (ast.Assign, ast.AnnAssign, ast.Expr,
                               ast.Return, ast.Raise)):
            found = _resolve(node, scope)
        if found:
            return found
    return set()


def _source_line(lineno: int) -> str:
    lines, _ = _module_index()
    if 1 <= lineno <= len(lines):
        return lines[lineno - 1]
    return ""


def _frames(traceback: TracebackType | None) -> list[tuple[str, int, dict]]:
    collected: list[tuple[str, int, dict]] = []
    while traceback is not None:
        frame = traceback.tb_frame
        if frame.f_globals.get("__name__") == v2_control.__name__:
            collected.append((
                frame.f_code.co_name, traceback.tb_lineno, frame.f_locals,
            ))
        traceback = traceback.tb_next
    return collected


def _runtime_kinds(locals_: Mapping[str, object]) -> set[str]:
    """Row kinds the live frame is holding, e.g. ``_parse_row``'s ``kind``."""
    found: set[str] = set()
    for key in ("kind", "schema_key", "section"):
        value = locals_.get(key)
        if isinstance(value, str):
            if value.startswith("unit_"):
                value = "unit"
            if value in BUCKET_ROW_KINDS:
                found.add(value)
    raw = locals_.get("row")
    if isinstance(raw, str) and raw.split(" ")[0] in BUCKET_ROW_KINDS:
        found.add(raw.split(" ")[0])
    return found


def attribute(error: BaseException) -> Rejection:
    """Attribute one raised projector error to the row kinds it is about."""
    cause = error.__cause__ if error.__cause__ is not None else error
    frames = _frames(cause.__traceback__)
    if not frames:
        frames = _frames(error.__traceback__)
    code = getattr(error, "code", type(error).__name__)
    if not frames:
        return Rejection(code, "<unknown>", 0, "", frozenset(), ())
    named = [item for item in frames if item[0] != "_fail"] or frames
    # Walk outward from the deepest projector frame: a rejection raised inside
    # a shared helper (``unique``, ``_entity_ref``) is about whatever its
    # caller was iterating.
    function, lineno, locals_ = named[-1]
    kinds: set[str] = set()
    for candidate_function, candidate_lineno, candidate_locals in reversed(named):
        found = _static_kinds(candidate_function, candidate_lineno)
        found |= _runtime_kinds(candidate_locals)
        if found:
            function, lineno, locals_ = (
                candidate_function, candidate_lineno, candidate_locals,
            )
            kinds = found
            break
    return Rejection(
        code=code,
        function=function,
        lineno=lineno,
        source_line=_source_line(lineno),
        row_kinds=frozenset(kinds),
        frames=tuple((name, line) for name, line, _ in frames),
    )


# --------------------------------------------------------------------------
# Projection helpers
# --------------------------------------------------------------------------


def project(
    rows: Sequence[str], *, generation: int = 1, revision: int = 11,
) -> Rejection | None:
    """Project one bundle on a fresh seat; return the rejection, if any."""
    control = V2SeatControl("game_attribution_probe", "agent", generation)
    observation = {
        "generation": generation,
        "native_revision": revision,
        "rows": tuple(rows),
    }
    try:
        control.state_page(observation)
    except V2ControlError as error:
        return attribute(error)
    except Exception as error:  # noqa: BLE001 - deliberately broad: see below
        # A non-V2ControlError escaping the projector is itself the finding:
        # the public envelope never forms, so the caller cannot even fail
        # closed with a code.  Report it as an unattributed rejection.
        return attribute(error)
    return None


def differential_row_kinds(
    accepted: Sequence[str], rejected: Sequence[str],
) -> frozenset[str]:
    """Name the row kinds whose reversion makes ``rejected`` acceptable.

    This is attribution with zero knowledge of the projector's source: revert
    one differing row at a time and see which reversion clears the rejection.
    It is exactly the probe a supervisor can run against a wedged seat.
    """
    accepted = tuple(accepted)
    rejected = tuple(rejected)
    if project(rejected) is None:
        return frozenset()
    added = [item for item in rejected if item not in set(accepted)]
    removed = [item for item in accepted if item not in set(rejected)]
    kinds: set[str] = set()
    for row in added:
        candidate = tuple(sorted(set(rejected) - {row} | set(
            item for item in removed
            if item.split(" ")[0] == row.split(" ")[0]
        )))
        if project(candidate) is None:
            kinds.add(row.split(" ")[0])
    if not kinds and added:
        # The bundle needs the whole differing family reverted at once.
        families = {row.split(" ")[0] for row in added}
        for family in families:
            candidate = tuple(sorted(
                {item for item in rejected if item.split(" ")[0] != family}
                | {item for item in accepted if item.split(" ")[0] == family}
            ))
            if project(candidate) is None:
                kinds.add(family)
    return frozenset(kinds)


__all__ = [
    "BUCKET_ROW_KINDS", "Rejection", "attribute", "differential_row_kinds",
    "project",
]
