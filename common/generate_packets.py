#!/usr/bin/env python3

#
# Freeciv - Copyright (C) 2003 - Raimar Falke
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#

# This script runs under Python 3.6 and up. Please leave it so.
# It might also run under older versions, but no such guarantees are made.

import re
import argparse
import sys
from pathlib import Path
from contextlib import contextmanager, ExitStack
from functools import partial
from itertools import chain, combinations, takewhile, zip_longest
from enum import Enum
from abc import ABC, abstractmethod

try:
    from functools import cache
except ImportError:
    from functools import lru_cache
    cache = lru_cache(None)
    del lru_cache

import typing
T_co = typing.TypeVar("T_co", covariant = True)


###################### Parsing Command Line Arguments ######################

def file_path(s: "str | Path") -> Path:
    """Parse the given path and check basic validity."""
    path = Path(s)

    if not path.name:
        raise ValueError(f"not a valid file path: {s!r}")
    if path.exists() and not path.is_file():
        raise ValueError(f"not a file: {s!r}")

    return path


class ScriptConfig:
    """Contains configuration info for the script's execution, along with
    functions closely tied to that configuration"""

    def_paths: "list[Path]"
    """Paths to definition files, in load order"""
    common_header_path: "Path | None"
    """Output path for the common header, or None if that should not
    be generated"""
    common_impl_path: "Path | None"
    """Output path for the common implementation, or None if that
    should not be generated"""
    server_header_path: "Path | None"
    """Output path for the server header, or None if that should not
    be generated"""
    server_impl_path: "Path | None"
    """Output path for the server implementation, or None if that
    should not be generated"""
    client_header_path: "Path | None"
    """Output path for the client header, or None if that should not
    be generated"""
    client_impl_path: "Path | None"
    """Output path for the client implementation, or None if that
    should not be generated"""

    verbose: bool
    """Whether to enable verbose logging"""
    lazy_overwrite: bool
    """Whether to lazily overwrite output files"""

    gen_stats: bool
    """Whether to generate delta stats code"""
    log_macro: "str | None"
    """The macro used for log calls, or None if no log calls should
    be generated"""

    fold_bool: bool
    """Whether to fold boolean fields into the packet header"""

    @staticmethod
    def get_argparser() -> argparse.ArgumentParser:
        """Construct an argument parser for a packet generation script"""
        parser = argparse.ArgumentParser(
            description = "Generate packet-related code from packets.def",
            add_help = False,   # we'll add a help option explicitly
        )

        # Argument groups
        # Note the order:
        # We want the path arguments to show up *first* in the help text

        paths = parser.add_argument_group(
            "Input and output paths",
            "The following parameters decide which files to read and write."
            " Omitting an output path will not generate that file.",
        )

        script = parser.add_argument_group(
            "Script configuration",
            "The following parameters change how the script operates.",
        )

        output = parser.add_argument_group(
            "Output configuration",
            "The following parameters change the amount of output.",
        )

        protocol = parser.add_argument_group(
            "Protocol configuration",
            "The following parameters CHANGE the protocol."
            " You have been warned.",
        )

        # Individual arguments
        # Note the order:
        # We want the path arguments to show up *last* in the usage summary

        script.add_argument("-h", "--help", action = "help",
                            help = "show this help message and exit")

        script.add_argument("-v", "--verbose", action = "store_true",
                            help = "enable log messages during code generation")

        # When enabled: Only overwrite existing output files when they
        # actually changed. This prevents the build system from rebuilding all
        # dependents in cases where that wouldn't even be necessary.
        script.add_argument("--lazy-overwrite", action = "store_true",
                            help = "only overwrite output files when their"
                            " contents actually changed")

        output.add_argument("-s", "--gen-stats", action = "store_true",
                            help = "generate code reporting packet usage"
                            " statistics; call delta_stats_report to get these")

        logs = output.add_mutually_exclusive_group()
        logs.add_argument("-l", "--log-macro", default = "log_packet_detailed",
                          help = "use the given macro for generated log calls")
        logs.add_argument("-L", "--no-logs", dest = "log_macro",
                          action = "store_const", const = None,
                          help = "disable generating log calls")

        protocol.add_argument("-B", "--no-fold-bool",
                              dest = "fold_bool", action = "store_false",
                              help = "explicitly encode boolean values in the"
                              " packet body, rather than folding them into the"
                              " packet header")

        output_path_args = (
            # (dest, option, canonical path)
            ("common_header_path", "--common-h", "common/packets_gen.h"),
            ("common_impl_path",   "--common-c", "common/packets_gen.c"),
            ("client_header_path", "--client-h", "client/packhand_gen.h"),
            ("client_impl_path",   "--client-c", "client/packhand_gen.c"),
            ("server_header_path", "--server-h", "server/hand_gen.h"),
            ("server_impl_path",   "--server-c", "server/hand_gen.c"),
        )

        for dest, option, canonical in output_path_args:
            paths.add_argument(option, dest = dest, type = file_path,
                               help = f"output path for {canonical}")

        paths.add_argument("def_paths", metavar = "def_path",
                           nargs = "+", type = file_path,
                           help = "paths to your packets.def file")

        return parser

    def __init__(self, args: "typing.Sequence[str] | None" = None):
        __class__.get_argparser().parse_args(args, namespace = self)

    def log_verbose(self, *args):
        """Print the given arguments iff verbose logging is enabled"""
        if self.verbose:
            print(*args)

    @property
    def _root_path(self) -> "Path | None":
        """Root Freeciv path, if we can find it."""
        path = Path(__file__).absolute()
        root = path.parent.parent
        if path != root / "common" / "generate_packets.py":
            self.log_verbose("Warning: couldn't find Freeciv root path")
            return None
        return root

    def _relative_path(self, path: Path) -> Path:
        """Find the relative path from the Freeciv root to the given path.
        Return the path unmodified if it's outside the Freeciv root, or if
        the Freeciv root could not be found."""
        root = self._root_path
        if root is not None:
            try:
                return path.absolute().relative_to(root)
            except ValueError:
                self.log_verbose(f"Warning: path {path} outside of Freeciv root")
        return path

    @property
    def _script_path(self) -> Path:
        """Relative path of the executed script. Under normal circumstances,
        this will be common/generate_packets.py, but it may differ when this
        module is imported from another script."""
        return self._relative_path(Path(sys.argv[0]))

    def _write_disclaimer(self, f: typing.TextIO):
        f.write(f"""\
 /**************************************************************************
 *                         THIS FILE WAS GENERATED                         *
 * Script: {self._script_path!s:63} *
""")

        for path in self.def_paths:
            f.write(f"""\
 * Input:  {self._relative_path(path)!s:63} *
""")

        f.write(f"""\
 *                         DO NOT CHANGE THIS FILE                         *
 **************************************************************************/

""")

    @contextmanager
    def _wrap_header(self, file: typing.TextIO, header_name: str) -> typing.Iterator[None]:
        """Add multiple inclusion protection to the given file"""
        name = f"FC__{header_name.upper()}_H"
        file.write(f"""\
#ifndef {name}
#define {name}

""")

        yield

        file.write(f"""\

#endif /* {name} */
""")

    @contextmanager
    def _wrap_cplusplus(self, file: typing.TextIO) -> typing.Iterator[None]:
        """Add code for `extern "C" {}` wrapping"""
        file.write(f"""\
#ifdef __cplusplus
extern "C" {{
#endif /* __cplusplus */

""")
        yield
        file.write(f"""\

#ifdef __cplusplus
}}
#endif /* __cplusplus */
""")

    @contextmanager
    def open_write(self, path: "str | Path", wrap_header: "str | None" = None, cplusplus: bool = True) -> typing.Iterator[typing.TextIO]:
        """Open a file for writing and write disclaimer.

        If enabled, lazily overwrites the given file.
        If wrap_header is given, add multiple inclusion protection; if
        cplusplus is also given (default), also add code for `extern "C"`
        wrapping."""
        path = Path(path)   # no-op if path is already a Path object
        self.log_verbose(f"writing {path}")

        with ExitStack() as stack:
            if self.lazy_overwrite:
                file = stack.enter_context(self.lazy_overwrite_open(path))
            else:
                file = stack.enter_context(path.open("w"))

            self._write_disclaimer(file)

            if wrap_header is not None:
                stack.enter_context(self._wrap_header(file, wrap_header))
                if cplusplus:
                    stack.enter_context(self._wrap_cplusplus(file))
            yield file
        self.log_verbose(f"done writing {path}")

    @contextmanager
    def lazy_overwrite_open(self, path: "str | Path", suffix: str = ".tmp") -> typing.Iterator[typing.TextIO]:
        """Open a file for writing, but only actually overwrite it if the new
        content differs from the old content.

        This creates a temporary file by appending the given suffix to the given
        file path. In the event of an error, this temporary file might remain in
        the target file's directory."""

        path = Path(path)
        tmp_path = path.with_name(path.name + suffix)

        # if tmp_path already exists, assume it's left over from a previous,
        # failed run and can be overwritten without trouble
        self.log_verbose(f"lazy: using {tmp_path}")
        with tmp_path.open("w") as file:
            yield file

        if path.exists() and files_equal(tmp_path, path):
            self.log_verbose("lazy: no change, deleting...")
            tmp_path.unlink()
        else:
            self.log_verbose("lazy: content changed, replacing...")
            tmp_path.replace(path)


################### General helper functions and classes ###################

def files_equal(path_a: "str | Path", path_b: "str | Path") -> bool:
    """Return whether the contents of two text files are identical"""
    with open(path_a) as file_a, open(path_b) as file_b:
        return all(a == b for a, b in zip_longest(file_a, file_b))

# Taken from https://docs.python.org/3.4/library/itertools.html#itertools-recipes
def powerset(iterable: typing.Iterable[T_co]) -> "typing.Iterator[tuple[T_co, ...]]":
    "powerset([1,2,3]) --> () (1,) (2,) (3,) (1,2) (1,3) (2,3) (1,2,3)"
    s = list(iterable)
    return chain.from_iterable(combinations(s, r) for r in range(len(s)+1))

INSERT_PREFIX_PATTERN: "typing.Final" = re.compile(r"^(?!#|$)", re.MULTILINE)
"""Matches the beginning of nonempty lines that are not preprocessor
directives, i.e. don't start with a #"""

def prefix(prefix: str, text: str) -> str:
    """Prepend prefix to every line of text, except blank lines and those
    starting with #"""
    return INSERT_PREFIX_PATTERN.sub(prefix, text)


class Location:
    """Roughly represents a location in memory for the generated code;
    outside of recursive field types like arrays, this will usually just be
    a field of a packet, but it serves to concisely handle the recursion."""

    # placeholder that will clearly be an error if it accidentally
    # shows up in generated code
    _PACKET = "#error gen_packet$"
    _INDICES = "ijk"

    name: str
    """The name associated with this location; used in log messages."""
    _location: str
    """The actual location as used in code, including placeholders for
    where the packet name goes"""
    depth: int
    """The array nesting depth of this location; used to determine index
    variable names."""
    json_depth: int
    """The total sub-location nesting depth of the JSON field address
    for this location"""

    def __init__(self, name: str, *, location: "str | None" = None,
                       depth: int = 0, json_depth: "int | None" = None):
        self.name = name
        self._location = location if location is not None else self._PACKET + name
        self.depth = depth
        self.json_depth = json_depth if json_depth is not None else depth

    @classmethod
    @cache
    def constant(cls, location: str, name: str = "(unknown)") -> "Location":
        """Construct a Location not dependent on the packet"""
        assert cls._PACKET not in location
        return cls(name, location = location)

    def replace(self, new_location: str) -> "Location":
        """Return the given string as a new Location with the same metadata
        as self"""
        return type(self)(
            name = self.name,
            location = new_location,
            depth = self.depth,
            json_depth = self.json_depth,
        )

    def deeper(self, new_location: str, json_step: int = 1) -> "Location":
        """Return the given string as a new Location with the same name as
        self and incremented depth"""
        return type(self)(
            name = self.name,
            location = new_location,
            depth = self.depth + 1,
            json_depth = self.json_depth + json_step,
        )

    def sub_full(self, json_step: int = 1) -> "Location":
        """Like self.sub, but with the option to step the JSON nesting
        depth by a different amount."""
        return self.deeper(f"{self}[{self.index}]", json_step)

    @property
    def index(self) -> str:
        """The index name for the current depth"""
        if self.depth > len(self._INDICES):
            return self._INDICES[0] + str(self.depth)   # i3, i4, i5...
        return self._INDICES[self.depth]

    @property
    def sub(self) -> "Location":
        """A location one level deeper with the current index subscript
        added to the end.

        `field` ~> `field[i]` ~> `field[i][j]` etc."""
        return self.sub_full()

    @property
    def json_subloc(self) -> str:
        """The plocation (JSON field address) to the sub-location
        of this location's corresponding field address"""
        return "field_addr.sub_location" + self.json_depth * "->sub_location"

    def __matmul__(self, packet: "str | None") -> str:
        """self @ packet
        Code fragment of this location in the given packet, or in local
        variables if packet is None"""
        packet = f"{packet}->" if packet is not None else ""
        return self._location.replace(self._PACKET, packet)

    def __str__(self) -> str:
        return self._location

    def __repr__(self) -> str:
        return f"<{type(self).__name__} {self.name}(depth={self.depth}, json_depth={self.json_depth}) {self @ 'PACKET'}>"

    def __eq__(self, other) -> bool:
        if other is self:
            return True
        if not isinstance(other, __class__):
            return NotImplemented
        return (
            self.name == other.name
            and self._location == other._location
            and self.depth == other.depth
            and self.json_depth == other.json_depth
        )

    def __hash__(self) -> int:
        return hash((__class__, self.name, self._location, self.depth, self.json_depth))


#################### Components of a packets definition ####################

class FieldFlags:
    """Information about flags of a given Field. Multiple Field objects can
    share one FieldFlags instance, e.g. when defined on the same line."""

    ADD_CAP_PATTERN = re.compile(r"^add-cap\(([^()]+)\)$")
    """Matches an add-cap flag (optional capability)"""

    REMOVE_CAP_PATTERN = re.compile(r"^remove-cap\(([^()]+)\)$")
    """Matches a remove-cap flag (optional capability)"""

    is_key: bool = False
    """Whether the field is a key field"""

    diff: bool = False
    """Whether the field should be deep-diffed for transmission"""

    add_caps: "set[str]"
    """The capabilities required to enable the field"""

    remove_caps: "set[str]"
    """The capabilities that disable the field"""

    @classmethod
    @cache
    def parse(cls, flags_text: str) -> "FieldFlags":
        """Parse a FieldFlags object from a comma-separated flag line"""
        return cls(
            stripped
            for flag in flags_text.split(",")
            for stripped in (flag.strip(),)
            if stripped
        )

    def __init__(self, flag_texts: typing.Iterable[str]):
        self.add_caps = set()
        self.remove_caps = set()

        for flag in flag_texts:
            if flag == "key":
                self.is_key = True
                continue
            if flag == "diff":
                self.diff = True
                continue
            mo = __class__.ADD_CAP_PATTERN.fullmatch(flag)
            if mo is not None:
                self.add_caps.add(mo.group(1))
                continue
            mo = __class__.REMOVE_CAP_PATTERN.fullmatch(flag)
            if mo is not None:
                self.remove_caps.add(mo.group(1))
                continue
            raise ValueError(f"unrecognized flag in field declaration: {flag}")

        contradictions = self.add_caps & self.remove_caps
        if contradictions:
            raise ValueError("cannot have same capabilities as both add-cap and remove-cap: " + ", ".join(contradictions))


