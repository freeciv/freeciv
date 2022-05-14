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

# This script runs under Python 3.4 and up. Please leave it so.
# It might also run under older versions, but no such guarantees are made.

import re
import argparse
from pathlib import Path
from contextlib import contextmanager
from functools import partial
from itertools import chain, combinations


###################### Parsing Command Line Arguments ######################

### Script configuration
# See get_argparser for what each of these does
# Keep initial values in sync with argparser defaults
is_verbose = False
lazy_overwrite = False
generate_stats = False
generate_logs = True
use_log_macro = "log_packet_detailed"
fold_bool_into_header = True

def config_script(args):
    """Update script configuration from the given argument namespace.

    args should be a namespace of a shape like those
    produced by get_argparser().parse_args()"""

    global lazy_overwrite, is_verbose
    global generate_stats, generate_logs, use_log_macro
    global fold_bool_into_header

    is_verbose = args.verbose
    lazy_overwrite = args.lazy_overwrite

    generate_stats = args.gen_stats
    generate_logs = args.log_macro is not None
    use_log_macro = args.log_macro

    fold_bool_into_header = args.fold_bool

def file_path(s):
    """Parse the given path and check basic validity."""
    path = Path(s)

    if path.is_reserved() or not path.name:
        raise ValueError("not a valid file path: %r" % s)
    if path.exists() and not path.is_file():
        raise ValueError("not a file: %r" % s)

    return path

