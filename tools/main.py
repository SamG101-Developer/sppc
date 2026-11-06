"""
Generate S++ FFI bindings ("stub.spp") from a C library's headers.

The headers are parsed with libclang, exactly as a C compiler would see them,
and every function the library exposes is written out as an S++ prototype:

    !ffi(symbol="sppc_close")
    fun close(fd: CInt) -> CInt { }

along with a class for every struct those prototypes use, and (optionally)
the library's integer constants. Anything that cannot be expressed is still
written out, commented, with the reason - so nothing goes missing silently.

Requirements:
    pip install libclang        (or the distro's python3-clang)
    a clang of the same major version, if libclang lacks its builtin headers
    (pip's does; it is found automatically, or pass --clang)
    pip install pyperclip       (only for --copy)

Usage:
    python tools/main.py HEADER [HEADER ...] [options] [-- CLANG_ARGS ...]

    HEADER          the header(s) a user of the library would include, or a
                    folder standing for every .h directly in it. Only
                    declarations from files under --root are bound, so the
                    system headers they pull in are left out.

The command is recorded at the top of the stub, to regenerate it with.

Examples:
    # sppc: everything is reachable from sppc.h; its constants are the ones
    # in types.h (config.h's SPPC_* macros are build switches, not api).
    python tools/main.py headers/sppc/sppc.h -I headers --prefix sppc_ \\
        --closure-type sppc_closure --constants --constants-root headers/sppc/types.h \\
        -o ../spp-stl/ffi/sppc/stub.spp

    # A library split across several .so files (OpenSSL): one stub per .so,
    # each from every header, keeping what that .so exports.
    python tools/main.py /usr/local/ssl/include/openssl -I /usr/local/ssl/include \\
        --library /usr/local/ssl/lib64/libssl.so.4 -o ffi/ssl/stub.spp

    # Any other library, cross-generating for windows.
    python tools/main.py /usr/include/zlib.h --root /usr/include/zlib.h \\
        --constants -- --target=x86_64-pc-windows-msvc

Type mapping (C -> S++, see std::cffi):
    void                         CVoid
    _Bool / bool                 CBool
    char                         CUChar (plain char's signedness is not portable)
    signed / unsigned char       CChar / CUChar
    other integers, enums        by size: CShort/CInt/CInt64, CUShort/CUInt/CUInt64
    size_t / ssize_t             CSizeT / CSSizeT
    float / double               CFloat / CDouble
    struct foo / foo_t           CFoo (a "cls" is generated for it)
    union, bitfields, ...        CFoo, as an opaque blob of the right size/alignment
    --closure-type struct        CClosure[(Params,), Ret]
    void* / char* (param)        CMutBytePtr (any char: plain, signed, unsigned, uint8_t)
    const void* / const char*    CRefBytePtr (param)
    T* / const T*                &mut T / &T
    T** (any nested pointer)     &mut CVoidPtr / &CVoidPtr
    pointer to incomplete type   CVoidPtr (an opaque handle)
    function pointer             CVoidPtr (a raw C function pointer is not a CClosure)
    any pointer returned         CVoidPtr (it may be null; S++ cannot return a borrow)
    any pointer in a struct      CVoidPtr (S++ cannot store a borrow)
    T[N] in a struct             [T; N_uz]
    noreturn function            -> !

Not supported (the function is emitted commented out): variadic functions (an
ffi function cannot be generic; bind each prototype by hand), long double,
__int128, _Complex, vector types, flexible array members in by-value structs.
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from enum import Enum, auto
from pathlib import Path
from typing import Optional

import clang.cindex as cl

TK = cl.TypeKind
CK = cl.CursorKind

# Every S++ keyword; a C name that collides with one gets a trailing "_".
SPP_KEYWORDS = frozenset({
    "and", "as", "async", "await", "caps", "case", "cls", "cmp", "cor", "defer", "else", "exit", "ext", "false",
    "fun", "gen", "in", "is", "let", "loop", "mut", "not", "of", "or", "res", "ret", "self", "skip", "sup", "to",
    "true", "type", "use", "with",
})

SIGNED_BY_SIZE = {1: "CChar", 2: "CShort", 4: "CInt", 8: "CInt64"}
UNSIGNED_BY_SIZE = {1: "CUChar", 2: "CUShort", 4: "CUInt", 8: "CUInt64"}
# Pointees passed as a byte pointer (CRefBytePtr / CMutBytePtr): void and every char-sized type.
BYTE_KINDS = {TK.VOID, TK.CHAR_S, TK.CHAR_U, TK.SCHAR, TK.UCHAR}

SIGNED_KINDS = {TK.SCHAR, TK.SHORT, TK.INT, TK.LONG, TK.LONGLONG, TK.WCHAR}
UNSIGNED_KINDS = {TK.UCHAR, TK.USHORT, TK.UINT, TK.ULONG, TK.ULONGLONG, TK.CHAR16, TK.CHAR32}

# Typedefs that mean something beyond their size, checked before desugaring.
NAMED_TYPEDEFS = {"size_t": "CSizeT", "ssize_t": "CSSizeT"}


class Unsupported(Exception):
    """A C type with no S++ equivalent; the declaration using it is skipped."""


class Pos(Enum):
    """Where a type appears, which changes how pointers are mapped."""
    PARAM = auto()
    RETURN = auto()
    FIELD = auto()
    POINTEE = auto()


@dataclass
class Options:
    roots: list[Path]
    prefixes: list[str] = field(default_factory=list)
    constant_roots: list[Path] = field(default_factory=list)  # empty: the same as roots
    keep_prefix: bool = False
    closure_types: set[str] = field(default_factory=set)
    constants: bool = False
    comments: bool = True
    exports: Optional[set[str]] = None  # --library: only bind the functions it exports


# ==================== NAMES ====================

def sanitize(name: str) -> str:
    """Make a C identifier a valid S++ one."""
    return f"{name}_" if name in SPP_KEYWORDS else name


def snake_to_pascal(name: str) -> str:
    return "".join(word[:1].upper() + word[1:] for word in name.split("_") if word)


def strip_leading_underscores(name: str) -> str:
    return name.lstrip("_")


def lowercase_start(name: str) -> str:
    """
    S++ identifiers (functions, parameters, fields) start with a lowercase
    letter: a name that doesn't (OPENSSL_init, M, CTX_new) is lowercased
    whole, while one that does (camelCase) is left alone. Empty if no letter
    can start it ("", "_new", "3des"), for the caller to choose a fallback.
    """
    name = name.lstrip("_")
    if not name[:1].islower():
        name = name.lower()
    return name if name[:1].isalpha() else ""


def common_field_prefix(names: list[str]) -> str:
    """The "st_" in st_dev/st_ino/..., stripped when every field shares it."""
    if len(names) < 2:
        return ""
    prefix = os.path.commonprefix(names)
    prefix = prefix[:prefix.rfind("_") + 1]
    # Never strip a name down to nothing.
    return prefix if prefix and all(len(n) > len(prefix) for n in names) else ""


# ==================== GENERATOR ====================

class Generator:
    def __init__(self, opts: Options):
        self._opts = opts
        self._uses: set[str] = set()
        self._records: dict[str, str] = {}  # S++ name -> rendered cls, in dependency order
        self._records_in_progress: set[str] = set()
        self._record_names: dict[str, str] = {}  # C record USR -> S++ name
        self._notes: list[str] = []  # comments attached to the declaration being mapped
        self._functions: list[str] = []
        self._constants: list[str] = []
        self._seen_symbols: set[str] = set()
        self._spp_functions: dict[str, str] = {}  # S++ function name -> the C name first bound to it
        self._seen_constants: set[str] = set()
        self.warnings: list[str] = []
        self.not_exported: set[str] = set()  # declared, but not in --library

    # ---------- filtering ----------

    def _in_root(self, cursor: cl.Cursor, roots: Optional[list[Path]] = None) -> bool:
        if cursor.location.file is None:
            return False
        path = Path(cursor.location.file.name).resolve()
        return any(path == root or root in path.parents for root in (roots or self._opts.roots))

    def _matching_prefix(self, name: str, upper: bool = False) -> Optional[str]:
        if not self._opts.prefixes:
            return ""
        for prefix in self._opts.prefixes:
            prefix = prefix.upper() if upper else prefix
            if name.startswith(prefix):
                return prefix
        return None

    def _spp_name(self, c_name: str, upper: bool = False) -> str:
        prefix = self._matching_prefix(c_name, upper) or ""
        if self._opts.keep_prefix or len(c_name) == len(prefix):
            prefix = ""
        return sanitize(c_name[len(prefix):])

    def _spp_function_name(self, c_name: str) -> str:
        """OPENSSL_init_ssl -> openssl_init_ssl, or init_ssl with --prefix OPENSSL_."""
        # Stripping the prefix may leave "_new" or "3des": keep the full name then.
        return sanitize(lowercase_start(self._spp_name(c_name)) or lowercase_start(c_name) or c_name.lower())

    # ---------- driving ----------

    def visit(self, tu: cl.TranslationUnit) -> None:
        for cursor in self._top_level(tu.cursor):
            if not self._in_root(cursor):
                continue
            if cursor.kind == CK.FUNCTION_DECL:
                self._visit_function(cursor)
            elif not self._in_root(cursor, self._opts.constant_roots or None):
                continue
            elif self._opts.constants and cursor.kind == CK.ENUM_DECL:
                self._visit_enum(cursor)
            elif self._opts.constants and cursor.kind == CK.MACRO_DEFINITION:
                self._visit_macro(cursor)

    def _top_level(self, cursor: cl.Cursor):
        for child in cursor.get_children():
            if child.kind == CK.LINKAGE_SPEC:  # extern "C" { ... }
                yield from self._top_level(child)
            else:
                yield child

    def render(self, sources: list[str], command: str) -> str:
        out = ["# Generated by sppc/tools/main.py from:"]
        out += [f"# - {source}" for source in sources]
        out += ["# Regenerate rather than editing by hand, with:", f"#   {command}", ""]
        out += [f"use std::cffi::{name}" for name in sorted(self._uses)]
        if self._constants:
            out += ["", "\n".join(self._constants)]
        for block in (*self._records.values(), *self._functions):
            out += ["", block]
        return "\n".join(out) + "\n"

    # ---------- functions ----------

    def _visit_function(self, cursor: cl.Cursor) -> None:
        c_name = cursor.spelling
        if c_name in self._seen_symbols or c_name.startswith("_"):
            return
        if cursor.storage_class == cl.StorageClass.STATIC:
            return
        if self._matching_prefix(c_name) is None:
            return
        if self._opts.exports is not None and c_name not in self._opts.exports:
            self.not_exported.add(c_name)
            return
        self._seen_symbols.add(c_name)

        spp_name = self._spp_function_name(c_name)
        if spp_name in self._spp_functions:
            self.warn(cursor, f"{c_name} and {self._spp_functions[spp_name]} are both bound as {spp_name}; "
                              f"rename one by hand")
        self._spp_functions.setdefault(spp_name, c_name)
        self._notes = []
        try:
            text = self._render_function(cursor, spp_name)
        except Unsupported as e:
            self.warn(cursor, f"skipped {c_name}: {e}")
            signature = " ".join(t.spelling for t in self._declaration_tokens(cursor))
            text = f"# Skipped: {e}.\n# {signature}"

        doc = self._doc_comment(cursor)
        notes = "".join(f"# Note: {n}\n" for n in dict.fromkeys(self._notes))
        self._functions.append(doc + notes + text)

    def _render_function(self, cursor: cl.Cursor, spp_name: str) -> str:
        func_type = cursor.type
        if self._is_noreturn(cursor):
            ret = "!"
        else:
            ret = self.map_type(func_type.get_result(), Pos.RETURN)

        params = []
        used_names: set[str] = set()
        for i, arg in enumerate(cursor.get_arguments()):
            name = sanitize(lowercase_start(arg.spelling) or f"arg{i}")
            if name in used_names:
                name = f"{name}{i}"
            used_names.add(name)
            params.append(f"{name}: {self.map_type(arg.type, Pos.PARAM)}")

        if func_type.kind == TK.FUNCTIONPROTO and func_type.is_function_variadic():
            # A generic ffi function is an error (E101): C has one prototype, so
            # bind each way it is called by hand (as sppc splits fcntl).
            raise Unsupported("it is variadic; bind each prototype it is called with by hand")
        if func_type.kind == TK.FUNCTIONNOPROTO:
            self._notes.append("declared without a prototype \"f()\"; parameters may be missing")

        return (f'!ffi(symbol="{cursor.spelling}")\n'
                f"fun {spp_name}({', '.join(params)}) -> {ret} {{ }}")

    def _is_noreturn(self, cursor: cl.Cursor) -> bool:
        # __attribute__((noreturn)) lives on the function type; C11's _Noreturn
        # and C23's [[noreturn]] are attributes libclang doesn't expose by name,
        # so read their tokens.
        if "__attribute__((noreturn))" in cursor.type.spelling:
            return True
        return any(t.spelling in ("_Noreturn", "noreturn", "__noreturn__")
                   for attr in cursor.get_children() if attr.kind.is_attribute()
                   for t in attr.get_tokens())

    def _declaration_tokens(self, cursor: cl.Cursor):
        """The declaration's tokens up to its body, if it has one."""
        for token in cursor.get_tokens():
            if token.spelling in ("{", ";"):
                return
            yield token

    def _doc_comment(self, cursor: cl.Cursor) -> str:
        if not self._opts.comments or not cursor.raw_comment:
            return ""
        lines = []
        for line in cursor.raw_comment.splitlines():
            line = re.sub(r"^\s*(/\*\*+|\*+/|\*(?!/)|///?|//!)", "", line).rstrip()
            line = re.sub(r"\*+/$", "", line).rstrip()
            lines.append(line)
        while lines and not lines[0].strip():
            lines.pop(0)
        while lines and not lines[-1].strip():
            lines.pop()
        return "".join(f"#{line}\n" if line.startswith(" ") or not line else f"# {line}\n" for line in lines)

    # ---------- constants ----------

    def _visit_enum(self, cursor: cl.Cursor) -> None:
        spp_type = self._integer(cursor.enum_type)
        for const in cursor.get_children():
            if const.kind == CK.ENUM_CONSTANT_DECL:
                self._add_constant(const.spelling, spp_type, const.enum_value)

    def _visit_macro(self, cursor: cl.Cursor) -> None:
        if cursor.spelling.startswith("_"):
            return
        if hasattr(cursor, "is_macro_function_like") and cursor.is_macro_function_like():
            return
        tokens = [t.spelling for t in cursor.get_tokens()][1:]
        value = parse_integer_literal(tokens)
        if value is not None:
            if -2 ** 31 <= value < 2 ** 31:
                spp_type = "CInt"
            elif -2 ** 63 <= value < 2 ** 63:
                spp_type = "CInt64"
            else:
                spp_type = "CUInt64"
            self._uses.add(spp_type)
            self._add_constant(cursor.spelling, spp_type, value)

    def _add_constant(self, c_name: str, spp_type: str, value: int) -> None:
        if c_name in self._seen_constants or c_name.startswith("_"):
            return
        if self._matching_prefix(c_name, upper=c_name.isupper()) is None:
            return
        self._seen_constants.add(c_name)
        name = self._spp_name(c_name, upper=c_name.isupper()).lower()
        self._constants.append(f"!public cmp {sanitize(name)}: {spp_type} = {value}")

    # ---------- types ----------

    def use(self, name: str) -> str:
        self._uses.add(name)
        return name

    def map_type(self, t: cl.Type, pos: Pos) -> str:
        """Map a C type to its S++ spelling, recording any std::cffi import."""
        # Typedef names carry meaning their canonical type has lost.
        for name in self._typedef_names(t):
            if name in NAMED_TYPEDEFS:
                return self.use(NAMED_TYPEDEFS[name])
            if name in self._opts.closure_types:
                return self._closure(t.get_canonical(), name)

        canon = t.get_canonical()
        kind = canon.kind

        if kind == TK.VOID:
            return self.use("CVoid")
        if kind == TK.BOOL:
            return self.use("CBool")
        if kind in (TK.CHAR_S, TK.CHAR_U):
            return self.use("CUChar")
        if kind in SIGNED_KINDS or kind in UNSIGNED_KINDS or kind == TK.ENUM:
            return self._integer(canon)
        if kind == TK.FLOAT:
            return self.use("CFloat")
        if kind == TK.DOUBLE:
            return self.use("CDouble")
        if kind == TK.POINTER:
            return self._pointer(t, pos)
        if kind in (TK.CONSTANTARRAY, TK.INCOMPLETEARRAY, TK.VARIABLEARRAY, TK.DEPENDENTSIZEDARRAY):
            return self._array(canon, pos)
        if kind == TK.RECORD:
            if pos == Pos.RETURN and canon.get_size() < 0:
                raise Unsupported(f"returns incomplete type '{t.spelling}' by value")
            return self._record(t)
        # long double, __int128, _Complex, vectors, ...
        raise Unsupported(f"no S++ type for '{t.spelling}'")

    def _typedef_names(self, t: cl.Type):
        """Every typedef name t is spelt through, outermost first."""
        while True:
            if t.kind == TK.ELABORATED:
                t = t.get_named_type()
            elif t.kind == TK.TYPEDEF:
                yield t.get_typedef_name() or t.get_declaration().spelling
                t = t.get_declaration().underlying_typedef_type
            else:
                return

    def _desugar(self, t: cl.Type) -> cl.Type:
        """Strip typedefs, keeping the sugar of whatever the typedef names."""
        while t.kind in (TK.ELABORATED, TK.TYPEDEF):
            t = t.get_named_type() if t.kind == TK.ELABORATED else t.get_declaration().underlying_typedef_type
        return t

    def _integer(self, t: cl.Type) -> str:
        canon = t.get_canonical()
        if canon.kind == TK.ENUM:
            canon = canon.get_declaration().enum_type.get_canonical()
        table = UNSIGNED_BY_SIZE if canon.kind in UNSIGNED_KINDS else SIGNED_BY_SIZE
        size = canon.get_size()
        if size not in table:
            raise Unsupported(f"no S++ integer of {size} bytes for '{t.spelling}'")
        return self.use(table[size])

    def _pointer(self, t: cl.Type, pos: Pos) -> str:
        sugared = self._desugar(t)
        pointee = sugared.get_pointee() if sugared.kind == TK.POINTER else t.get_canonical().get_pointee()
        return self._pointer_to(pointee, t, pos)

    def _pointer_to(self, pointee: cl.Type, t: cl.Type, pos: Pos) -> str:
        pointee_canon = pointee.get_canonical()

        if pointee_canon.kind in (TK.FUNCTIONPROTO, TK.FUNCTIONNOPROTO):
            self._notes.append(f"'{t.spelling}' is a raw C function pointer, passed as CVoidPtr")
            return self.use("CVoidPtr")
        # Anything that may be null or must outlive the call can't be a borrow.
        if pos in (Pos.RETURN, Pos.FIELD, Pos.POINTEE):
            return self.use("CVoidPtr")
        # Bytes: a buffer S++ hands over for the call, from std::cffi::view_ptr_ref / view_ptr_mut. The pointee's
        # constness says whether C may write through it, so a read-only view never yields a writable pointer.
        if pointee_canon.kind in BYTE_KINDS:
            return self.use("CRefBytePtr" if pointee.is_const_qualified() else "CMutBytePtr")
        # An opaque handle: nothing on the S++ side can know its layout.
        if pointee_canon.kind == TK.RECORD and pointee_canon.get_size() < 0:
            return self.use("CVoidPtr")

        ref = "&" if pointee.is_const_qualified() else "&mut "
        return ref + self.map_type(pointee, Pos.POINTEE)

    def _array(self, canon: cl.Type, pos: Pos) -> str:
        if pos == Pos.PARAM:
            # "int a[4]" as a parameter is really "int *a".
            return self._pointer_to(canon.get_array_element_type(), canon, pos)
        if pos != Pos.FIELD:
            raise Unsupported(f"array type '{canon.spelling}' outside a struct")
        if canon.kind != TK.CONSTANTARRAY:
            raise Unsupported(f"variable or flexible array '{canon.spelling}'")
        element = self.map_type(canon.get_array_element_type(), Pos.FIELD)
        return f"[{element}; {canon.get_array_size()}_uz]"

    # ---------- records ----------

    def _record_c_name(self, t: cl.Type) -> str:
        """
        struct foo -> foo. A struct whose tag is anonymous or reserved
        ("typedef struct _IO_FILE FILE") takes the name it was spelt with.
        """
        decl = t.get_canonical().get_declaration()
        tag = "" if decl.is_anonymous() or " " in decl.spelling else decl.spelling
        if tag and not tag.startswith("_"):
            return tag
        for name in self._typedef_names(t):
            if not name.startswith("_"):
                return name
        if tag:
            return tag
        raise Unsupported(f"anonymous record '{t.spelling}' has no name to bind it by")

    def _record(self, t: cl.Type) -> str:
        # One class per C record, however it was spelt the first time.
        usr = t.get_canonical().get_declaration().get_usr()
        if usr in self._record_names:
            return self._record_names[usr]

        c_name = self._record_c_name(t)
        if c_name in self._opts.closure_types:
            return self._closure(t.get_canonical(), c_name)

        prefix = self._matching_prefix(c_name) or ""
        stripped = c_name if self._opts.keep_prefix else c_name[len(prefix):] or c_name
        spp_name = "C" + snake_to_pascal(strip_leading_underscores(stripped))
        self._record_names[usr] = spp_name
        if spp_name in self._records or spp_name in self._records_in_progress:
            self.warnings.append(f"two C records are both bound as {spp_name}; rename one by hand")
            return spp_name

        self._records_in_progress.add(spp_name)
        saved_notes, self._notes = self._notes, []
        try:
            self._records[spp_name] = self._render_record(t.get_canonical(), c_name, spp_name)
        finally:
            self._notes = saved_notes
            self._records_in_progress.discard(spp_name)
        return spp_name

    def _render_record(self, canon: cl.Type, c_name: str, spp_name: str) -> str:
        decl = canon.get_declaration()
        size, align = canon.get_size(), canon.get_align()
        if size < 0:
            return f"# Opaque: \"{c_name}\" is incomplete, so only usable behind a pointer.\n!public\ncls {spp_name} {{ }}"

        reason = None
        fields: list[str] = []
        if decl.kind == CK.UNION_DECL:
            reason = "it is a union"
        else:
            c_fields = list(canon.get_fields())
            names = [strip_leading_underscores(f.spelling) for f in c_fields]
            public = [n for f, n in zip(c_fields, names) if not f.spelling.startswith("_")]
            strip = common_field_prefix(public if len(public) >= 2 else names)
            used_names: set[str] = set()
            for i, (f, name) in enumerate(zip(c_fields, names)):
                if f.is_bitfield():
                    reason = f"field \"{f.spelling}\" is a bitfield"
                    break
                if f.is_anonymous() or not f.spelling:
                    reason = "it has an anonymous member"
                    break
                try:
                    spp_type = self.map_type(f.type, Pos.FIELD)
                except Unsupported as e:
                    reason = f"field \"{f.spelling}\": {e}"
                    break
                private = f.spelling.startswith("_")
                name = name[len(strip):] if name.startswith(strip) else name
                name = sanitize(lowercase_start(name) or f"field{i}")
                if name in used_names:
                    name = f"{name}{i}"
                used_names.add(name)
                fields.append(f"  !{'private' if private else 'public'} {name}: {spp_type}")

        if reason is not None:
            # Keep the size and alignment right, which is all the C side needs
            # to pass it around; the fields are reached through C functions.
            unit = min(align, 8)
            count = size // unit
            primitive = f"U{unit * 8}"
            return (f"!public\ncls {spp_name} {{\n"
                    f"  # Opaque stand-in for C's \"{c_name}\" ({reason}):\n"
                    f"  # {size} bytes, {align}-byte aligned.\n"
                    f"  !private bytes: [{primitive}; {count}_uz] = [0_{primitive.lower()}; {count}_uz]\n}}")

        body = "\n".join(fields)
        return f"!public\ncls {spp_name} {{\n{body}\n}}" if body else f"!public\ncls {spp_name} {{ }}"

    def _closure(self, canon: cl.Type, c_name: str) -> str:
        """A {fn, env} pair whose fn takes the env first: CClosure[(rest,), ret]."""
        fn = None
        for f in canon.get_fields():
            pointee = f.type.get_canonical().get_pointee()
            if pointee.kind == TK.FUNCTIONPROTO:
                fn = pointee
                break
        if fn is None:
            raise Unsupported(f"closure type \"{c_name}\" has no function pointer field")

        args = list(fn.argument_types())[1:]  # the first is the environment
        params = [self.map_type(a, Pos.PARAM) for a in args]
        ret = self.map_type(fn.get_result(), Pos.RETURN)
        tuple_ = "()" if not params else f"({params[0]},)" if len(params) == 1 else f"({', '.join(params)})"
        return f"{self.use('CClosure')}[{tuple_}, {ret}]"

    # ---------- diagnostics ----------

    def warn(self, cursor: cl.Cursor, message: str) -> None:
        loc = cursor.location
        where = f"{loc.file.name}:{loc.line}: " if loc.file else ""
        self.warnings.append(f"{where}{message}")