class SizeInfo:
    """Information about size along one dimension of an array or other sized
    field type. Contains both the declared / maximum size, and the actual
    used size (if different)."""

    ARRAY_SIZE_PATTERN = re.compile(r"^([^:]+)(?:\:([^:]+))?$")
    """Matches an array size declaration (without the brackets)

    Groups:
    - the declared / maximum size
    - the field name for the actual size (optional)"""

    declared: Location
    """Maximum size; used in declarations"""

    actual: Location
    """Location of the field to use for the actual size."""

    @classmethod
    @cache
    def parse(cls, size_text) -> "SizeInfo":
        """Parse the given array size text (without brackets)"""
        mo = cls.ARRAY_SIZE_PATTERN.fullmatch(size_text)
        if mo is None:
            raise ValueError(f"invalid array size declaration: [{size_text}]")
        return cls(*mo.groups())

    def __init__(self, declared: str, actual: "str | Location | None"):
        self.declared = Location.constant(declared)
        if actual is None:
            self.actual = self.declared
        elif isinstance(actual, Location):
            self.actual = actual
        else:
            self.actual = Location(actual)

    @property
    def constant(self) -> bool:
        """Whether the actual size doesn't change"""
        # Only check whether we were initialized with no actual size;
        # we're ignoring the possibility of a constant custom actual size
        return self.actual is self.declared

    def size_check_get(self, field_name: str, packet: str) -> str:
        """Generate a code snippet checking whether the received size is in
        range when receiving a packet."""
        if self.constant:
            return ""
        return f"""\
if ({self.actual @ packet} > {self.declared}) {{
  RECEIVE_PACKET_FIELD_ERROR({field_name}, ": array truncated");
}}
"""

    def size_check_index(self, field_name: str, packet: str) -> str:
        """Generate a code snippet asserting that indices can be correctly
        transmitted for array-diff."""
        if self.constant:
            return f"""\
FC_STATIC_ASSERT({self.declared} <= MAX_UINT16, packet_array_too_long_{field_name});
"""
        else:
            return f"""\
fc_assert({self.actual @ packet} <= MAX_UINT16);
"""

    def index_put(self, packet: str, index: str) -> str:
        """Generate a code snippet writing the given value to the network
        output, encoded as the appropriate index type"""
        if self.constant:
            return f"""\
#if {self.declared} <= MAX_UINT8
e |= DIO_PUT(uint8, &dout, &field_addr, {index});
#else
e |= DIO_PUT(uint16, &dout, &field_addr, {index});
#endif
"""
        else:
            return f"""\
if ({self.actual @ packet} <= MAX_UINT8) {{
  e |= DIO_PUT(uint8, &dout, &field_addr, {index});
}} else {{
  e |= DIO_PUT(uint16, &dout, &field_addr, {index});
}}
"""

    def index_get(self, packet: str, location: Location) -> str:
        """Generate a code snippet reading the next index from the
        network input decoded as the correct type"""
        if self.constant:
            return f"""\
#if {self.declared} <= MAX_UINT8
if (!DIO_GET(uint8, &din, &field_addr, &{location.index})) {{
#else
if (!DIO_GET(uint16, &din, &field_addr, &{location.index})) {{
#endif
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
"""
        else:
            return f"""\
if (({self.actual @ packet} <= MAX_UINT8)
    ? !DIO_GET(uint8, &din, &field_addr, &{location.index})
    : !DIO_GET(uint16, &din, &field_addr, &{location.index})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
"""

    def __str__(self) -> str:
        if self.constant:
            return f"{self.declared}"
        elif self.actual.name != self.actual @ None:
            # custom location
            return "*"
        else:
            return f"{self.declared}:{self.actual.name}"

    def __eq__(self, other) -> bool:
        if other is self:
            return True
        if not isinstance(other, __class__):
            return NotImplemented
        return (self.declared == other.declared
                and self.actual == other.actual)

    def __hash__(self) -> int:
        return hash((__class__, self.declared, self.actual))


class RawFieldType(ABC):
    """Abstract base class (ABC) for classes representing types defined in a
    packets definition file. These types may require the addition of a size
    in order to be usable; see the array() method and the FieldType class."""

    @abstractmethod
    def array(self, size: SizeInfo) -> "FieldType":
        """Add an array size to this field type, either to make a type which
        needs a size fully usable, or to make an array type with self as
        the element type."""
        raise NotImplementedError

    @abstractmethod
    def __str__(self) -> str:
        return super().__str__()

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__} {self}>"


FieldTypeConstructor: "typing.TypeAlias" = typing.Callable[[str, str], RawFieldType]

class TypeRegistry:
    """Determines what Python class to use for field types based on their
    dataio type and public type."""

    TYPE_INFO_PATTERN = re.compile(r"^([^()]*)\(([^()]*)\)$")
    """Matches a field type.

    Groups:
    - dataio type
    - public type (aka struct type)"""

    dataio_types: "dict[str, FieldTypeConstructor]"
    """Dictionary mapping dataio types to the constructor used for
    field types with that dataio type.

    This is the primary factor deciding what constructor to use for a
    given field type."""

    dataio_patterns: "dict[typing.Pattern[str], FieldTypeConstructor]"
    """Dictionary mapping RegEx patterns to the constructor used for
    field types whose dataio type matches that pattern.

    Matches are cached in self.dataio_types."""

    public_types: "dict[str, FieldTypeConstructor]"
    """Like self.dataio_types, but for public types.

    This is only checked if there are no matches in self.dataio_types
    and self.dataio_patterns."""

    public_patterns: "dict[typing.Pattern[str], FieldTypeConstructor]"
    """Like self.dataio_patterns, but for public types.

    Matches are cached in self.public_types."""

    fallback: FieldTypeConstructor
    """Fallback constructor used when there are no matches for either
    dataio type or public type."""

    @staticmethod
    def _resolve(
        key: str,
        direct: "dict[str, FieldTypeConstructor]",
        patterns: "dict[typing.Pattern[str], FieldTypeConstructor]",
        fallback: FieldTypeConstructor
    ) -> FieldTypeConstructor:
        """Helper function. Figures out which constructor to use for a given
        key (dataio type or public type), and caches the result."""
        try:
            return direct[key]
        except KeyError:
            pass

        for pat, ctor in patterns.items():
            mo = pat.fullmatch(key)
            if mo is not None:
                # cache the match
                direct[key] = ctor
                return ctor

        # cache that there was no match
        direct[key] = fallback
        return fallback

    def __init__(self, fallback: FieldTypeConstructor):
        self.dataio_types = {}
        self.dataio_patterns = {}
        self.public_types = {}
        self.public_patterns = {}
        self.fallback = fallback

    def parse(self, type_text: str) -> RawFieldType:
        """Parse a single field type"""
        mo = __class__.TYPE_INFO_PATTERN.fullmatch(type_text)
        if mo is None:
            raise ValueError(f"malformed type or undefined alias: {type_text!r}")
        return self(*mo.groups())

    def __call__(self, dataio_type: str, public_type: str) -> RawFieldType:
        ctor = self._resolve(dataio_type, self.dataio_types, self.dataio_patterns, self._by_public)
        return ctor(dataio_type, public_type)

    def _by_public(self, dataio_type: str, public_type: str) -> RawFieldType:
        ctor = self._resolve(public_type, self.public_types, self.public_patterns, self.fallback)
        return ctor(dataio_type, public_type)


class NeedSizeType(RawFieldType):
    """Helper class for field types that require a size to be usable."""

    dataio_type: str
    """The dataio type passed to self.cls"""

    public_type: str
    """The public type passed to self.cls"""

    cls: typing.Callable[[str, str, SizeInfo], "FieldType"]
    """The field type constructed when adding a size to this type"""

    def __init__(self, dataio_type: str, public_type: str, cls: typing.Callable[[str, str, SizeInfo], "FieldType"]):
        self.dataio_type = dataio_type
        self.public_type = public_type
        self.cls = cls

    def array(self, size: SizeInfo) -> "FieldType":
        """Add an array size to make a usable type."""
        return self.cls(self.dataio_type, self.public_type, size)

    def __str__(self) -> str:
        return f"{self.dataio_type}({self.public_type})"


class FieldType(RawFieldType):
    """Abstract base class (ABC) for classes representing type information
    usable for fields of a packet"""

    foldable: bool = False
    """Whether a field of this type can be folded into the packet header"""

    complex: bool = False
    """Whether a field of this type needs special handling when initializing,
    copying or destroying the packet struct"""

    @cache
    def array(self, size: SizeInfo) -> "FieldType":
        """Construct a FieldType for an array with element type self and the
        given size"""
        if f"{size.declared}" == "*":
            raise ValueError(f"vectors not supported for field type {self}")
        return ArrayType(self, size)

    @abstractmethod
    def get_code_declaration(self, location: Location) -> str:
        """Generate a code snippet declaring a field with this type in a
        packet struct."""
        raise NotImplementedError

    @abstractmethod
    def get_code_param(self, location: Location) -> str:
        """Generate a code fragment declaring a parameter with this type for
        a handle function.

        See also self.get_code_handle_arg()"""
        raise NotImplementedError

    def get_code_handle_arg(self, location: Location, packet: str) -> str:
        """Generate a code fragment passing an argument with this type to a
        handle function.

        See also self.get_code_param()"""
        return f"{location @ packet}"

    def get_code_init(self, location: Location, packet: str) -> str:
        """Generate a code snippet initializing a field of this type in the
        packet struct, after the struct has already been zeroed.

        Subclasses must override this if self.complex is True"""
        if self.complex:
            raise ValueError(f"default get_code_init implementation called for field {location.name} with complex type {self!r}")
        # no work needed
        return ""

    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        """Generate a code snippet deep-copying a field of this type from
        one packet struct to another that has already been initialized.

        Subclasses must override this if self.complex is True"""
        if self.complex:
            raise ValueError(f"default get_code_copy implementation called for field {location.name} with complex type {self!r}")
        return f"""\
{location @ dest} = {location @ src};
"""

    def get_code_fill(self, location: Location, packet: str) -> str:
        """Generate a code snippet shallow-copying a value of this type from
        dsend arguments into a packet struct."""
        return f"""\
{location @ packet} = {location @ None};
"""

    def get_code_free(self, location: Location, packet: str) -> str:
        """Generate a code snippet deinitializing a field of this type in
        the packet struct before it gets destroyed.

        Subclasses must override this if self.complex is True"""
        if self.complex:
            raise ValueError(f"default get_code_free implementation called for field {location.name} with complex type {self!r}")
        # no work needed
        return ""

    @abstractmethod
    def get_code_hash(self, location: Location, packet: str) -> str:
        """Generate a code snippet factoring a field of this type into a
        hash computation's `result`."""
        raise NotImplementedError

    @abstractmethod
    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        """Generate a code snippet comparing a field of this type between
        the given packets and setting `differ` accordingly. The `old`
        packet is one we know to have been initialized by our own code."""
        raise NotImplementedError

    @abstractmethod
    def get_code_put(self, location: Location, packet: str, diff_packet: "str | None" = None) -> str:
        """Generate a code snippet writing a field of this type to the
        dataio stream."""
        raise NotImplementedError

    @abstractmethod
    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        """Generate a code snippet reading a field of this type from the
        dataio stream."""
        raise NotImplementedError

    def _compat_keys(self, location: Location, packet: str):
        """Internal helper function. Yield keys to compare for
        type compatibility. See is_type_compatible()"""
        yield self.get_code_declaration(location)
        yield self.get_code_param(location)
        yield self.get_code_handle_arg(location, packet)
        yield self.get_code_fill(location, packet)
        yield self.complex
        if self.complex:
            yield self.get_code_init(location, packet)
            yield self.get_code_free(location, packet)

    def is_type_compatible(self, other: "FieldType") -> bool:
        """Determine whether two field types can be used interchangeably as
        part of the packet struct, i.e. differ in dataio transmission only"""
        if other is self:
            return True
        loc = Location("compat_test_field_name")
        pak = "compat_test_packet_name"
        return all(
            a == b
            for a, b in zip_longest(
                self._compat_keys(loc, pak),
                other._compat_keys(loc, pak),
            )
        )


class BasicType(FieldType):
    """Type information for a field without any specialized treatment"""

    dataio_type: str
    """How fields of this type are transmitted over network"""

    public_type: str
    """How fields of this type are represented in C code"""

    def __init__(self, dataio_type: str, public_type: str):
        self.dataio_type = dataio_type
        self.public_type = public_type

    def get_code_declaration(self, location: Location) -> str:
        return f"""\
{self.public_type} {location @ None};
"""

    def get_code_param(self, location: Location) -> str:
        return f"{self.public_type} {location @ None}"

    def get_code_hash(self, location: Location, packet: str) -> str:
        raise ValueError(f"hash not supported for type {self} in field {location.name}")

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        return f"""\
differ = ({location @ old} != {location @ new});
"""

    def get_code_put(self, location: Location, packet: str, diff_packet: "str | None" = None) -> str:
        return f"""\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, {location @ packet});
"""

    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        return f"""\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, &{location @ packet})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
"""

    def __str__(self) -> str:
        return f"{self.dataio_type}({self.public_type})"

DEFAULT_REGISTRY: "typing.Final" = TypeRegistry(BasicType)
"""The default type registry used by a PacketsDefinition when no other
registry is given."""


class IntType(BasicType):
    """Type information for an integer field"""

    TYPE_PATTERN = re.compile(r"^[su]int\d+$")
    """Matches an int dataio type"""

    @typing.overload
    def __init__(self, dataio_info: str, public_type: str): ...
    @typing.overload
    def __init__(self, dataio_info: "re.Match[str]", public_type: str): ...
    def __init__(self, dataio_info: "str | re.Match[str]", public_type: str):
        if isinstance(dataio_info, str):
            mo = self.TYPE_PATTERN.fullmatch(dataio_info)
            if mo is None:
                raise ValueError("not a valid int type")
            dataio_info = mo
        dataio_type = dataio_info.group(0)

        super().__init__(dataio_type, public_type)

    def get_code_hash(self, location: Location, packet: str) -> str:
        return f"""\
result += {location @ packet};
"""

    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        if self.public_type in ("int", "bool"):
            # read directly
            return super().get_code_get(location, packet, deep_diff)
        # read indirectly to make sure coercions between different integer
        # types happen correctly
        return f"""\
{{
  int readin;

  if (!DIO_GET({self.dataio_type}, &din, &field_addr, &readin)) {{
    RECEIVE_PACKET_FIELD_ERROR({location.name});
  }}
  {location @ packet} = readin;
}}
"""

DEFAULT_REGISTRY.dataio_patterns[IntType.TYPE_PATTERN] = IntType


class BoolType(IntType):
    """Type information for a boolean field"""

    TYPE_PATTERN = re.compile(r"^bool\d*$")
    """Matches a bool dataio type"""

    foldable: bool = True

    @typing.overload
    def __init__(self, dataio_info: str, public_type: str): ...
    @typing.overload
    def __init__(self, dataio_info: "re.Match[str]", public_type: str): ...
    def __init__(self, dataio_info: "str | re.Match[str]", public_type: str):
        if isinstance(dataio_info, str):
            mo = self.TYPE_PATTERN.fullmatch(dataio_info)
            if mo is None:
                raise ValueError("not a valid bool type")
            dataio_info = mo

        if public_type != "bool":
            raise ValueError(f"bool dataio type with non-bool public type: {public_type!r}")

        super().__init__(dataio_info, public_type)

DEFAULT_REGISTRY.dataio_patterns[BoolType.TYPE_PATTERN] = BoolType


class FloatType(BasicType):
    """Type information for a float field"""

    TYPE_PATTERN = re.compile(r"^([su]float)(\d+)?$")
    """Matches a float dataio type

    Note: Will also match float types without a float factor to avoid
    falling back to the default; in this case, the second capturing group
    will not match.

    Groups:
    - non-numeric dataio type
    - numeric float factor"""

    float_factor: int
    """Granularity (fixed-point factor) used to transmit this type in an
    integer"""

    @typing.overload
    def __init__(self, dataio_info: str, public_type: str): ...
    @typing.overload
    def __init__(self, dataio_info: "re.Match[str]", public_type: str): ...
    def __init__(self, dataio_info: "str | re.Match[str]", public_type: str):
        if isinstance(dataio_info, str):
            mo = self.TYPE_PATTERN.fullmatch(dataio_info)
            if mo is None:
                raise ValueError("not a valid float type")
            dataio_info = mo
        dataio_type, float_factor = dataio_info.groups()
        if float_factor is None:
            raise ValueError(f"float type without float factor: {dataio_info.string!r}")

        if public_type != "float":
            raise ValueError(f"float dataio type with non-float public type: {public_type!r}")

        super().__init__(dataio_type, public_type)
        self.float_factor = int(float_factor)

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        return f"""\
differ = ((int) ({location @ old} * {self.float_factor}) != (int) ({location @ new} * {self.float_factor}));
"""

    def get_code_put(self, location: Location, packet: str, diff_packet: "str | None" = None) -> str:
        return f"""\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, {location @ packet}, {self.float_factor:d});
"""

    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        return f"""\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, &{location @ packet}, {self.float_factor:d})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
"""

    def __str__(self) -> str:
        return f"{self.dataio_type}{self.float_factor:d}({self.public_type})"

