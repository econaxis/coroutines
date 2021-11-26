from pycparser import parse_file
from pycparser import c_ast, c_generator
import copy

from pycparser.c_ast import Decl, TypeDecl, IdentifierType, Struct, FileAST, Assignment, ID, StructRef, Constant, \
    FuncCall, ExprList, UnaryOp, Cast, Return, Compound, PtrDecl, FuncDef, FuncDecl, ParamList

text = str(open("gc.c", "r").read())

ast = parse_file("gc.c", use_cpp=True,
                 cpp_args=[
                     '-D__attribute__(x)=',
                     '-D__deprecated__(x)=',
                     '-I../pycparser/utils/fake_libc_include', '-I/usr/include/glib-2.0',
                     '-I/usr/lib/x86_64-linux-gnu/glib-2.0/include'])

files = set()


def ours(ast):
    for stmt in ast.ext:
        if stmt.coord.file == 'gc.c':
            yield stmt


ast1 = list(ours(ast))[31:32]

GCTYPES = {
    "ArrayType", "GCDouble", "GCInt", "GCPointer", "GCString", "ArrOfNested"}

USERTYPES = {
    "Nested1",
}


def extract_name(elem):
    if type(elem.type) == PtrDecl:
        return extract_name(elem.type)
    if type(elem.type) == IdentifierType:
        return elem.type.names[0]
    if type(elem.type) == Struct:
        return elem.type.name
    if type(elem.type) == TypeDecl:
        return extract_name(elem.type)
    raise RuntimeError


def collect_gc_roots(stmt):
    res = []
    for index, a in enumerate(stmt.block_items):
        if type(a) == Decl and extract_name(a) in GCTYPES.union(USERTYPES):
            stmt.block_items[index] = None
            res.append(a)

    stmt.block_items = list(filter(lambda k: k is not None, stmt.block_items))
    return res


def recurse(node, fn):
    fn(node)
    for child in node:
        recurse(child, fn)


def change_local_vars(node, local_vars):
    if type(node) == StructRef:
        if type(node.name) == StructRef:
            change_local_vars(node.name, local_vars)
        elif node.name.name in local_vars:
            node.name = ID(f"stack_frame.{node.name.name}")
    elif type(node) == ID and node.name in local_vars:
        node.name = f"stack_frame.{node.name}"
    else:
        for n in node:
            change_local_vars(n, local_vars)


def decl_name_type(name, type, struct=False, ptr=False):
    return Decl(name, [], [], [], [], type_decl(name, type, struct, ptr), None, None)


def mark_command(varname, typename):
    typename_str = extract_name(typename)
    if type(typename.type) == PtrDecl:
        arg = StructRef(ID("stc"), "->", ID(varname))
        return Compound([FuncCall(ID(f"gc_mark_{typename_str}"), ExprList([arg, ID("table")])),
                         FuncCall(ID(f"gc_mark_plain"), ExprList([arg, ID("table")]))])
    else:
        arg = UnaryOp('&', StructRef(ID("stc"), "->", ID(varname)))
        return FuncCall(ID(f"gc_mark_{typename_str}"),
                        ExprList([arg, ID("table")]))


def generate_decl(name, type):
    return Decl(name, [], [], [], [], type, None, None)


def mark_func_decl(struct):
    fn_name = f"gc_mark_{struct.name}"
    body = Compound([mark_command(decl.name, decl) for decl in struct if decl.name != "id"])

    if_stmt = c_ast.If(c_ast.BinaryOp('!=', ID('stc'), Constant('int', 'NULL')), body, None)
    if_stmt_cmpd = Compound([if_stmt])
    fndef = FuncDef(
        generate_decl(fn_name, FuncDecl(ParamList([decl_name_type("stc", struct.name, True, True),
                                                   decl_name_type("table", "GHashTable", False, True)]),
                                        type_decl(fn_name, 'void')))
        , None, body=if_stmt_cmpd)
    return fndef


def type_decl(name, type, struct=False, ptr=False):
    innermost = Struct(type, None) if struct else IdentifierType([type])
    if not ptr:
        return TypeDecl(name, [], [], innermost)
    else:
        return PtrDecl([], type_decl(name, type, struct, False))


