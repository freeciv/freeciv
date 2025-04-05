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

# This script runs under Python 3.5 and up. Please leave it so.
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
        raise ValueError("not a valid file path: %r" % s)
    if path.exists() and not path.is_file():
        raise ValueError("not a file: %r" % s)

    return path


class ScriptConfig:
    """Contains configuration info for the script's execution, along with
    functions closely tied to that configuration"""

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
        # actually changed. This prevents make from rebuilding all dependents
        # in cases where that wouldn't even be necessary.
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
                               help = "output path for %s" % canonical)

        paths.add_argument("def_paths", metavar = "def_path",
                           nargs = "+", type = file_path,
                           help = "paths to your packets.def file")

        return parser

    def __init__(self, args: "typing.Sequence[str] | None" = None):
        # type hints and docstrings for fields
        # FIXME: Once we can use Python 3.6 features, turn these into
        # (class-level) variable annotations
        if typing.TYPE_CHECKING:
            self.def_paths = [file_path("")]
            """Paths to definition files, in load order"""
            optional_path = (file_path(""), None)[int()]
            self.common_header_path = optional_path
            """Output path for the common header, or None if that should not
            be generated"""
            self.common_impl_path = optional_path
            """Output path for the common implementation, or None if that
            should not be generated"""
            self.server_header_path = optional_path
            """Output path for the server header, or None if that should not
            be generated"""
            self.server_impl_path = optional_path
            """Output path for the server implementation, or None if that
            should not be generated"""
            self.client_header_path = optional_path
            """Output path for the client header, or None if that should not
            be generated"""
            self.client_impl_path = optional_path
            """Output path for the client implementation, or None if that
            should not be generated"""

            self.verbose = False
            """Whether to enable verbose logging"""
            self.lazy_overwrite = False
            """Whether to lazily overwrite output files"""

            self.gen_stats = False
            """Whether to generate delta stats code"""
            self.log_macro = str() or None
            """The macro used for log calls, or None if no log calls should
            be generated"""

            self.fold_bool = True
            """Whether to fold boolean fields into the packet header"""

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
                self.log_verbose("Warning: path %s outside of Freeciv root" % path)
        return path

    @property
    def _script_path(self) -> Path:
        """Relative path of the executed script. Under normal circumstances,
        this will be common/generate_packets.py, but it may differ when this
        module is imported from another script."""
        return self._relative_path(Path(sys.argv[0]))

    def _write_disclaimer(self, f: typing.TextIO):
        f.write("""\
 /**************************************************************************
 *                         THIS FILE WAS GENERATED                         *
 * Script: %-63s *
""" % self._script_path)

        for path in self.def_paths:
            f.write("""\
 * Input:  %-63s *
""" % self._relative_path(path))

        f.write("""\
 *                         DO NOT CHANGE THIS FILE                         *
 **************************************************************************/

""")

    @contextmanager
    def _wrap_header(self, file: typing.TextIO, header_name: str) -> typing.Iterator[None]:
        """Add multiple inclusion protection to the given file"""
        name = "FC__%s_H" % header_name.upper()
        file.write("""\
#ifndef {name}
#define {name}

""".format(name = name))

        yield

        file.write("""\

#endif /* {name} */
""".format(name = name))

    @contextmanager
    def _wrap_cplusplus(self, file: typing.TextIO) -> typing.Iterator[None]:
        """Add code for `extern "C" {}` wrapping"""
        file.write("""\
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

""")
        yield
        file.write("""\

#ifdef __cplusplus
}
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
        self.log_verbose("writing %s" % path)

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
        self.log_verbose("done writing %s" % path)

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
        self.log_verbose("lazy: using %s" % tmp_path)
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
    with Path(path_a).open() as file_a, Path(path_b).open() as file_b:
        return all(a == b for a, b in zip_longest(file_a, file_b))

# Taken from https://docs.python.org/3.4/library/itertools.html#itertools-recipes
def powerset(iterable: typing.Iterable[T_co]) -> "typing.Iterator[tuple[T_co, ...]]":
    "powerset([1,2,3]) --> () (1,) (2,) (3,) (1,2) (1,3) (2,3) (1,2,3)"
    s = list(iterable)
    return chain.from_iterable(combinations(s, r) for r in range(len(s)+1))

INSERT_PREFIX_PATTERN = re.compile(r"^(?!#|$)", re.MULTILINE)
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

    _INDICES = "ijk"

    def __init__(self, name: str, location: "str | None" = None, depth: int = 0):
        self.name = name
        """The name associated with this location; used in log messages."""
        if location is None:
            self.location = name
            """The actual location as used in code"""
        else:
            self.location = location
        self.depth = depth
        """The array nesting depth of this location; used to determine index
        variable names."""

    def deeper(self, new_location: str) -> "Location":
        """Return the given string as a new Location with the same name as
        self and incremented depth"""
        return type(self)(self.name, new_location, self.depth + 1)

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
        return self.deeper("{self}[{self.index}]".format(self = self))

    def __str__(self) -> str:
        return self.location

    def __repr__(self) -> str:
        return "{self.__class__.__name__}({self.name!r}, {self.location!r}, {self.depth!r})".format(self = self)


#################### Components of a packets definition ####################

class FieldFlags:
    """Information about flags of a given Field. Multiple Field objects can
    share one FieldFlags instance, e.g. when defined on the same line."""

    ADD_CAP_PATTERN = re.compile(r"^add-cap\(([^()]+)\)$")
    """Matches an add-cap flag (optional capability)"""

    REMOVE_CAP_PATTERN = re.compile(r"^remove-cap\(([^()]+)\)$")
    """Matches a remove-cap flag (optional capability)"""

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

    is_key = False
    """Whether the field is a key field"""

    diff = False
    """Whether the field should be deep-diffed for transmission"""

    def __init__(self, flag_texts: typing.Iterable[str]):
        self.add_caps = set()
        """The capabilities required to enable the field"""

        self.remove_caps = set()
        """The capabilities that disable the field"""

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
            raise ValueError("unrecognized flag in field declaration: %s" % flag)

        contradictions = self.add_caps & self.remove_caps
        if contradictions:
            raise ValueError("cannot have same capabilities as both add-cap and remove-cap: %s" % ", ".join(contradictions))


class SizeInfo:
    """Information about size along one dimension of an array or other sized
    field type. Contains both the declared / maximum size, and the actual
    used size (if different)."""

    ARRAY_SIZE_PATTERN = re.compile(r"^([^:]+)(?:\:([^:]+))?$")
    """Matches an array size declaration (without the brackets)

    Groups:
    - the declared / maximum size
    - the field name for the actual size (optional)"""

    @classmethod
    @cache
    def parse(cls, size_text) -> "SizeInfo":
        """Parse the given array size text (without brackets)"""
        mo = cls.ARRAY_SIZE_PATTERN.fullmatch(size_text)
        if mo is None:
            raise ValueError("invalid array size declaration: [%s]" % size_text)
        return cls(*mo.groups())

    def __init__(self, declared: str, actual: "str | None"):
        self.declared = declared
        """Maximum size; used in declarations"""
        self._actual = actual
        """Name of the field to use for the actual size, or None if the
        entire array should always be transmitted."""

    @property
    def constant(self) -> bool:
        """Whether the actual size doesn't change"""
        return self._actual is None

    def actual_for(self, packet: str) -> str:
        """Return a code expression representing the actual size for the
        given packet"""
        if self.constant:
            return self.declared
        return "%s->%s" % (packet, self._actual)

    @property
    def real(self) -> str:
        """The number of elements to transmit. Either the same as the
        declared size, or a field of `*real_packet`."""
        return self.actual_for("real_packet")

    @property
    def old(self) -> str:
        """The number of elements transmitted last time. Either the same as
        the declared size, or a field of `*old`."""
        return self.actual_for("old")

    def size_check_get(self, field_name: str) -> str:
        """Generate a code snippet checking whether the received size is in
        range when receiving a packet."""
        if self.constant:
            return ""
        return """\
if ({self.real} > {self.declared}) {{
  RECEIVE_PACKET_FIELD_ERROR({field_name}, ": truncation array");
}}
""".format(self = self, field_name = field_name)

    def size_check_index(self, field_name: str) -> str:
        """Generate a code snippet asserting that indices can be correctly
        transmitted for array-diff."""
        if self.constant:
            return """\
FC_STATIC_ASSERT({self.declared} <= MAX_UINT16, packet_array_too_long_{field_name});
""".format(self = self, field_name = field_name)
        else:
            return """\
fc_assert({self.real} <= MAX_UINT16);
""".format(self = self)

    def index_put(self, index: str) -> str:
        """Generate a code snippet writing the given value to the network
        output, encoded as the appropriate index type"""
        if self.constant:
            return """\
#if {self.declared} <= MAX_UINT8
e |= DIO_PUT(uint8, &dout, &field_addr, {index});
#else
e |= DIO_PUT(uint16, &dout, &field_addr, {index});
#endif
""".format(self = self, index = index)
        else:
            return """\
if ({self.real} <= MAX_UINT8) {{
  e |= DIO_PUT(uint8, &dout, &field_addr, {index});
}} else {{
  e |= DIO_PUT(uint16, &dout, &field_addr, {index});
}}
""".format(self = self, index = index)

    def index_get(self, location: Location) -> str:
        """Generate a code snippet reading the next index from the
        network input decoded as the correct type"""
        if self.constant:
            return """\
#if {self.declared} <= MAX_UINT8
if (!DIO_GET(uint8, &din, &field_addr, &{location.index})) {{
#else
if (!DIO_GET(uint16, &din, &field_addr, &{location.index})) {{
#endif
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
""".format(self = self, location = location)
        else:
            return """\
if (({self.real} <= MAX_UINT8)
    ? !DIO_GET(uint8, &din, &field_addr, &{location.index})
    : !DIO_GET(uint16, &din, &field_addr, &{location.index})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
""".format(self = self, location = location)

    def __str__(self) -> str:
        if self._actual is None:
            return self.declared
        return "%s:%s" % (self.declared, self._actual)

    def __eq__(self, other) -> bool:
        if not isinstance(other, __class__):
            return NotImplemented
        return (self.declared == other.declared
                and self._actual == other._actual)

    def __hash__(self) -> int:
        return hash((__class__, self.declared, self._actual))


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
        return "<{self.__class__.__name__} {self}>".format(self = self)


FieldTypeConstructor = typing.Callable[[str, str], RawFieldType]

class TypeRegistry:
    """Determines what Python class to use for field types based on their
    dataio type and public type."""

    TYPE_INFO_PATTERN = re.compile(r"^([^()]*)\(([^()]*)\)$")
    """Matches a field type.

    Groups:
    - dataio type
    - public type (aka struct type)"""

    def __init__(self, fallback: FieldTypeConstructor):
        # FIXME: Once we can use Python 3.6 features, turn these into
        # (class-level) variable annotations
        self.dataio_types = {} # type: dict[str, FieldTypeConstructor]
        """Dictionary mapping dataio types to the constructor used for
        field types with that dataio type.

        This is the primary factor deciding what constructor to use for a
        given field type."""
        self.dataio_patterns = {} # type: dict[typing.Pattern[str], FieldTypeConstructor]
        """Dictionary mapping RegEx patterns to the constructor used for
        field types whose dataio type matches that pattern.

        Matches are cached in self.dataio_types."""
        self.public_types = {} # type: dict[str, FieldTypeConstructor]
        """Like self.dataio_types, but for public types.

        This is only checked if there are no matches in self.dataio_types
        and self.dataio_patterns."""
        self.public_patterns = {} # type: dict[typing.Pattern[str], FieldTypeConstructor]
        """Like self.dataio_patterns, but for public types.

        Matches are cached in self.public_types."""
        self.fallback = fallback
        """Fallback constructor used when there are no matches for either
        dataio type or public type."""

    def parse(self, type_text: str) -> RawFieldType:
        """Parse a single field type"""
        mo = __class__.TYPE_INFO_PATTERN.fullmatch(type_text)
        if mo is None:
            raise ValueError("malformed type or undefined alias: %r" % type_text)
        return self(*mo.groups())

    def __call__(self, dataio_type: str, public_type: str) -> RawFieldType:
        try:
            ctor = self.dataio_types[dataio_type]
        except KeyError:
            pass
        else:
            return ctor(dataio_type, public_type)

        for pat, ctor in self.dataio_patterns.items():
            mo = pat.fullmatch(dataio_type)
            if mo is not None:
                self.dataio_types[dataio_type] = ctor
                return ctor(dataio_type, public_type)

        self.dataio_types[dataio_type] = self._by_public
        return self._by_public(dataio_type, public_type)

    def _by_public(self, dataio_type: str, public_type: str) -> RawFieldType:
        try:
            ctor = self.public_types[public_type]
        except KeyError:
            pass
        else:
            return ctor(dataio_type, public_type)

        for pat, ctor in self.public_patterns.items():
            mo = pat.fullmatch(public_type)
            if mo is not None:
                self.public_types[public_type] = ctor
                return ctor(dataio_type, public_type)

        self.public_types[public_type] = self.fallback
        return self.fallback(dataio_type, public_type)


class NeedSizeType(RawFieldType):
    """Helper class for field types that require a size to be usable."""

    def __init__(self, dataio_type: str, public_type: str, cls: typing.Callable[[str, str, SizeInfo], "FieldType"]):
        self.dataio_type = dataio_type
        """The dataio type passed to self.cls"""
        self.public_type = public_type
        """The public type passed to self.cls"""
        self.cls = cls
        """The field type constructed when adding a size to this type"""

    def array(self, size: SizeInfo) -> "FieldType":
        """Add an array size to make a usable type."""
        return self.cls(self.dataio_type, self.public_type, size)

    def __str__(self) -> str:
        return "{self.dataio_type}({self.public_type})".format(self = self)


class FieldType(RawFieldType):
    """Abstract base class (ABC) for classes representing type information
    usable for fields of a packet"""

    foldable = False
    """Whether a field of this type can be folded into the packet header"""

    @cache
    def array(self, size: SizeInfo) -> "FieldType":
        """Construct a FieldType for an array with element type self and the
        given size"""
        return ArrayType(self, size)

    @abstractmethod
    def get_code_declaration(self, location: Location) -> str:
        """Generate a code snippet declaring a field with this type in a
        packet struct."""
        raise NotImplementedError

    @abstractmethod
    def get_code_handle_param(self, location: Location) -> str:
        """Generate a code fragment declaring a parameter with this type for
        a handle function.

        See also self.get_code_handle_arg()"""
        raise NotImplementedError

    def get_code_handle_arg(self, location: Location) -> str:
        """Generate a code fragment passing an argument with this type to a
        handle function.

        See also self.get_code_handle_param()"""
        return str(location)

    @abstractmethod
    def get_code_fill(self, location: Location) -> str:
        """Generate a code snippet moving a value of this type from dsend
        arguments into a packet struct."""
        raise NotImplementedError

    @abstractmethod
    def get_code_hash(self, location: Location) -> str:
        """Generate a code snippet factoring a field of this type into a
        hash computation's `result`."""
        raise NotImplementedError

    @abstractmethod
    def get_code_cmp(self, location: Location) -> str:
        """Generate a code snippet comparing a field of this type between
        the `old` and `real_packet` and setting `differ` accordingly."""
        raise NotImplementedError

    @abstractmethod
    def get_code_put(self, location: Location, deep_diff: bool = False) -> str:
        """Generate a code snippet writing a field of this type to the
        dataio stream."""
        raise NotImplementedError

    @abstractmethod
    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        """Generate a code snippet reading a field of this type from the
        dataio stream."""
        raise NotImplementedError

    def _compat_keys(self, location: Location):
        """Internal helper function. Yield keys to compare for
        type compatibility. See is_type_compatible()"""
        yield self.get_code_declaration(location)
        yield self.get_code_handle_param(location)
        yield self.get_code_handle_arg(location)
        yield self.get_code_fill(location)

    def is_type_compatible(self, other: "FieldType") -> bool:
        """Determine whether two field types can be used interchangeably as
        part of the packet struct, i.e. differ in dataio transmission only"""
        if other is self:
            return True
        loc = Location("compat_test_field_name")
        return all(
            a == b
            for a, b in zip_longest(
                self._compat_keys(loc),
                other._compat_keys(loc),
            )
        )


class BasicType(FieldType):
    """Type information for a field without any specialized treatment"""

    def __init__(self, dataio_type: str, public_type: str):
        self.dataio_type = dataio_type
        """How fields of this type are transmitted over network"""
        self.public_type = public_type
        """How fields of this type are represented in C code"""

    def get_code_declaration(self, location: Location) -> str:
        return """\
{self.public_type} {location};
""".format(self = self, location = location)

    def get_code_handle_param(self, location: Location) -> str:
        return "{self.public_type} {location}".format(self = self, location = location)

    def get_code_fill(self, location: Location) -> str:
        return """\
real_packet->{location} = {location};
""".format(location = location)

    def get_code_hash(self, location: Location) -> str:
        raise ValueError("hash not supported for type %s in field %s" % (self, location.name))

    def get_code_cmp(self, location: Location) -> str:
        return """\
differ = (old->{location} != real_packet->{location});
""".format(self = self, location = location)

    def get_code_put(self, location: Location, deep_diff: bool = False) -> str:
        return """\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{location});
""".format(self = self, location = location)

    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        return """\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{location})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
""".format(self = self, location = location)

    def __str__(self) -> str:
        return "{self.dataio_type}({self.public_type})".format(self = self)

DEFAULT_REGISTRY = TypeRegistry(BasicType)
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

    def get_code_hash(self, location: Location) -> str:
        return """\
result += key->%s;
""" % location

    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        if self.public_type in ("int", "bool"):
            # read directly
            return super().get_code_get(location, deep_diff)
        # read indirectly to make sure coercions between different integer
        # types happen correctly
        return """\
{{
  int readin;

  if (!DIO_GET({self.dataio_type}, &din, &field_addr, &readin)) {{
    RECEIVE_PACKET_FIELD_ERROR({location.name});
  }}
  real_packet->{location} = readin;
}}
""".format(self = self, location = location)

DEFAULT_REGISTRY.dataio_patterns[IntType.TYPE_PATTERN] = IntType


class BoolType(IntType):
    """Type information for a boolean field"""

    TYPE_PATTERN = re.compile(r"^bool\d*$")
    """Matches a bool dataio type"""

    foldable = True

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
            raise ValueError("bool dataio type with non-bool public type: %r" % public_type)

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
            raise ValueError("float type without float factor: %r" % dataio_info.string)

        if public_type != "float":
            raise ValueError("float dataio type with non-float public type: %r" % public_type)

        super().__init__(dataio_type, public_type)
        self.float_factor = int(float_factor)
        """Granularity (fixed-point factor) used to transmit this type in an
        integer"""

    def get_code_put(self, location: Location, deep_diff: bool = False) -> str:
        return """\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{location}, {self.float_factor:d});
""".format(self = self, location = location)

    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        return """\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{location}, {self.float_factor:d})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
""".format(self = self, location = location)

    def __str__(self) -> str:
        return "{self.dataio_type}{self.float_factor:d}({self.public_type})".format(self = self)

DEFAULT_REGISTRY.dataio_patterns[FloatType.TYPE_PATTERN] = FloatType


class BitvectorType(BasicType):
    """Type information for a bitvector field"""

    def __init__(self, dataio_type: str, public_type: str):
        if dataio_type != "bitvector":
            raise ValueError("not a valid bitvector type")

        super().__init__(dataio_type, public_type)

    def get_code_cmp(self, location: Location) -> str:
        return """\
differ = !BV_ARE_EQUAL(old->{location}, real_packet->{location});
""".format(self = self, location = location)

    def get_code_put(self, location: Location, deep_diff: bool = False) -> str:
        return """\
e |= DIO_BV_PUT(&dout, &field_addr, packet->{location});
""".format(location = location)

    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        return """\
if (!DIO_BV_GET(&din, &field_addr, real_packet->{location})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
""".format(self = self, location = location)

DEFAULT_REGISTRY.dataio_types["bitvector"] = BitvectorType


class StructType(BasicType):
    """Type information for a field of some general struct type"""

    TYPE_PATTERN = re.compile(r"^struct \w+$")
    """Matches a struct public type"""

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
        public_type = public_info.group(0)

        super().__init__(dataio_type, public_type)

    def get_code_handle_param(self, location: Location) -> str:
        if not location.depth:
            # top level: pass by-reference
            return "const " + super().get_code_handle_param(location.deeper("*%s" % location))
        return super().get_code_handle_param(location)

    def get_code_handle_arg(self, location: Location) -> str:
        if not location.depth:
            # top level: pass by-reference
            return super().get_code_handle_arg(location.deeper("&%s" % location))
        return super().get_code_handle_arg(location)

    def get_code_cmp(self, location: Location) -> str:
        return """\
differ = !are_{self.dataio_type}s_equal(&old->{location}, &real_packet->{location});
""".format(self = self, location = location)

    def get_code_put(self, location: Location, deep_diff: bool = False) -> str:
        return """\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, &real_packet->{location});
""".format(self = self, location = location)

DEFAULT_REGISTRY.public_patterns[StructType.TYPE_PATTERN] = StructType


class CmParameterType(StructType):
    """Type information for a worklist field"""

    def __init__(self, dataio_type: str, public_type: str):
        if dataio_type != "cm_parameter":
            raise ValueError("not a valid cm_parameter type")

        if public_type != "struct cm_parameter":
            raise ValueError("cm_parameter dataio type with non-cm_parameter public type: %r" % public_type)

        super().__init__(dataio_type, public_type)

    def get_code_cmp(self, location: Location) -> str:
        return """\
differ = !cm_are_parameter_equal(&old->{location}, &real_packet->{location});
""".format(self = self, location = location)

DEFAULT_REGISTRY.dataio_types["cm_parameter"] = CmParameterType


class WorklistType(StructType):
    """Type information for a worklist field"""

    def __init__(self, dataio_type: str, public_type: str):
        if dataio_type != "worklist":
            raise ValueError("not a valid worklist type")

        if public_type != "struct worklist":
            raise ValueError("worklist dataio type with non-worklist public type: %r" % public_type)

        super().__init__(dataio_type, public_type)

    def get_code_fill(self, location: Location) -> str:
        return """\
worklist_copy(&real_packet->{location}, {location});
""".format(location = location)

DEFAULT_REGISTRY.dataio_types["worklist"] = WorklistType


class SizedType(BasicType):
    """Abstract base class (ABC) for field types that include a size"""

    def __init__(self, dataio_type: str, public_type: str, size: SizeInfo):
        super().__init__(dataio_type, public_type)
        self.size = size
        """Size info (maximum and actual) of this type"""

    def get_code_declaration(self, location: Location) -> str:
        return super().get_code_declaration(
            location.deeper("%s[%s]" % (location, self.size.declared))
        )

    def get_code_handle_param(self, location: Location) -> str:
        # add "const" if top level
        pre = "" if location.depth else "const "
        return pre + super().get_code_handle_param(location.deeper("*%s" % location))

    @abstractmethod
    def get_code_fill(self, location: Location) -> str:
        return super().get_code_fill(location)

    def __str__(self) -> str:
        return "%s[%s]" % (super().__str__(), self.size)


class StringType(SizedType):
    """Type information for a string field"""

    def __init__(self, dataio_type: str, public_type: str, size: SizeInfo):
        if dataio_type not in ("string", "estring"):
            raise ValueError("not a valid string type")

        if public_type != "char":
            raise ValueError("string dataio type with non-char public type: %r" % public_type)

        super().__init__(dataio_type, public_type, size)

    def get_code_fill(self, location: Location) -> str:
        return """\
sz_strlcpy(real_packet->{location}, {location});
""".format(location = location)

    def get_code_cmp(self, location: Location) -> str:
        return """\
differ = (strcmp(old->{location}, real_packet->{location}) != 0);
""".format(self = self, location = location)

    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        return """\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, real_packet->{location}, sizeof(real_packet->{location}))) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
""".format(self = self, location = location)

DEFAULT_REGISTRY.dataio_types["string"] = DEFAULT_REGISTRY.dataio_types["estring"] = partial(NeedSizeType, cls = StringType)


class MemoryType(SizedType):
    """Type information for a memory field"""

    def __init__(self, dataio_type: str, public_type: str, size: SizeInfo):
        if dataio_type != "memory":
            raise ValueError("not a valid memory type")

        super().__init__(dataio_type, public_type, size)

    def get_code_fill(self, location: Location) -> str:
        raise NotImplementedError("fill not supported for memory-type fields")

    def get_code_cmp(self, location: Location) -> str:
        if self.size.constant:
            return """\
differ = (memcmp(old->{location}, real_packet->{location}, {self.size.real}) != 0);
""".format(self = self, location = location)
        return """\
differ = (({self.size.old} != {self.size.real})
          || (memcmp(old->{location}, real_packet->{location}, {self.size.real}) != 0));
""".format(self = self, location = location)

    def get_code_put(self, location: Location, deep_diff: bool = False) -> str:
        return """\
e |= DIO_PUT({self.dataio_type}, &dout, &field_addr, &real_packet->{location}, {self.size.real});
""".format(self = self, location = location)

    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        size_check = self.size.size_check_get(location.name)
        return """\
{size_check}\
if (!DIO_GET({self.dataio_type}, &din, &field_addr, real_packet->{location}, {self.size.real})) {{
  RECEIVE_PACKET_FIELD_ERROR({location.name});
}}
""".format(self = self, location = location, size_check = size_check)

DEFAULT_REGISTRY.dataio_types["memory"] = partial(NeedSizeType, cls = MemoryType)


class ArrayType(FieldType):
    """Type information for an array field. Consists of size information and
    another FieldType for the array's elements, which may also be an
    ArrayType (for multi-dimensionaly arrays)."""

    def __init__(self, elem: FieldType, size: SizeInfo):
        self.elem = elem
        """The type of the array elements"""
        self.size = size
        """The length (maximum and actual) of the array"""

    def get_code_declaration(self, location: Location) -> str:
        return self.elem.get_code_declaration(
            location.deeper("%s[%s]" % (location, self.size.declared))
        )

    def get_code_handle_param(self, location: Location) -> str:
        # add "const" if top level
        pre = "" if location.depth else "const "
        return pre + self.elem.get_code_handle_param(location.deeper("*%s" % location))

    def get_code_fill(self, location: Location) -> str:
        inner_fill = prefix("    ", self.elem.get_code_fill(location.sub))
        return """\
{{
  int {location.index};

  for ({location.index} = 0; {location.index} < {self.size.real}; {location.index}++) {{
{inner_fill}\
  }}
}}
""".format(self = self, location = location, inner_fill = inner_fill)

    def get_code_hash(self, location: Location) -> str:
        raise ValueError("hash not supported for array type %s in field %s" % (self, location.name))

    def get_code_cmp(self, location: Location) -> str:
        if not self.size.constant:
            head = """\
differ = ({self.size.old} != {self.size.real});
if (!differ) {{
""".format(self = self)
        else:
            head = """\
differ = FALSE;
{
"""
        inner_cmp = prefix("    ", self.elem.get_code_cmp(location.sub))
        return head + """\
  int {location.index};

  for ({location.index} = 0; {location.index} < {self.size.real}; {location.index}++) {{
{inner_cmp}\
    if (differ) {{
      break;
    }}
  }}
}}
""".format(self = self, location = location, inner_cmp = inner_cmp)

    def _get_code_put_full(self, location: Location, inner_put: str) -> str:
        """Helper method. Generate put code without array-diff."""
        inner_put = prefix("    ", inner_put)
        return """\
{{
  int {location.index};

#ifdef FREECIV_JSON_CONNECTION
  /* Create the array. */
  e |= DIO_PUT(farray, &dout, &field_addr, {self.size.real});

  /* Enter the array. */
  field_addr.sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

  for ({location.index} = 0; {location.index} < {self.size.real}; {location.index}++) {{
#ifdef FREECIV_JSON_CONNECTION
    /* Next array element. */
    field_addr.sub_location->number = {location.index};
#endif /* FREECIV_JSON_CONNECTION */

{inner_put}\
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Exit array. */
  FC_FREE(field_addr.sub_location);
#endif /* FREECIV_JSON_CONNECTION */
}}
""".format(self = self, location = location, inner_put = inner_put)

    def _get_code_put_diff(self, location: Location, inner_put: str) -> str:
        """Helper method. Generate array-diff put code."""
        size_check = self.size.size_check_index(location.name)
        inner_put = prefix("      ", inner_put)
        inner_cmp = prefix("    ", self.elem.get_code_cmp(location.sub))
        index_put = prefix("      ", self.size.index_put(location.index))
        index_put_sentinel = prefix("  ", self.size.index_put(self.size.real))
        return """\
{size_check}\
{{
  int {location.index};

#ifdef FREECIV_JSON_CONNECTION
  size_t count_{location.index} = 0;

  /* Create the array. */
  e |= DIO_PUT(farray, &dout, &field_addr, 0);

  /* Enter array. */
  field_addr.sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

  for ({location.index} = 0; {location.index} < {self.size.real}; {location.index}++) {{
{inner_cmp}\

    if (differ) {{
#ifdef FREECIV_JSON_CONNECTION
      /* Append next diff array element. */
      field_addr.sub_location->number = -1;

      /* Create the diff array element. */
      e |= DIO_PUT(farray, &dout, &field_addr, 2);

      /* Enter diff array element (start at the index address). */
      field_addr.sub_location->number = count_{location.index}++;
      field_addr.sub_location->sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

      /* Write the index */
{index_put}\

#ifdef FREECIV_JSON_CONNECTION
      /* Content address. */
      field_addr.sub_location->sub_location->number = 1;
#endif /* FREECIV_JSON_CONNECTION */

{inner_put}\

#ifdef FREECIV_JSON_CONNECTION
      /* Exit diff array element. */
      FC_FREE(field_addr.sub_location->sub_location);
#endif /* FREECIV_JSON_CONNECTION */
    }}
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Append diff array element. */
  field_addr.sub_location->number = -1;

  /* Create the terminating diff array element. */
  e |= DIO_PUT(farray, &dout, &field_addr, 1);

  /* Enter diff array element (start at the index address). */
  field_addr.sub_location->number = count_{location.index};
  field_addr.sub_location->sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

  /* Write the sentinel value */
{index_put_sentinel}\

#ifdef FREECIV_JSON_CONNECTION
  /* Exit diff array element. */
  FC_FREE(field_addr.sub_location->sub_location);

  /* Exit array. */
  FC_FREE(field_addr.sub_location);
#endif /* FREECIV_JSON_CONNECTION */
}}
""".format(self = self, location = location, size_check = size_check, inner_cmp = inner_cmp,
           inner_put = inner_put, index_put = index_put, index_put_sentinel = index_put_sentinel)

    def get_code_put(self, location: Location, deep_diff: bool = False) -> str:
        inner_put = self.elem.get_code_put(location.sub, deep_diff)
        if deep_diff:
            return self._get_code_put_diff(location, inner_put)
        else:
            return self._get_code_put_full(location, inner_put)

    def _get_code_get_full(self, location: Location, inner_get: str) -> str:
        """Helper method. Generate get code without array-diff."""
        size_check = self.size.size_check_get(location.name)
        inner_get = prefix("    ", inner_get)
        return """\
{size_check}\
{{
  int {location.index};

#ifdef FREECIV_JSON_CONNECTION
  /* Enter array. */
  field_addr.sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

  for ({location.index} = 0; {location.index} < {self.size.real}; {location.index}++) {{
#ifdef FREECIV_JSON_CONNECTION
    field_addr.sub_location->number = {location.index};
#endif /* FREECIV_JSON_CONNECTION */

{inner_get}\
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Exit array. */
  FC_FREE(field_addr.sub_location);
#endif /* FREECIV_JSON_CONNECTION */
}}
""".format(self = self, location = location, inner_get = inner_get, size_check = size_check)

    def _get_code_get_diff(self, location: Location, inner_get: str) -> str:
        """Helper method. Generate array-diff get code."""
        size_check = self.size.size_check_get(location.name) + self.size.size_check_index(location.name)
        inner_get = prefix("  ", inner_get)
        index_get = prefix("  ", self.size.index_get(location))
        return """\
{size_check}\
#ifdef FREECIV_JSON_CONNECTION
/* Enter array (start at initial element). */
field_addr.sub_location = plocation_elem_new(0);
/* Enter diff array element (start at the index address). */
field_addr.sub_location->sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

while (TRUE) {{
  int {location.index};

  /* Read next index */
{index_get}\

  if ({location.index} == {self.size.real}) {{
    break;
  }}
  if ({location.index} > {self.size.real}) {{
    RECEIVE_PACKET_FIELD_ERROR({location.name},
                               ": unexpected value %d "
                               "(> {self.size.real}) in array diff",
                               {location.index});
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Content address. */
  field_addr.sub_location->sub_location->number = 1;
#endif /* FREECIV_JSON_CONNECTION */

{inner_get}\

#ifdef FREECIV_JSON_CONNECTION
  /* Move to the next diff array element. */
  field_addr.sub_location->number++;
  /* Back to the index address. */
  field_addr.sub_location->sub_location->number = 0;
#endif /* FREECIV_JSON_CONNECTION */
}}

#ifdef FREECIV_JSON_CONNECTION
/* Exit diff array element. */
FC_FREE(field_addr.sub_location->sub_location);
/* Exit array. */
FC_FREE(field_addr.sub_location);
#endif /* FREECIV_JSON_CONNECTION */
""".format(self = self, location = location, size_check = size_check,
           inner_get = inner_get, index_get = index_get)

    def get_code_get(self, location: Location, deep_diff: bool = False) -> str:
        inner_get = self.elem.get_code_get(location.sub, deep_diff)
        if deep_diff:
            return self._get_code_get_diff(location, inner_get)
        else:
            return self._get_code_get_full(location, inner_get)

    def __str__(self) -> str:
        return "{self.elem}[{self.size}]".format(self = self)


class Field:
    """A single field of a packet. Consists of a name, type information
    (including array sizes) and flags."""

    FIELDS_LINE_PATTERN = re.compile(r"^\s*(\w+(?:\([^()]*\))?)\s+([^;()]*?)\s*;\s*(.*?)\s*$")
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

    @classmethod
    def parse(cls, cfg: ScriptConfig, line: str, resolve_type: typing.Callable[[str], RawFieldType]) -> "typing.Iterable[Field]":
        """Parse a single line defining one or more fields"""
        mo = cls.FIELDS_LINE_PATTERN.fullmatch(line)
        if mo is None:
            raise ValueError("invalid field definition: %r" % line)
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
                raise ValueError("need an array size to use type %s" % field_type)

            yield Field(cfg, field_text, field_type, flag_info)

    def __init__(self, cfg: ScriptConfig, name: str, type_info: FieldType, flags: FieldFlags):
        self.cfg = cfg
        """Configuration used when generating code for this field"""
        self.name = name
        """This field's name (identifier)"""

        self.type_info = type_info
        """This field's type information; see FieldType"""
        self.flags = flags
        """This field's flags; see FieldFlags"""

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

    def get_declar(self) -> str:
        """Generate the way this field is declared in the packet struct"""
        return self.type_info.get_code_declaration(Location(self.name))

    def get_handle_param(self) -> str:
        """Generate the way this field is declared as a parameter of a
        handle function.

        See also self.get_handle_arg()"""
        return self.type_info.get_code_handle_param(Location(self.name))

    def get_handle_arg(self, packet_arrow: str) -> str:
        """Generate the way this field is passed as an argument to a handle
        function.

        See also self.get_handle_param()"""
        return self.type_info.get_code_handle_arg(Location(
            self.name,
            packet_arrow + self.name,
        ))

    def get_fill(self) -> str:
        """Generate code moving this field from the dsend arguments into
        the packet struct."""
        return self.type_info.get_code_fill(Location(self.name))

    def get_hash(self) -> str:
        """Generate code factoring this field into a hash computation."""
        assert self.is_key
        return self.type_info.get_code_hash(Location(self.name))

    def get_cmp(self) -> str:
        """Generate code checking whether this field changed.

        This code is primarily used by self.get_cmp_wrapper()"""
        return self.type_info.get_code_cmp(Location(self.name))

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
            if pack.is_info != "no":
                cmp = self.get_cmp()
                differ_part = """\
if (differ) {
  different++;
}
"""
            else:
                cmp = ""
                differ_part = ""
            b = "packet->{self.name}".format(self = self)
            return cmp + differ_part + """\
if (%s) {
  BV_SET(fields, %d);
}

""" % (b, i)
        else:
            cmp = self.get_cmp()
            if pack.is_info != "no":
                return """\
%s\
if (differ) {
  different++;
  BV_SET(fields, %d);
}

""" % (cmp, i)
            else:
                return """\
%s\
if (differ) {
  BV_SET(fields, %d);
}

""" % (cmp, i)

    def get_put_wrapper(self, packet: "Variant", i: int, deltafragment: bool) -> str:
        """Generate code conditionally putting this field iff its bit in the
        `fields` bitvector is set.

        Does nothing for boolean fields folded into the packet header.

        See also self.get_put()"""
        if self.folded_into_head:
            return """\
/* field {i:d} is folded into the header */
""".format(i = i)
        put = prefix("  ", self.get_put(deltafragment))
        if packet.gen_log:
            f = """\
  {packet.log_macro}("  field \'{self.name}\' has changed");
""".format(packet = packet, self = self)
        else:
            f=""
        if packet.gen_stats:
            s = """\
  stats_{packet.name}_counters[{i:d}]++;
""".format(packet = packet, i = i)
        else:
            s=""
        return """\
if (BV_ISSET(fields, {i:d})) {{
{f}\
{s}\
{put}\
}}
""".format(i = i, f = f, s = s, put = put)

    def get_put(self, deltafragment: bool) -> str:
        """Generate the code putting this field, i.e. writing it to the
        dataio stream.

        This does not include delta-related code checking whether to
        transmit the field in the first place; see self.get_put_wrapper()"""
        real = self.get_put_real(deltafragment)
        return """\
#ifdef FREECIV_JSON_CONNECTION
field_addr.name = "{self.name}";
#endif /* FREECIV_JSON_CONNECTION */
e = 0;
{real}\
if (e) {{
  log_packet_detailed("'{self.name}' field error detected");
}}
""".format(self = self, real = real)

    def get_put_real(self, deltafragment: bool) -> str:
        """Generate the bare core of this field's put code. This code is not
        yet wrapped for full delta and JSON protocol support.

        See self.get_put() for more info"""
        return self.type_info.get_code_put(Location(self.name), deltafragment and self.diff)

    def get_get_wrapper(self, packet: "Variant", i: int, deltafragment: bool) -> str:
        """Generate code conditionally getting this field iff its bit in the
        `fields` bitvector is set.

        For boolean fields folded into the packet header, instead reads the
        field from the bitvector.

        See also self.get_get()"""
        if self.folded_into_head:
            return  """\
real_packet->{self.name} = BV_ISSET(fields, {i:d});
""".format(self = self, i = i)
        get = prefix("  ", self.get_get(deltafragment))
        if packet.gen_log:
            f = """\
  {packet.log_macro}("  got field '{self.name}'");
""".format(self = self, packet = packet)
        else:
            f=""
        return """\
if (BV_ISSET(fields, {i:d})) {{
{f}\
{get}\
}}
""".format(i = i, f = f, get = get)

    def get_get(self, deltafragment: bool) -> str:
        """Generate the code getting this field, i.e. reading it from the
        dataio stream.

        This does not include delta-related code checking if the field
        was transmitted in the first place; see self.get_get_wrapper()"""
        return """\
#ifdef FREECIV_JSON_CONNECTION
field_addr.name = \"{self.name}\";
#endif /* FREECIV_JSON_CONNECTION */
""".format(self = self) + self.get_get_real(deltafragment)

    def get_get_real(self, deltafragment: bool) -> str:
        """Generate the bare core of this field's get code. This code is not
        yet wrapped for full delta and JSON protocol support.

        See self.get_get() for more info"""
        return self.type_info.get_code_get(Location(self.name), deltafragment and self.diff)


class Variant:
    """Represents one variant of a packet. Packets with add-cap or
    remove-cap fields have different variants for different combinations of
    the relevant optional capabilities."""

    def __init__(self, poscaps: typing.Iterable[str], negcaps: typing.Iterable[str],
                       packet: "Packet", no: int):
        self.packet = packet
        """The packet this is a variant of"""
        self.no=no
        """The numeric variant ID (not packet ID) of this variant"""
        self.name = "%s_%d" % (packet.name, no)
        """The full name of this variant"""

        self.poscaps = set(poscaps)
        """The set of optional capabilities that must be present to use this
        variant"""
        self.negcaps = set(negcaps)
        """The set of optional capabilities that must *not* be present to
        use this variant"""
        self.fields = [
            field
            for field in packet.all_fields
            if field.present_with_caps(self.poscaps)
        ]
        """All fields that are transmitted when using this variant"""
        self.key_fields = [field for field in self.fields if field.is_key]
        """The key fields that are used for this variant"""
        self.other_fields = [field for field in self.fields if not field.is_key]
        """The non-key fields that are transmitted when using this variant"""
        # FIXME: Doesn't work with non-int key fields
        self.keys_format=", ".join(["%d"]*len(self.key_fields))
        """The printf format string for this variant's key fields in
        generated log calls

        See also self.keys_arg"""
        self.keys_arg = ", ".join("real_packet->" + field.name for field in self.key_fields)
        """The arguments passed when formatting this variant's key fields in
        generated log calls

        See also self.keys_format"""
        if self.keys_arg:
            self.keys_arg=",\n    "+self.keys_arg

        if not self.fields and packet.fields:
            raise ValueError("empty variant for nonempty {self.packet_name} with capabilities {self.poscaps}".format(self = self))

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
    def cancel(self) -> "list[str]":
        """List of packets to cancel when sending or receiving this packet

        See Packet.cancel"""
        return self.packet.cancel

    @property
    def differ_used(self) -> bool:
        """Whether the send function needs a `differ` boolean.

        See get_send()"""
        return (
            (not self.no_packet)
            and self.delta
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
            cap_fmt = "has_capability(\"%s\", capability)"
            return " && ".join(chain(
                (cap_fmt % cap for cap in sorted(self.poscaps)),
                ("!" + cap_fmt % cap for cap in sorted(self.negcaps)),
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
        return "static struct {self.packet_name} *receive_{self.name}(struct connection *pc)".format(self = self)

    @property
    def send_prototype(self) -> str:
        """The prototype of this variant's send function"""
        return "static int send_{self.name}(struct connection *pc{self.packet.extra_send_args})".format(self = self)

    @property
    def send_handler(self) -> str:
        """Code to set the send handler for this variant

        See get_packet_handlers_fill_initial and
        get_packet_handlers_fill_capability"""
        if self.no_packet:
            return """\
phandlers->send[{self.type}].no_packet = (int(*)(struct connection *)) send_{self.name};
""".format(self = self)
        elif self.want_force:
            return """\
phandlers->send[{self.type}].force_to_send = (int(*)(struct connection *, const void *, bool)) send_{self.name};
""".format(self = self)
        else:
            return """\
phandlers->send[{self.type}].packet = (int(*)(struct connection *, const void *)) send_{self.name};
""".format(self = self)

    @property
    def receive_handler(self) -> str:
        """Code to set the receive handler for this variant

        See get_packet_handlers_fill_initial and
        get_packet_handlers_fill_capability"""
        return """\
phandlers->receive[{self.type}] = (void *(*)(struct connection *)) receive_{self.name};
""".format(self = self)

    def get_stats(self) -> str:
        """Generate the declaration of the delta stats counters associated
        with this packet variant"""
        names = ", ".join(
            "\"%s\"" % field.name
            for field in self.other_fields
        )

        return """\
static int stats_{self.name}_sent;
static int stats_{self.name}_discarded;
static int stats_{self.name}_counters[{self.bits:d}];
static char *stats_{self.name}_names[] = {{{names}}};

""".format(self = self, names = names)

    def get_bitvector(self) -> str:
        """Generate the declaration of the fields bitvector type for this
        packet variant"""
        return """\
BV_DEFINE({self.name}_fields, {self.bits});
""".format(self = self)

    def get_report_part(self) -> str:
        """Generate the part of the delta_stats_report8) function specific
        to this packet variant"""
        return """\

if (stats_{self.name}_sent > 0
    && stats_{self.name}_discarded != stats_{self.name}_sent) {{
  log_test(\"{self.name} %d out of %d got discarded\",
    stats_{self.name}_discarded, stats_{self.name}_sent);
  for (i = 0; i < {self.bits}; i++) {{
    if (stats_{self.name}_counters[i] > 0) {{
      log_test(\"  %4d / %4d: %2d = %s\",
        stats_{self.name}_counters[i],
        (stats_{self.name}_sent - stats_{self.name}_discarded),
        i, stats_{self.name}_names[i]);
    }}
  }}
}}
""".format(self = self)

    def get_reset_part(self) -> str:
        """Generate the part of the delta_stats_reset() function specific
        to this packet variant"""
        return """\
stats_{self.name}_sent = 0;
stats_{self.name}_discarded = 0;
memset(stats_{self.name}_counters, 0,
       sizeof(stats_{self.name}_counters));
""".format(self = self)

    def get_hash(self) -> str:
        """Generate the key hash function for this variant"""
        if not self.key_fields:
            return """\
#define hash_{self.name} hash_const

""".format(self = self)

        intro = """\
static genhash_val_t hash_{self.name}(const void *vkey)
{{
  const struct {self.packet_name} *key = (const struct {self.packet_name} *) vkey;
  genhash_val_t result = 0;

""".format(self = self)

        body = """\

  result *= 5;

""".join(prefix("  ", field.get_hash()) for field in self.key_fields)

        extro = """\

  result &= 0xFFFFFFFF;
  return result;
}

"""

        return intro + body + extro

    def get_cmp(self) -> str:
        """Generate the key comparison function for this variant"""
        if not self.key_fields:
            return """\
#define cmp_{self.name} cmp_const

""".format(self = self)

        # note: the names `old` and `real_packet` allow reusing
        # field-specific cmp code
        intro = """\
static bool cmp_{self.name}(const void *vkey1, const void *vkey2)
{{
  const struct {self.packet_name} *old = (const struct {self.packet_name} *) vkey1;
  const struct {self.packet_name} *real_packet = (const struct {self.packet_name} *) vkey2;
  bool differ;

""".format(self = self)

        body = """\

  if (differ) {
    return !differ;
  }

""".join(prefix("  ", field.get_cmp()) for field in self.key_fields)

        extro = """\

  return !differ;
}
"""

        return intro + body + extro

    def get_send(self) -> str:
        """Generate the send function for this packet variant"""
        if self.gen_stats:
            report = """\

  stats_total_sent++;
  stats_{self.name}_sent++;
""".format(self = self)
        else:
            report=""
        if self.gen_log:
            log = """\

  {self.log_macro}("{self.name}: sending info about ({self.keys_format})"{self.keys_arg});
""".format(self = self)
        else:
            log=""

        if self.no_packet:
            main_header = ""
        else:
            if self.packet.want_pre_send:
                main_header = """\
  /* copy packet for pre-send */
  struct {self.packet_name} packet_buf = *packet;
  const struct {self.packet_name} *real_packet = &packet_buf;
""".format(self = self)
            else:
                main_header = """\
  const struct {self.packet_name} *real_packet = packet;
""".format(self = self)
            main_header += """\
  int e;
"""

        if not self.packet.want_pre_send:
            pre = ""
        elif self.no_packet:
            pre = """\

  pre_send_{self.packet_name}(pc, NULL);
""".format(self = self)
        else:
            pre = """\

  pre_send_{self.packet_name}(pc, &packet_buf);
""".format(self = self)

        if not self.no_packet:
            if self.delta:
                if self.want_force:
                    diff = "force_to_send"
                else:
                    diff = "0"
                delta_header = """\
#ifdef FREECIV_DELTA_PROTOCOL
  {self.name}_fields fields;
  struct {self.packet_name} *old;
""".format(self = self)
                if self.differ_used:
                    delta_header += """\
  bool differ;
"""
                delta_header += """\
  struct genhash **hash = pc->phs.sent + {self.type};
""".format(self = self)
                if self.is_info != "no":
                    delta_header += """\
  int different = {diff};
""".format(diff = diff)
                delta_header += """\
#endif /* FREECIV_DELTA_PROTOCOL */
"""
                body = prefix("  ", self.get_delta_send_body()) + """\
#ifndef FREECIV_DELTA_PROTOCOL
"""
            else:
                delta_header=""
                body = """\
#if 1 /* To match endif */
"""
            body += "".join(
                prefix("  ", field.get_put(False))
                for field in self.fields
            )
            body += """\

#endif
"""
        else:
            body=""
            delta_header=""

        if self.packet.want_post_send:
            if self.no_packet:
                post = """\
  post_send_{self.packet_name}(pc, NULL);
""".format(self = self)
            else:
                post = """\
  post_send_{self.packet_name}(pc, real_packet);
""".format(self = self)
        else:
            post=""

        if self.fields:
            faddr = """\
#ifdef FREECIV_JSON_CONNECTION
  struct plocation field_addr;
  {
    struct plocation *field_addr_tmp = plocation_field_new(NULL);
    field_addr = *field_addr_tmp;
    FC_FREE(field_addr_tmp);
  }
#endif /* FREECIV_JSON_CONNECTION */
"""
        else:
            faddr = ""

        return "".join((
            """\
{self.send_prototype}
{{
""".format(self = self),
            main_header,
            delta_header,
            """\
  SEND_PACKET_START({self.type});
""".format(self = self),
            faddr,
            log,
            report,
            pre,
            body,
            post,
            """\
  SEND_PACKET_END({self.type});
}}

""".format(self = self),
        ))

    def get_delta_send_body(self, before_return: str = "") -> str:
        """Helper for get_send(). Generate the part of the send function
        that computes and transmits the delta between the real packet and
        the last cached packet."""
        intro = """\

#ifdef FREECIV_DELTA_PROTOCOL
if (NULL == *hash) {{
  *hash = genhash_new_full(hash_{self.name}, cmp_{self.name},
                           NULL, NULL, NULL, free);
}}
BV_CLR_ALL(fields);

if (!genhash_lookup(*hash, real_packet, (void **) &old)) {{
  old = fc_malloc(sizeof(*old));
  *old = *real_packet;
  genhash_insert(*hash, old, old);
  memset(old, 0, sizeof(*old));
""".format(self = self)
        if self.is_info != "no":
            intro += """\
  different = 1;      /* Force to send. */
"""
        intro += """\
}
"""
        body = "".join(
            field.get_cmp_wrapper(i, self)
            for i, field in enumerate(self.other_fields)
        )
        if self.gen_log:
            fl = """\
  {self.log_macro}("  no change -> discard");
""".format(self = self)
        else:
            fl=""
        if self.gen_stats:
            s = """\
  stats_{self.name}_discarded++;
""".format(self = self)
        else:
            s=""

        if self.is_info != "no":
            body += """\
if (different == 0) {{
{fl}\
{s}\
{before_return}\
  SEND_PACKET_DISCARD();
}}
""".format(fl = fl, s = s, before_return = before_return)

        body += """\

#ifdef FREECIV_JSON_CONNECTION
field_addr.name = "fields";
#endif /* FREECIV_JSON_CONNECTION */
e = 0;
e |= DIO_BV_PUT(&dout, &field_addr, fields);
if (e) {
  log_packet_detailed("fields bitvector error detected");
}
"""

        body += "".join(
            field.get_put(True)
            for field in self.key_fields
        )
        body += "\n"

        body += "".join(
            field.get_put_wrapper(self, i, True)
            for i, field in enumerate(self.other_fields)
        )
        body += """\

*old = *real_packet;
"""

        # Cancel some is-info packets.
        for i in self.cancel:
            body += """\

hash = pc->phs.sent + %s;
if (NULL != *hash) {
  genhash_remove(*hash, real_packet);
}
""" % i
        body += """\
#endif /* FREECIV_DELTA_PROTOCOL */
"""

        return intro+body

    def get_receive(self) -> str:
        """Generate the receive function for this packet variant"""
        if self.delta:
            delta_header = """\
#ifdef FREECIV_DELTA_PROTOCOL
  {self.name}_fields fields;
  struct {self.packet_name} *old;
  struct genhash **hash = pc->phs.received + {self.type};
#endif /* FREECIV_DELTA_PROTOCOL */
""".format(self = self)
            delta_body1 = """\

#ifdef FREECIV_DELTA_PROTOCOL
#ifdef FREECIV_JSON_CONNECTION
  field_addr.name = "fields";
#endif /* FREECIV_JSON_CONNECTION */
  DIO_BV_GET(&din, &field_addr, fields);
"""
            body1 = "".join(
                prefix("  ", field.get_get(True))
                for field in self.key_fields
            )
            body1 += """\

#else /* FREECIV_DELTA_PROTOCOL */
"""
            body2 = prefix("  ", self.get_delta_receive_body())
        else:
            delta_header=""
            delta_body1=""
            body1 = """\
#if 1 /* To match endif */
"""
            body2=""
        nondelta = "".join(
            prefix("  ", field.get_get(False))
            for field in self.fields
        ) or """\
  real_packet->__dummy = 0xff;
"""
        body1 += nondelta + """\
#endif
"""

        if self.gen_log:
            log = """\
  {self.log_macro}("{self.name}: got info about ({self.keys_format})"{self.keys_arg});
""".format(self = self)
        else:
            log=""

        if self.packet.want_post_recv:
            post = """\
  post_receive_{self.packet_name}(pc, real_packet);
""".format(self = self)
        else:
            post=""

        if self.fields:
            faddr = """\
#ifdef FREECIV_JSON_CONNECTION
  struct plocation field_addr;
  {
    struct plocation *field_addr_tmp = plocation_field_new(NULL);
    field_addr = *field_addr_tmp;
    FC_FREE(field_addr_tmp);
  }
#endif /* FREECIV_JSON_CONNECTION */
"""
        else:
            faddr = ""

        return "".join((
            """\
{self.receive_prototype}
{{
""".format(self = self),
            delta_header,
            """\
  RECEIVE_PACKET_START({self.packet_name}, real_packet);
""".format(self = self),
            faddr,
            delta_body1,
            body1,
            log,
            body2,
            post,
            """\
  RECEIVE_PACKET_END(real_packet);
}

""",
        ))

    def get_delta_receive_body(self) -> str:
        """Helper for get_receive(). Generate the part of the receive
        function responsible for recreating the full packet from the
        received delta and the last cached packet."""
        if self.key_fields:
            # bit-copy the values, since we're moving (not cloning)
            # the key fields
            # FIXME: might not work for arrays
            backup_key = "".join(
                prefix("  ", field.get_declar())
                for field in self.key_fields
            ) + "\n"+ "".join(
                """\
  {field.name} = real_packet->{field.name};
""".format(field = field)
                for field in self.key_fields
            ) + "\n"
            restore_key = "\n" + "".join(
                """\
  real_packet->{field.name} = {field.name};
""".format(field = field)
                for field in self.key_fields
            )
        else:
            backup_key = restore_key = ""
        if self.gen_log:
            fl = """\
  {self.log_macro}("  no old info");
""".format(self = self)
        else:
            fl=""
        body = """\

#ifdef FREECIV_DELTA_PROTOCOL
if (NULL == *hash) {{
  *hash = genhash_new_full(hash_{self.name}, cmp_{self.name},
                           NULL, NULL, NULL, free);
}}

if (genhash_lookup(*hash, real_packet, (void **) &old)) {{
  *real_packet = *old;
}} else {{
{backup_key}\
{fl}\
  memset(real_packet, 0, sizeof(*real_packet));
{restore_key}\
}}

""".format(self = self, backup_key = backup_key, restore_key = restore_key, fl = fl)
        body += "".join(
            field.get_get_wrapper(self, i, True)
            for i, field in enumerate(self.other_fields)
        )

        extro = """\

if (NULL == old) {
  old = fc_malloc(sizeof(*old));
  *old = *real_packet;
  genhash_insert(*hash, old, old);
} else {
  *old = *real_packet;
}
"""

        # Cancel some is-info packets.
        extro += "".join(
            """\

hash = pc->phs.received + %s;
if (NULL != *hash) {
  genhash_remove(*hash, real_packet);
}
""" % cancel_pack
            for cancel_pack in self.cancel
        )

        return body + extro + """\

#endif /* FREECIV_DELTA_PROTOCOL */
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

    CANCEL_PATTERN = re.compile(r"^cancel\((.*)\)$")
    """Matches a cancel flag

    Groups:
    - the packet type to cancel"""

    is_info = "no"
    """Whether this is an is-info or is-game-info packet.
    "no" means normal, "yes" means is-info, "game" means is-game-info"""

    want_dsend = False
    """Whether to generate a direct-send function taking field values
    instead of a packet struct"""

    want_lsend = False
    """Whether to generate a list-send function sending a packet to
    multiple connections"""

    want_force = False
    """Whether send functions should take a force_to_send parameter
    to override discarding is-info packets where nothing changed"""

    want_pre_send = False
    """Whether a pre-send hook should be called when sending this packet"""

    want_post_send = False
    """Whether a post-send hook should be called after sending this packet"""

    want_post_recv = False
    """Wheter a post-receive hook should be called when receiving this
    packet"""

    delta = True
    """Whether to use delta optimization for this packet"""

    no_handle = False
    """Whether this packet should *not* be handled normally"""

    handle_via_packet = True
    """Whether to pass the entire packet (by reference) to the handle
    function (rather than each field individually)"""

    handle_per_conn = False
    """Whether this packet's handle function should be called with the
    connection instead of the attached player"""

    def __init__(self, cfg: ScriptConfig, packet_type: str, packet_number: int, flags_text: str,
                       lines: typing.Iterable[str], resolve_type: typing.Callable[[str], RawFieldType]):
        self.cfg = cfg
        """Configuration used when generating code for this packet"""
        self.type = packet_type
        """The packet type in allcaps (PACKET_FOO), as defined in the
        packet_type enum

        See also self.name"""
        self.type_number = packet_number
        """The numeric ID of this packet type"""

        # FIXME: Once we can use Python 3.6 features, use variable
        # annotations instead of empty comprehensions to set element type
        self.cancel = [str(_) for _ in ()]
        """List of packet types to drop from the cache when sending or
        receiving this packet type"""
        dirs = set()

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

            mo = __class__.CANCEL_PATTERN.fullmatch(flag)
            if mo is not None:
                self.cancel.append(mo.group(1))
                continue

            raise ValueError("unrecognized flag for %s: %r" % (self.name, flag))

        if not dirs:
            raise ValueError("no directions defined for %s" % self.name)
        self.dirs = Directions(frozenset(dirs))
        """Which directions this packet can be sent in"""

        raw_fields = [
            field
            for line in lines
            for field in Field.parse(self.cfg, line, resolve_type)
        ]
        # put key fields before all others
        key_fields = [field for field in raw_fields if field.is_key]
        other_fields = [field for field in raw_fields if not field.is_key]
        self.all_fields = key_fields + other_fields
        """List of all fields of this packet, including name duplicates for
        different capability variants that are compatible.

        Only relevant for creating Variants; self.fields should be used when
        not dealing with capabilities or Variants."""

        self.fields = [Field(_, _, _, _) for _ in []]
        """List of all fields of this packet, with only one field of each name"""
        # check for duplicate field names
        for next_field in self.all_fields:
            duplicates = [field for field in self.fields if field.name == next_field.name]
            if not duplicates:
                self.fields.append(next_field)
                continue
            if not all(field.is_compatible(next_field) for field in duplicates):
                raise ValueError("incompatible fields with duplicate name: %s(%d).%s"
                                 % (packet_type, packet_number, next_field.name))

        # valid, since self.fields is already set
        if self.no_packet:
            self.delta = False
            self.handle_via_packet = False

            if self.want_dsend:
                raise ValueError("requested dsend for %s without fields isn't useful" % self.name)

        # create cap variants
        all_caps = self.all_caps    # valid, since self.all_fields is already set
        self.variants = [
            Variant(caps, all_caps.difference(caps), self, i + 100)
            for i, caps in enumerate(powerset(sorted(all_caps)))
        ]
        """List of all variants of this packet"""

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
    def extra_send_args(self) -> str:
        """Argements for the regular send function"""
        return (
            ", const struct {self.name} *packet".format(self = self) if not self.no_packet else ""
        ) + (
            ", bool force_to_send" if self.want_force else ""
        )

    @property
    def extra_send_args2(self) -> str:
        """Arguments passed from lsend to send

        See also extra_send_args"""
        assert self.want_lsend
        return (
            ", packet" if not self.no_packet else ""
        ) + (
            ", force_to_send" if self.want_force else ""
        )

    @property
    def extra_send_args3(self) -> str:
        """Arguments for the dsend and dlsend functions"""
        assert self.want_dsend
        return "".join(
            ", %s" % field.get_handle_param()
            for field in self.fields
        ) + (", bool force_to_send" if self.want_force else "")

    @property
    def send_prototype(self) -> str:
        """Prototype for the regular send function"""
        return "int send_{self.name}(struct connection *pc{self.extra_send_args})".format(self = self)

    @property
    def lsend_prototype(self) -> str:
        """Prototype for the lsend function (takes a list of connections)"""
        assert self.want_lsend
        return "void lsend_{self.name}(struct conn_list *dest{self.extra_send_args})".format(self = self)

    @property
    def dsend_prototype(self) -> str:
        """Prototype for the dsend function (directly takes values instead of a packet struct)"""
        assert self.want_dsend
        return "int dsend_{self.name}(struct connection *pc{self.extra_send_args3})".format(self = self)

    @property
    def dlsend_prototype(self) -> str:
        """Prototype for the dlsend function (directly takes values; list of connections)"""
        assert self.want_dsend
        assert self.want_lsend
        return "void dlsend_{self.name}(struct conn_list *dest{self.extra_send_args3})".format(self = self)

    @property
    def all_caps(self) -> "set[str]":
        """Set of all capabilities affecting this packet"""
        return {cap for field in self.all_fields for cap in field.all_caps}


    def get_struct(self) -> str:
        """Generate the struct definition for this packet"""
        intro = """\
struct {self.name} {{
""".format(self = self)
        extro = """\
};

"""

        body = "".join(
            prefix("  ", field.get_declar())
            for field in self.fields
        ) or """\
  char __dummy;                 /* to avoid malloc(0); */
"""
        return intro+body+extro

    def get_prototypes(self) -> str:
        """Generate the header prototype declarations for the public
        functions associated with this packet."""
        result = """\
{self.send_prototype};
""".format(self = self)
        if self.want_lsend:
            result += """\
{self.lsend_prototype};
""".format(self = self)
        if self.want_dsend:
            result += """\
{self.dsend_prototype};
""".format(self = self)
            if self.want_lsend:
                result += """\
{self.dlsend_prototype};
""".format(self = self)
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

    def get_send(self) -> str:
        """Generate the implementation of the send function, which sends a
        given packet to a given connection."""
        if self.no_packet:
            func="no_packet"
            args=""
        elif self.want_force:
            func="force_to_send"
            args=", packet, force_to_send"
        else:
            func="packet"
            args=", packet"

        return """\
{self.send_prototype}
{{
  if (!pc->used) {{
    log_error("WARNING: trying to send data to the closed connection %s",
              conn_description(pc));
    return -1;
  }}
  fc_assert_ret_val_msg(pc->phs.handlers->send[{self.type}].{func} != NULL, -1,
                        "Handler for {self.type} not installed");
  return pc->phs.handlers->send[{self.type}].{func}(pc{args});
}}

""".format(self = self, func = func, args = args)

    def get_variants(self) -> str:
        """Generate all code associated with individual variants of this
        packet; see the Variant class (and its methods) for details."""
        result=""
        for v in self.variants:
            if v.delta:
                result += """\
#ifdef FREECIV_DELTA_PROTOCOL
"""
                result += v.get_hash()
                result += v.get_cmp()
                result += v.get_bitvector()
                result += """\
#endif /* FREECIV_DELTA_PROTOCOL */

"""
            result += v.get_receive()
            result += v.get_send()
        return result

    def get_lsend(self) -> str:
        """Generate the implementation of the lsend function, which takes
        a list of connections to send a packet to."""
        if not self.want_lsend: return ""
        return """\
{self.lsend_prototype}
{{
  conn_list_iterate(dest, pconn) {{
    send_{self.name}(pconn{self.extra_send_args2});
  }} conn_list_iterate_end;
}}

""".format(self = self)

    def get_dsend(self) -> str:
        """Generate the implementation of the dsend function, which directly
        takes packet fields instead of a packet struct."""
        if not self.want_dsend: return ""
        fill = "".join(
            prefix("  ", field.get_fill())
            for field in self.fields
        )
        return """\
{self.dsend_prototype}
{{
  struct {self.name} packet, *real_packet = &packet;

{fill}\

  return send_{self.name}(pc, real_packet);
}}

""".format(self = self, fill = fill)

    def get_dlsend(self) -> str:
        """Generate the implementation of the dlsend function, combining
        dsend and lsend functionality.

        See self.get_dsend() and self.get_lsend()"""
        if not (self.want_lsend and self.want_dsend): return ""
        fill = "".join(
            prefix("  ", field.get_fill())
            for field in self.fields
        )
        return """\
{self.dlsend_prototype}
{{
  struct {self.name} packet, *real_packet = &packet;

{fill}\

  lsend_{self.name}(dest, real_packet);
}}

""".format(self = self, fill = fill)


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
                    raise ValueError("packet number %d for %s outside legal range [0,65536)" % (packet_number, packet_type))
                if packet_number in self.packets_by_number:
                    raise ValueError("Duplicate packet number: %d (%s and %s)" % (
                        packet_number,
                        self.packets_by_number[packet_number].type,
                        packet_type,
                    ))

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
            if meaning == self.types[alias]:
                self.cfg.log_verbose("duplicate typedef: %r = %r" % (alias, meaning))
                return
            else:
                raise ValueError("duplicate type alias %r: %r and %r"
                                    % (alias, self.types[alias], meaning))

        self.types[alias] = self.resolve_type(meaning)

    def __init__(self, cfg: ScriptConfig, type_registry: "TypeRegistry | None" = None):
        self.cfg = cfg
        """Configuration used for code generated from this definition"""
        self.type_registry = type_registry or DEFAULT_REGISTRY
        """Type registry used to resolve type classes for field types"""
        # FIXME: Once we can use Python 3.6 features, use variable
        # annotations instead of empty comprehensions to set element type
        self.types = {
            str(_): self.type_registry(_, _)
            for _ in ()
        }
        """Maps type aliases and definitions to the parsed type"""
        self.packets = [
            Packet(*[_])
            for _ in ()
        ]
        """List of all packets, in order of definition"""
        self.packets_by_type = {
            str(_): self.packets[_]
            for _ in ()
        }
        """Maps packet types (PACKET_FOO) to the packet with that type"""
        self.packets_by_number = {
            int(_): self.packets[_]
            for _ in ()
        }
        """Maps packet IDs to the packet with that ID"""
        self.packets_by_dirs = {
            dirs: [
                self.packets[_]
                for _ in ()
            ]
            for dirs in Directions
        }
        """Maps packet directions to lists of packets with those
        directions, in order of definition"""

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
        return set().union(*(p.all_caps for p in self))

    @property
    def code_packet_functional_capability(self) -> str:
        """Code fragment defining the packet_functional_capability string"""
        return """\

const char *const packet_functional_capability = "%s";
""" % " ".join(sorted(self.all_caps))

    @property
    def code_delta_stats_report(self) -> str:
        """Code fragment implementing the delta_stats_report() function"""
        if not self.cfg.gen_stats: return """\
void delta_stats_report(void) {}

"""

        intro = """\
void delta_stats_report(void) {
  int i;
"""
        extro = """\
}

"""
        body = "".join(
            prefix("  ", packet.get_report_part())
            for packet in self
        )
        return intro + body + extro

    @property
    def code_delta_stats_reset(self) -> str:
        """Code fragment implementing the delta_stats_reset() function"""
        if not self.cfg.gen_stats: return """\
void delta_stats_reset(void) {}

"""

        intro = """\
void delta_stats_reset(void) {
"""
        extro = """\
}

"""
        body = "\n".join(
            prefix("  ", packet.get_reset_part())
            for packet in self
        )
        return intro + body + extro

    @property
    def code_packet_name(self) -> str:
        """Code fragment implementing the packet_name() function"""
        intro = """\
const char *packet_name(enum packet_type type)
{
  static const char *const names[PACKET_LAST] = {
"""

        body = ""
        for _, packet, skipped in self.iter_by_number():
            body += """\
    "unknown",
""" * skipped
            body += """\
    "%s",
""" % packet.type

        extro = """\
  };

  return (type < PACKET_LAST ? names[type] : "unknown");
}

"""
        return intro + body + extro

    @property
    def code_packet_has_game_info_flag(self) -> str:
        """Code fragment implementing the packet_has_game_info_flag()
        function"""
        intro = """\
bool packet_has_game_info_flag(enum packet_type type)
{
  static const bool flag[PACKET_LAST] = {
"""
        body = ""
        for _, packet, skipped in self.iter_by_number():
            body += """\
    FALSE,
""" * skipped
            if packet.is_info != "game":
                body += """\
    FALSE, /* %s */
""" % packet.type
            else:
                body += """\
    TRUE, /* %s */
""" % packet.type

        extro = """\
  };

  return (type < PACKET_LAST ? flag[type] : FALSE);
}

"""
        return intro + body + extro

    @property
    def code_packet_handlers_fill_initial(self) -> str:
        """Code fragment implementing the packet_handlers_fill_initial()
        function"""
        intro = """\
void packet_handlers_fill_initial(struct packet_handlers *phandlers)
{
"""
        for cap in sorted(self.all_caps):
            intro += """\
  fc_assert_msg(has_capability("{0}", our_capability),
                "Packets have support for unknown '{0}' capability!");
""".format(cap)

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
            body += prefix("  ", variant.send_handler)
            body += prefix("  ", variant.receive_handler)
        body += """\
  if (is_server()) {
"""
        for variant in down_only:
            body += prefix("    ", variant.send_handler)
        for variant in up_only:
            body += prefix("    ", variant.receive_handler)
        body += """\
  } else {
"""
        for variant in up_only:
            body += prefix("    ", variant.send_handler)
        for variant in down_only:
            body += prefix("    ", variant.receive_handler)

        extro = """\
  }
}

"""
        return intro + body + extro

    @property
    def code_packet_handlers_fill_capability(self) -> str:
        """Code fragment implementing the packet_handlers_fill_capability()
        function"""
        intro = """\
void packet_handlers_fill_capability(struct packet_handlers *phandlers,
                                     const char *capability)
{
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
        for p in unrestricted:
            body += "  "
            for v in p.variants:
                hand = prefix("    ", v.send_handler + v.receive_handler)
                body += """if ({v.condition}) {{
    {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
{hand}\
  }} else """.format(v = v, hand = hand)
            body += """{{
    log_error("Unknown {p.type} variant for cap %s", capability);
  }}
""".format(p = p)
        if up_only or down_only:
            body += """\
  if (is_server()) {
"""
            for p in down_only:
                body += "    "
                for v in p.variants:
                    hand = prefix("      ", v.send_handler)
                    body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
{hand}\
    }} else """.format(v = v, hand = hand)
                body += """{{
      log_error("Unknown {p.type} variant for cap %s", capability);
    }}
""".format(p = p)
            for p in up_only:
                body += "    "
                for v in p.variants:
                    hand = prefix("      ", v.receive_handler)
                    body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
{hand}\
    }} else """.format(v = v, hand = hand)
                body += """{{
      log_error("Unknown {p.type} variant for cap %s", capability);
    }}
""".format(p = p)
            body += """\
  } else {
"""
            for p in up_only:
                body += "    "
                for v in p.variants:
                    hand = prefix("      ", v.send_handler)
                    body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
{hand}\
    }} else """.format(v = v, hand = hand)
                body += """{{
      log_error("Unknown {p.type} variant for cap %s", capability);
    }}
""".format(p = p)
            for p in down_only:
                body += "    "
                for v in p.variants:
                    hand = prefix("      ", v.receive_handler)
                    body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
{hand}\
    }} else """.format(v = v, hand = hand)
                body += """{{
      log_error("Unknown {p.type} variant for cap %s", capability);
    }}
""".format(p = p)
            body += """\
  }
"""

        extro = """\
}
"""
        return intro + body + extro

    @property
    def code_enum_packet(self) -> str:
        """Code fragment declaring the packet_type enum"""
        intro = """\
enum packet_type {
"""
        body = ""
        for n, packet, skipped in self.iter_by_number():
            if skipped:
                line = "  %s = %d," % (packet.type, n)
            else:
                line = "  %s," % (packet.type)

            if not (n % 10):
                line = "%-40s /* %d */" % (line, n)
            body += line + "\n"

        extro = """\

  PACKET_LAST  /* leave this last */
};

"""
        return intro + body + extro


########################### Writing output files ###########################

def write_common_header(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for common/packets_gen.h to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path, wrap_header = "packets_gen") as output_h:
        output_h.write("""\
/* common */
#include "actions.h"
#include "city.h"
#include "disaster.h"
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
        output_h.write("""\
void delta_stats_report(void);
void delta_stats_reset(void);
""")

def write_common_impl(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for common/packets_gen.c to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path) as output_c:
        output_c.write("""\
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
        output_c.write("""\

#ifdef FREECIV_DELTA_PROTOCOL
static genhash_val_t hash_const(const void *vkey)
{
  return 0;
}

static bool cmp_const(const void *vkey1, const void *vkey2)
{
  return TRUE;
}
#endif /* FREECIV_DELTA_PROTOCOL */

""")

        if packets.cfg.gen_stats:
            output_c.write("""\
static int stats_total_sent;

""")
            # write stats
            for p in packets:
                output_c.write(p.get_stats())
            # write report()
        output_c.write(packets.code_delta_stats_report)
        output_c.write(packets.code_delta_stats_reset)

        output_c.write(packets.code_packet_name)
        output_c.write(packets.code_packet_has_game_info_flag)

        # write hash, cmp, send, receive
        for p in packets:
            output_c.write(p.get_variants())
            output_c.write(p.get_send())
            output_c.write(p.get_lsend())
            output_c.write(p.get_dsend())
            output_c.write(p.get_dlsend())

        output_c.write(packets.code_packet_handlers_fill_initial)
        output_c.write(packets.code_packet_handlers_fill_capability)

def write_server_header(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for server/hand_gen.h to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path, wrap_header = "hand_gen", cplusplus = False) as f:
        f.write("""\
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
            if p.dirs.up and not p.no_handle:
                a=p.name[len("packet_"):]
                b = "".join(
                    ", %s" % field.get_handle_param()
                    for field in p.fields
                )
                if p.handle_per_conn:
                    sender = "struct connection *pc"
                else:
                    sender = "struct player *pplayer"
                if p.handle_via_packet:
                    f.write("""\
struct %s;
void handle_%s(%s, const struct %s *packet);
""" % (p.name, a, sender, p.name))
                else:
                    f.write("""\
void handle_%s(%s%s);
""" % (a, sender, b))

def write_client_header(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for client/packhand_gen.h to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path, wrap_header = "packhand_gen") as f:
        f.write("""\
/* utility */
#include "shared.h"

/* common */
#include "packets.h"

bool client_handle_packet(enum packet_type type, const void *packet);

""")
        for p in packets:
            if not p.dirs.down: continue

            a=p.name[len("packet_"):]
            b = ", ".join(
                field.get_handle_param()
                for field in p.fields
            ) or "void"
            if p.handle_via_packet:
                f.write("""\
struct %s;
void handle_%s(const struct %s *packet);
""" % (p.name, a, p.name))
            else:
                f.write("""\
void handle_%s(%s);
""" % (a, b))

def write_server_impl(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for server/hand_gen.c to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path) as f:
        f.write("""\
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "packets.h"

#include "hand_gen.h"

bool server_handle_packet(enum packet_type type, const void *packet,
                          struct player *pplayer, struct connection *pconn)
{
  switch (type) {
""")
        for p in packets:
            if not p.dirs.up: continue
            if p.no_handle: continue
            a=p.name[len("packet_"):]

            if p.handle_via_packet:
                args = ", packet"

            else:
                packet_arrow = "((const struct %s *)packet)->" % p.name
                args = "".join(
                    ",\n      " + field.get_handle_arg(packet_arrow)
                    for field in p.fields
                )

            if p.handle_per_conn:
                first_arg = "pconn"
            else:
                first_arg = "pplayer"

            f.write("""\
  case %s:
    handle_%s(%s%s);
    return TRUE;

""" % (p.type, a, first_arg, args))
        f.write("""\
  default:
    return FALSE;
  }
}
""")

def write_client_impl(path: "str | Path | None", packets: PacketsDefinition):
    """Write contents for client/packhand_gen.c to the given path"""
    if path is None:
        return
    with packets.cfg.open_write(path) as f:
        f.write("""\
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "packets.h"

#include "packhand_gen.h"

bool client_handle_packet(enum packet_type type, const void *packet)
{
  switch (type) {
""")
        for p in packets:
            if not p.dirs.down: continue
            if p.no_handle: continue
            a=p.name[len("packet_"):]

            if p.handle_via_packet:
                args="packet"
            else:
                packet_arrow = "((const struct %s *)packet)->" % p.name
                args = "".join(
                    ",\n      " + field.get_handle_arg(packet_arrow)
                    for field in p.fields
                )[1:]   # cut off initial comma

            f.write("""\
  case %s:
    handle_%s(%s);
    return TRUE;

""" % (p.type, a, args))
        f.write("""\
  default:
    return FALSE;
  }
}
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
