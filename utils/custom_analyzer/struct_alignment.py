#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import List

import click
from pycparser import c_generator, c_parser, parse_file


# used for ptr, enum, data[0], long, etc
default_align = 4
name_to_size = {"char": 1, "short": 2, "int": 4, "long": 8}
name_to_align = {"char": 1, "short": 2, "int": 4, "long": default_align}
enum_to_int = {}
list_of_enum: List[str] = []
only_detect_cfg = True
enable_sort_cfg = False


def get_pad_decl(size, idx):
    size = int(size)
    text = r"""
    typedef char uint8_t;
    uint8_t pad[0];
    """
    parser = c_parser.CParser()
    pad = parser.parse(text, filename="<none>")
    pad = pad.ext[1]
    pad.type.dim.value = str(size)
    if idx != 0:
        pad.name += str(idx)
        pad.type.type.declname += str(idx)
    return pad


def parse_enum(node):
    counter = -1
    for e in node.values.enumerators:
        if e.value:
            counter = int(e.value.value)
        else:
            counter += 1
        enum_to_int[e.name] = counter


def class_name(node):
    return node.__class__.__name__


def info_of_identifier_type(node):
    size = -1
    align = -1
    for name in node.names:
        if name in name_to_size and name in name_to_align:
            size = name_to_size[name]
            align = name_to_align[name]
            break
    if size == -1 or align == -1:
        raise Exception("{} not found for identifier".format(node.name))
    return size, align


def info_of_enum():
    return default_align, default_align


def info_of_ptr_decl():
    return default_align, default_align


def info_of_decl(node):
    child = node.type
    type = class_name(child)
    if type == "PtrDecl":
        return info_of_ptr_decl()
    elif type == "ArrayDecl":
        c = child.dim
        if class_name(c) == "Constant":
            num = int(c.value)
        elif class_name(c) == "ID":
            num = enum_to_int[c.name]
        elif class_name(c) == "BinaryOp":
            if c.op == "*":
                num = int(c.left.value) * int(c.right.value)
            elif c.op == "+":
                num = int(c.left.value) + int(c.right.value)
            elif c.op == "-":
                num = int(c.left.value) - int(c.right.value)
            else:
                c.show()
                raise Exception("{} unhandled operator".format(c.op))
        else:
            c.show()
            raise Exception("{} not found for decl".format(class_name(c)))
        s, a = info_of_type_decl(child.type)
        # array with size zero are meant to be casted
        if num == 0:
            a = max(a, default_align)
        return num * s, a
    elif type == "TypeDecl":
        return info_of_type_decl(child)
    else:
        child.show()
        raise Exception("{} not found for decl".format(type))


# function to sort data[0] at the bottom
def sort_member(entry):
    if class_name(entry["decl"].type) == "TypeDecl":
        if class_name(entry["decl"].type.type) == "Union":
            return 0
    if entry["size"] == 0:
        return 0
    else:
        return entry["align"]


def pad_struct(members):
    offset = 0
    pad_idx = 0
    max_align = 1
    padded_members = []
    if (not only_detect_cfg) and (enable_sort_cfg):
        members.sort(key=sort_member, reverse=True)
    for m in members:
        # these padding loops won't be needed if enable_sort_cfg is set
        pad_size = m["align"] - (offset % m["align"])
        if pad_size != m["align"]:
            if only_detect_cfg:
                print(members)
                m["decl"].show()
                raise Exception(
                    "Bad align, offset={}, align={}".format(offset, m["align"])
                )
            pad_decl = get_pad_decl(pad_size, pad_idx)
            padded_members.append({"decl": pad_decl, "size": pad_size, "align": 1})
            pad_idx += 1
            offset += pad_size
        padded_members.append(m)
        offset += m["size"]
        max_align = max(max_align, m["align"])

    pad_size = max_align - (offset % max_align)
    if pad_size != max_align:
        if only_detect_cfg:
            print(members)
            members[-1]["decl"].show()
            raise Exception(
                "Bad struct size, offset={}, align={}".format(offset, max_align)
            )
        pad_decl = get_pad_decl(pad_size, pad_idx)
        padded_members.append({"decl": pad_decl, "size": pad_size, "align": 1})
        pad_idx += 1
        offset += pad_size

    return padded_members