DEFAULT_REGISTRY.dataio_patterns[FloatType.TYPE_PATTERN] = FloatType


class BitvectorType(BasicType):
    """Type information for a bitvector field"""

    def __init__(self, dataio_type: str, public_type: str):
        if dataio_type != "bitvector":
            raise ValueError("not a valid bitvector type")

        super().__init__(dataio_type, public_type)

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        return f"""\
differ = !BV_ARE_EQUAL({location @ old}, {location @ new});
"""

    def get_code_put(self, location: Location, packet: str, diff_packet: "str | None" = None) -> str:
        return f"""\
e |= DIO_BV_PUT(&dout, &field_addr, {location @ packet});
"""

    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        return f"""\
if (!DIO_BV_GET(&din, &field_addr, {location @ packet})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
"""

DEFAULT_REGISTRY.dataio_types["bitvector"] = BitvectorType


class StructType(BasicType):
    """Type information for a field of some general struct type"""

    TYPE_PATTERN = re.compile(r"^struct\s+(\w+)$")
    """Matches a struct public type

    Groups:
    - the struct name (without the `struct ` prefix)"""

    struct_type: str
    """The struct name (without the `struct ` prefix)"""

    @typing.overload
    def __init__(self, dataio_type: str, public_info: str): ...
    @typing.overload
    def __init__(self, dataio_type: str, public_info: "re.Match[str]"): ...
    def __init__(self, dataio_type: str, public_info: "str | re.Match[str]"):
        if isinstance(public_info, str):
            mo = self.TYPE_PATTERN.fullmatch(public_info)
            if mo is None:
                raise ValueError("not a valid struct type")
            public_info = mo
        struct_type = public_info.group(1)

        super().__init__(dataio_type, f"struct {struct_type}")
        self.struct_type = struct_type

    @cache
    def array(self, size: SizeInfo) -> "FieldType":
        """Construct a FieldType for an array or vector with element type
        self and the given size"""
        if f"{size.declared}" == "*":
            return SpecvecType(self)
        return super().array(size)

    def get_code_param(self, location: Location) -> str:
        if not location.depth:
            # top level: pass by-reference
            return "const " + super().get_code_param(location.replace(f"*{location}"))
        return super().get_code_param(location)

    def get_code_handle_arg(self, location: Location, packet: str) -> str:
        # top level: pass by-reference
        prefix = "&" if not location.depth else ""
        return prefix + super().get_code_handle_arg(location, packet)

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        return f"""\
differ = !are_{self.dataio_type}s_equal(&{location @ old}, &{location @ new});
"""

    def get_code_put(self, location: Location, packet: str, diff_packet: "str | None" = None) -> str:
        return f"""\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, &{location @ packet});
"""

DEFAULT_REGISTRY.public_patterns[StructType.TYPE_PATTERN] = StructType


class CmParameterType(StructType):
    """Type information for a worklist field"""

    def __init__(self, dataio_type: str, public_type: str):
        if dataio_type != "cm_parameter":
            raise ValueError("not a valid cm_parameter type")

        if public_type != "struct cm_parameter":
            raise ValueError(f"cm_parameter dataio type with non-cm_parameter public type: {public_type!r}")

        super().__init__(dataio_type, public_type)

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        return f"""\
differ = !cm_are_parameter_equal(&{location @ old}, &{location @ new});
"""

DEFAULT_REGISTRY.dataio_types["cm_parameter"] = CmParameterType


class WorklistType(StructType):
    """Type information for a worklist field"""

    def __init__(self, dataio_type: str, public_type: str):
        if dataio_type != "worklist":
            raise ValueError("not a valid worklist type")

        if public_type != "struct worklist":
            raise ValueError(f"worklist dataio type with non-worklist public type: {public_type!r}")

        super().__init__(dataio_type, public_type)

    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        return f"""\
worklist_copy(&{location @ dest}, &{location @ src});
"""

    def get_code_fill(self, location: Location, packet: str) -> str:
        return f"""\
worklist_copy(&{location @ packet}, {location @ None});
"""

DEFAULT_REGISTRY.dataio_types["worklist"] = WorklistType


class SizedType(BasicType):
    """Abstract base class (ABC) for field types that include a size"""

    size: SizeInfo
    """Size info (maximum and actual) of this type"""

    def __init__(self, dataio_type: str, public_type: str, size: SizeInfo):
        super().__init__(dataio_type, public_type)
        self.size = size

    def get_code_declaration(self, location: Location) -> str:
        return super().get_code_declaration(
            location.replace(f"{location}[{self.size.declared}]")
        )

    def get_code_param(self, location: Location) -> str:
        # see ArrayType.get_code_param() for explanation
        if not location.depth:
            return "const " + super().get_code_param(location.replace(f"*{location}"))
        else:
            return super().get_code_param(location.replace(f"*const {location}"))

    @abstractmethod
    def get_code_fill(self, location: Location, packet: str) -> str:
        return super().get_code_fill(location, packet)

    @abstractmethod
    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        return super().get_code_copy(location, dest, src)

    def __str__(self) -> str:
        return f"{super().__str__()}[{self.size}]"


class StringType(SizedType):
    """Type information for a string field"""

    def __init__(self, dataio_type: str, public_type: str, size: SizeInfo):
        if dataio_type not in ("string", "estring"):
            raise ValueError("not a valid string type")

        if public_type != "char":
            raise ValueError(f"string type with illegal public type: {public_type!r}")

        super().__init__(dataio_type, public_type, size)

    def get_code_fill(self, location: Location, packet: str) -> str:
        return f"""\
sz_strlcpy({location @ packet}, {location @ None});
"""

    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        return f"""\
sz_strlcpy({location @ dest}, {location @ src});
"""

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        return f"""\
differ = (strcmp({location @ old}, {location @ new}) != 0);
"""

    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        return f"""\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, {location @ packet}, sizeof({location @ packet}))) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
"""


class MemoryType(SizedType):
    """Type information for a memory field"""

    def __init__(self, dataio_type: str, public_type: str, size: SizeInfo):
        if dataio_type != "memory":
            raise ValueError("not a valid memory type")

        super().__init__(dataio_type, public_type, size)

    def get_code_fill(self, location: Location, packet: str) -> str:
        raise NotImplementedError("fill not supported for memory-type fields")

    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        return f"""\
memcpy({location @ dest}, {location @ src}, {self.size.actual @ src});
"""

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        if self.size.constant:
            return f"""\
differ = (memcmp({location @ old}, {location @ new}, {self.size.declared}) != 0);
"""
        return f"""\
differ = (({self.size.actual @ old} != {self.size.actual @ new})
          || (memcmp({location @ old}, {location @ new}, {self.size.actual @ new}) != 0));
"""

    def get_code_put(self, location: Location, packet: str, diff_packet: "str | None" = None) -> str:
        return f"""\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, &{location @ packet}, {self.size.actual @ packet});
"""

    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        return f"""\
{self.size.size_check_get(location.name, packet)}\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, {location @ packet}, {self.size.actual @ packet})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
"""

DEFAULT_REGISTRY.dataio_types["memory"] = partial(NeedSizeType, cls = MemoryType)


class SequenceType(FieldType):
    """Abstract base class (ABC) for field types representing homogeneous
    sequences of elements"""

    @abstractmethod
    def size_at(self, location: Location) -> SizeInfo:
        """Return a size info object for a field of this type"""
        raise NotImplementedError()

    def resize(self, location: Location, packet: str, new_size: str) -> str:
        """Return a code snippet updating the size of a field of this type,
        or the empty string if this type doesn't need to individually transmit
        and update its size"""
        return ""

    def null_condition(self, location: Location) -> "Location | None":
        """Return a code condition that if TRUE means the field cannot be used
        normally and should be treated as empty, or None if this never happens"""
        return None

    @abstractmethod
    def inner_cmp(self, location: Location, new: str, old: str) -> str:
        """Generate the loop body for get_code_cmp()

        This is placed at the very beginning of a scope, so variable
        declarations are permitted without requiring an extra block."""
        raise NotImplementedError()

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        size = self.size_at(location)
        inner_cmp = prefix("    ", self.inner_cmp(location, new, old))

        null_con = self.null_condition(location)
        if null_con is None:
            safe_size = size
        else:
            # new packet is passed in from outside, might be null
            safe_size = SizeInfo(size.declared @ None, size.actual.replace(f"{null_con} ? 0 : {size.actual}"))
        if not safe_size.constant:
            # ends mid-line
            head = f"""\
differ = ({size.actual @ old} != {safe_size.actual @ new});
if (!differ) """
        else:
            head = f"""\
differ = FALSE;
"""

        return f"""\
{head}{{
  int {location.index};

  for ({location.index} = 0; {location.index} < {size.actual @ old}; {location.index}++) {{
{inner_cmp}\
    if (differ) {{
      break;
    }}
  }}
}}
"""

    @abstractmethod
    def inner_put(self, location: Location, packet: str,
                         diff_packet: "str | None" = None, json_step: int = 1) -> str:
        """Generate the main part of the loop body for get_code_put()"""
        raise NotImplementedError()

    def get_code_put_full(self, location: Location, packet: str) -> str:
        """Generate put code without array-diff."""
        size = self.size_at(location)
        inner_put = prefix("    ", self.inner_put(location, packet))

        send_size = bool(self.resize(location, packet, "s"))
        if send_size:
            # Note: strictly speaking, we could allow size == MAX_UINT16,
            # but we might want to use that in the future to signal overlong
            # vectors (like with jumbo packets)
            # Though that would also mean packets larger than 64 KiB,
            # which we're a long way from
            size_part = f"""\
  fc_assert({size.actual @ packet} < MAX_UINT16);
  e |= DIO_PUT(arraylen, &dout, &field_addr, {size.actual @ packet});

#ifdef FREECIV_JSON_CONNECTION
"""
        else:
            size_part = f"""\
#ifdef FREECIV_JSON_CONNECTION
  /* Create the array. */
  e |= DIO_PUT(farray, &dout, &field_addr, {size.actual @ packet});

"""

        null_condition = self.null_condition(location)
        if null_condition is None:
            head = ""
        elif send_size:
            # ends mid-line
            head = f"""\
if ({null_condition @ packet}) {{
  /* Transmit null as empty */
  e |= DIO_PUT(arraylen, &dout, &field_addr, 0);
}} else """
        else:
            # ends mid-line
            head = f"""\
if ({null_condition @ packet}) {{
  /* Transmit null as empty */
#ifdef FREECIV_JSON_CONNECTION
  /* Create the array. */
  e |= DIO_PUT(farray, &dout, &field_addr, 0);
#endif /* FREECIV_JSON_CONNECTION */
}} else """

        return f"""\
{head}{{
  int {location.index};

{size_part}\
  /* Enter array. */
  {location.json_subloc} = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

  for ({location.index} = 0; {location.index} < {size.actual @ packet}; {location.index}++) {{
#ifdef FREECIV_JSON_CONNECTION
    /* Next array element. */
    {location.json_subloc}->number = {location.index};
#endif /* FREECIV_JSON_CONNECTION */

{inner_put}\
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Exit array. */
  FC_FREE({location.json_subloc});
#endif /* FREECIV_JSON_CONNECTION */
}}
"""

    def get_code_put_diff(self, location: Location, packet: str, diff_packet: str) -> str:
        """Generate array-diff put code."""
        size = self.size_at(location)

        null_condition = self.null_condition(location)
        if null_condition is not None:
            safe_size = SizeInfo(size.declared @ None, size.actual.replace(f"({null_condition} ? 0 : {size.actual})"))
        else:
            safe_size = size

        if self.resize(location, packet, "s"):
            # starts and ends inside JSON ifdef
            size_head = f"""\

  /* Create the object to hold new size and delta. */
  e |= DIO_PUT(object, &dout, &field_addr);

  /* Enter object (start at size address). */
  {location.json_subloc} = plocation_field_new("size");
#endif /* FREECIV_JSON_CONNECTION */

  /* Write the new size */
  e |= DIO_PUT(uint16, &dout, &field_addr, {safe_size.actual @ packet});

#ifdef FREECIV_JSON_CONNECTION
  /* Delta address. */
  {location.json_subloc}->name = "delta";
"""
            # starts and ends inside JSON ifdef
            size_tail = f"""\
  /* Exit object. */
  FC_FREE({location.json_subloc});
"""
            # everything else is nested one level deeper than usual
            location.json_depth += 1
        else:
            size_head = size_tail = ""

        # Note: At the moment, we're only deep-diffing our elements
        # if our array size is constant
        value_put = prefix("    ", self.inner_put(location, packet, diff_packet if size.constant else None, 2))
        inner_cmp = prefix("    ", self.inner_cmp(location, packet, diff_packet))
        index_put = prefix("    ", size.index_put(packet, location.index))
        index_put_sentinel = prefix("  ", safe_size.index_put(packet, safe_size.actual @ packet))
        size_check = prefix("  ", safe_size.size_check_index(location.name, packet))

        if not size.constant:
            inner_cmp = f"""\
    if ({location.index} < {size.actual @ diff_packet}) {{
{prefix("  ", inner_cmp)}\
    }} else {{
      /* Always transmit new elements */
      differ = TRUE;
    }}
"""

        return f"""\
{{
  int {location.index};

{size_check}\

#ifdef FREECIV_JSON_CONNECTION
  size_t count_{location.index} = 0;
{size_head}\

  /* Create the array. */
  e |= DIO_PUT(farray, &dout, &field_addr, 0);

  /* Enter array. */
  {location.json_subloc} = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

  for ({location.index} = 0; {location.index} < {safe_size.actual @ packet}; {location.index}++) {{
{inner_cmp}\

    if (!differ) {{
      continue;
    }}

#ifdef FREECIV_JSON_CONNECTION
    /* Append next diff array element. */
    {location.json_subloc}->number = -1;

    /* Create the diff array element. */
    e |= DIO_PUT(object, &dout, &field_addr);

    /* Enter diff array element (start at the index address). */
    {location.json_subloc}->number = count_{location.index}++;
    {location.json_subloc}->sub_location = plocation_field_new("index");
#endif /* FREECIV_JSON_CONNECTION */

    /* Write the index */
{index_put}\

#ifdef FREECIV_JSON_CONNECTION
    /* Content address. */
    {location.json_subloc}->sub_location->name = "data";
#endif /* FREECIV_JSON_CONNECTION */

{value_put}\

#ifdef FREECIV_JSON_CONNECTION
    /* Exit diff array element. */
    FC_FREE({location.json_subloc}->sub_location);
#endif /* FREECIV_JSON_CONNECTION */
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Append diff array element. */
  {location.json_subloc}->number = -1;

  /* Create the terminating diff array element. */
  e |= DIO_PUT(object, &dout, &field_addr);

  /* Enter diff array element (start at the index address). */
  {location.json_subloc}->number = count_{location.index};
  {location.json_subloc}->sub_location = plocation_field_new("index");
#endif /* FREECIV_JSON_CONNECTION */

  /* Write the sentinel value */
{index_put_sentinel}\

#ifdef FREECIV_JSON_CONNECTION
  /* Exit diff array element. */
  FC_FREE({location.json_subloc}->sub_location);
  /* Exit array. */
  FC_FREE({location.json_subloc});
{size_tail}\
#endif /* FREECIV_JSON_CONNECTION */
}}
"""

    def get_code_put(self, location: Location, packet: str, diff_packet: "str | None" = None) -> str:
        if diff_packet is not None:
            return self.get_code_put_diff(location, packet, diff_packet)
        else:
            return self.get_code_put_full(location, packet)

    @abstractmethod
    def inner_get(self, location:Location, packet: str,
                         deep_diff: bool = False, json_step: int = 1) -> str:
        """Generate the main part of the loop body for get_code_get()"""
        raise NotImplementedError()

    def get_code_get_full(self, location: Location, packet: str) -> str:
        """Generate get code without array-diff."""
        size = self.size_at(location)
        inner_get = prefix("    ", self.inner_get(location, packet))

        resize = self.resize(location, packet, location.index)
        if resize:
            size_part = f"""\
  if (!DIO_GET(arraylen, &din, &field_addr, &{location.index})) {{
    RECEIVE_PACKET_FIELD_ERROR({location.name});
  }}
{prefix("  ", resize)}\
"""
        else:
            size_part = prefix("  ", size.size_check_get(location.name, packet))

        return f"""\
{{
  int {location.index};

{size_part}\

#ifdef FREECIV_JSON_CONNECTION
  /* Enter array. */
  {location.json_subloc} = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

  for ({location.index} = 0; {location.index} < {size.actual @ packet}; {location.index}++) {{
#ifdef FREECIV_JSON_CONNECTION
    /* Next array element */
    {location.json_subloc}->number = {location.index};
#endif /* FREECIV_JSON_CONNECTION */

{inner_get}\
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Exit array. */
  FC_FREE({location.json_subloc});
#endif /* FREECIV_JSON_CONNECTION */
}}
"""

    def get_code_get_diff(self, location: Location, packet: str) -> str:
        """Generate array-diff get code."""
        size = self.size_at(location)

        resize = self.resize(location, packet, location.index)
        if resize:
            # ends inside a JSON ifdef
            size_head = f"""\
#ifdef FREECIV_JSON_CONNECTION
/* Enter object (start at size address). */
{location.json_subloc} = plocation_field_new("size");
#endif /* FREECIV_JSON_CONNECTION */

{{
  int readin;

  if (!DIO_GET(uint16, &din, &field_addr, &readin)) {{
    RECEIVE_PACKET_FIELD_ERROR({location.name});
  }}
{prefix("  ", resize)}\
}}

#ifdef FREECIV_JSON_CONNECTION
/* Delta address. */
{location.json_subloc}->name = "delta";
"""
            size_tail = f"""\
/* Exit diff object. */
FC_FREE({location.json_subloc});
"""
            location.json_depth += 1
        else:
            size_head = f"""\
{prefix("  ", size.size_check_get(location.name, packet))}\
#ifdef FREECIV_JSON_CONNECTION
"""
            size_tail = ""

        # Note: At the moment, we're only deep-diffing our elements
        # if our array size is constant
        value_get = prefix("  ", self.inner_get(location, packet, size.constant, 2))
        index_get = prefix("  ", size.index_get(packet, location))

        return f"""\
{size.size_check_index(location.name, packet)}\
{size_head}\
/* Enter array (start at initial element). */
{location.json_subloc} = plocation_elem_new(0);
/* Enter diff array element (start at the index address). */
{location.json_subloc}->sub_location = plocation_field_new("index");
#endif /* FREECIV_JSON_CONNECTION */

while (TRUE) {{
  int {location.index};

  /* Read next index */
{index_get}\

  if ({location.index} == {size.actual @ packet}) {{
    break;
  }}
  if ({location.index} > {size.actual @ packet}) {{
    RECEIVE_PACKET_FIELD_ERROR({location.name},
                               ": unexpected index %d "
                               "> length %d in array diff",
                               {location.index},
                               {size.actual @ packet});
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Content address. */
  {location.json_subloc}->sub_location->name = "data";
#endif /* FREECIV_JSON_CONNECTION */

{value_get}\

#ifdef FREECIV_JSON_CONNECTION
  /* Move to the next diff array element. */
  {location.json_subloc}->number++;
  /* Back to the index address. */
  {location.json_subloc}->sub_location->name = "index";
#endif /* FREECIV_JSON_CONNECTION */
}}

#ifdef FREECIV_JSON_CONNECTION
/* Exit diff array element. */
FC_FREE({location.json_subloc}->sub_location);
/* Exit array. */
FC_FREE({location.json_subloc});
{size_tail}\
#endif /* FREECIV_JSON_CONNECTION */
"""

    def get_code_get(self, location: Location, packet: str, deep_diff: bool = False) -> str:
        if deep_diff:
            return self.get_code_get_diff(location, packet)
        else:
            return self.get_code_get_full(location, packet)