def get_argparser():
    parser = argparse.ArgumentParser(
        description = "Generate packet-related code from packets.def",
        add_help = False,   # we'll add a help option explicitly
    )

    # Argument groups
    # Note the order:
    # We want the path arguments to show up *first* in the help text

    paths = parser.add_argument_group(
        "Output paths",
        "The following parameters decide which output files are generated,"
        " and where the generated code is written.",
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

    path_args = (
        # (dest, option, canonical path)
        ("common_header_path", "--common-h", "common/packets_gen.h"),
        ("common_impl_path",   "--common-c", "common/packets_gen.c"),
        ("client_header_path", "--client-h", "client/packhand_gen.h"),
        ("client_impl_path",   "--client-c", "client/packhand_gen.c"),
        ("server_header_path", "--server-h", "server/hand_gen.h"),
        ("server_impl_path",   "--server-c", "server/hand_gen.c"),
    )

    for dest, option, canonical in path_args:
        paths.add_argument(option, dest = dest, type = file_path,
                           help = "output path for %s" % canonical)

    return parser

def verbose(s):
    if is_verbose:
        print(s)


####################### File access helper functions #######################

def write_disclaimer(f):
    f.write('''
 /****************************************************************************
 *                       THIS FILE WAS GENERATED                             *
 * Script: common/generate_packets.py                                        *
 * Input:  common/networking/packets.def                                     *
 *                       DO NOT CHANGE THIS FILE                             *
 ****************************************************************************/

''')

@contextmanager
def wrap_header(file, header_name, cplusplus=True):
    """Add multiple inclusion protection to the given file. If cplusplus
    is given (default), also add code for `extern "C" {}` wrapping"""
    name = "FC__%s_H" % header_name.upper()
    file.write("""
#ifndef {name}
#define {name}
""".format(name = name))

    if cplusplus:
        file.write("""
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
""")

    yield

    if cplusplus:
        file.write("""
#ifdef __cplusplus
}
#endif /* __cplusplus */
""")

    file.write("""
#endif /* {name} */
""".format(name = name))

@contextmanager
def fc_open(path):
    """Open a file for writing and write disclaimer.

    If enabled, lazily overwrites the given file."""
    path = Path(path)   # no-op if path is already a Path object
    verbose("writing %s" % path)

    if lazy_overwrite:
        open_fun = lazy_overwrite_open
    else:
        open_fun = partial(Path.open, mode = "w")

    with open_fun(path) as file:
        write_disclaimer(file)
        yield file
    verbose("done writing %s" % path)

def read_text(path, allow_missing=True):
    """Load all text from the file at the given path

    If allow_missing is True, return the empty string if the file does not
    exist. Otherwise, raise FileNotFoundException as per open()."""
    path = Path(path)
    if allow_missing and not path.exists():
        return ""
    with path.open() as file:
        return file.read()

def files_equal(path_a, path_b):
    """Return whether the contents of two text files are identical"""
    return read_text(path_a) == read_text(path_b)

@contextmanager
def lazy_overwrite_open(path, suffix=".tmp"):
    """Open a file for writing, but only actually overwrite it if the new
    content differs from the old content.

    This creates a temporary file by appending the given suffix to the given
    file path. In the event of an error, this temporary file might remain in
    the target file's directory."""

    path = Path(path)
    tmp_path = path.with_name(path.name + suffix)

    # if tmp_path already exists, assume it's left over from a previous,
    # failed run and can be overwritten without trouble
    verbose("lazy: using %s" % tmp_path)
    with tmp_path.open("w") as file:
        yield file

    if path.exists() and files_equal(tmp_path, path):
        verbose("lazy: no change, deleting...")
        tmp_path.unlink()
    else:
        verbose("lazy: content changed, replacing...")
        tmp_path.replace(path)

######################### General helper functions #########################

# Taken from https://docs.python.org/3.4/library/itertools.html#itertools-recipes
def powerset(iterable):
    "powerset([1,2,3]) --> () (1,) (2,) (3,) (1,2) (1,3) (2,3) (1,2,3)"
    s = list(iterable)
    return chain.from_iterable(combinations(s, r) for r in range(len(s)+1))

def prefix(prefix, text):
    """Prepend prefix to every line of text"""
    return "\n".join(prefix + line for line in text.split("\n"))


# matches an entire field definition line (type, fields and flag info)
FIELDS_LINE_PATTERN = re.compile(r"^\s*(\S+(?:\(.*\))?)\s+([^;()]*)\s*;\s*(.*)\s*$")
# matches a field type (dataio type and struct/public type)
TYPE_INFO_PATTERN = re.compile(r"^(.*)\((.*)\)$")
# matches a dataio type with float factor
FLOAT_FACTOR_PATTERN = re.compile(r"^(\D+)(\d+)$")
# matches a 2D-array field definition (name, first array size, second array size)
ARRAY_2D_PATTERN = re.compile(r"^(.*)\[(.*)\]\[(.*)\]$")
# matches a 1D-array field definition (name, array size)
ARRAY_1D_PATTERN = re.compile(r"^(.*)\[(.*)\]$")
# matches an add-cap flag (optional capability)
ADD_CAP_PATTERN = re.compile(r"^add-cap\((.*)\)$")
# matches a remove-cap flag (optional capability)
REMOVE_CAP_PATTERN = re.compile(r"^remove-cap\((.*)\)$")

# Parses a line of the form "COORD x, y; key" and returns a list of
# Field objects. types is a dict mapping type aliases to their meaning
def parse_fields(line, types):
    mo = FIELDS_LINE_PATTERN.fullmatch(line)
    if mo is None:
        raise ValueError("invalid field definition: %r" % line)
    type_text, fields_, flags = (i.strip() for i in mo.groups(""))

    # analyze type
    # FIXME: no infinite loop detection
    while type_text in types:
        type_text = types[type_text]

    typeinfo={}
    mo = TYPE_INFO_PATTERN.fullmatch(type_text)
    if mo is None:
        raise ValueError("malformed or undefined type: %r" % type_text)
    typeinfo["dataio_type"],typeinfo["struct_type"]=mo.groups()

    if typeinfo["struct_type"]=="float":
        mo = FLOAT_FACTOR_PATTERN.fullmatch(typeinfo["dataio_type"])
        if mo is None:
            raise ValueError("float type without float factor: %r" % type_text)
        typeinfo["dataio_type"]=mo.group(1)
        typeinfo["float_factor"]=int(mo.group(2))

    # analyze fields
    fields=[]
    for i in fields_.split(","):
        i=i.strip()
        t={}

        def f(x):
            arr=x.split(":")
            if len(arr)==1:
                return [x,x,x]
            elif len(arr) == 2:
                arr.append("old->"+arr[1])
                arr[1]="real_packet->"+arr[1]
                return arr
            else:
                raise ValueError("Invalid array size declaration: %r" % x)

        mo = ARRAY_2D_PATTERN.fullmatch(i)
        if mo:
            t["name"]=mo.group(1)
            t["is_array"]=2
            t["array_size1_d"],t["array_size1_u"],t["array_size1_o"]=f(mo.group(2))
            t["array_size2_d"],t["array_size2_u"],t["array_size2_o"]=f(mo.group(3))
        else:
            mo = ARRAY_1D_PATTERN.fullmatch(i)
            if mo:
                t["name"]=mo.group(1)
                t["is_array"]=1
                t["array_size_d"],t["array_size_u"],t["array_size_o"]=f(mo.group(2))
            else:
                t["name"]=i
                t["is_array"]=0
        fields.append(t)

    # analyze flags
    flaginfo={}
    arr = [
        stripped
        for flag in flags.split(",")
        for stripped in (flag.strip(),)
        if stripped
    ]
    flaginfo["is_key"]=("key" in arr)
    if flaginfo["is_key"]: arr.remove("key")
    flaginfo["diff"]=("diff" in arr)
    if flaginfo["diff"]: arr.remove("diff")
    adds=[]
    removes=[]
    remaining=[]
    for i in arr:
        mo = ADD_CAP_PATTERN.fullmatch(i)
        if mo:
            adds.append(mo.group(1))
            continue
        mo = REMOVE_CAP_PATTERN.fullmatch(i)
        if mo:
            removes.append(mo.group(1))
            continue
        remaining.append(i)
    if len(adds) + len(removes) > 1:
        raise ValueError("A field can only have one add-cap or remove-cap: %s" % line)

    if remaining:
        raise ValueError("unrecognized flags in field declaration: %s" % " ".join(remaining))

    if adds:
        flaginfo["add_cap"]=adds[0]
    else:
        flaginfo["add_cap"]=""

    if removes:
        flaginfo["remove_cap"]=removes[0]
    else:
        flaginfo["remove_cap"]=""

    return [Field(fieldinfo, typeinfo, flaginfo) for fieldinfo in fields]

# Class for a field (part of a packet). It has a name, serveral types,
# flags and some other attributes.
class Field:
    def __init__(self, fieldinfo, typeinfo, flaginfo):
        self.name = fieldinfo["name"]
        self.is_array = fieldinfo["is_array"]
        if self.is_array == 2:
            self.array_size1_d = fieldinfo["array_size1_d"]
            self.array_size1_u = fieldinfo["array_size1_u"]
            self.array_size1_o = fieldinfo["array_size1_o"]
            self.array_size2_d = fieldinfo["array_size2_d"]
            self.array_size2_u = fieldinfo["array_size2_u"]
            self.array_size2_o = fieldinfo["array_size2_o"]
        elif self.is_array == 1:
            self.array_size_d = fieldinfo["array_size_d"]
            self.array_size_u = fieldinfo["array_size_u"]
            self.array_size_o = fieldinfo["array_size_o"]

        self.dataio_type = typeinfo["dataio_type"]
        self.struct_type = typeinfo["struct_type"]
        self.float_factor = typeinfo.get("float_factor")
        self.is_struct = self.struct_type.startswith("struct")

        self.is_key = flaginfo["is_key"]
        self.diff = flaginfo["diff"]
        self.add_cap = flaginfo["add_cap"]
        self.remove_cap = flaginfo["remove_cap"]

    # Helper function for the dictionary variant of the % operator
    # ("%(name)s"%dict) or str.format ("{name}".format(**dict)).
    def get_dict(self, vars_):
        result=self.__dict__.copy()
        result.update(vars_)
        return result

    def get_handle_type(self):
        if self.dataio_type=="string" or self.dataio_type=="estring":
            return "const char *"
        if self.dataio_type=="worklist":
            return "const %s *"%self.struct_type
        if self.is_array:
            return "const %s *"%self.struct_type
        return self.struct_type+" "

    # Returns code which is used in the declaration of the field in
    # the packet struct.
    def get_declar(self):
        if self.is_array==2:
            return "{self.struct_type} {self.name}[{self.array_size1_d}][{self.array_size2_d}]".format(self = self)
        if self.is_array:
            return "{self.struct_type} {self.name}[{self.array_size_d}]".format(self = self)
        else:
            return "{self.struct_type} {self.name}".format(self = self)

    # Returns code which copies the arguments of the direct send
    # functions in the packet struct.
    def get_fill(self):
        if self.dataio_type=="worklist":
            return "  worklist_copy(&real_packet->{self.name}, {self.name});".format(self = self)
        if self.is_array==0:
            return "  real_packet->{self.name} = {self.name};".format(self = self)
        if self.dataio_type=="string" or self.dataio_type=="estring":
            return "  sz_strlcpy(real_packet->{self.name}, {self.name});".format(self = self)
        if self.is_array==1:
            return """  {{
    int i;

    for (i = 0; i < {self.array_size_u}; i++) {{
      real_packet->{self.name}[i] = {self.name}[i];
    }}
  }}""".format(self = self)

        return repr(self.__dict__)

    # Returns code which sets "differ" by comparing the field
    # instances of "old" and "readl_packet".
    def get_cmp(self):
        if self.dataio_type=="memory":
            return "  differ = (memcmp(old->{self.name}, real_packet->{self.name}, {self.array_size_d}) != 0);".format(self = self)
        if self.dataio_type=="bitvector":
            return "  differ = !BV_ARE_EQUAL(old->{self.name}, real_packet->{self.name});".format(self = self)
        if self.dataio_type in ["string", "estring"] and self.is_array==1:
            return "  differ = (strcmp(old->{self.name}, real_packet->{self.name}) != 0);".format(self = self)
        if self.dataio_type == "cm_parameter":
            return "  differ = !cm_are_parameter_equal(&old->{self.name}, &real_packet->{self.name});".format(self = self)
        if self.is_struct and self.is_array==0:
            return "  differ = !are_{self.dataio_type}s_equal(&old->{self.name}, &real_packet->{self.name});".format(self = self)
        if not self.is_array:
            return "  differ = (old->{self.name} != real_packet->{self.name});".format(self = self)

        if self.dataio_type=="string" or self.dataio_type=="estring":
            c = "strcmp(old->{self.name}[i], real_packet->{self.name}[i]) != 0".format(self = self)
            array_size_u = self.array_size1_u
            array_size_o = self.array_size1_o
        elif self.is_struct:
            c = "!are_{self.dataio_type}s_equal(&old->{self.name}[i], &real_packet->{self.name}[i])".format(self = self)
            array_size_u = self.array_size_u
            array_size_o = self.array_size_o
        else:
            c = "old->{self.name}[i] != real_packet->{self.name}[i]".format(self = self)
            array_size_u = self.array_size_u
            array_size_o = self.array_size_o

        return """
    {{
      differ = ({array_size_o} != {array_size_u});
      if (!differ) {{
        int i;

        for (i = 0; i < {array_size_u}; i++) {{
          if ({c}) {{
            differ = TRUE;
            break;
          }}
        }}
      }}
    }}""".format(c = c, array_size_u = array_size_u, array_size_o = array_size_o)

    @property
    def folded_into_head(self):
        return (
            fold_bool_into_header
            and self.struct_type == "bool"
            and not self.is_array
        )

    # Returns a code fragment which updates the bit of the this field
    # in the "fields" bitvector. The bit is either a "content-differs"
    # bit or (for bools which gets folded in the header) the actual
    # value of the bool.
    def get_cmp_wrapper(self, i, pack):
        if self.folded_into_head:
            if pack.is_info != "no":
                cmp = self.get_cmp()
                differ_part = '''
  if (differ) {
    different++;
  }
'''
            else:
                cmp = ""
                differ_part = ""
            b = "packet->{self.name}".format(self = self)
            return cmp + differ_part + '''  if (%s) {
    BV_SET(fields, %d);
  }

'''%(b,i)
        else:
            cmp = self.get_cmp()
            if pack.is_info != "no":
                return '''%s
  if (differ) {
    different++;
    BV_SET(fields, %d);
  }

'''%(cmp, i)
            else:
                return '''%s
  if (differ) {
    BV_SET(fields, %d);
  }

'''%(cmp, i)

    # Returns a code fragment which will put this field if the
    # content has changed. Does nothing for bools-in-header.
    def get_put_wrapper(self,packet,i,deltafragment):
        if fold_bool_into_header and self.struct_type=="bool" and \
           not self.is_array:
            return "  /* field {i:d} is folded into the header */\n".format(i = i)
        put=self.get_put(deltafragment)
        if packet.gen_log:
            f = '    {packet.log_macro}("  field \'{self.name}\' has changed");\n'.format(packet = packet, self = self)
        else:
            f=""
        if packet.gen_stats:
            s = "    stats_{packet.name}_counters[{i:d}]++;\n".format(packet = packet, i = i)
        else:
            s=""
        return """  if (BV_ISSET(fields, {i:d})) {{
{f}{s}  {put}
  }}
""".format(i = i, f = f, s = s, put = put)

    # Returns code which put this field.
    def get_put(self,deltafragment):
        return """#ifdef FREECIV_JSON_CONNECTION
  field_addr.name = \"{self.name}\";
#endif /* FREECIV_JSON_CONNECTION */
""".format(self = self) \
               + self.get_put_real(deltafragment);

    # The code which put this field before it is wrapped in address adding.
    def get_put_real(self,deltafragment):
        if self.dataio_type=="bitvector":
            return "DIO_BV_PUT(&dout, &field_addr, packet->{self.name});".format(self = self)

        if self.struct_type=="float" and not self.is_array:
            return "  DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{self.name}, {self.float_factor:d});".format(self = self)

        if self.dataio_type in ["worklist", "cm_parameter"]:
            return "  DIO_PUT({self.dataio_type}, &dout, &field_addr, &real_packet->{self.name});".format(self = self)

        if self.dataio_type in ["memory"]:
            return "  DIO_PUT({self.dataio_type}, &dout, &field_addr, &real_packet->{self.name}, {self.array_size_u});".format(self = self)

        arr_types=["string","estring","city_map"]
        if (self.dataio_type in arr_types and self.is_array==1) or \
           (self.dataio_type not in arr_types and self.is_array==0):
            return "  DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{self.name});".format(self = self)
        if self.is_struct:
            if self.is_array==2:
                c = "DIO_PUT({self.dataio_type}, &dout, &field_addr, &real_packet->{self.name}[i][j]);".format(self = self)
            else:
                c = "DIO_PUT({self.dataio_type}, &dout, &field_addr, &real_packet->{self.name}[i]);".format(self = self)
                array_size_u = self.array_size_u
        elif self.dataio_type=="string" or self.dataio_type=="estring":
            c = "DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{self.name}[i]);".format(self = self)
            array_size_u=self.array_size1_u

        elif self.struct_type=="float":
            if self.is_array==2:
                c = "  DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{self.name}[i][j], {self.float_factor:d});".format(self = self)
            else:
                c = "  DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{self.name}[i], {self.float_factor:d});".format(self = self)
                array_size_u = self.array_size_u
        else:
            if self.is_array==2:
                c = "DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{self.name}[i][j]);".format(self = self)
            else:
                c = "DIO_PUT({self.dataio_type}, &dout, &field_addr, real_packet->{self.name}[i]);".format(self = self)
                array_size_u = self.array_size_u

        if deltafragment and self.diff and self.is_array == 1:
            return """
    {{
      int i;

#ifdef FREECIV_JSON_CONNECTION
      int count = 0;

      for (i = 0; i < {self.array_size_u}; i++) {{
        if (old->{self.name}[i] != real_packet->{self.name}[i]) {{
          count++;
        }}
      }}
      /* Create the array. */
      DIO_PUT(farray, &dout, &field_addr, count + 1);

      /* Enter array. */
      field_addr.sub_location = plocation_elem_new(0);

      count = 0;
#endif /* FREECIV_JSON_CONNECTION */

      fc_assert({self.array_size_u} < 255);

      for (i = 0; i < {self.array_size_u}; i++) {{
        if (old->{self.name}[i] != real_packet->{self.name}[i]) {{
#ifdef FREECIV_JSON_CONNECTION
          /* Next diff array element. */
          field_addr.sub_location->number = count - 1;

          /* Create the diff array element. */
          DIO_PUT(farray, &dout, &field_addr, 2);

          /* Enter diff array element (start at the index address). */
          field_addr.sub_location->sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */
          DIO_PUT(uint8, &dout, &field_addr, i);

#ifdef FREECIV_JSON_CONNECTION
          /* Content address. */
          field_addr.sub_location->sub_location->number = 1;
#endif /* FREECIV_JSON_CONNECTION */
          {c}

#ifdef FREECIV_JSON_CONNECTION
          /* Exit diff array element. */
          free(field_addr.sub_location->sub_location);
          field_addr.sub_location->sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
        }}
      }}
#ifdef FREECIV_JSON_CONNECTION
      field_addr.sub_location->number = count - 1;

      /* Create the diff array element. */
      DIO_PUT(farray, &dout, &field_addr, {self.array_size_u});

      /* Enter diff array element. Point to index address. */
      field_addr.sub_location->sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */
      DIO_PUT(uint8, &dout, &field_addr, 255);

#ifdef FREECIV_JSON_CONNECTION
      /* Exit diff array element. */
      free(field_addr.sub_location->sub_location);
      field_addr.sub_location->sub_location = NULL;

      /* Exit array. */
      free(field_addr.sub_location);
      field_addr.sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
    }}""".format(self = self, c = c)
        if self.is_array == 2 and self.dataio_type != "string" \
           and self.dataio_type != "estring":
            return """
    {{
      int i, j;

#ifdef FREECIV_JSON_CONNECTION
      /* Create the outer array. */
      DIO_PUT(farray, &dout, &field_addr, {self.array_size1_u});

      /* Enter the outer array. */
      field_addr.sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

      for (i = 0; i < {self.array_size1_u}; i++) {{
#ifdef FREECIV_JSON_CONNECTION
        /* Next inner array (an element in the outer array). */
        field_addr.sub_location->number = i;

        /* Create the inner array. */
        DIO_PUT(farray, &dout, &field_addr, {self.array_size2_u});

        /* Enter the inner array. */
        field_addr.sub_location->sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

        for (j = 0; j < {self.array_size2_u}; j++) {{
#ifdef FREECIV_JSON_CONNECTION
          /* Next element (in the inner array). */
          field_addr.sub_location->sub_location->number = j;
#endif /* FREECIV_JSON_CONNECTION */
          {c}
        }}

#ifdef FREECIV_JSON_CONNECTION
        /* Exit the inner array. */
        free(field_addr.sub_location->sub_location);
        field_addr.sub_location->sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
      }}

#ifdef FREECIV_JSON_CONNECTION
      /* Exit the outer array. */
      free(field_addr.sub_location);
      field_addr.sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
    }}""".format(self = self, c = c)
        else:
            return """
    {{
      int i;

#ifdef FREECIV_JSON_CONNECTION
      /* Create the array. */
      DIO_PUT(farray, &dout, &field_addr, {array_size_u});

      /* Enter the array. */
      field_addr.sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */

      for (i = 0; i < {array_size_u}; i++) {{
#ifdef FREECIV_JSON_CONNECTION
        /* Next array element. */
        field_addr.sub_location->number = i;
#endif /* FREECIV_JSON_CONNECTION */
        {c}
      }}

#ifdef FREECIV_JSON_CONNECTION
      /* Exit array. */
      free(field_addr.sub_location);
      field_addr.sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
    }}""".format(c = c, array_size_u = array_size_u)

    # Returns a code fragment which will get the field if the
    # "fields" bitvector says so.
    def get_get_wrapper(self,packet,i,deltafragment):
        get=self.get_get(deltafragment)
        if fold_bool_into_header and self.struct_type=="bool" and \
           not self.is_array:
            return  "  real_packet->{self.name} = BV_ISSET(fields, {i:d});\n".format(self = self, i = i)
        get=prefix("    ",get)
        if packet.gen_log:
            f = "    {packet.log_macro}(\"  got field '{self.name}'\");\n".format(self = self, packet = packet)
        else:
            f=""
        return """  if (BV_ISSET(fields, {i:d})) {{
{f}{get}
  }}
""".format(i = i, f = f, get = get)

    # Returns code which get this field.
    def get_get(self,deltafragment):
        return """#ifdef FREECIV_JSON_CONNECTION
field_addr.name = \"{self.name}\";
#endif /* FREECIV_JSON_CONNECTION */
""".format(self = self) \
               + self.get_get_real(deltafragment);

    # The code which get this field before it is wrapped in address adding.
    def get_get_real(self,deltafragment):
        if self.struct_type=="float" and not self.is_array:
            return """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name}, {self.float_factor:d})) {{
  RECEIVE_PACKET_FIELD_ERROR({self.name});
}}""".format(self = self)
        if self.dataio_type=="bitvector":
            return """if (!DIO_BV_GET(&din, &field_addr, real_packet->{self.name})) {{
  RECEIVE_PACKET_FIELD_ERROR({self.name});
}}""".format(self = self)
        if self.dataio_type in ["string","estring","city_map"] and \
           self.is_array!=2:
            return """if (!DIO_GET({self.dataio_type}, &din, &field_addr, real_packet->{self.name}, sizeof(real_packet->{self.name}))) {{
  RECEIVE_PACKET_FIELD_ERROR({self.name});
}}""".format(self = self)
        if self.is_struct and self.is_array==0:
            return """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name})) {{
  RECEIVE_PACKET_FIELD_ERROR({self.name});
}}""".format(self = self)
        if not self.is_array:
            if self.struct_type in ["int","bool"]:
                return """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name})) {{
  RECEIVE_PACKET_FIELD_ERROR({self.name});
}}""".format(self = self)
            else:
                return """{{
  int readin;

  if (!DIO_GET({self.dataio_type}, &din, &field_addr, &readin)) {{
    RECEIVE_PACKET_FIELD_ERROR({self.name});
  }}
  real_packet->{self.name} = readin;
}}""".format(self = self)

        if self.is_struct:
            if self.is_array==2:
                c = """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name}[i][j])) {{
      RECEIVE_PACKET_FIELD_ERROR({self.name});
    }}""".format(self = self)
            else:
                c = """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name}[i])) {{
      RECEIVE_PACKET_FIELD_ERROR({self.name});
    }}""".format(self = self)
        elif self.dataio_type=="string" or self.dataio_type=="estring":
            c = """if (!DIO_GET({self.dataio_type}, &din, &field_addr, real_packet->{self.name}[i], sizeof(real_packet->{self.name}[i]))) {{
      RECEIVE_PACKET_FIELD_ERROR({self.name});
    }}""".format(self = self)
        elif self.struct_type=="float":
            if self.is_array==2:
                c = """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name}[i][j], {self.float_factor:d})) {{
      RECEIVE_PACKET_FIELD_ERROR({self.name});
    }}""".format(self = self)
            else:
                c = """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name}[i], {self.float_factor:d})) {{
      RECEIVE_PACKET_FIELD_ERROR({self.name});
    }}""".format(self = self)
        elif self.is_array==2:
            if self.struct_type in ["int","bool"]:
                c = """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name}[i][j])) {{
      RECEIVE_PACKET_FIELD_ERROR({self.name});
    }}""".format(self = self)
            else:
                c = """{{
      int readin;

      if (!DIO_GET({self.dataio_type}, &din, &field_addr, &readin)) {{
        RECEIVE_PACKET_FIELD_ERROR({self.name});
      }}
      real_packet->{self.name}[i][j] = readin;
    }}""".format(self = self)
        elif self.struct_type in ["int","bool"]:
            c = """if (!DIO_GET({self.dataio_type}, &din, &field_addr, &real_packet->{self.name}[i])) {{
      RECEIVE_PACKET_FIELD_ERROR({self.name});
    }}""".format(self = self)
        else:
            c = """{{
      int readin;

      if (!DIO_GET({self.dataio_type}, &din, &field_addr, &readin)) {{
        RECEIVE_PACKET_FIELD_ERROR({self.name});
      }}
      real_packet->{self.name}[i] = readin;
    }}""".format(self = self)

        if self.is_array==2:
            array_size_u=self.array_size1_u
            array_size_d=self.array_size1_d
        else:
            array_size_u=self.array_size_u
            array_size_d=self.array_size_d

        if not self.diff or self.dataio_type=="memory":
            if array_size_u != array_size_d:
                extra = """
  if ({array_size_u} > {array_size_d}) {{
    RECEIVE_PACKET_FIELD_ERROR({self.name}, ": truncation array");
  }}""".format(self = self, array_size_u = array_size_u, array_size_d = array_size_d)
            else:
                extra=""
            if self.dataio_type=="memory":
                return """{extra}
  if (!DIO_GET({self.dataio_type}, &din, &field_addr, real_packet->{self.name}, {array_size_u})) {{
    RECEIVE_PACKET_FIELD_ERROR({self.name});
  }}""".format(self = self, array_size_u = array_size_u, extra = extra)
            elif self.is_array==2 and self.dataio_type!="string" \
                 and self.dataio_type!="estring":
                return """
{{
  int i, j;

#ifdef FREECIV_JSON_CONNECTION
  /* Enter outer array. */
  field_addr.sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */
{extra}
  for (i = 0; i < {self.array_size1_u}; i++) {{
#ifdef FREECIV_JSON_CONNECTION
    /* Update address of outer array element (inner array). */
    field_addr.sub_location->number = i;

    /* Enter inner array. */
    field_addr.sub_location->sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */
    for (j = 0; j < {self.array_size2_u}; j++) {{
#ifdef FREECIV_JSON_CONNECTION
      /* Update address of element in inner array. */
      field_addr.sub_location->sub_location->number = j;
#endif /* FREECIV_JSON_CONNECTION */
      {c}
    }}

#ifdef FREECIV_JSON_CONNECTION
    /* Exit inner array. */
    free(field_addr.sub_location->sub_location);
    field_addr.sub_location->sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Exit outer array. */
  free(field_addr.sub_location);
  field_addr.sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
}}""".format(self = self, c = c, extra = extra)
            else:
                return """
{{
  int i;

#ifdef FREECIV_JSON_CONNECTION
  /* Enter array. */
  field_addr.sub_location = plocation_elem_new(0);
#endif /* FREECIV_JSON_CONNECTION */
{extra}
  for (i = 0; i < {array_size_u}; i++) {{
#ifdef FREECIV_JSON_CONNECTION
    field_addr.sub_location->number = i;
#endif /* FREECIV_JSON_CONNECTION */
    {c}
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Exit array. */
  free(field_addr.sub_location);
  field_addr.sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
}}""".format(array_size_u = array_size_u, c = c, extra = extra)
        elif deltafragment and self.diff and self.is_array == 1:
            return """
{{
#ifdef FREECIV_JSON_CONNECTION
int count;

/* Enter array. */
field_addr.sub_location = plocation_elem_new(0);

for (count = 0;; count++) {{
  int i;

  field_addr.sub_location->number = count;

  /* Enter diff array element (start at the index address). */
  field_addr.sub_location->sub_location = plocation_elem_new(0);
#else /* FREECIV_JSON_CONNECTION */
while (TRUE) {{
  int i;
#endif /* FREECIV_JSON_CONNECTION */

  if (!DIO_GET(uint8, &din, &field_addr, &i)) {{
    RECEIVE_PACKET_FIELD_ERROR({self.name});
  }}
  if (i == 255) {{
#ifdef FREECIV_JSON_CONNECTION
    /* Exit diff array element. */
    free(field_addr.sub_location->sub_location);
    field_addr.sub_location->sub_location = NULL;

    /* Exit diff array. */
    free(field_addr.sub_location);
    field_addr.sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */

    break;
  }}
  if (i > {array_size_u}) {{
    RECEIVE_PACKET_FIELD_ERROR({self.name},
                               \": unexpected value %%d \"
                               \"(> {array_size_u}) in array diff\",
                               i);
  }} else {{
#ifdef FREECIV_JSON_CONNECTION
    /* Content address. */
    field_addr.sub_location->sub_location->number = 1;
#endif /* FREECIV_JSON_CONNECTION */
    {c}
  }}

#ifdef FREECIV_JSON_CONNECTION
  /* Exit diff array element. */
  free(field_addr.sub_location->sub_location);
  field_addr.sub_location->sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
}}

#ifdef FREECIV_JSON_CONNECTION
/* Exit array. */
free(field_addr.sub_location);
field_addr.sub_location = NULL;
#endif /* FREECIV_JSON_CONNECTION */
}}""".format(self = self, array_size_u = array_size_u, c = c)
        else:
            return """
{{
  int i;

  for (i = 0; i < {array_size_u}; i++) {{
    {c}
  }}
}}""".format(array_size_u = array_size_u, c = c)


# Class which represents a capability variant.
class Variant:
    def __init__(self,poscaps,negcaps,name,fields,packet,no):
        self.log_macro=use_log_macro
        self.gen_stats=generate_stats
        self.gen_log=generate_logs
        self.name=name
        self.packet_name=packet.name
        self.fields=fields
        self.no=no

        self.no_packet=packet.no_packet
        self.want_post_recv=packet.want_post_recv
        self.want_pre_send=packet.want_pre_send
        self.want_post_send=packet.want_post_send
        self.type=packet.type
        self.delta=packet.delta
        self.is_info=packet.is_info
        self.differ_used = (
            (not self.no_packet)
            and self.delta
            and (
                self.is_info != "no"
                or any(
                    not field.folded_into_head
                    for field in self.fields
                )
            )
        )
        self.cancel=packet.cancel
        self.want_force=packet.want_force

        self.poscaps = set(poscaps)
        self.negcaps = set(negcaps)
        if self.poscaps or self.negcaps:
            cap_fmt = """has_capability("%s", capability)"""
            self.condition = " && ".join(chain(
                (cap_fmt % cap for cap in sorted(self.poscaps)),
                ("!" + cap_fmt % cap for cap in sorted(self.negcaps)),
            ))
        else:
            self.condition="TRUE"
        self.key_fields = [field for field in self.fields if field.is_key]
        self.other_fields = [field for field in self.fields if not field.is_key]
        self.bits=len(self.other_fields)
        self.keys_format=", ".join(["%d"]*len(self.key_fields))
        self.keys_arg = ", ".join("real_packet->" + field.name for field in self.key_fields)
        if self.keys_arg:
            self.keys_arg=",\n    "+self.keys_arg

        if len(self.fields)==0:
            self.delta=0
            self.no_packet=1

        if len(self.fields)>5 or self.name.split("_")[1]=="ruleset":
            self.handle_via_packet=1

        self.extra_send_args=""
        self.extra_send_args2=""
        self.extra_send_args3 = "".join(
            ", %s%s" % (field.get_handle_type(), field.name)
            for field in self.fields
        )

        if not self.no_packet:
            self.extra_send_args = ", const struct {self.packet_name} *packet".format(self = self) + self.extra_send_args
            self.extra_send_args2=', packet'+self.extra_send_args2

        if self.want_force:
            self.extra_send_args=self.extra_send_args+', bool force_to_send'
            self.extra_send_args2=self.extra_send_args2+', force_to_send'
            self.extra_send_args3=self.extra_send_args3+', bool force_to_send'

        self.receive_prototype = "static struct {self.packet_name} *receive_{self.name}(struct connection *pc)".format(self = self)
        self.send_prototype = "static int send_{self.name}(struct connection *pc{self.extra_send_args})".format(self = self)


        if self.no_packet:
            self.send_handler = "phandlers->send[{self.type}].no_packet = (int(*)(struct connection *)) send_{self.name};".format(self = self)
        elif self.want_force:
            self.send_handler = "phandlers->send[{self.type}].force_to_send = (int(*)(struct connection *, const void *, bool)) send_{self.name};".format(self = self)
        else:
            self.send_handler = "phandlers->send[{self.type}].packet = (int(*)(struct connection *, const void *)) send_{self.name};".format(self = self)
        self.receive_handler = "phandlers->receive[{self.type}] = (void *(*)(struct connection *)) receive_{self.name};".format(self = self)

    # See Field.get_dict
    def get_dict(self,vars_):
        result=self.__dict__.copy()
        result.update(vars_)
        return result

    # Returns a code fragment which contains the declarations of the
    # statistical counters of this packet.
    def get_stats(self):
        names = ", ".join(
            "\"%s\"" % field.name
            for field in self.other_fields
        )

        return """static int stats_{self.name}_sent;
static int stats_{self.name}_discarded;
static int stats_{self.name}_counters[{self.bits:d}];
static char *stats_{self.name}_names[] = {{{names}}};

""".format(self = self, names = names)

    # Returns a code fragment which declares the packet specific
    # bitvector. Each bit in this bitvector represents one non-key
    # field.
    def get_bitvector(self):
        return "BV_DEFINE({self.name}_fields, {self.bits});\n".format(self = self)

    # Returns a code fragment which is the packet specific part of
    # the delta_stats_report() function.
    def get_report_part(self):
        return """
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

    # Returns a code fragment which is the packet specific part of
    # the delta_stats_reset() function.
    def get_reset_part(self):
        return """
  stats_{self.name}_sent = 0;
  stats_{self.name}_discarded = 0;
  memset(stats_{self.name}_counters, 0,
         sizeof(stats_{self.name}_counters));
""".format(self = self)

    # Returns a code fragment which is the implementation of the hash
    # function. The hash function is using all key fields.
    def get_hash(self):
        if len(self.key_fields)==0:
            return "#define hash_{self.name} hash_const\n\n".format(self = self)
        else:
            intro = """static genhash_val_t hash_{self.name}(const void *vkey)
{{
""".format(self = self)

            body = """  const struct {self.packet_name} *key = (const struct {self.packet_name} *) vkey;

""".format(self = self)

            keys = ["key->" + field.name for field in self.key_fields]
            if len(keys)==1:
                a=keys[0]
            elif len(keys)==2:
                a="({} << 8) ^ {}".format(*keys)
            else:
                raise ValueError("unsupported number of key fields for %s" % self.name)
            body=body+('  return %s;\n'%a)
            extro="}\n\n"
            return intro+body+extro

    # Returns a code fragment which is the implementation of the cmp
    # function. The cmp function is using all key fields. The cmp
    # function is used for the hash table.
    def get_cmp(self):
        if len(self.key_fields)==0:
            return "#define cmp_{self.name} cmp_const\n\n".format(self = self)
        else:
            intro = """static bool cmp_{self.name}(const void *vkey1, const void *vkey2)
{{
""".format(self = self)
            body=""
            body += """  const struct {self.packet_name} *key1 = (const struct {self.packet_name} *) vkey1;
  const struct {self.packet_name} *key2 = (const struct {self.packet_name} *) vkey2;

""".format(self = self)
            for field in self.key_fields:
                body += """  return key1->{field.name} == key2->{field.name};
""".format(field = field)
            extro="}\n"
            return intro+body+extro

    # Returns a code fragment which is the implementation of the send
    # function. This is one of the two real functions. So it is rather
    # complex to create.
    def get_send(self):
        temp='''%(send_prototype)s
{
<real_packet1><delta_header>  SEND_PACKET_START(%(type)s);
<faddr><log><report><pre1><body><pre2><post>  SEND_PACKET_END(%(type)s);
}

'''
        if self.gen_stats:
            report='''
  stats_total_sent++;
  stats_%(name)s_sent++;
'''
        else:
            report=""
        if self.gen_log:
            log='\n  %(log_macro)s("%(name)s: sending info about (%(keys_format)s)"%(keys_arg)s);\n'
        else:
            log=""
        if self.want_pre_send:
            pre1='''
  {
    struct %(packet_name)s *tmp = fc_malloc(sizeof(*tmp));

    *tmp = *packet;
    pre_send_%(packet_name)s(pc, tmp);
    real_packet = tmp;
  }
'''
            pre2='''
  if (real_packet != packet) {
    free((void *) real_packet);
  }
'''
        else:
            pre1=""
            pre2=""

        if not self.no_packet:
            real_packet1="  const struct %(packet_name)s *real_packet = packet;\n"
        else:
            real_packet1=""

        if not self.no_packet:
            if self.delta:
                if self.want_force:
                    diff='force_to_send'
                else:
                    diff='0'
                delta_header = '''#ifdef FREECIV_DELTA_PROTOCOL
  %(name)s_fields fields;
  struct %(packet_name)s *old;
'''
                delta_header2 = '''  struct genhash **hash = pc->phs.sent + %(type)s;
'''
                if self.is_info != "no":
                    delta_header2 = delta_header2 + '''  int different = %(diff)s;
'''
                delta_header2 = delta_header2 + '''#endif /* FREECIV_DELTA_PROTOCOL */
'''
                body=self.get_delta_send_body()+"\n#ifndef FREECIV_DELTA_PROTOCOL"
            else:
                delta_header=""
                body="#if 1 /* To match endif */"
            body=body+"\n"
            for field in self.fields:
                body=body+field.get_put(0)+"\n"
            body=body+"\n#endif\n"
        else:
            body=""
            delta_header=""

        if self.want_post_send:
            if self.no_packet:
                post="  post_send_%(packet_name)s(pc, NULL);\n"
            else:
                post="  post_send_%(packet_name)s(pc, real_packet);\n"
        else:
            post=""

        if len(self.fields) != 0:
            faddr = '''#ifdef FREECIV_JSON_CONNECTION
  struct plocation field_addr;
  {
    struct plocation *field_addr_tmp = plocation_field_new(NULL);
    field_addr = *field_addr_tmp;
    FC_FREE(field_addr_tmp);
  }
#endif /* FREECIV_JSON_CONNECTION */
'''
        else:
            faddr = ""

        if delta_header != "":
            if self.differ_used:
                delta_header = delta_header + '''  bool differ;
''' + delta_header2
            else:
                delta_header = delta_header + delta_header2
        for i in range(2):
            for k,v in vars().items():
                if type(v)==type(""):
                    temp=temp.replace("<%s>"%k,v)
        return temp%self.get_dict(vars())

    # '''

    # Helper for get_send()
    def get_delta_send_body(self):
        intro='''
#ifdef FREECIV_DELTA_PROTOCOL
  if (NULL == *hash) {
    *hash = genhash_new_full(hash_%(name)s, cmp_%(name)s,
                             NULL, NULL, NULL, free);
  }
  BV_CLR_ALL(fields);

  if (!genhash_lookup(*hash, real_packet, (void **) &old)) {
    old = fc_malloc(sizeof(*old));
    *old = *real_packet;
    genhash_insert(*hash, old, old);
    memset(old, 0, sizeof(*old));
'''
        if self.is_info != "no":
            intro = intro + '''    different = 1;      /* Force to send. */
'''
        intro = intro + '''  }
'''
        body=""
        for i, field in enumerate(self.other_fields):
            body = body + field.get_cmp_wrapper(i, self)
        if self.gen_log:
            fl='    %(log_macro)s("  no change -> discard");\n'
        else:
            fl=""
        if self.gen_stats:
            s='    stats_%(name)s_discarded++;\n'
        else:
            s=""

        if self.is_info != "no":
            body += """
  if (different == 0) {{
{fl}{s}<pre2>    return 0;
  }}
""".format(fl = fl, s = s)

        body=body+'''
#ifdef FREECIV_JSON_CONNECTION
  field_addr.name = "fields";
#endif /* FREECIV_JSON_CONNECTION */
  DIO_BV_PUT(&dout, &field_addr, fields);
'''

        for field in self.key_fields:
            body=body+field.get_put(1)+"\n"
        body=body+"\n"

        for i, field in enumerate(self.other_fields):
            body=body+field.get_put_wrapper(self,i,1)
        body=body+'''
  *old = *real_packet;
'''

        # Cancel some is-info packets.
        for i in self.cancel:
            body=body+'''
  hash = pc->phs.sent + %s;
  if (NULL != *hash) {
    genhash_remove(*hash, real_packet);
  }
'''%i
        body=body+'''#endif /* FREECIV_DELTA_PROTOCOL */'''

        return intro+body

    # Returns a code fragment which is the implementation of the receive
    # function. This is one of the two real functions. So it is rather
    # complex to create.
    def get_receive(self):
        if self.delta:
            delta_header='''#ifdef FREECIV_DELTA_PROTOCOL
  %(name)s_fields fields;
  struct %(packet_name)s *old;
  struct genhash **hash = pc->phs.received + %(type)s;
#endif /* FREECIV_DELTA_PROTOCOL */
'''
            delta_body1='''
#ifdef FREECIV_DELTA_PROTOCOL
#ifdef FREECIV_JSON_CONNECTION
  field_addr.name = "fields";
#endif /* FREECIV_JSON_CONNECTION */
  DIO_BV_GET(&din, &field_addr, fields);
  '''
            body1=""
            for field in self.key_fields:
                body1=body1+prefix("  ",field.get_get(1))+"\n"
            body1=body1+"\n#else /* FREECIV_DELTA_PROTOCOL */\n"
            body2=self.get_delta_receive_body()
        else:
            delta_header=""
            delta_body1=""
            body1="#if 1 /* To match endif */\n"
            body2=""
        nondelta=""
        for field in self.fields:
            nondelta=nondelta+prefix("  ",field.get_get(0))+"\n"
        if not nondelta:
            nondelta="  real_packet->__dummy = 0xff;"
        body1=body1+nondelta+"\n#endif\n"

        if self.gen_log:
            log='  %(log_macro)s("%(name)s: got info about (%(keys_format)s)"%(keys_arg)s);\n'
        else:
            log=""

        if self.want_post_recv:
            post="  post_receive_%(packet_name)s(pc, real_packet);\n"
        else:
            post=""

        if len(self.fields) != 0:
            faddr = '''#ifdef FREECIV_JSON_CONNECTION
  struct plocation field_addr;
  {
    struct plocation *field_addr_tmp = plocation_field_new(NULL);
    field_addr = *field_addr_tmp;
    FC_FREE(field_addr_tmp);
  }
#endif /* FREECIV_JSON_CONNECTION */
'''
        else:
            faddr = ""

        return "".join((
            """\
%(receive_prototype)s
{
""",
            delta_header,
            """\
  RECEIVE_PACKET_START(%(packet_name)s, real_packet);
""",
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
        )) % self.get_dict(vars())

    # Helper for get_receive()
    def get_delta_receive_body(self):
        key1 = map("    {0.struct_type} {0.name} = real_packet->{0.name};".format, self.key_fields)
        key2 = map("    real_packet->{0.name} = {0.name};".format, self.key_fields)
        key1="\n".join(key1)
        key2="\n".join(key2)
        if key1: key1=key1+"\n\n"
        if key2: key2="\n\n"+key2
        if self.gen_log:
            fl='    %(log_macro)s("  no old info");\n'
        else:
            fl=""
        body = """
#ifdef FREECIV_DELTA_PROTOCOL
  if (NULL == *hash) {{
    *hash = genhash_new_full(hash_{self.name}, cmp_{self.name},
                             NULL, NULL, NULL, free);
  }}

  if (genhash_lookup(*hash, real_packet, (void **) &old)) {{
    *real_packet = *old;
  }} else {{
{key1}{fl}    memset(real_packet, 0, sizeof(*real_packet));{key2}
  }}

""".format(self = self, key1 = key1, key2 = key2, fl = fl)
        for i, field in enumerate(self.other_fields):
            body=body+field.get_get_wrapper(self,i,1)

        extro="""
  if (NULL == old) {
    old = fc_malloc(sizeof(*old));
    *old = *real_packet;
    genhash_insert(*hash, old, old);
  } else {
    *old = *real_packet;
  }
"""

        # Cancel some is-info packets.
        for i in self.cancel:
            extro=extro+'''
  hash = pc->phs.received + %s;
  if (NULL != *hash) {
    genhash_remove(*hash, real_packet);
  }
'''%i

        return body+extro+'''
#endif /* FREECIV_DELTA_PROTOCOL */
'''

# Class which represents a packet. A packet contains a list of fields.
class Packet:
    # matches a packet's header line (packet type, number, flags)
    HEADER_PATTERN = re.compile(r"^\s*(\S+)\s*=\s*(\d+)\s*;\s*(.*?)\s*$")
    # matches a packet cancel flag (cancelled packet type)
    CANCEL_PATTERN = re.compile(r"^cancel\((.*)\)$")

    def __init__(self, text, types):
        self.types=types
        self.log_macro=use_log_macro
        self.gen_stats=generate_stats
        self.gen_log=generate_logs
        text = text.strip()
        lines = text.split("\n")

        mo = __class__.HEADER_PATTERN.fullmatch(lines[0])
        if mo is None:
            raise ValueError("not a valid packet header line: %r" % lines[0])

        self.type=mo.group(1)
        self.name=self.type.lower()
        self.type_number=int(mo.group(2))
        if self.type_number not in range(65536):
            raise ValueError("packet number %d for %s outside legal range [0,65536)" % (self.type_number, self.name))
        dummy=mo.group(3)

        del lines[0]

        arr=list(item.strip() for item in dummy.split(",") if item)

        self.dirs=[]

        if "sc" in arr:
            self.dirs.append("sc")
            arr.remove("sc")
        if "cs" in arr:
            self.dirs.append("cs")
            arr.remove("cs")

        if not self.dirs:
            raise ValueError("no directions defined for %s" % self.name)

        # "no" means normal packet
        # "yes" means is-info packet
        # "game" means is-game-info packet
        self.is_info="no"
        if "is-info" in arr:
            self.is_info="yes"
            arr.remove("is-info")
        if "is-game-info" in arr:
            self.is_info="game"
            arr.remove("is-game-info")

        self.want_pre_send="pre-send" in arr
        if self.want_pre_send: arr.remove("pre-send")

        self.want_post_recv="post-recv" in arr
        if self.want_post_recv: arr.remove("post-recv")

        self.want_post_send="post-send" in arr
        if self.want_post_send: arr.remove("post-send")

        self.delta="no-delta" not in arr
        if not self.delta: arr.remove("no-delta")

        self.no_packet="no-packet" in arr
        if self.no_packet: arr.remove("no-packet")

        self.handle_via_packet="handle-via-packet" in arr
        if self.handle_via_packet: arr.remove("handle-via-packet")

        self.handle_per_conn="handle-per-conn" in arr
        if self.handle_per_conn: arr.remove("handle-per-conn")

        self.no_handle="no-handle" in arr
        if self.no_handle: arr.remove("no-handle")

        self.dsend_given="dsend" in arr
        if self.dsend_given: arr.remove("dsend")

        self.want_lsend="lsend" in arr
        if self.want_lsend: arr.remove("lsend")

        self.want_force="force" in arr
        if self.want_force: arr.remove("force")

        self.cancel=[]
        removes=[]
        remaining=[]
        for i in arr:
            mo = __class__.CANCEL_PATTERN.fullmatch(i)
            if mo:
                self.cancel.append(mo.group(1))
                continue
            remaining.append(i)

        if remaining:
            raise ValueError("unrecognized flags for %s: %r" % (self.name, remaining))

        self.fields = [
            field
            for line in lines
            for field in parse_fields(line, types)
        ]
        self.key_fields = [field for field in self.fields if field.is_key]
        self.other_fields = [field for field in self.fields if not field.is_key]
        self.bits=len(self.other_fields)
        self.keys_format=", ".join(["%d"]*len(self.key_fields))
        self.keys_arg = ", ".join(
            "real_packet->" + field.name
            for field in self.key_fields
        )
        if self.keys_arg:
            self.keys_arg=",\n    "+self.keys_arg


        self.want_dsend=self.dsend_given

        if len(self.fields)==0:
            self.delta=0
            self.no_packet=1

            if self.want_dsend:
                raise ValueError("requested dsend for %s without fields isn't useful" % self.name)

        if len(self.fields)>5 or self.name.split("_")[1]=="ruleset":
            self.handle_via_packet=1

        self.extra_send_args=""
        self.extra_send_args2=""
        self.extra_send_args3 = "".join(
            ", %s%s" % (field.get_handle_type(), field.name)
            for field in self.fields
        )

        if not self.no_packet:
            self.extra_send_args = ", const struct {self.name} *packet".format(self = self) + self.extra_send_args
            self.extra_send_args2=', packet'+self.extra_send_args2

        if self.want_force:
            self.extra_send_args=self.extra_send_args+', bool force_to_send'
            self.extra_send_args2=self.extra_send_args2+', force_to_send'
            self.extra_send_args3=self.extra_send_args3+', bool force_to_send'

        self.send_prototype = "int send_{self.name}(struct connection *pc{self.extra_send_args})".format(self = self)
        if self.want_lsend:
            self.lsend_prototype = "void lsend_{self.name}(struct conn_list *dest{self.extra_send_args})".format(self = self)
        if self.want_dsend:
            self.dsend_prototype = "int dsend_{self.name}(struct connection *pc{self.extra_send_args3})".format(self = self)
            if self.want_lsend:
                self.dlsend_prototype = "void dlsend_{self.name}(struct conn_list *dest{self.extra_send_args3})".format(self = self)

        # create cap variants
        all_caps = self.all_caps    # valid, since self.fields is already set
        self.variants=[]
        for i, poscaps in enumerate(powerset(sorted(all_caps))):
            negcaps = all_caps.difference(poscaps)
            fields = [
                field
                for field in self.fields
                if (not field.add_cap and not field.remove_cap)
                or (field.add_cap and field.add_cap in poscaps)
                or (field.remove_cap and field.remove_cap in negcaps)
            ]
            no=i+100

            self.variants.append(Variant(poscaps,negcaps,"%s_%d"%(self.name,no),fields,self,no))

    @property
    def all_caps(self):
        """Set of all capabilities affecting this packet"""
        return ({f.add_cap for f in self.fields if f.add_cap}
                | {f.remove_cap for f in self.fields if f.remove_cap})


    # Returns a code fragment which contains the struct for this packet.
    def get_struct(self):
        intro = "struct {self.name} {{\n".format(self = self)
        extro="};\n\n"

        body = "".join(
            "  %s;\n" % field.get_declar()
            for field in chain(self.key_fields, self.other_fields)
        ) or "  char __dummy;\t\t\t/* to avoid malloc(0); */\n"
        return intro+body+extro
    # '''

    # Returns a code fragment which represents the prototypes of the
    # send and receive functions for the header file.
    def get_prototypes(self):
        result=self.send_prototype+";\n"
        if self.want_lsend:
            result=result+self.lsend_prototype+";\n"
        if self.want_dsend:
            result=result+self.dsend_prototype+";\n"
            if self.want_lsend:
                result=result+self.dlsend_prototype+";\n"
        return result+"\n"

    # See Field.get_dict
    def get_dict(self, vars_):
        result=self.__dict__.copy()
        result.update(vars_)
        return result

    # See Variant.get_stats
    def get_stats(self):
        return "".join(v.get_stats() for v in self.variants)

    # See Variant.get_report_part
    def get_report_part(self):
        return "".join(v.get_report_part() for v in self.variants)

    # See Variant.get_reset_part
    def get_reset_part(self):
        return "".join(v.get_reset_part() for v in self.variants)

    def get_send(self):
        if self.no_packet:
            func="no_packet"
            args=""
        elif self.want_force:
            func="force_to_send"
            args=", packet, force_to_send"
        else:
            func="packet"
            args=", packet"

        return """{self.send_prototype}
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

    def get_variants(self):
        result=""
        for v in self.variants:
            if v.delta:
                result=result+"#ifdef FREECIV_DELTA_PROTOCOL\n"
                result=result+v.get_hash()
                result=result+v.get_cmp()
                result=result+v.get_bitvector()
                result=result+"#endif /* FREECIV_DELTA_PROTOCOL */\n\n"
            result=result+v.get_receive()
            result=result+v.get_send()
        return result

    # Returns a code fragment which is the implementation of the
    # lsend function.
    def get_lsend(self):
        if not self.want_lsend: return ""
        return """{self.lsend_prototype}
{{
  conn_list_iterate(dest, pconn) {{
    send_{self.name}(pconn{self.extra_send_args2});
  }} conn_list_iterate_end;
}}

""".format(self = self)

    # Returns a code fragment which is the implementation of the
    # dsend function.
    def get_dsend(self):
        if not self.want_dsend: return ""
        fill = "\n".join(field.get_fill() for field in self.fields)
        return """{self.dsend_prototype}
{{
  struct {self.name} packet, *real_packet = &packet;

{fill}

  return send_{self.name}(pc, real_packet);
}}

""".format(self = self, fill = fill)

    # Returns a code fragment which is the implementation of the
    # dlsend function.
    def get_dlsend(self):
        if not (self.want_lsend and self.want_dsend): return ""
        fill = "\n".join(field.get_fill() for field in self.fields)
        return """{self.dlsend_prototype}
{{
  struct {self.name} packet, *real_packet = &packet;

{fill}

  lsend_{self.name}(dest, real_packet);
}}

""".format(self = self, fill = fill)


def all_caps_union(packets):
    """Return a set of all capabilities affecting the given packets"""
    return set().union(*(p.all_caps for p in packets))

# Returns a code fragment which is the implementation of the
# packet_functional_capability string.
def get_packet_functional_capability(packets):
    all_caps = all_caps_union(packets)
    return '''
const char *const packet_functional_capability = "%s";
'''%' '.join(sorted(all_caps))

# Returns a code fragment which is the implementation of the
# delta_stats_report() function.
def get_report(packets):
    if not generate_stats: return 'void delta_stats_report(void) {}\n\n'

    intro='''
void delta_stats_report(void) {
  int i;

'''
    extro='}\n\n'
    body = "".join(
        packet.get_report_part()
        for packet in packets
    )
    return intro+body+extro

# Returns a code fragment which is the implementation of the
# delta_stats_reset() function.
def get_reset(packets):
    if not generate_stats: return 'void delta_stats_reset(void) {}\n\n'
    intro='''
void delta_stats_reset(void) {
'''
    extro='}\n\n'
    body = "".join(
        packet.get_reset_part()
        for packet in packets
    )
    return intro+body+extro

# Returns a code fragment which is the implementation of the
# packet_name() function.
def get_packet_name(packets):
    intro='''const char *packet_name(enum packet_type type)
{
  static const char *const names[PACKET_LAST] = {
'''

    mapping = {
        packet.type_number: packet
        for packet in packets
    }

    last=-1
    body=""
    for n in sorted(mapping.keys()):
        body += '    "unknown",\n' * (n - last - 1)
        body=body+'    "%s",\n'%mapping[n].type
        last=n

    extro='''  };

  return (type < PACKET_LAST ? names[type] : "unknown");
}

'''
    return intro+body+extro

# Returns a code fragment which is the implementation of the
# packet_has_game_info_flag() function.
def get_packet_has_game_info_flag(packets):
    intro='''bool packet_has_game_info_flag(enum packet_type type)
{
  static const bool flag[PACKET_LAST] = {
'''

    mapping = {
        packet.type_number: packet
        for packet in packets
    }

    last=-1
    body=""
    for n in sorted(mapping.keys()):
        body += '    FALSE,\n' * (n - last - 1)
        if mapping[n].is_info!="game":
            body=body+'    FALSE, /* %s */\n'%mapping[n].type
        else:
            body=body+'    TRUE, /* %s */\n'%mapping[n].type
        last=n

    extro='''  };

  return (type < PACKET_LAST ? flag[type] : FALSE);
}

'''
    return intro+body+extro

# Returns a code fragment which is the implementation of the
# packet_handlers_fill_initial() function.
def get_packet_handlers_fill_initial(packets):
    intro='''void packet_handlers_fill_initial(struct packet_handlers *phandlers)
{
'''
    all_caps = all_caps_union(packets)
    for cap in sorted(all_caps):
        intro=intro+'''  fc_assert_msg(has_capability("{0}", our_capability),
                "Packets have support for unknown '{0}' capability!");
'''.format(cap)

    sc_packets=[]
    cs_packets=[]
    unrestricted=[]
    for p in packets:
        if len(p.variants)==1:
            # Packets with variants are correctly handled in
            # packet_handlers_fill_capability(). They may remain without
            # handler at connecting time, because it would be anyway wrong
            # to use them before the network capability string would be
            # known.
            if len(p.dirs)==1 and p.dirs[0]=="sc":
                sc_packets.append(p)
            elif len(p.dirs)==1 and p.dirs[0]=="cs":
                cs_packets.append(p)
            else:
                unrestricted.append(p)

    body=""
    for p in unrestricted:
        body += """  {p.variants[0].send_handler}
  {p.variants[0].receive_handler}
""".format(p = p)
    body=body+'''  if (is_server()) {
'''
    for p in sc_packets:
        body += """    {p.variants[0].send_handler}
""".format(p = p)
    for p in cs_packets:
        body += """    {p.variants[0].receive_handler}
""".format(p = p)
    body=body+'''  } else {
'''
    for p in cs_packets:
        body += """    {p.variants[0].send_handler}
""".format(p = p)
    for p in sc_packets:
        body += """    {p.variants[0].receive_handler}
""".format(p = p)

    extro='''  }
}

'''
    return intro+body+extro

# Returns a code fragment which is the implementation of the
# packet_handlers_fill_capability() function.
def get_packet_handlers_fill_capability(packets):
    intro='''void packet_handlers_fill_capability(struct packet_handlers *phandlers,
                                     const char *capability)
{
'''

    sc_packets=[]
    cs_packets=[]
    unrestricted=[]
    for p in packets:
        if len(p.variants)>1:
            if len(p.dirs)==1 and p.dirs[0]=="sc":
                sc_packets.append(p)
            elif len(p.dirs)==1 and p.dirs[0]=="cs":
                cs_packets.append(p)
            else:
                unrestricted.append(p)

    body=""
    for p in unrestricted:
        body=body+"  "
        for v in p.variants:
            body += """if ({v.condition}) {{
    {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
    {v.send_handler}
    {v.receive_handler}
  }} else """.format(v = v)
        body += """{{
    log_error("Unknown {v.type} variant for cap %s", capability);
  }}
""".format(v = v)
    if len(cs_packets)>0 or len(sc_packets)>0:
        body=body+'''  if (is_server()) {
'''
        for p in sc_packets:
            body=body+"    "
            for v in p.variants:
                body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
      {v.send_handler}
    }} else """.format(v = v)
            body += """{{
      log_error("Unknown {v.type} variant for cap %s", capability);
    }}
""".format(v = v)
        for p in cs_packets:
            body=body+"    "
            for v in p.variants:
                body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
      {v.receive_handler}
    }} else """.format(v = v)
            body += """{{
      log_error("Unknown {v.type} variant for cap %s", capability);
    }}
""".format(v = v)
        body=body+'''  } else {
'''
        for p in cs_packets:
            body=body+"    "
            for v in p.variants:
                body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
      {v.send_handler}
    }} else """.format(v = v)
            body += """{{
      log_error("Unknown {v.type} variant for cap %s", capability);
    }}
""".format(v = v)
        for p in sc_packets:
            body=body+"    "
            for v in p.variants:
                body += """if ({v.condition}) {{
      {v.log_macro}("{v.type}: using variant={v.no} cap=%s", capability);
      {v.receive_handler}
    }} else """.format(v = v)
            body += """{{
      log_error("Unknown {v.type} variant for cap %s", capability);
    }}
""".format(v = v)
        body=body+'''  }
'''

    extro='''}
'''
    return intro+body+extro

# Returns a code fragment which is the declartion of
# "enum packet_type".
def get_enum_packet(packets):
    intro="enum packet_type {\n"

    mapping={}
    for p in packets:
        if p.type_number in mapping:
            num = p.type_number
            other = mapping[num]
            raise ValueError("Duplicate packet ID %d: %s and %s" % (num, other.name, p.name))
        mapping[p.type_number]=p

    last=-1
    body=""
    for i in sorted(mapping.keys()):
        p=mapping[i]
        if i!=last+1:
            line="  %s = %d,"%(p.type,i)
        else:
            line="  %s,"%(p.type)

        if (i%10)==0:
            line="%-40s /* %d */"%(line,i)
        body=body+line+"\n"

        last=i
    extro='''
  PACKET_LAST  /* leave this last */
};

'''
    return intro+body+extro


####################### Parsing packets.def contents #######################

# matches /* ... */ block comments as well as # ... and // ... EOL comments
COMMENT_PATTERN = re.compile(r"""
    (?:         # block comment
        /\*         # initial /*
        (?:.|\s)*?  # note the reluctant quantifier
        \*/         # terminating */
    ) | (?:     # EOL comment
        (?:\#|//)   # initial # or //
        .*          # does *not* match newline without DOTALL
        $           # matches line end in MULTILINE mode
    )
""", re.VERBOSE | re.MULTILINE)
# matches "type alias = dest" lines
TYPE_PATTERN = re.compile(r"^type\s+(?P<alias>\S+)\s*=\s*(?P<dest>.+)\s*$")
# matches the "end" line separating packet definitions
PACKET_SEP_PATTERN = re.compile(r"^end$", re.MULTILINE)

def packets_def_lines(def_text):
    """Yield only actual content lines without comments and whitespace"""
    text = COMMENT_PATTERN.sub("", def_text)
    return filter(None, map(str.strip, text.split("\n")))

def parse_packets_def(def_text):
    """Parse the given string as contents of packets.def"""

    # parse type alias definitions
    packets_lines = []
    types = {}
    for line in packets_def_lines(def_text):
        mo = TYPE_PATTERN.fullmatch(line)
        if mo is None:
            packets_lines.append(line)
            continue

        alias = mo.group("alias")
        dest = mo.group("dest")
        if alias in types:
            if dest == types[alias]:
                verbose("duplicate typedef: %r = %r" % (alias, dest))
                continue
            else:
                raise ValueError("duplicate type alias %r: %r and %r"
                                    % (alias, types[alias], dest))
        types[alias] = dest

    # parse packet definitions
    packets=[]
    for packet_text in PACKET_SEP_PATTERN.split("\n".join(packets_lines)):
        packet_text = packet_text.strip()
        if packet_text:
            packets.append(Packet(packet_text, types))

    # Note: only the packets are returned, as the type aliases
    # are not needed any further
    return packets


########################### Writing output files ###########################

def write_common_header(path, packets):
    """Write contents for common/packets_gen.h to the given path"""
    if path is None:
        return
    with fc_open(path) as output_h, wrap_header(output_h, "packets_gen"):
        output_h.write('''
/* common */
#include "actions.h"
#include "city.h"
#include "disaster.h"
#include "unit.h"

/* common/aicore */
#include "cm.h"

''')

        # write structs
        for p in packets:
            output_h.write(p.get_struct())

        output_h.write(get_enum_packet(packets))

        # write function prototypes
        for p in packets:
            output_h.write(p.get_prototypes())
        output_h.write('''
void delta_stats_report(void);
void delta_stats_reset(void);
''')

def write_common_impl(path, packets):
    """Write contents for common/packets_gen.c to the given path"""
    if path is None:
        return
    with fc_open(path) as output_c:
        output_c.write('''
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
''')
        output_c.write(get_packet_functional_capability(packets))
        output_c.write('''
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
''')

        if generate_stats:
            output_c.write('''
static int stats_total_sent;

''')

        if generate_stats:
            # write stats
            for p in packets:
                output_c.write(p.get_stats())
            # write report()
        output_c.write(get_report(packets))
        output_c.write(get_reset(packets))

        output_c.write(get_packet_name(packets))
        output_c.write(get_packet_has_game_info_flag(packets))

        # write hash, cmp, send, receive
        for p in packets:
            output_c.write(p.get_variants())
            output_c.write(p.get_send())
            output_c.write(p.get_lsend())
            output_c.write(p.get_dsend())
            output_c.write(p.get_dlsend())

        output_c.write(get_packet_handlers_fill_initial(packets))
        output_c.write(get_packet_handlers_fill_capability(packets))

def write_server_header(path, packets):
    """Write contents for server/hand_gen.h to the given path"""
    if path is None:
        return
    with fc_open(path) as f, wrap_header(f, "hand_gen", cplusplus = False):
        f.write('''
/* utility */
#include "shared.h"

/* common */
#include "fc_types.h"
#include "packets.h"

struct connection;

bool server_handle_packet(enum packet_type type, const void *packet,
                          struct player *pplayer, struct connection *pconn);

''')

        for p in packets:
            if "cs" in p.dirs and not p.no_handle:
                a=p.name[len("packet_"):]
                b = "".join(
                    ", %s%s" % (field.get_handle_type(), field.name)
                    for field in p.fields
                )
                if p.handle_via_packet:
                    f.write('struct %s;\n'%p.name)
                    if p.handle_per_conn:
                        f.write('void handle_%s(struct connection *pc, const struct %s *packet);\n'%(a,p.name))
                    else:
                        f.write('void handle_%s(struct player *pplayer, const struct %s *packet);\n'%(a,p.name))
                else:
                    if p.handle_per_conn:
                        f.write('void handle_%s(struct connection *pc%s);\n'%(a,b))
                    else:
                        f.write('void handle_%s(struct player *pplayer%s);\n'%(a,b))

def write_client_header(path, packets):
    """Write contents for client/packhand_gen.h to the given path"""
    if path is None:
        return
    with fc_open(path) as f, wrap_header(f, "packhand_gen"):
        f.write('''
/* utility */
#include "shared.h"

/* common */
#include "packets.h"

bool client_handle_packet(enum packet_type type, const void *packet);

''')
        for p in packets:
            if "sc" not in p.dirs: continue

            a=p.name[len("packet_"):]
            b = ", ".join(
                "%s%s" % (field.get_handle_type(), field.name)
                for field in p.fields
            ) or "void"
            if p.handle_via_packet:
                f.write('struct %s;\n'%p.name)
                f.write('void handle_%s(const struct %s *packet);\n'%(a,p.name))
            else:
                f.write('void handle_%s(%s);\n'%(a,b))

def write_server_impl(path, packets):
    """Write contents for server/hand_gen.c to the given path"""
    if path is None:
        return
    with fc_open(path) as f:
        f.write('''

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
''')
        for p in packets:
            if "cs" not in p.dirs: continue
            if p.no_handle: continue
            a=p.name[len("packet_"):]
            c='((const struct %s *)packet)->'%p.name
            b=[]
            for x in p.fields:
                y="%s%s"%(c,x.name)
                if x.dataio_type=="worklist":
                    y="&"+y
                b.append(y)
            b=",\n      ".join(b)
            if b:
                b=",\n      "+b

            if p.handle_via_packet:
                if p.handle_per_conn:
                    args="pconn, packet"
                else:
                    args="pplayer, packet"

            else:
                if p.handle_per_conn:
                    args="pconn"+b
                else:
                    args="pplayer"+b

            f.write('''  case %s:
    handle_%s(%s);
    return TRUE;

'''%(p.type,a,args))
        f.write('''  default:
    return FALSE;
  }
}
''')

def write_client_impl(path, packets):
    """Write contents for client/packhand_gen.c to the given path"""
    if path is None:
        return
    with fc_open(path) as f:
        f.write('''

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "packets.h"

#include "packhand_gen.h"

bool client_handle_packet(enum packet_type type, const void *packet)
{
  switch (type) {
''')
        for p in packets:
            if "sc" not in p.dirs: continue
            if p.no_handle: continue
            a=p.name[len("packet_"):]
            c='((const struct %s *)packet)->'%p.name
            b=[]
            for x in p.fields:
                y="%s%s"%(c,x.name)
                if x.dataio_type=="worklist":
                    y="&"+y
                b.append(y)
            b=",\n      ".join(b)
            if b:
                b="\n      "+b

            if p.handle_via_packet:
                args="packet"
            else:
                args=b

            f.write('''  case %s:
    handle_%s(%s);
    return TRUE;

'''%(p.type,a,args))
        f.write('''  default:
    return FALSE;
  }
}
''')


# Main function. It reads and parses the input and generates the
# various files.
def main(raw_args=None):
    ### parsing arguments
    global is_verbose
    script_args = get_argparser().parse_args(raw_args)
    config_script(script_args)

    ### parsing input
    src_dir = Path(__file__).parent
    input_path = src_dir / "networking" / "packets.def"

    def_text = read_text(input_path, allow_missing = False)
    packets = parse_packets_def(def_text)
    ### parsing finished

    write_common_header(script_args.common_header_path, packets)
    write_common_impl(script_args.common_impl_path, packets)
    write_server_header(script_args.server_header_path, packets)
    write_client_header(script_args.client_header_path, packets)
    write_server_impl(script_args.server_impl_path, packets)
    write_client_impl(script_args.client_impl_path, packets)


if __name__ == "__main__":
    main()