def info_of_struct(node):
    size = 0
    align = 1
    members = []
    for decl in node.decls:
        s, a = info_of_decl(decl)
        members.append({"decl": decl, "size": s, "align": a})
    members = pad_struct(members)
    node.decls = [entry["decl"] for entry in members]
    for entry in members:
        size += entry["size"]
        align = max(align, entry["align"])
    return size, align


def info_of_union(node):
    size = 0
    align = 1
    for decl in node.decls:
        s, a = info_of_decl(decl)
        size = max(size, s)
        align = max(align, a)
    if size % align != 0:
        if only_detect_cfg:
            node.show()
            raise Exception("un-aligned union, size={}, align={}".format(size, align))
        size = (int(size / align) + 1) * align
        pad = get_pad_decl(size, 0)
        node.decls.append(pad)
    return size, align


def info_of_type_decl(node):
    child = node.type
    if class_name(child) == "IdentifierType":
        if set(child.names) & set(list_of_enum):
            node.show(showcoord=True)
            raise Exception("Bad usage of enum={}".format(child.names))
        return info_of_identifier_type(child)
    elif class_name(child) == "Enum":
        parse_enum(child)
        return info_of_enum()
    elif class_name(child) == "Struct":
        return info_of_struct(child)
    elif class_name(child) == "Union":
        return info_of_union(child)
    elif class_name(child) == "TypeDecl":
        return info_of_type_decl(child)
    else:
        child.show()
        raise Exception("{} not found for type decl".format(class_name(child)))


@click.command()
@click.option(
    "--input", "-i", type=str, help="input file e.g. path of fb_tg_fw_pt_if.h"
)
@click.option(
    "--output", "-o", type=str, help="output/tmp file e.g. /tmp/fb_tg_fw_pt_if.h"
)
@click.option(
    "--only_detect/--fix", default=True, help="only_detect or fix alignment issues"
)
@click.option("--enable_sort/--disable_sort", default=False, help="sort struct members")
def cli(input: str, output: str, only_detect: bool, enable_sort: bool):
    """
    definitions
    1) alignment constraint: 8bit=1, 16bit=2, 32bit/64bit/data[0]=4
    2) alignment of struct/union = max of alignment of members
    3) alignment of array = alignment of single element
    throws exception if input violates:
    1) All structure structure members should be aligned
    2) Size of struct/union should be multiple of its alignment
    3) Enums should not be used (just definition is fine)

    options
    If --fix flag is used it will generate output to fix alignments
    If --enable_sort --fix used output struct member are in order of decreasing alignments
    """
    global only_detect_cfg
    global enable_sort_cfg
    only_detect_cfg = only_detect
    enable_sort_cfg = enable_sort
    with open(input) as f:
        input_lines = f.readlines()
        input_lines = ["#define __attribute__(x)\n"] + input_lines
    with open(output, "w") as f:
        f.write("".join(input_lines))

    ast = parse_file(output, use_cpp=True)

    for node in ast.ext:
        if node.__class__.__name__ == "Typedef":
            name = node.name
            child = node.type

            if class_name(child) == "PtrDecl":
                name_to_size[name], name_to_align[name] = info_of_ptr_decl()
            elif class_name(child) == "TypeDecl":
                name_to_size[name], name_to_align[name] = info_of_type_decl(child)
                if class_name(child.type) == "Enum":
                    list_of_enum.append(name)
            else:
                child.show()
                raise Exception("{} not found".format(class_name(child)))

    generator = c_generator.CGenerator()
    with open(output, "w") as f:
        f.write(generator.visit(ast))


if __name__ == "__main__":
    cli()