class ArrayType(SequenceType):
    """Type information for an array field. Consists of size information and
    another FieldType for the array's elements, which may also be an
    ArrayType (for multi-dimensional arrays) or another SequenceType."""

    elem: FieldType
    """The type of the array elements"""

    size: SizeInfo
    """The length (maximum and actual) of the array"""

    def __init__(self, elem: FieldType, size: SizeInfo):
        self.elem = elem
        self.size = size

    @property
    def complex(self) -> bool:
        return self.elem.complex

    def get_code_declaration(self, location: Location) -> str:
        return self.elem.get_code_declaration(
            location.deeper(f"{location}[{self.size.declared}]")
        )

    def get_code_param(self, location: Location) -> str:
        # When changing this, update SizedType.get_code_param() accordingly

        # Note: If we're fine with writing `foo_t const *fieldname`,
        # we'd only need one case, .deeper(f"const *{location}")
        if not location.depth:
            # foo_t fieldname ~> const foo_t *fieldname
            return "const " + self.elem.get_code_param(location.deeper(f"*{location}"))
        else:
            # const foo_t *fieldname ~> const foo_t *const *fieldname
            # the final * is already part of the location
            return self.elem.get_code_param(location.deeper(f"*const {location}"))

    def get_code_init(self, location: Location, packet: str) -> str:
        if not self.complex:
            return super().get_code_init(location, packet)
        inner_init = prefix("    ", self.elem.get_code_init(location.sub, packet))
        # Note: we're initializing and destroying *all* elements of the array,
        # not just those up to the actual size; otherwise we'd have to
        # dynamically initialize and destroy elements as the actual size changes
        return f"""\
{{
  int {location.index};

  for ({location.index} = 0; {location.index} < {self.size.declared}; {location.index}++) {{
{inner_init}\
  }}
}}
"""

    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        # can't use direct assignment to bit-copy a raw array,
        # even if our type is not complex
        inner_copy = prefix("    ", self.elem.get_code_copy(location.sub, dest, src))
        return f"""\
{{
  int {location.index};

  for ({location.index} = 0; {location.index} < {self.size.actual @ src}; {location.index}++) {{
{inner_copy}\
  }}
}}
"""

    def get_code_fill(self, location: Location, packet: str) -> str:
        inner_fill = prefix("    ", self.elem.get_code_fill(location.sub, packet))
        return f"""\
{{
  int {location.index};

  for ({location.index} = 0; {location.index} < {self.size.actual @ packet}; {location.index}++) {{
{inner_fill}\
  }}
}}
"""

    def get_code_free(self, location: Location, packet: str) -> str:
        if not self.complex:
            return super().get_code_free(location, packet)
        inner_free = prefix("    ", self.elem.get_code_free(location.sub, packet))
        # Note: we're initializing and destroying *all* elements of the array,
        # not just those up to the actual size; otherwise we'd have to
        # dynamically initialize and destroy elements as the actual size changes
        return f"""\
{{
  int {location.index};

  for ({location.index} = 0; {location.index} < {self.size.declared}; {location.index}++) {{
{inner_free}\
  }}
}}
"""

    def get_code_hash(self, location: Location) -> str:
        raise ValueError(f"hash not supported for array type {self} in field {location.name}")

    def size_at(self, location: Location) -> SizeInfo:
        return self.size

    def inner_cmp(self, location: Location, new: str, old: str) -> str:
        return self.elem.get_code_cmp(location.sub, new, old)

    def inner_put(self, location: Location, packet: str,
                         diff_packet: "str | None" = None, json_step: int = 1) -> str:
        return self.elem.get_code_put(location.sub_full(json_step), packet, diff_packet)

    def inner_get(self, location:Location, packet: str,
                         deep_diff: bool = False, json_step: int = 1) -> str:
        return self.elem.get_code_get(location.sub_full(json_step), packet, deep_diff)

    def __str__(self) -> str:
        return f"{self.elem}[{self.size}]"


# order matters: we want SequenceType methods to override StructType methods
class SpecvecType(SequenceType, StructType):
    """Type information for a specialized vector field"""

    elem: FieldType
    """The type of the vector elements"""

    complex: bool = True

    def __init__(self, elem: StructType):
        if elem.complex:
            raise ValueError("vectors with complex fields are not supported")
        super().__init__(elem.dataio_type, f"struct {elem.struct_type}_vector")
        self.elem = elem

    def get_code_init(self, location: Location, packet: str) -> str:
        return f"""\
{self.struct_type}_init(&{location @ packet});
"""

    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        return f"""\
{self.struct_type}_copy(&{location @ dest}, &{location @ src});
"""

    def get_code_free(self, location: Location, packet: str) -> str:
        return f"""\
{self.struct_type}_free(&{location @ packet});
"""

    def _sub(self, location: Location, json_step: int = 1) -> Location:
        ## return location.deeper(f"(*{self.struct_type}_get(&{location}, {location.index}))")
        return location.deeper(f"{location}.p[{location.index}]", json_step)

    @cache
    def size_at(self, location: Location) -> SizeInfo:
        return SizeInfo("GENERATE_PACKETS_ERROR", location.replace(f"{self.struct_type}_size(&{location})"))

    def resize(self, location: Location, packet: str, new_size: str) -> str:
        # if elem is complex, adjusting vector size takes extra work
        # not currently supported; enforced in self.__init__()
        assert not self.elem.complex

        return f"""\
{self.struct_type}_reserve(&{location @ packet}, {new_size});
"""

    def inner_cmp(self, location: Location, new: str, old: str) -> str:
        return self.elem.get_code_cmp(self._sub(location), new, old)

    def inner_put(self, location: Location, packet: str,
                         diff_packet: "str | None" = None, json_step: int = 1) -> str:
        return self.elem.get_code_put(self._sub(location, json_step), packet, diff_packet)

    def inner_get(self, location:Location, packet: str,
                         deep_diff: bool = False, json_step: int = 1) -> str:
        return self.elem.get_code_get(self._sub(location, json_step), packet, deep_diff)

    def __str__(self) -> str:
        return f"{self.elem}[*]"


class StrvecType(SequenceType):
    """Type information for a string vector field"""

    dataio_type: str
    """How fields of this type are transmitted over network"""

    public_type: str
    """How fields of this type are represented in C code"""

    complex: bool = True

    def __init__(self, dataio_type: str, public_type: str):
        if dataio_type not in ("string", "estring"):
            raise ValueError("not a valid strvec type")

        if public_type != "struct strvec":
            raise ValueError(f"strvec type with illegal public type: {public_type!r}")

        self.dataio_type = dataio_type
        self.public_type = public_type

    def get_code_declaration(self, location: Location) -> str:
        return f"""\
{self.public_type} *{location @ None};
"""

    def get_code_param(self, location: Location) -> str:
        if not location.depth:
            return f"const {self.public_type} *{location @ None}"
        else:
            # const struct strvec *const *fieldname
            # the final * is already part of the location
            # initial const gets added from outside
            return f"{self.public_type} *const {location @ None}"

    def get_code_init(self, location: Location, packet: str) -> str:
        # we're always allocating our vectors, even if they're empty
        return f"""\
{location @ packet} = strvec_new();
"""

    def get_code_fill(self, location: Location, packet: str) -> str:
        """Generate a code snippet shallow-copying a value of this type from
        dsend arguments into a packet struct."""
        # safety: the packet's contents will not be modified without cloning
        # it first, so discarding 'const' qualifier here is safe
        return f"""\
{location @ packet} = (struct strvec *) {location @ None};
"""

    def get_code_copy(self, location: Location, dest: str, src: str) -> str:
        # dest is initialized by us ~> not null
        # src might be a packet passed in from outside ~> could be null
        return f"""\
if ({location @ src}) {{
  strvec_copy({location @ dest}, {location @ src});
}} else {{
  strvec_clear({location @ dest});
}}
"""

    def get_code_free(self, location: Location, packet: str) -> str:
        return f"""\
if ({location @ packet}) {{
  strvec_destroy({location @ packet});
  {location @ packet} = nullptr;
}}
"""

    def get_code_hash(self, location: Location) -> str:
        raise ValueError(f"hash not supported for strvec type {self} in field {location.name}")

    def get_code_cmp(self, location: Location, new: str, old: str) -> str:
        # "new" packet passed in from outside might have null vector
        return f"""\
if ({location @ new}) {{
  differ = !are_strvecs_equal({location @ old}, {location @ new});
}} else {{
  differ = (strvec_size({location @ old}) > 0);
}}
"""

    @staticmethod
    @cache
    def size_at(location: Location) -> SizeInfo:
        return SizeInfo("GENERATE_PACKETS_ERROR", location.replace(f"strvec_size({location})"))

    def resize(self, location: Location, packet: str, new_size: str) -> str:
        return f"""\
strvec_reserve({location @ packet}, {new_size});
"""

    def null_condition(self, location: Location) -> Location:
        return location.replace(f"!{location}")

    def inner_cmp(self, location: Location, new: str, old: str) -> str:
        return f"""\
const char *pstr = strvec_get({location @ new}, {location.index});
const char *pstr_old = strvec_get({location @ old}, {location.index});

differ = (strcmp(pstr_old ? pstr_old : "", pstr ? pstr : "") != 0);
"""

    def inner_put(self, location: Location, packet: str,
                         diff_packet: "str | None" = None, json_step: int = 1) -> str:
        return f"""\
{{
  const char *pstr = strvec_get({location @ packet}, {location.index});

  e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, pstr ? pstr : "");
}}
"""

    def inner_get(self, location:Location, packet: str,
                         deep_diff: bool = False, json_step: int = 1) -> str:
        return f"""\
{{
  char readin[MAX_LEN_PACKET];

  if (!DIO_GET({self.dataio_type}, &din, &field_addr, readin, sizeof(readin))
      || !strvec_set({location @ packet}, {location.index}, readin)) {{
    RECEIVE_PACKET_FIELD_ERROR({location.name});
  }}
}}
"""

    def __str__(self) -> str:
        return f"{self.dataio_type}({self.public_type})"


def string_type_ctor(dataio_type: str, public_type: str) -> RawFieldType:
    """Field type constructor for both strings and string vectors"""
    if dataio_type not in ("string", "estring"):
        raise ValueError(f"not a valid string type: {dataio_type}")

    if public_type == "char":
        return NeedSizeType(dataio_type, public_type, cls = StringType)
    elif public_type == "struct strvec":
        return StrvecType(dataio_type, public_type)
    else:
        raise ValueError(f"public type {public_type} not legal for dataio type {dataio_type}")

DEFAULT_REGISTRY.dataio_types["string"] = DEFAULT_REGISTRY.dataio_types["estring"] = string_type_ctor