def parse_integer_literal(tokens: list[str]) -> Optional[int]:
    """The value of a macro body like "42", "(-1)", "0x10u" or "0755", else None."""
    while len(tokens) >= 2 and tokens[0] == "(" and tokens[-1] == ")":
        tokens = tokens[1:-1]
    sign = 1
    if len(tokens) == 2 and tokens[0] in ("-", "+"):
        sign = -1 if tokens[0] == "-" else 1
        tokens = tokens[1:]
    if len(tokens) != 1:
        return None
    m = re.fullmatch(r"(0[xX][0-9a-fA-F']+|0[bB][01']+|0[0-7']*|[1-9][0-9']*)([uU]?[lL]{0,2}[uU]?)", tokens[0])
    if m is None:
        return None
    digits = m.group(1).replace("'", "")
    if digits[:2].lower() in ("0x", "0b"):
        value = int(digits, 0)
    elif digits.startswith("0") and len(digits) > 1:
        value = int(digits, 8)
    else:
        value = int(digits)
    return sign * value


# ==================== CLANG SETUP ====================

def _major_version(version_text: str) -> Optional[int]:
    m = re.search(r"clang version (\d+)\.", version_text)
    return int(m.group(1)) if m else None


def libclang_major_version() -> Optional[int]:
    """The major version of the loaded libclang, e.g. 19."""
    try:
        get_version = cl.conf.lib.clang_getClangVersion
        get_version.argtypes = []
        get_version.restype = cl._CXString
        get_version.errcheck = cl._CXString.from_result
        return _major_version(get_version())
    except (AttributeError, TypeError):
        return None