pop_stack_call = FuncCall(ID("gc_pop_stack_frame"), ExprList([]))


def generate_pop_frames(fndef):
    for index, stmt in enumerate(fndef):
        if type(stmt) == Return:
            compound = Compound([
                pop_stack_call,
                stmt
            ])
            if type(fndef) == Compound:
                fndef.block_items[index] = compound
            else:
                raise RuntimeError
        else:
            generate_pop_frames(stmt)

    if type(fndef) == FuncDef:
        fndef.body.block_items.append(pop_stack_call)


def generate_stack_struct(local, struct_name):
    struct = Struct(name=struct_name, decls=
    [generate_decl('id', type_decl('id', 'int'))] + copy.deepcopy(local))

    for decl in struct.decls:
        decl.init = None
    return struct


def get_init_list(local, stack_id):
    inits = [
        FuncCall(ID("memset"), ExprList(
            [UnaryOp("&", ID("stack_frame")), Constant(ID("int"), '0'), UnaryOp("sizeof", ID("stack_frame"))])),
        Assignment(
            '=',
            StructRef(ID('stack_frame'), '.', ID('id')),
            Constant('int', stack_id)
        ), FuncCall(ID("gc_push_stack_frame"), ExprList([
            Cast(type_decl(None, 'void', False, True), UnaryOp('&', ID('stack_frame')))
        ]))]
    for a in local:
        if hasattr(a, 'init') and a.init is not None:
            inits.append(
                Assignment(
                    '=',
                    StructRef(ID('stack_frame'), '.', ID(a.name)),
                    a.init
                )
            )

    return inits


def generate_forward_decls(decl):
    if type(decl) == FuncDef:
        return decl.decl
    if type(decl) == Struct:
        copied = copy.copy(decl)
        copied.decls = None
        return copied


def generate_global_stack_map(map):
    global_res = c_ast.InitList([])
    for this_id, fn_name in map.items():
        this_frame = c_ast.InitList([Constant('int', str(this_id)), ID(fn_name)])
        global_res.exprs.append(this_frame)

        decl = generate_decl('STACK_MAP', c_ast.ArrayDecl(type_decl('STACK_MAP', 'StackMap', struct=True), None, []))
        decl.init = global_res
    return decl


stack_id = -1
stack_map = {}

push_list = []
for stmt in ours(ast):
    if type(stmt) == Decl and type(stmt.type) == Struct and extract_name(stmt) in USERTYPES:
        result = mark_func_decl(stmt.type)
        push_list.extend([result])
    if type(stmt) == FuncDef:
        if stmt.decl.name.startswith("gc") or stmt.coord.file != "gc.c":
            continue

        stack_id += 1
        func_locals = collect_gc_roots(stmt.body)

        local_names = [a.name for a in func_locals]

        struct_name = f"StackFrame_{stmt.decl.name}"
        mark_name = f"gc_mark_{struct_name}"
        stck_declaration = Decl('stack_frame', [], [], [], [],
                                type=type_decl('stack_frame', struct_name, True),
                                init=None,
                                bitsize=None)
        inits = get_init_list(func_locals, stack_id)
        stmt.body.block_items = [stck_declaration] + inits + stmt.body.block_items

        local_struct = generate_stack_struct(func_locals, struct_name)

        mark_func = mark_func_decl(local_struct)
        stack_map[stack_id] = mark_func.decl.name

        generate_pop_frames(stmt)
        change_local_vars(stmt, local_names)
        push_list.extend([local_struct, mark_func])

forwards = list(map(generate_forward_decls, push_list))
stack_map_code = generate_global_stack_map(stack_map)
ast.ext = list(filter(lambda k: k.coord.file == "gc.c", ast.ext))

for index, stmt in enumerate(ast.ext):
    if getattr(stmt, "name", None) == "END_CODE_HERE":
        ast.ext[index:index] = forwards + push_list
        break

ast.ext.append(stack_map_code)
file = open("gc-1.c", "w+")
file.write("""
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <glib.h>
#include <assert.h>
""")
file.write(c_generator.CGenerator().visit(ast))

b = 5