class Field:
    """A single field of a packet. Consists of a name, type information
    (including array sizes) and flags."""

    FIELDS_LINE_PATTERN = re.compile(r"""
        ^\s*
        (       # field type; see also TypeRegistry.TYPE_INFO_PATTERN
            \w+     # alias or dataio type
            (?:     # optionally, public type (if this is not an alias)
                \(
                [^()]*
                \)
            )?
        )
        \s+
        (       # zero or more field declarations
            [^;()]*?
        )
        \s*;\s*
        (.*?)   # flags
        \s*$
    """, re.VERBOSE)
    """Matches an entire field definition line.

    Groups:
    - type
    - field names and array sizes
    - flags"""

    FIELD_ARRAY_PATTERN = re.compile(r"^(.+)\[([^][]+)\]$")
    """Matches a field definition with one or more array sizes

    Groups:
    - everything except the final array size
    - the final array size"""

    FORBIDDEN_NAMES = {"pid", "fields"}
    """Field names that are not allowed because they would conflict
    with the special fields used by the JSON protocol"""

    cfg: ScriptConfig
    """Configuration used when generating code for this field"""

    name: str
    """This field's name (identifier)"""

    type_info: FieldType
    """This field's type information; see FieldType"""

    flags: FieldFlags
    """This field's flags; see FieldFlags"""

    @classmethod
    def parse(cls, cfg: ScriptConfig, line: str, resolve_type: typing.Callable[[str], RawFieldType]) -> "typing.Iterable[Field]":
        """Parse a single line defining one or more fields"""
        mo = cls.FIELDS_LINE_PATTERN.fullmatch(line)
        if mo is None:
            raise ValueError(f"invalid field definition: {line!r}")
        type_text, fields, flags = (i.strip() for i in mo.groups(""))

        type_info = resolve_type(type_text)
        flag_info = FieldFlags.parse(flags)

        # analyze fields
        for field_text in fields.split(","):
            field_text = field_text.strip()
            field_type = type_info

            mo = cls.FIELD_ARRAY_PATTERN.fullmatch(field_text)
            while mo is not None:
                field_text = mo.group(1)
                field_type = field_type.array(SizeInfo.parse(mo.group(2)))
                mo = cls.FIELD_ARRAY_PATTERN.fullmatch(field_text)

            if not isinstance(field_type, FieldType):
                raise ValueError(f"need an array size to use type {field_type}")

            if field_text in cls.FORBIDDEN_NAMES:
                raise ValueError(f"illegal field name: {field_text}")

            yield cls(cfg, field_text, field_type, flag_info)

    def __init__(self, cfg: ScriptConfig, name: str, type_info: FieldType, flags: FieldFlags):
        self.cfg = cfg
        self.name = name
        self.type_info = type_info
        self.flags = flags

    @property
    def is_key(self) -> bool:
        """Whether this is a key field"""
        return self.flags.is_key

    @property
    def diff(self) -> bool:
        """Whether this field uses deep diff / array-diff when transmitted
        as part of a delta packet"""
        return self.flags.diff

    @property
    def all_caps(self) -> "typing.AbstractSet[str]":
        """Set of all capabilities affecting this field"""
        return self.flags.add_caps | self.flags.remove_caps

    @property
    def complex(self) -> bool:
        """Whether this field's type requires special handling;
        see FieldType.complex"""
        return self.type_info.complex

    def is_compatible(self, other: "Field") -> bool:
        """Whether two field objects are variants of the same field, i.e.
        type-compatible in the packet struct and mutually exclusive based
        on their required capabilities.

        Note that this function does not test field name."""
        return bool(
            (
                (self.flags.add_caps & other.flags.remove_caps)
            or
                (self.flags.remove_caps & other.flags.add_caps)
            )
        and
            self.type_info.is_type_compatible(other.type_info)
        )

    def present_with_caps(self, caps: typing.Container[str]) -> bool:
        """Determine whether this field should be part of a variant with the
        given capabilities"""
        return (
            all(cap in caps for cap in self.flags.add_caps)
        ) and (
            all(cap not in caps for cap in self.flags.remove_caps)
        )

    def get_declaration(self) -> str:
        """Generate the way this field is declared in the packet struct"""
        return self.type_info.get_code_declaration(Location(self.name))

    def get_param(self) -> str:
        """Generate the way this field is declared as a parameter of a
        handle or dsend function.

        See also self.get_handle_arg()"""
        return self.type_info.get_code_param(Location(self.name))

    def get_handle_arg(self, packet: str) -> str:
        """Generate the way this field is passed as an argument to a handle
        function.

        See also self.get_handle_param()"""
        return self.type_info.get_code_handle_arg(Location(self.name), packet)

    def get_init(self) -> str:
        """Generate code initializing this field in the packet struct, after
        the struct has already been zeroed."""
        return self.type_info.get_code_init(Location(self.name), "packet")

    def get_copy(self, dest: str, src: str) -> str:
        """Generate code deep-copying this field from *src to *dest."""
        return self.type_info.get_code_copy(Location(self.name), dest, src)

    def get_fill(self) -> str:
        """Generate code shallow-copying this field from the dsend arguments
        into the packet struct."""
        return self.type_info.get_code_fill(Location(self.name), "real_packet")

    def get_free(self) -> str:
        """Generate code deinitializing this field in the packet struct
        before destroying the packet."""
        return self.type_info.get_code_free(Location(self.name), "packet")

    def get_hash(self) -> str:
        """Generate code factoring this field into a hash computation."""
        assert self.is_key
        return self.type_info.get_code_hash(Location(self.name), "key")

    @property
    def folded_into_head(self) -> bool:
        """Whether this field is folded into the packet header.

        If enabled, lone booleans (which only carry one bit of information)
        get directly written into the `fields` bitvector, since they don't
        take any more space than the usual "content-differs" bit would.

        See also get_cmp_wrapper()"""
        return (
            self.cfg.fold_bool
            and self.type_info.foldable
        )

    def get_cmp_wrapper(self, i: int, pack: "Variant") -> str:
        """Generate code setting this field's bit in the `fields` bitvector.

        This bit marks whether the field changed and is being transmitted,
        except for (non-array) boolean fields folded into the header;
        see self.folded_into_head for more details.

        See also self.get_cmp()"""
        if self.folded_into_head:
            info_part = f"""\
{self.get_cmp()}\
if (differ) {{
  different++;
}}
""" if pack.is_info != "no" else ""

            return f"""\
{info_part}\
/* folded into head */
if (real_packet->{self.name}) {{
  BV_SET(fields, {i:d});
}}
"""
        else:
            info_part = f"""\
  different++;
""" if pack.is_info != "no" else ""

            return f"""\
{self.get_cmp()}\
if (differ) {{
{info_part}\
  BV_SET(fields, {i:d});
}}
"""

    def get_cmp(self) -> str:
        """Generate code checking whether this field changed.

        This code is primarily used by self.get_cmp_wrapper()"""
        return self.type_info.get_code_cmp(Location(self.name), "real_packet", "old")

    def get_put_wrapper(self, packet: "Variant", index: int, deltafragment: bool) -> str:
        """Generate code conditionally putting this field iff its bit in the
        `fields` bitvector is set.

        Does nothing for boolean fields folded into the packet header.

        See also self.get_put()"""
        if self.folded_into_head:
            return f"""\
/* field {index:d} is folded into the header */
"""

        log = f"""\
  {packet.log_macro}("  field '{self.name}' has changed");

""" if packet.gen_log else ""

        stats = f"""\
  stats_{packet.name}_counters[{index:d}]++;

""" if packet.gen_stats else ""

        put = prefix("  ", self.get_put(deltafragment))

        return f"""\
if (BV_ISSET(fields, {index:d})) {{
{log}\
{stats}\
{put}\
}}
"""

    def get_put(self, deltafragment: bool) -> str:
        """Generate the code putting this field, i.e. writing it to the
        dataio stream.

        This does not include delta-related code checking whether to
        transmit the field in the first place; see self.get_put_wrapper()"""
        return f"""\
#ifdef FREECIV_JSON_CONNECTION
field_addr.name = "{self.name}";
#endif /* FREECIV_JSON_CONNECTION */
e = 0;

{self.get_put_real(deltafragment)}\

if (e) {{
  log_packet_detailed("'{self.name}' field error detected");
}}
"""

    def get_put_real(self, deltafragment: bool) -> str:
        """Generate the bare core of this field's put code. This code is not
        yet wrapped for full delta and JSON protocol support.

        See self.get_put() for more info"""
        return self.type_info.get_code_put(Location(self.name), "real_packet", "old" if deltafragment and self.diff else None)

    def get_get_wrapper(self, packet: "Variant", i: int, deltafragment: bool) -> str:
        """Generate code conditionally getting this field iff its bit in the
        `fields` bitvector is set.

        For boolean fields folded into the packet header, instead reads the
        field from the bitvector.

        See also self.get_get()"""
        if self.folded_into_head:
            return  f"""\
real_packet->{self.name} = BV_ISSET(fields, {i:d});
"""

        get = prefix("  ", self.get_get(deltafragment))
        log = f"""\
  {packet.log_macro}("  got field '{self.name}'");

""" if packet.gen_log else ""

        return f"""\
if (BV_ISSET(fields, {i:d})) {{
{log}\
{get}\
}}
"""

    def get_get(self, deltafragment: bool) -> str:
        """Generate the code getting this field, i.e. reading it from the
        dataio stream.

        This does not include delta-related code checking if the field
        was transmitted in the first place; see self.get_get_wrapper()"""
        return f"""\
#ifdef FREECIV_JSON_CONNECTION
field_addr.name = "{self.name}";
#endif /* FREECIV_JSON_CONNECTION */

{self.get_get_real(deltafragment)}\
"""

    def get_get_real(self, deltafragment: bool) -> str:
        """Generate the bare core of this field's get code. This code is not
        yet wrapped for full delta and JSON protocol support.

        See self.get_get() for more info"""
        return self.type_info.get_code_get(Location(self.name), "real_packet", deltafragment and self.diff)