def clang_major_version(exe: str) -> Optional[int]:
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return _major_version(out.stdout)


def libclang_has_builtin_headers() -> bool:
    """Whether libclang finds its own stddef.h/stdint.h (distro packages ship them; pip's libclang doesn't)."""
    probe = "sppc_probe.c"
    source = "#include <stddef.h>\n#include <stdarg.h>\n#include <stdint.h>\nuint64_t x;\n"
    tu = cl.Index.create().parse(probe, args=["-x", "c"], unsaved_files=[(probe, source)])
    return not any(d.severity >= cl.Diagnostic.Error for d in tu.diagnostics)


def clang_candidates(major: int) -> list[str]:
    """Where a clang of the given major version is usually installed."""
    found = [shutil.which(f"clang-{major}"), shutil.which("clang")]
    found += [str(p) for p in (Path(f"/usr/lib/llvm-{major}/bin/clang"),
                               Path(f"/usr/local/opt/llvm@{major}/bin/clang"),
                               Path(f"/opt/homebrew/opt/llvm@{major}/bin/clang"),
                               Path(f"/usr/local/llvm{major}/bin/clang"),
                               Path("C:/Program Files/LLVM/bin/clang.exe")) if p.is_file()]
    return list(dict.fromkeys(e for e in found if e))


def resource_include(exe: str) -> Optional[str]:
    try:
        out = subprocess.run([exe, "-print-resource-dir"], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    include = Path(out.stdout.strip()) / "include"
    return str(include) if include.is_dir() else None


def find_resource_include(clang_exe: Optional[str]) -> Optional[str]:
    """
    The builtin headers (stddef.h, stdint.h, ...) for libclang to parse with.

    The pip "libclang" package doesn't ship them, so they are borrowed from an
    installed clang, which must be libclang's own version: libclang still
    searches its own builtin directory, and a different version's stdint.h
    shares its include guard, so "#include_next <stdint.h>" stops there and
    never reaches the C library's (every uint64_t is then an unknown type).
    """
    major = libclang_major_version()
    if clang_exe:
        found = clang_major_version(clang_exe)
        if major and found and found != major:
            print(f"warning: --clang is clang {found} but libclang is {major}; "
                  f"stdint.h types may be missing", file=sys.stderr)
        return resource_include(clang_exe)

    if libclang_has_builtin_headers():
        return None
    if major is not None:
        for exe in clang_candidates(major):
            if clang_major_version(exe) == major:
                return resource_include(exe)

    fallback = shutil.which("clang")
    if fallback is None:
        print("warning: libclang has no builtin headers and no clang was found; pass --clang", file=sys.stderr)
        return None
    print(f"warning: no clang {major or '?'} found to match libclang; borrowing {fallback}'s builtin headers, "
          f"which may hide stdint.h types (pass --clang)", file=sys.stderr)
    return resource_include(fallback)


def library_exports(library: str) -> set[str]:
    """The function names a shared library exports, read with nm (or llvm-nm)."""
    path = Path(library)
    flags = ["-g", "--defined-only"] if path.suffix in (".dylib", ".dll") else ["-D", "--defined-only"]
    for nm in ("nm", "llvm-nm"):
        exe = shutil.which(nm)
        if exe is None:
            continue
        out = subprocess.run([exe, *flags, str(path)], capture_output=True, text=True)
        if out.returncode != 0:
            continue
        names = set()
        for line in out.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[-2] in ("T", "W", "i"):  # code, weak code, ifunc
                name = parts[-1].split("@")[0]  # glibc versions: "SSL_new@@OPENSSL_4.0.0"
                names.add(name[1:] if path.suffix == ".dylib" and name.startswith("_") else name)
        return names
    raise SystemExit(f"error: cannot read the exports of {library} (nm or llvm-nm is needed)")


def expand_headers(paths: list[str]) -> list[str]:
    """A folder stands for every .h directly in it."""
    headers = []
    for p in paths:
        headers += sorted(str(h) for h in Path(p).glob("*.h")) if Path(p).is_dir() else [p]
    return headers


def parse_headers(headers: list[str], clang_args: list[str], want_macros: bool) -> list[cl.TranslationUnit]:
    index = cl.Index.create()
    options = cl.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
    if want_macros:
        options |= cl.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
    return [index.parse(h, args=clang_args, options=options) for h in headers]


def report_diagnostics(tus: list[cl.TranslationUnit], verbose: bool) -> int:
    errors = 0
    seen = set()
    for tu in tus:
        for d in tu.diagnostics:
            # Every header is parsed as a main file, which "#pragma once" warns about.
            if "#pragma once in main file" in d.spelling:
                continue
            if d.severity < cl.Diagnostic.Error and not verbose:
                continue
            key = (str(d.location), d.spelling)
            if key in seen:
                continue
            seen.add(key)
            errors += d.severity >= cl.Diagnostic.Error
            loc = d.location
            where = f"{loc.file.name}:{loc.line}:{loc.column}: " if loc.file else ""
            label = {cl.Diagnostic.Warning: "warning", cl.Diagnostic.Error: "error",
                     cl.Diagnostic.Fatal: "fatal"}.get(d.severity, "note")
            print(f"{where}{label}: {d.spelling}", file=sys.stderr)
    return errors


# ==================== CLI ====================

def main(argv: Optional[list[str]] = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    command = shlex.join(["python", "tools/main.py", *argv])
    clang_extra: list[str] = []
    if "--" in argv:
        split = argv.index("--")
        argv, clang_extra = argv[:split], argv[split + 1:]

    ap = argparse.ArgumentParser(
        description="Generate S++ FFI bindings from C headers.",
        epilog="Arguments after \"--\" are passed straight to clang (e.g. --target=..., -std=c23).")
    ap.add_argument("headers", nargs="+", help="the public header(s) of the library, or folders of them")
    ap.add_argument("-o", "--output", help="write here instead of stdout")
    ap.add_argument("-I", dest="include_dirs", action="append", default=[], metavar="DIR", help="include directory")
    ap.add_argument("-D", dest="defines", action="append", default=[], metavar="NAME[=VALUE]", help="define a macro")
    ap.add_argument("--root", action="append", default=[], metavar="PATH",
                    help="only bind declarations from files under PATH (default: each header's directory)")
    ap.add_argument("--prefix", action="append", default=[], metavar="PREFIX",
                    help="only bind functions starting with PREFIX, and drop it from S++ names (repeatable)")
    ap.add_argument("--keep-prefix", action="store_true", help="filter on --prefix but keep it in S++ names")
    ap.add_argument("--closure-type", action="append", default=[], metavar="NAME",
                    help="a {fn, env} struct/typedef to map to CClosure (e.g. sppc_closure)")
    ap.add_argument("--constants", action="store_true", help="also emit enum constants and integer #defines")
    ap.add_argument("--constants-root", action="append", default=[], metavar="PATH",
                    help="only emit constants defined under PATH (default: the same as --root)")
    ap.add_argument("--library", metavar="FILE",
                    help="only bind functions this .so/.dylib exports (to split one header set across libraries)")
    ap.add_argument("--no-comments", action="store_true", help="don't copy doc comments across")
    ap.add_argument("--libclang", metavar="FILE", help="path to libclang.so/.dylib/.dll")
    ap.add_argument("--clang", metavar="EXE", help="clang executable used to find the builtin headers")
    ap.add_argument("--copy", action="store_true", help="also copy the result to the clipboard (needs pyperclip)")
    ap.add_argument("-v", "--verbose", action="store_true", help="show clang warnings too")
    args = ap.parse_args(argv)

    if args.libclang:
        cl.Config.set_library_file(args.libclang)

    for h in args.headers:
        if not Path(h).exists():
            ap.error(f"no such header or folder: {h}")
    headers = expand_headers(args.headers)
    if not headers:
        ap.error("no headers found")

    clang_args = ["-x", "c", "-std=c17"]
    clang_args += [f"-I{d}" for d in args.include_dirs]
    clang_args += [f"-D{d}" for d in args.defines]
    resource_include = find_resource_include(args.clang)
    if resource_include:
        clang_args += ["-isystem", resource_include]
    clang_args += clang_extra

    roots = [Path(r).resolve() for r in args.root] or sorted(
        {Path(h).resolve() if Path(h).is_dir() else Path(h).resolve().parent for h in args.headers})
    constant_roots = [Path(r).resolve() for r in args.constants_root]
    opts = Options(roots=roots, prefixes=args.prefix, constant_roots=constant_roots, keep_prefix=args.keep_prefix,
                   closure_types=set(args.closure_type), constants=args.constants, comments=not args.no_comments,
                   exports=library_exports(args.library) if args.library else None)

    tus = parse_headers(headers, clang_args, want_macros=args.constants)
    if report_diagnostics(tus, args.verbose):
        print("error: the headers did not parse cleanly; fix the flags above (-I, -D, -- ...)", file=sys.stderr)
        return 1

    gen = Generator(opts)
    for tu in tus:
        gen.visit(tu)
    for w in gen.warnings:
        print(f"warning: {w}", file=sys.stderr)
    if gen.not_exported:
        print(f"note: left out {len(gen.not_exported)} declared functions {args.library} doesn't export", file=sys.stderr)

    spp = gen.render(args.headers, command)
    if args.output:
        Path(args.output).write_text(spp)
    else:
        sys.stdout.write(spp)
    if args.copy:
        import pyperclip
        pyperclip.copy(spp)
    return 0


if __name__ == "__main__":
    sys.exit(main())