class Variant:
    """Represents one variant of a packet. Packets with add-cap or
    remove-cap fields have different variants for different combinations of
    the relevant optional capabilities."""

    packet: "Packet"
    """The packet this is a variant of"""

    var_number: int
    """The numeric variant ID (not packet ID) of this variant"""

    name: str
    """The full name of this variant"""

    poscaps: typing.AbstractSet[str]
    """The set of optional capabilities that must be present to use this
    variant"""

    negcaps: typing.AbstractSet[str]
    """The set of optional capabilities that must *not* be present to
    use this variant"""

    fields: typing.Sequence[Field]
    """All fields that are transmitted when using this variant"""

    key_fields: typing.Sequence[Field]
    """The key fields that are used for this variant"""

    other_fields: typing.Sequence[Field]
    """The non-key fields that are transmitted when using this variant"""

    keys_format: str
    """The printf format string for this variant's key fields in
    generated log calls

    See also self.keys_arg"""

    keys_arg: str
    """The arguments passed when formatting this variant's key fields in
    generated log calls

    See also self.keys_format"""

    def __init__(self, poscaps: typing.Iterable[str], negcaps: typing.Iterable[str],
                       packet: "Packet", var_number: int):
        self.packet = packet
        self.var_number = var_number
        self.name = f"{packet.name}_{var_number:d}"

        self.poscaps = set(poscaps)
        self.negcaps = set(negcaps)
        self.fields = [
            field
            for field in packet.all_fields
            if field.present_with_caps(self.poscaps)
        ]
        self.key_fields = [field for field in self.fields if field.is_key]
        self.other_fields = [field for field in self.fields if not field.is_key]
        # FIXME: Doesn't work with non-int key fields
        self.keys_format = ", ".join(["%d"] * len(self.key_fields))
        self.keys_arg = ", ".join("real_packet->" + field.name for field in self.key_fields)
        if self.keys_arg:
            self.keys_arg = ",\n    " + self.keys_arg

        if not self.fields and packet.fields:
            raise ValueError(f"empty variant for nonempty {self.packet_name} with capabilities {self.poscaps}")

    @property
    def cfg(self) -> ScriptConfig:
        """Configuration used when generating code for this packet
        variant

        See self.packet and Packet.cfg"""
        return self.packet.cfg

    @property
    def gen_stats(self) -> bool:
        """Whether to generate delta stats code for this packet variant

        See self.cfg and ScriptConfig.gen_stats"""
        return self.cfg.gen_stats

    @property
    def log_macro(self) -> "str | None":
        """The log macro used to generate log calls for this packet variant,
        or None if no log calls should be generated

        See self.cfg and ScriptConfig.log_macro"""
        return self.cfg.log_macro

    @property
    def gen_log(self) -> bool:
        """Whether to generate log calls for this packet variant

        See self.log_macro"""
        return self.log_macro is not None

    @property
    def packet_name(self) -> str:
        """Name of the packet this is a variant of

        See Packet.name"""
        return self.packet.name

    @property
    def type(self) -> str:
        """Type (enum constant) of the packet this is a variant of

        See Packet.type"""
        return self.packet.type

    @property
    def no_packet(self) -> bool:
        """Whether the send function should not take/need a packet struct

        See Packet.no_packet"""
        return self.packet.no_packet

    @property
    def delta(self) -> bool:
        """Whether this packet can use delta optimization

        See Packet.delta"""
        return self.packet.delta

    @property
    def want_force(self):
        """Whether send function takes a force_to_send boolean

        See Packet.want_force"""
        return self.packet.want_force

    @property
    def is_info(self) -> str:
        """Whether this is an info or game-info packet"""
        return self.packet.is_info

    @property
    def reset_packets(self) -> "list[str]":
        """List of packets to reset when sending or receiving this packet

        See Packet.reset_packets"""
        return self.packet.reset_packets

    @property
    def complex(self) -> bool:
        """Whether this packet's struct requires special handling for
        initialization, copying, and destruction.

        Note that this is still True even if the complex-typed fields
        of the packet are excluded from this Variant."""
        return self.packet.complex

    @property
    def differ_used(self) -> bool:
        """Whether the send function needs a `differ` boolean.

        See get_send()"""
        return (
            self.delta
            and (
                self.is_info != "no"
                or any(
                    not field.folded_into_head
                    for field in self.other_fields
                )
            )
        )

    @property
    def condition(self) -> str:
        """The condition determining whether this variant should be used,
        based on capabilities.

        See get_packet_handlers_fill_capability()"""
        if self.poscaps or self.negcaps:
            return " && ".join(chain(
                (f"has_capability(\"{cap}\", capability)" for cap in sorted(self.poscaps)),
                (f"!has_capability(\"{cap}\", capability)" for cap in sorted(self.negcaps)),
            ))
        else:
            return "TRUE"

    @property
    def bits(self) -> int:
        """The length of the bitvector for this variant."""
        return len(self.other_fields)

    @property
    def receive_prototype(self) -> str:
        """The prototype of this variant's receive function"""
        return f"static struct {self.packet_name} *receive_{self.name}(struct connection *pc)"

    @property
    def send_prototype(self) -> str:
        """The prototype of this variant's send function"""
        return f"static int send_{self.name}(struct connection *pc{self.packet.send_params})"

    @property
    def fill_send_handler(self) -> str:
        """Code to set the send handler for this variant

        See get_packet_handlers_fill_initial and
        get_packet_handlers_fill_capability"""
        if self.no_packet:
            return f"""\
phandlers->send[{self.type}].no_packet = (int(*)(struct connection *)) send_{self.name};
"""
        elif self.want_force:
            return f"""\
phandlers->send[{self.type}].force_to_send = (int(*)(struct connection *, const void *, bool)) send_{self.name};
"""
        else:
            return f"""\
phandlers->send[{self.type}].packet = (int(*)(struct connection *, const void *)) send_{self.name};
"""

    @property
    def fill_receive_handler(self) -> str:
        """Code to set the receive handler for this variant

        See get_packet_handlers_fill_initial and
        get_packet_handlers_fill_capability"""
        return f"""\
phandlers->receive[{self.type}] = (void *(*)(struct connection *)) receive_{self.name};
"""

    def get_copy(self, dest: str, src: str) -> str:
        """Generate code deep-copying the fields relevant to this variant
        from *src to *dest"""
        if not self.complex:
            return f"""\
*{dest} = *{src};
"""
        return "".join(
            field.get_copy(dest, src)
            for field in self.fields
        )

    def get_stats(self) -> str:
        """Generate the declaration of the delta stats counters associated
        with this packet variant"""
        names = ", ".join(
            f"\"{field.name}\""
            for field in self.other_fields
        )

        return f"""\
static int stats_{self.name}_sent;
static int stats_{self.name}_discarded;
static int stats_{self.name}_counters[{self.bits:d}];
static const char *stats_{self.name}_names[] = {{{names}}};

"""

    def get_bitvector(self) -> str:
        """Generate the declaration of the fields bitvector type for this
        packet variant"""
        return f"""\
BV_DEFINE({self.name}_fields, {self.bits});
"""

    def get_report_part(self) -> str:
        """Generate the part of the delta_stats_report() function specific
        to this packet variant"""
        return f"""\

if (stats_{self.name}_sent > 0
    && stats_{self.name}_discarded != stats_{self.name}_sent) {{
  log_test("{self.name} %d out of %d got discarded",
    stats_{self.name}_discarded, stats_{self.name}_sent);
  for (i = 0; i < {self.bits}; i++) {{
    if (stats_{self.name}_counters[i] > 0) {{
      log_test("  %4d / %4d: %2d = %s",
        stats_{self.name}_counters[i],
        (stats_{self.name}_sent - stats_{self.name}_discarded),
        i, stats_{self.name}_names[i]);
    }}
  }}
}}
"""

    def get_reset_part(self) -> str:
        """Generate the part of the delta_stats_reset() function specific
        to this packet variant"""
        return f"""\
stats_{self.name}_sent = 0;
stats_{self.name}_discarded = 0;
memset(stats_{self.name}_counters, 0,
       sizeof(stats_{self.name}_counters));
"""

    def get_hash(self) -> str:
        """Generate the key hash function for this variant"""
        if not self.key_fields:
            return f"""\
#define hash_{self.name} hash_const
"""

        intro = f"""\
static genhash_val_t hash_{self.name}(const void *vkey)
{{
  const struct {self.packet_name} *key = (const struct {self.packet_name} *) vkey;
  genhash_val_t result = 0;

"""

        body = f"""\

  result *= 5;

""".join(prefix("  ", field.get_hash()) for field in self.key_fields)

        extro = f"""\

  result &= 0xFFFFFFFF;
  return result;
}}

"""

        return intro + body + extro

    def get_cmp(self) -> str:
        """Generate the key comparison function for this variant"""
        if not self.key_fields:
            return f"""\
#define cmp_{self.name} cmp_const
"""

        # note: the names `old` and `real_packet` allow reusing
        # field-specific cmp code
        intro = f"""\
static bool cmp_{self.name}(const void *vkey1, const void *vkey2)
{{
  const struct {self.packet_name} *old = (const struct {self.packet_name} *) vkey1;
  const struct {self.packet_name} *real_packet = (const struct {self.packet_name} *) vkey2;
  bool differ;

"""

        body = f"""\

  if (differ) {{
    return !differ;
  }}

""".join(prefix("  ", field.get_cmp()) for field in self.key_fields)

        extro = f"""\

  return !differ;
}}
"""

        return intro + body + extro

    def get_send(self) -> str:
        """Generate the send function for this packet variant"""
        if self.no_packet:
            # empty packet, don't need anything
            main_header = after_header = before_return = ""
        elif not self.packet.want_pre_send:
            # no pre-send, don't need to copy the packet
            main_header = f"""\
  const struct {self.packet_name} *real_packet = packet;
  int e;
"""
            after_header = before_return = ""
        elif not self.complex:
            # bit-copy the packet, no cleanup needed
            main_header = f"""\
  /* copy packet for pre-send */
  struct {self.packet_name} packet_buf = *packet;
  const struct {self.packet_name} *real_packet = &packet_buf;
  int e;
"""
            after_header = before_return = ""
        else:
            # deep-copy the packet for pre-send, have to destroy the copy
            # Note: SEND_PACKET_START has both declarations and statements,
            # so we have to break the pre-send copying up across either side
            copy = prefix("  ", self.get_copy("(&packet_buf)", "packet"))
            main_header = f"""\
  /* buffer to hold packet copy for pre-send */
  struct {self.packet_name} packet_buf;
  const struct {self.packet_name} *real_packet = &packet_buf;
  int e;
"""
            after_header = f"""\
  init_{self.packet_name}(&packet_buf);
{copy}\
"""
            before_return = f"""\
  free_{self.packet_name}(&packet_buf);
"""

        report = f"""\

  stats_total_sent++;
  stats_{self.name}_sent++;
""" if self.gen_stats else ""

        log_key = f"""\

  {self.log_macro}("{self.name}: sending info about ({self.keys_format})"{self.keys_arg});
""" if self.gen_log else ""

        if not self.packet.want_pre_send:
            pre_send = ""
        elif self.no_packet:
            pre_send = f"""\

  pre_send_{self.packet_name}(pc, nullptr);
"""
        else:
            pre_send = f"""\

  pre_send_{self.packet_name}(pc, &packet_buf);
"""

        delta_header = "\n" + prefix("  ", self.get_delta_send_header(before_return)) if self.delta else ""

        init_field_addr = f"""\

#ifdef FREECIV_JSON_CONNECTION
  struct plocation field_addr;
  {{
    struct plocation *field_addr_tmp = plocation_field_new(nullptr);
    field_addr = *field_addr_tmp;
    FC_FREE(field_addr_tmp);
  }}
#endif /* FREECIV_JSON_CONNECTION */

""" if self.fields else ""

        put_key = "".join(
            prefix("  ", field.get_put(False)) + "\n"
            for field in self.key_fields
        )

        nondelta_body = "\n".join(
            prefix("  ", field.get_put(False))
            for field in self.other_fields
        )

        if self.delta:
            delta_body = prefix("  ", self.get_delta_send_body())
            body = f"""\
#ifdef FREECIV_DELTA_PROTOCOL
{delta_body}\

#else /* FREECIV_DELTA_PROTOCOL */
{nondelta_body}\
#endif /* FREECIV_DELTA_PROTOCOL */
"""
        else:
            body = nondelta_body

        if not self.packet.want_post_send:
            post_send = ""
        elif self.no_packet:
            post_send = f"""\
  post_send_{self.packet_name}(pc, nullptr);
"""
        else:
            post_send = f"""\
  post_send_{self.packet_name}(pc, real_packet);
"""

        return f"""\
{self.send_prototype}
{{
{main_header}\
  SEND_PACKET_START({self.type});
{after_header}\
{log_key}\
{report}\
{pre_send}\
{delta_header}\
{init_field_addr}\
{put_key}\
{body}\

{post_send}\
{before_return}\
  SEND_PACKET_END({self.type});
}}

"""

    def get_delta_send_header(self, before_return: str = "") -> str:
        """Helper for get_send(). Generate the part of the send function
        that determined which fields differ between the real packet and
        the last cached packet, and possibly discards is-info packets."""
        declare_differ = f"""\
bool differ;
""" if self.differ_used else ""

        if self.is_info == "no":
            declare_different = ""
        elif self.want_force:
            declare_different = f"""\
int different = force_to_send;
"""
        else:
            declare_different = f"""\
int different = 0;
"""

        force_info = """\
  different = 1;      /* Force to send. */
""" if self.is_info != "no" else ""

        cmp_part = "\n".join(
            field.get_cmp_wrapper(i, self)
            for i, field in enumerate(self.other_fields)
        )

        if self.is_info != "no":
            log_discard = f"""\
  {self.log_macro}("  no change -> discard");
""" if self.gen_log else ""

            stats_discard = f"""\
  stats_{self.name}_discarded++;
""" if self.gen_stats else ""

            discard_part = f"""\

if (different == 0) {{
{log_discard}\
{stats_discard}\
{before_return}\
  SEND_PACKET_DISCARD();
}}
"""
        else:
            discard_part = ""

        return f"""\
#ifdef FREECIV_DELTA_PROTOCOL
{self.name}_fields fields;
struct {self.packet_name} *old;
{declare_differ}\
{declare_different}\
struct genhash **hash = pc->phs.sent + {self.type};

if (nullptr == *hash) {{
  *hash = genhash_new_full(hash_{self.name}, cmp_{self.name},
                           nullptr, nullptr, nullptr, destroy_{self.packet_name});
}}
BV_CLR_ALL(fields);

if (!genhash_lookup(*hash, real_packet, (void **) &old)) {{
  old = fc_malloc(sizeof(*old));
  /* temporary bitcopy just to insert correctly */
  *old = *real_packet;
  genhash_insert(*hash, old, old);
  init_{self.packet_name}(old);
{force_info}\
}}

{cmp_part}\
{discard_part}\
#endif /* FREECIV_DELTA_PROTOCOL */
"""

    def get_delta_send_body(self) -> str:
        """Helper for get_send(). Generate the part of the send function
        that transmits the delta between the real packet and the last
        cached packet.

        See also get_delta_send_header()"""
        body = "\n".join(
            field.get_put_wrapper(self, i, True)
            for i, field in enumerate(self.other_fields)
        )

        copy_to_old = self.get_copy("old", "real_packet")

        # Reset some packets' delta state
        reset_part = "".join(
            f"""\

hash = pc->phs.sent + {reset_packet};
if (nullptr != *hash) {{
  genhash_remove(*hash, real_packet);
}}
"""
            for reset_packet in self.reset_packets
        )

        return f"""\
#ifdef FREECIV_JSON_CONNECTION
field_addr.name = "fields";
#endif /* FREECIV_JSON_CONNECTION */
e = 0;
e |= DIO_BV_PUT(&dout, &field_addr, fields);
if (e) {{
  log_packet_detailed("fields bitvector error detected");
}}

{body}\

{copy_to_old}\
{reset_part}\
"""

    def get_receive(self) -> str:
        """Generate the receive function for this packet variant"""
        init_field_addr = f"""\

#ifdef FREECIV_JSON_CONNECTION
  struct plocation field_addr;
  {{
    struct plocation *field_addr_tmp = plocation_field_new(nullptr);
    field_addr = *field_addr_tmp;
    FC_FREE(field_addr_tmp);
  }}
#endif /* FREECIV_JSON_CONNECTION */
""" if self.fields else ""

        get_key = "".join(
            prefix("  ", field.get_get(False)) + "\n"
            for field in self.key_fields
        )

        log_key = f"""\
  {self.log_macro}("{self.name}: got info about ({self.keys_format})"{self.keys_arg});

""" if self.gen_log else ""

        nondelta_body = "\n".join(
            prefix("  ", field.get_get(False))
            for field in self.other_fields
        ) or f"""\
  real_packet->__dummy = 0xff;
"""

        if self.delta:
            delta_body = prefix("  ", self.get_delta_receive_body())
            get_body = f"""\
#ifdef FREECIV_DELTA_PROTOCOL
{delta_body}\

#else /* FREECIV_DELTA_PROTOCOL */
{nondelta_body}\
#endif /* FREECIV_DELTA_PROTOCOL */
"""
        else:
            get_body = nondelta_body

        post_receive = f"""\
  post_receive_{self.packet_name}(pc, real_packet);
""" if self.packet.want_post_recv else ""

        return f"""\
{self.receive_prototype}
{{
#define FREE_PACKET_STRUCT(_packet) free_{self.packet_name}(_packet)
  RECEIVE_PACKET_START({self.packet_name}, real_packet);
{init_field_addr}\

{get_key}\
{log_key}\
{get_body}\

{post_receive}\
  RECEIVE_PACKET_END(real_packet);
#undef FREE_PACKET_STRUCT
}}

"""

    def get_delta_receive_body(self) -> str:
        """Helper for get_receive(). Generate the part of the receive
        function responsible for recreating the full packet from the
        received delta and the last cached packet.

        Note: This code fragment declares variables. To comply with
        CodingStyle, it should be enclosed in a block {} or #ifdef."""
        log_no_old = f"""\
  {self.log_macro}("  no old info");
""" if self.gen_log else ""

        copy_from_old = prefix("  ", self.get_copy("real_packet", "old"))
        copy_to_old = prefix("  ", self.get_copy("old", "real_packet"))

        body = "\n".join(
            field.get_get_wrapper(self, i, True)
            for i, field in enumerate(self.other_fields)
        )

        # Reset some packets' delta state
        reset_part = "".join(
            f"""\

hash = pc->phs.received + {reset_packet};
if (nullptr != *hash) {{
  genhash_remove(*hash, real_packet);
}}
"""
            for reset_packet in self.reset_packets
        )

        return f"""\
{self.name}_fields fields;
struct {self.packet_name} *old;
struct genhash **hash = pc->phs.received + {self.type};

if (nullptr == *hash) {{
  *hash = genhash_new_full(hash_{self.name}, cmp_{self.name},
                           nullptr, nullptr, nullptr, destroy_{self.packet_name});
}}

if (genhash_lookup(*hash, real_packet, (void **) &old)) {{
{copy_from_old}\
}} else {{
  /* packet is already initialized empty */
{log_no_old}\
}}

#ifdef FREECIV_JSON_CONNECTION
field_addr.name = "fields";
#endif /* FREECIV_JSON_CONNECTION */
DIO_BV_GET(&din, &field_addr, fields);

{body}\

if (nullptr == old) {{
  old = fc_malloc(sizeof(*old));
  init_{self.packet_name}(old);
{copy_to_old}\
  genhash_insert(*hash, old, old);
}} else {{
{copy_to_old}\
}}
{reset_part}\
"""


class Directions(Enum):
    """Describes the possible combinations of directions for which a packet
    can be valid"""

    # Note: "sc" and "cs" are used to match the packet flags

    DOWN_ONLY = frozenset({"sc"})
    """Packet may only be sent from server to client"""

    UP_ONLY = frozenset({"cs"})
    """Packet may only be sent from client to server"""

    UNRESTRICTED = frozenset({"sc", "cs"})
    """Packet may be sent both ways"""

    @property
    def down(self) -> bool:
        """Whether a packet may be sent from server to client"""
        return "sc" in self.value

    @property
    def up(self) -> bool:
        """Whether a packet may be sent from client to server"""
        return "cs" in self.value


class Packet:
    """Represents a single packet type (possibly with multiple variants)"""

    RESET_PATTERN = re.compile(r"^reset\((.*)\)$")
    """Matches a reset flag

    Groups:
    - the packet type to reset"""

    cfg: ScriptConfig
    """Configuration used when generating code for this packet"""

    type: str
    """The packet type in allcaps (PACKET_FOO), as defined in the
    packet_type enum

    See also self.name"""

    type_number: int
    """The numeric ID of this packet type"""

    reset_packets: "list[str]"
    """List of packet types to drop from the cache when sending or
    receiving this packet type"""

    is_info: 'typing.Literal["no", "yes", "game"]' = "no"
    """Whether this is an is-info or is-game-info packet.
    "no" means normal, "yes" means is-info, "game" means is-game-info"""

    want_dsend: bool = False
    """Whether to generate a direct-send function taking field values
    instead of a packet struct"""

    want_lsend: bool = False
    """Whether to generate a list-send function sending a packet to
    multiple connections"""

    want_force: bool = False
    """Whether send functions should take a force_to_send parameter
    to override discarding is-info packets where nothing changed"""

    want_pre_send: bool = False
    """Whether a pre-send hook should be called when sending this packet"""

    want_post_send: bool = False
    """Whether a post-send hook should be called after sending this packet"""

    want_post_recv: bool = False
    """Whether a post-receive hook should be called when receiving this
    packet"""

    delta: bool = True
    """Whether to use delta optimization for this packet"""

    no_handle: bool = False
    """Whether this packet should *not* be handled normally"""

    handle_via_packet: bool = True
    """Whether to pass the entire packet (by reference) to the handle
    function (rather than each field individually)"""

    handle_per_conn: bool = False
    """Whether this packet's handle function should be called with the
    connection instead of the attached player"""

    dirs: Directions
    """Which directions this packet can be sent in"""

    all_fields: "list[Field]"
    """List of all fields of this packet, including name duplicates for
    different capability variants that are compatible.

    Only relevant for creating Variants; self.fields should be used when
    not dealing with capabilities or Variants."""

    fields: "list[Field]"
    """List of all fields of this packet, with only one field of each name"""

    variants: "list[Variant]"
    """List of all variants of this packet"""

    def __init__(self, cfg: ScriptConfig, packet_type: str, packet_number: int, flags_text: str,
                       lines: typing.Iterable[str], resolve_type: typing.Callable[[str], RawFieldType]):
        self.cfg = cfg
        self.type = packet_type
        self.type_number = packet_number

        self.reset_packets = []
        dirs: 'set[typing.Literal["sc", "cs"]]' = set()

        for flag in flags_text.split(","):
            flag = flag.strip()
            if not flag:
                continue

            if flag in ("sc", "cs"):
                dirs.add(flag)
                continue
            if flag == "is-info":
                self.is_info = "yes"
                continue
            if flag == "is-game-info":
                self.is_info = "game"
                continue
            if flag == "dsend":
                self.want_dsend = True
                continue
            if flag == "lsend":
                self.want_lsend = True
                continue
            if flag == "force":
                self.want_force = True
                continue
            if flag == "pre-send":
                self.want_pre_send = True
                continue
            if flag == "post-send":
                self.want_post_send = True
                continue
            if flag == "post-recv":
                self.want_post_recv = True
                continue
            if flag == "no-delta":
                self.delta = False
                continue
            if flag == "no-handle":
                self.no_handle = True
                continue
            if flag == "handle-via-fields":
                self.handle_via_packet = False
                continue
            if flag == "handle-per-conn":
                self.handle_per_conn = True
                continue

            mo = __class__.RESET_PATTERN.fullmatch(flag)
            if mo is not None:
                self.reset_packets.append(mo.group(1))
                continue

            raise ValueError(f"unrecognized flag for {self.type}: {flag!r}")

        if not dirs:
            raise ValueError(f"no directions defined for {self.type}")
        self.dirs = Directions(frozenset(dirs))

        raw_fields = [
            field
            for line in lines
            for field in Field.parse(self.cfg, line, resolve_type)
        ]
        # put key fields before all others
        key_fields = [field for field in raw_fields if field.is_key]
        other_fields = [field for field in raw_fields if not field.is_key]
        self.all_fields = key_fields + other_fields

        self.fields = []
        # check for duplicate field names
        for next_field in self.all_fields:
            duplicates = [field for field in self.fields if field.name == next_field.name]
            if not duplicates:
                self.fields.append(next_field)
                continue
            if not all(field.is_compatible(next_field) for field in duplicates):
                raise ValueError(f"incompatible fields with duplicate name: {packet_type}({packet_number}).{next_field.name}")

        # valid, since self.fields is already set
        if self.no_packet:
            self.delta = False
            self.handle_via_packet = False

            if self.want_dsend:
                raise ValueError(f"requested dsend for {self.type} without fields isn't useful")

        # create cap variants
        all_caps = self.all_caps    # valid, since self.all_fields is already set
        self.variants = [
            Variant(caps, all_caps.difference(caps), self, i + 100)
            for i, caps in enumerate(powerset(sorted(all_caps)))
        ]

    @property
    def name(self) -> str:
        """Snake-case name of this packet type"""
        return self.type.lower()

    @property
    def no_packet(self) -> bool:
        """Whether this packet's send functions should take no packet
        argument. This is the case iff this packet has no fields."""
        return not self.fields

    @property
    def send_params(self) -> str:
        """Parameters for the send and lsend functions, not including the
        connection or list of connections to send to"""
        return (
            f", const struct {self.name} *packet" if not self.no_packet else ""
        ) + (
            ", bool force_to_send" if self.want_force else ""
        )

    @property
    def send_args(self) -> str:
        """Arguments passed from lsend to send, not including the
        connection to send to

        See also extra_send_args"""
        assert self.want_lsend
        return (
            ", packet" if not self.no_packet else ""
        ) + (
            ", force_to_send" if self.want_force else ""
        )

    @property
    def dsend_params(self) -> str:
        """Parameters for the dsend and dlsend functions, not including the
        connection or list of connections to send to"""
        assert self.want_dsend
        return "".join(
            f", {field.get_param()}"
            for field in self.fields
        ) + (", bool force_to_send" if self.want_force else "")

    @property
    def send_prototype(self) -> str:
        """Prototype for the regular send function"""
        return f"int send_{self.name}(struct connection *pc{self.send_params})"

    @property
    def lsend_prototype(self) -> str:
        """Prototype for the lsend function (takes a list of connections)"""
        assert self.want_lsend
        return f"void lsend_{self.name}(struct conn_list *dest{self.send_params})"

    @property
    def dsend_prototype(self) -> str:
        """Prototype for the dsend function (directly takes values instead of a packet struct)"""
        assert self.want_dsend
        return f"int dsend_{self.name}(struct connection *pc{self.dsend_params})"

    @property
    def dlsend_prototype(self) -> str:
        """Prototype for the dlsend function (directly takes values; list of connections)"""
        assert self.want_dsend
        assert self.want_lsend
        return f"void dlsend_{self.name}(struct conn_list *dest{self.dsend_params})"

    @property
    def all_caps(self) -> "set[str]":
        """Set of all capabilities affecting this packet"""
        return {cap for field in self.all_fields for cap in field.all_caps}

    @property
    def complex(self) -> bool:
        """Whether this packet's struct requires special handling for
        initialization, copying, and destruction."""
        return any(field.complex for field in self.fields)

    def get_struct(self) -> str:
        """Generate the struct definition for this packet"""
        intro = f"""\
struct {self.name} {{
"""
        extro = f"""\
}};

"""

        body = "".join(
            prefix("  ", field.get_declaration())
            for field in self.fields
        ) or f"""\
  char __dummy;                 /* to avoid malloc(0); */
"""
        return intro+body+extro

    def get_prototypes(self) -> str:
        """Generate the header prototype declarations for the public
        functions associated with this packet."""
        result = f"""\
{self.send_prototype};
"""
        if self.want_lsend:
            result += f"""\
{self.lsend_prototype};
"""
        if self.want_dsend:
            result += f"""\
{self.dsend_prototype};
"""
            if self.want_lsend:
                result += f"""\
{self.dlsend_prototype};
"""
        return result + "\n"

    def get_stats(self) -> str:
        """Generate the code declaring counters for this packet's variants.

        See Variant.get_stats()"""
        return "".join(v.get_stats() for v in self.variants)

    def get_report_part(self) -> str:
        """Generate this packet's part of the delta_stats_report() function.

        See Variant.get_report_part() and
        PacketsDefinition.code_delta_stats_report"""
        return "".join(v.get_report_part() for v in self.variants)

    def get_reset_part(self) -> str:
        """Generate this packet's part of the delta_stats_reset() function.

        See Variant.get_reset_part() and
        PacketsDefinition.code_delta_stats_reset"""
        return "\n".join(v.get_reset_part() for v in self.variants)

    def get_init(self) -> str:
        """Generate this packet's init function, which initializes the
        packet struct so its complex-typed fields are usable, and sets
        all fields to the empty default state used for computing deltas"""
        if self.complex:
            field_parts = "\n" + "".join(
                    prefix("  ", field.get_init())
                    for field in self.fields
                )
            assert len(field_parts) > 1, f"complex packet with no field initializers: {self.type}({self.type_number})"
        else:
            field_parts = ""
        return f"""\
static inline void init_{self.name}(struct {self.name} *packet)
{{
  memset(packet, 0, sizeof(*packet));
{field_parts}\
}}

"""

    def get_free_destroy(self) -> str:
        """Generate this packet's free and destroy functions, which free
        memory associated with complex-typed fields of this packet, and
        optionally the allocation of the packet itself (destroy)."""
        if not self.complex:
            return f"""\
#define free_{self.name}(_packet) (void) 0
#define destroy_{self.name} free

"""

        # drop fields in reverse order, in case later fields depend on
        # earlier fields (e.g. for actual array sizes)
        field_parts = "".join(
            prefix("  ", field.get_free())
            for field in reversed(self.fields)
        )
        assert field_parts, f"complex packet with no field destructors: {self.type}({self.type_number})"
        # NB: destroy_*() takes void* to avoid casts
        return f"""\
static inline void free_{self.name}(struct {self.name} *packet)
{{
{field_parts}\
}}

static inline void destroy_{self.name}(void *packet)
{{
    free_{self.name}((struct {self.name} *) packet);
    free(packet);
}}

"""

    def get_send(self) -> str:
        """Generate the implementation of the send function, which sends a
        given packet to a given connection."""
        if self.no_packet:
            func = "no_packet"
            args = ""
        elif self.want_force:
            func = "force_to_send"
            args = ", packet, force_to_send"
        else:
            func = "packet"
            args = ", packet"

        return f"""\
{self.send_prototype}
{{
  if (!pc->used) {{
    log_error("WARNING: trying to send data to the closed connection %s",
              conn_description(pc));
    return -1;
  }}
  fc_assert_ret_val_msg(pc->phs.handlers->send[{self.type}].{func} != nullptr, -1,
                        "Handler for {self.type} not installed");
  return pc->phs.handlers->send[{self.type}].{func}(pc{args});
}}

"""

    def get_variants(self) -> str:
        """Generate all code associated with individual variants of this
        packet; see the Variant class (and its methods) for details."""
        result = ""
        for v in self.variants:
            if v.delta:
                result += f"""\
#ifdef FREECIV_DELTA_PROTOCOL
{v.get_hash()}\
{v.get_cmp()}\
{v.get_bitvector()}\
#endif /* FREECIV_DELTA_PROTOCOL */

"""
            result += v.get_receive()
            result += v.get_send()
        return result

    def get_lsend(self) -> str:
        """Generate the implementation of the lsend function, which takes
        a list of connections to send a packet to."""
        if not self.want_lsend: return ""
        return f"""\
{self.lsend_prototype}
{{
  conn_list_iterate(dest, pconn) {{
    send_{self.name}(pconn{self.send_args});
  }} conn_list_iterate_end;
}}

"""

    def get_dsend(self) -> str:
        """Generate the implementation of the dsend function, which directly
        takes packet fields instead of a packet struct."""
        if not self.want_dsend: return ""
        # safety: fill just borrows the given values; no init/free necessary
        fill = "".join(
            prefix("  ", field.get_fill())
            for field in self.fields
        )
        return f"""\
{self.dsend_prototype}
{{
  struct {self.name} packet, *real_packet = &packet;

{fill}\

  return send_{self.name}(pc, real_packet);
}}

"""

    def get_dlsend(self) -> str:
        """Generate the implementation of the dlsend function, combining
        dsend and lsend functionality.

        See self.get_dsend() and self.get_lsend()"""
        if not (self.want_lsend and self.want_dsend): return ""
        # safety: fill just borrows the given values; no init/free necessary
        fill = "".join(
            prefix("  ", field.get_fill())
            for field in self.fields
        )
        return f"""\
{self.dlsend_prototype}
{{
  struct {self.name} packet, *real_packet = &packet;

{fill}\

  lsend_{self.name}(dest, real_packet);
}}

"""


class PacketsDefinition(typing.Iterable[Packet]):
    """Represents an entire packets definition file"""

    COMMENT_START_PATTERN = re.compile(r"""
        ^\s*    # strip initial whitespace
        (.*?)   # actual content; note the reluctant quantifier
        \s*     # note: this can cause quadratic backtracking
        (?:     # match a potential comment
            (?:     # EOL comment (or just EOL)
                (?:
                    (?:\#|//)   # opening # or //
                    .*
                )?
            ) | (?: # block comment ~> capture remaining text
                /\*     # opening /*
                [^*]*   # text that definitely can't end the block comment
                (.*)    # remaining text, might contain a closing */
            )
        )
        (?:\n)? # optional newline in case those aren't stripped
        $
    """, re.VERBOSE)
    """Used to clean lines when not starting inside a block comment. Finds
    the start of a block comment, if it exists.

    Groups:
    - Actual content before any comment starts; stripped.
    - Remaining text after the start of a block comment. Not present if no
      block comment starts on this line."""

    COMMENT_END_PATTERN = re.compile(r"""
        ^
        .*?     # comment; note the reluctant quantifier
        (?:     # end of block comment ~> capture remaining text
            \*/     # closing */
            \s*     # strip whitespace after comment
            (.*)    # remaining text
        )?
        (?:\n)? # optional newline in case those aren't stripped
        $
    """, re.VERBOSE)
    """Used to clean lines when starting inside a block comment. Finds the
    end of a block comment, if it exists.

    Groups:
    - Remaining text after the end of the block comment; lstripped. Not
      present if the block comment doesn't end on this line."""

    TYPE_PATTERN = re.compile(r"^\s*type\s+(\w+)\s*=\s*(.+?)\s*$")
    """Matches type alias definition lines

    Groups:
    - the alias to define
    - the meaning for the alias"""

    PACKET_HEADER_PATTERN = re.compile(r"^\s*(PACKET_\w+)\s*=\s*(\d+)\s*;\s*(.*?)\s*$")
    """Matches the header line of a packet definition

    Groups:
    - packet type name
    - packet number
    - packet flags text"""

    PACKET_END_PATTERN = re.compile(r"^\s*end\s*$")
    """Matches the "end" line terminating a packet definition"""

    cfg: ScriptConfig
    """Configuration used for code generated from this definition"""

    type_registry: TypeRegistry
    """Type registry used to resolve type classes for field types"""

    types: "dict[str, RawFieldType]"
    """Maps type aliases and definitions to the parsed type"""

    packets: "list[Packet]"
    """List of all packets, in order of definition"""

    packets_by_type: "dict[str, Packet]"
    """Maps packet types (PACKET_FOO) to the packet with that type"""

    packets_by_number: "dict[int, Packet]"
    """Maps packet IDs to the packet with that ID"""

    packets_by_dirs: "dict[Directions, list[Packet]]"
    """Maps packet directions to lists of packets with those
    directions, in order of definition"""

    @classmethod
    def _clean_lines(cls, lines: typing.Iterable[str]) -> typing.Iterator[str]:
        """Strip comments and leading/trailing whitespace from the given
        lines. If a block comment starts in one line and ends in another,
        the remaining parts are joined together and yielded as one line."""
        inside_comment = False
        parts = []

        for line in lines:
            while line:
                if inside_comment:
                    # currently inside a block comment ~> look for */
                    mo = cls.COMMENT_END_PATTERN.fullmatch(line)
                    assert mo, repr(line)
                    # If the group wasn't captured (None), we haven't found
                    # a */ to end our comment ~> still inside_comment
                    # Otherwise, group captured remaining line content
                    line, = mo.groups(None)
                    inside_comment = line is None
                else:
                    mo = cls.COMMENT_START_PATTERN.fullmatch(line)
                    assert mo, repr(line)
                    # If the second group wasn't captured (None), there is
                    # no /* to start a block comment ~> not inside_comment
                    part, line = mo.groups(None)
                    inside_comment = line is not None
                    if part: parts.append(part)

            if (not inside_comment) and parts:
                # when ending a line outside a block comment, yield what
                # we've accumulated
                yield " ".join(parts)
                parts.clear()

        if inside_comment:
            raise ValueError("EOF while scanning block comment")

    def parse_lines(self, lines: typing.Iterable[str]):
        """Parse the given lines as type and packet definitions."""
        self.parse_clean_lines(self._clean_lines(lines))

    def parse_clean_lines(self, lines: typing.Iterable[str]):
        """Parse the given lines as type and packet definitions. Comments
        and blank lines must already be removed beforehand."""
        # hold on to the iterator itself
        lines_iter = iter(lines)
        for line in lines_iter:
            mo = self.TYPE_PATTERN.fullmatch(line)
            if mo is not None:
                self.define_type(*mo.groups())
                continue

            mo = self.PACKET_HEADER_PATTERN.fullmatch(line)
            if mo is not None:
                packet_type, packet_number, flags_text = mo.groups("")
                packet_number = int(packet_number)

                if packet_type in self.packets_by_type:
                    raise ValueError("Duplicate packet type: " + packet_type)

                if packet_number not in range(65536):
                    raise ValueError(f"packet number {packet_number:d} for {packet_type} outside legal range [0,65536)")
                if packet_number in self.packets_by_number:
                    raise ValueError(f"Duplicate packet number: {packet_number:d} ({self.packets_by_number[packet_number].type} and {packet_type})")

                packet = Packet(
                    self.cfg, packet_type, packet_number, flags_text,
                    takewhile(
                        lambda line: self.PACKET_END_PATTERN.fullmatch(line) is None,
                        lines_iter, # advance the iterator used by this for loop
                    ),
                    self.resolve_type,
                )

                self.packets.append(packet)
                self.packets_by_number[packet_number] = packet
                self.packets_by_type[packet_type] = packet
                self.packets_by_dirs[packet.dirs].append(packet)
                continue

            raise ValueError("Unexpected line: " + line)

    def resolve_type(self, type_text: str) -> RawFieldType:
        """Resolve the given type"""
        if type_text not in self.types:
            self.types[type_text] = self.type_registry.parse(type_text)
        return self.types[type_text]

    def define_type(self, alias: str, meaning: str):
        """Define a type alias"""
        if alias in self.types:
            raise ValueError(f"duplicate type alias {alias!r}: {self.types[alias]} and {meaning}")
        self.types[alias] = self.resolve_type(meaning)

    def __init__(self, cfg: ScriptConfig, type_registry: "TypeRegistry | None" = None):
        self.cfg = cfg
        self.type_registry = type_registry or DEFAULT_REGISTRY
        self.types = {}
        self.packets = []
        self.packets_by_type = {}
        self.packets_by_number = {}
        self.packets_by_dirs = {
            dirs: []
            for dirs in Directions
        }

    def __iter__(self) -> typing.Iterator[Packet]:
        return iter(self.packets)

    def iter_by_number(self) -> "typing.Generator[tuple[int, Packet, int], None, int]":
        """Yield (number, packet, skipped) tuples in order of packet number.

        skipped is how many numbers were skipped since the last packet

        Return the maximum packet number (or -1 if there are no packets)
        when used with `yield from`."""
        last = -1
        for n, packet in sorted(self.packets_by_number.items()):
            assert n == packet.type_number
            yield (n, packet, n - last - 1)
            last = n
        return last

    @property
    def all_caps(self) -> "set[str]":
        """Set of all capabilities affecting the defined packets"""
        return { cap for p in self for cap in p.all_caps }

    @property
    def code_packet_functional_capability(self) -> str:
        """Code fragment defining the packet_functional_capability string"""
        return f"""\
const char *const packet_functional_capability = "{' '.join(sorted(self.all_caps))}";
"""

    @property
    def code_delta_stats_report(self) -> str:
        """Code fragment implementing the delta_stats_report() function"""
        if not self.cfg.gen_stats: return f"""\
void delta_stats_report(void) {{}}
"""

        intro = f"""\
void delta_stats_report(void) {{
  int i;
"""
        extro = f"""\
}}

"""
        body = "".join(
            prefix("  ", packet.get_report_part())
            for packet in self
        )
        return intro + body + extro

    @property
    def code_delta_stats_reset(self) -> str:
        """Code fragment implementing the delta_stats_reset() function"""
        if not self.cfg.gen_stats: return f"""\
void delta_stats_reset(void) {{}}

"""

        intro = f"""\
void delta_stats_reset(void) {{
"""
        extro = f"""\
}}

"""
        body = "\n".join(
            prefix("  ", packet.get_reset_part())
            for packet in self
        )
        return intro + body + extro

    @property
    def code_packet_name(self) -> str:
        """Code fragment implementing the packet_name() function"""
        intro = f"""\
const char *packet_name(enum packet_type type)
{{
  static const char *const names[PACKET_LAST] = {{
"""

        body = ""
        for _, packet, skipped in self.iter_by_number():
            body += f"""\
    "unknown",
""" * skipped
            body += f"""\
    "{packet.type}",
"""

        extro = f"""\
  }};

  return (type < PACKET_LAST ? names[type] : "unknown");
}}

"""
        return intro + body + extro

    @property
    def code_packet_has_game_info_flag(self) -> str:
        """Code fragment implementing the packet_has_game_info_flag()
        function"""
        intro = f"""\
bool packet_has_game_info_flag(enum packet_type type)
{{
  static const bool flag[PACKET_LAST] = {{
"""
        body = "".join(
            f"""\
    [{packet.type}] = TRUE,
"""
            for packet in self
            if packet.is_info == "game"
        )

        extro = f"""\
    /* others are FALSE by default */
  }};

  return (type < PACKET_LAST) && flag[type];
}}

"""
        return intro + body + extro

    @property
    def code_packet_handlers_fill_initial(self) -> str:
        """Code fragment implementing the packet_handlers_fill_initial()
        function"""
        intro = f"""\
void packet_handlers_fill_initial(struct packet_handlers *phandlers)
{{
"""
        for cap in sorted(self.all_caps):
            intro += f"""\
  fc_assert_msg(has_capability("{cap}", our_capability),
                "Packets have support for unknown '{cap}' capability!");
"""

        down_only = [
            packet.variants[0]
            for packet in self.packets_by_dirs[Directions.DOWN_ONLY]
            if len(packet.variants) == 1
        ]
        up_only = [
            packet.variants[0]
            for packet in self.packets_by_dirs[Directions.UP_ONLY]
            if len(packet.variants) == 1
        ]
        unrestricted = [
            packet.variants[0]
            for packet in self.packets_by_dirs[Directions.UNRESTRICTED]
            if len(packet.variants) == 1
        ]

        body = ""
        for variant in unrestricted:
            body += prefix("  ", variant.fill_send_handler)
            body += prefix("  ", variant.fill_receive_handler)
        body += f"""\
  if (is_server()) {{
"""
        for variant in down_only:
            body += prefix("    ", variant.fill_send_handler)
        for variant in up_only:
            body += prefix("    ", variant.fill_receive_handler)
        body += f"""\
  }} else /* not is_server() */ {{
"""
        for variant in up_only:
            body += prefix("    ", variant.fill_send_handler)
        for variant in down_only:
            body += prefix("    ", variant.fill_receive_handler)

        extro = f"""\
  }}
}}

"""
        return intro + body + extro

    @property
    def code_packet_handlers_fill_capability(self) -> str:
        """Code fragment implementing the packet_handlers_fill_capability()
        function"""
        intro = f"""\
void packet_handlers_fill_capability(struct packet_handlers *phandlers,
                                     const char *capability)
{{
"""

        down_only = [
            packet
            for packet in self.packets_by_dirs[Directions.DOWN_ONLY]
            if len(packet.variants) > 1
        ]
        up_only = [
            packet
            for packet in self.packets_by_dirs[Directions.UP_ONLY]
            if len(packet.variants) > 1
        ]
        unrestricted = [
            packet
            for packet in self.packets_by_dirs[Directions.UNRESTRICTED]
            if len(packet.variants) > 1
        ]

        body = ""
        for packet in unrestricted:
            # indent for the start of the if-else
            # ends mid-line
            body += f"""\
  """
            for variant in packet.variants:
                hand = prefix("    ", variant.fill_send_handler + variant.fill_receive_handler)
                # starts and ends mid-line
                body += f"""if ({variant.condition}) {{
    {self.cfg.log_macro}("{packet.type}: using variant={variant.var_number} cap=%s", capability);
{hand}\
  }} else """
            # starts mid-line
            body += f"""{{
    log_error("Unknown {packet.type} variant for cap %s", capability);
  }}
"""
        if up_only or down_only:
            body += f"""\
  if (is_server()) {{
"""
            for packet in down_only:
                # indent for the start of the if-else
                # ends mid-line
                body += f"""\
    """
                for variant in packet.variants:
                    hand = prefix("      ", variant.fill_send_handler)
                    # starts and ends mid-line
                    body += f"""if ({variant.condition}) {{
      {self.cfg.log_macro}("{packet.type}: using variant={variant.var_number} cap=%s", capability);
{hand}\
    }} else """
                # starts mid-line
                body += f"""{{
      log_error("Unknown {packet.type} variant for cap %s", capability);
    }}
"""
            for packet in up_only:
                # indent for the start of the if-else
                # ends mid-line
                body += f"""\
    """
                for variant in packet.variants:
                    hand = prefix("      ", variant.fill_receive_handler)
                    # starts and ends mid-line
                    body += f"""if ({variant.condition}) {{
      {self.cfg.log_macro}("{packet.type}: using variant={variant.var_number} cap=%s", capability);
{hand}\
    }} else """
                #  starts mid-line
                body += f"""{{
      log_error("Unknown {packet.type} variant for cap %s", capability);
    }}
"""
            body += f"""\
  }} else /* not is_server() */ {{
"""
            for packet in up_only:
                # indent for the start of the if-else
                # ends mid-line
                body += f"""\
    """
                for variant in packet.variants:
                    hand = prefix("      ", variant.fill_send_handler)
                    # starts and ends mid-line
                    body += f"""if ({variant.condition}) {{
      {self.cfg.log_macro}("{packet.type}: using variant={variant.var_number} cap=%s", capability);
{hand}\
    }} else """
                # starts mid-line
                body += f"""{{
      log_error("Unknown {packet.type} variant for cap %s", capability);
    }}
"""
            for packet in down_only:
                # indent for the start of the if-else
                # ends mid-line
                body += f"""\
    """
                for variant in packet.variants:
                    hand = prefix("      ", variant.fill_receive_handler)
                    # starts and ends mid-line
                    body += f"""if ({variant.condition}) {{
      {self.cfg.log_macro}("{packet.type}: using variant={variant.var_number} cap=%s", capability);
{hand}\
    }} else """
                # starts mid-line
                body += f"""{{
      log_error("Unknown {packet.type} variant for cap %s", capability);
    }}
"""
            body += f"""\
  }}
"""

        extro = f"""\
}}
"""
        return intro + body + extro

    @property
    def code_packet_destroy(self) -> str:
        """Code fragment implementing the packet_destroy() function"""
        # NB: missing packet IDs are empty-initialized, i.e. set to nullptr by default
        handlers = "".join(
            f"""\
    [{packet.type}] = destroy_{packet.name},
"""
            for packet in self
        )

        return f"""\

void packet_destroy(void *packet, enum packet_type type)
{{
  static void (*const destroy_handlers[PACKET_LAST])(void *packet) = {{
{handlers}\
  }};
  void (*handler)(void *packet) = (type < PACKET_LAST ? destroy_handlers[type] : nullptr);

  if (handler == nullptr) {{
    handler = free;
    log_error("packet_destroy(): Invalid packet type %d", type);
  }}

  handler(packet);
}}
"""

    @property
    def code_enum_packet(self) -> str:
        """Code fragment declaring the packet_type enum"""
        intro = f"""\
enum packet_type {{
"""
        body = ""
        for n, packet, skipped in self.iter_by_number():
            if skipped:
                line = f"  {packet.type} = {n:d},"
            else:
                line = f"  {packet.type},"

            if not (n % 10):
                line = f"{line:40} /* {n:d} */"
            body += line + "\n"

        extro = f"""\

  PACKET_LAST  /* leave this last */
}};

"""
        return intro + body + extro


########################### Writing output files ###########################

def write_common_header(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for common/packets_gen.h to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path, wrap_header = "packets_gen") as output_h:
        output_h.write(f"""\
/* common */
#include "actions.h"
#include "city.h"
#include "conn_types.h"
#include "disaster.h"
#include "events.h"
#include "government.h"
#include "player.h"
#include "tech.h"
#include "unit.h"

/* common/aicore */
#include "cm.h"

""")

        # write structs
        for p in packets:
            output_h.write(p.get_struct())

        output_h.write(packets.code_enum_packet)

        # write function prototypes
        for p in packets:
            output_h.write(p.get_prototypes())
        output_h.write(f"""\
void delta_stats_report(void);
void delta_stats_reset(void);
""")

def write_common_impl(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for common/packets_gen.c to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path) as output_c:
        output_c.write(f"""\
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <string.h>

/* utility */
#include "bitvector.h"
#include "capability.h"
#include "genhash.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "capstr.h"
#include "connection.h"
#include "dataio.h"
#include "game.h"

#include "packets.h"

""")
        output_c.write(packets.code_packet_functional_capability)
        output_c.write(f"""\

#ifdef FREECIV_DELTA_PROTOCOL
static genhash_val_t hash_const(const void *vkey)
{{
  return 0;
}}

static bool cmp_const(const void *vkey1, const void *vkey2)
{{
  return TRUE;
}}
#endif /* FREECIV_DELTA_PROTOCOL */

""")

        if packets.cfg.gen_stats:
            output_c.write(f"""\
static int stats_total_sent;

""")
            for p in packets:
                output_c.write(p.get_stats())
        # report and reset functions always exist
        output_c.write(packets.code_delta_stats_report)
        output_c.write(packets.code_delta_stats_reset)

        output_c.write(packets.code_packet_name)
        output_c.write(packets.code_packet_has_game_info_flag)

        # write packet-specific code
        for p in packets:
            output_c.write(p.get_init())
            output_c.write(p.get_free_destroy())
            output_c.write(p.get_variants())
            output_c.write(p.get_send())
            output_c.write(p.get_lsend())
            output_c.write(p.get_dsend())
            output_c.write(p.get_dlsend())

        output_c.write(packets.code_packet_handlers_fill_initial)
        output_c.write(packets.code_packet_handlers_fill_capability)
        output_c.write(packets.code_packet_destroy)

def write_server_header(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for server/hand_gen.h to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path, wrap_header = "hand_gen", cplusplus = False) as f:
        f.write(f"""\
/* utility */
#include "shared.h"

/* common */
#include "fc_types.h"
#include "packets.h"

struct connection;

bool server_handle_packet(enum packet_type type, const void *packet,
                          struct player *pplayer, struct connection *pconn);

""")

        for p in packets:
            if not p.dirs.up: continue
            if p.no_handle: continue
            name_tail = p.name[len("packet_"):]

            if p.handle_per_conn:
                params = "struct connection *pc"
            else:
                params = "struct player *pplayer"

            if p.handle_via_packet:
                params += f", const struct {p.name} *packet"
            else:
                params += "".join(
                    f", {field.get_param()}"
                    for field in p.fields
                )

            f.write(f"""\
void handle_{name_tail}({params});
""")

def write_client_header(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for client/packhand_gen.h to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path, wrap_header = "packhand_gen") as f:
        f.write(f"""\
/* utility */
#include "shared.h"

/* common */
#include "packets.h"

bool client_handle_packet(enum packet_type type, const void *packet);

""")
        for p in packets:
            if not p.dirs.down: continue
            if p.no_handle: continue
            name_tail = p.name[len("packet_"):]

            if p.handle_via_packet:
                params = f"const struct {p.name} *packet"
            else:
                params = ", ".join(
                    field.get_param()
                    for field in p.fields
                ) or "void"

            f.write(f"""\
void handle_{name_tail}({params});
""")

def write_server_impl(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for server/hand_gen.c to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path) as f:
        f.write(f"""\
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "packets.h"

#include "hand_gen.h"

bool server_handle_packet(enum packet_type type, const void *packet,
                          struct player *pplayer, struct connection *pconn)
{{
  switch (type) {{
""")
        for p in packets:
            if not p.dirs.up: continue
            if p.no_handle: continue
            name_tail = p.name[len("packet_"):]

            if p.handle_per_conn:
                args = "pconn"
            else:
                args = "pplayer"

            if p.handle_via_packet:
                args += ", packet"
            else:
                packet = f"((const struct {p.name} *)packet)"
                args += "".join(
                    ",\n      " + field.get_handle_arg(packet)
                    for field in p.fields
                )

            f.write(f"""\
  case {p.type}:
    handle_{name_tail}({args});
    return TRUE;

""")
        f.write(f"""\
  default:
    return FALSE;
  }}
}}
""")

def write_client_impl(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for client/packhand_gen.c to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path) as f:
        f.write(f"""\
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "packets.h"

#include "packhand_gen.h"

bool client_handle_packet(enum packet_type type, const void *packet)
{{
  switch (type) {{
""")
        for p in packets:
            if not p.dirs.down: continue
            if p.no_handle: continue
            name_tail = p.name[len("packet_"):]

            if p.handle_via_packet:
                args = "packet"
            else:
                packet = f"((const struct {p.name} *)packet)"
                args = ",".join(
                    "\n      " + field.get_handle_arg(packet)
                    for field in p.fields
                )

            f.write(f"""\
  case {p.type}:
    handle_{name_tail}({args});
    return TRUE;

""")
        f.write(f"""\
  default:
    return FALSE;
  }}
}}
""")


def main(raw_args: "typing.Sequence[str] | None" = None):
    """Main function. Read the given arguments, or the command line
    arguments if raw_args is not given, and run the packet code generation
    script accordingly."""
    script_args = ScriptConfig(raw_args)

    packets = PacketsDefinition(script_args)
    for path in script_args.def_paths:
        with path.open() as input_file:
            packets.parse_lines(input_file)

    write_common_header(script_args.common_header_path, packets)
    write_common_impl(script_args.common_impl_path, packets)
    write_server_header(script_args.server_header_path, packets)
    write_client_header(script_args.client_header_path, packets)
    write_server_impl(script_args.server_impl_path, packets)
    write_client_impl(script_args.client_impl_path, packets)


if __name__ == "__main__":
    main()
