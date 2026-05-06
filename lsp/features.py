"""LSP feature handlers: hover, definition, documentSymbol, completion."""

from ast_index import SymbolDef, SymbolKind


def _lsp_range(sym: SymbolDef):
    """Convert a SymbolDef span to an LSP range (0-indexed)."""
    ln = sym.line - 1
    cs = sym.col_start - 1
    ce = sym.col_end
    return {
        "start": {"line": ln, "character": cs},
        "end": {"line": ln, "character": ce},
    }


def _lookup(name: str, symbols_by_name: dict, prefer_file: str = None):
    """Return best SymbolDef for name, preferring prefer_file."""
    candidates = symbols_by_name.get(name, [])
    if not candidates:
        return None
    if prefer_file:
        for sym in candidates:
            if (
                sym.file == prefer_file
                or prefer_file.endswith(sym.file)
                or sym.file.endswith(prefer_file)
            ):
                return sym
    return candidates[0]


def hover(ast, symbols_by_name, handler_scopes, uri, file_path, line_0, char_0):
    """Handle textDocument/hover. line_0 and char_0 are 0-indexed."""
    from ast_index import find_node_at

    line_1 = line_0 + 1
    col_1 = char_0 + 1
    node = find_node_at(ast, line_1, col_1, file=file_path)
    if node is None:
        return None

    name = _name_from_node(node)
    if name is None:
        return None

    sym = (_lookup(name, symbols_by_name, file_path)
           or _BUILTIN_BY_NAME.get(name)
           or _handler_field_sym(name, handler_scopes, file_path, line_0 + 1))
    if sym is None:
        return None

    md = f"```armlet\n{sym.detail}\n```"
    if sym.doc:
        md += f"\n\n{sym.doc}"
    return {"contents": {"kind": "markdown", "value": md}}


def definition(ast, symbols_by_name, uri, file_path, line_0, char_0):
    """Handle textDocument/definition. Returns list of Location objects."""
    from ast_index import find_node_at

    line_1 = line_0 + 1
    col_1 = char_0 + 1
    node = find_node_at(ast, line_1, col_1, file=file_path)
    if node is None:
        return []

    # Import path → navigate to the imported file
    if node.get("type") == "AST_IMPORT":
        import os
        path = node.get("path", "")
        if path:
            server_dir = os.path.dirname(os.path.abspath(__file__))
            project_dir = os.path.dirname(server_dir)
            abs_path = os.path.join(project_dir, path + ".aml")
            if os.path.isfile(abs_path):
                return [{"uri": _file_path_to_uri(abs_path),
                         "range": {"start": {"line": 0, "character": 0},
                                   "end":   {"line": 0, "character": 0}}}]
        return []

    name = _name_from_node(node)
    if name is None:
        return []

    sym = _lookup(name, symbols_by_name, file_path)
    if sym is None:
        return []

    def_uri = _path_to_uri(sym.file, file_path, uri)
    return [
        {
            "uri": def_uri,
            "range": _lsp_range(sym),
        }
    ]


def document_symbol(symbols, file_path):
    """Handle textDocument/documentSymbol. Returns flat list of SymbolInformation."""
    result = []
    for sym in symbols:
        if not _file_matches(sym.file, file_path):
            continue
        result.append(
            {
                "name": sym.name,
                "kind": int(sym.kind),
                "location": {
                    "uri": _file_path_to_uri(file_path),
                    "range": _lsp_range(sym),
                },
            }
        )
    return result


def _name_from_node(node):
    """Extract identifier name from an AST node if it represents one."""
    if not isinstance(node, dict):
        return None
    t = node.get("type", "")
    if t == "AST_VALUE" and node.get("tag") == "name":
        return node.get("value")
    if t in ("AST_BITLAYOUT", "AST_ENUM"):
        return node.get("name") if isinstance(node.get("name"), str) else None
    if t == "AST_FUNDEF":
        name_node = node.get("name", {})
        if isinstance(name_node, dict):
            return name_node.get("value")
    if t == "AST_TYPE_ALIAS":
        from_node = node.get("from", {})
        if isinstance(from_node, dict):
            return from_node.get("value")
    if t == "AST_VAR_DEF":
        name_node = node.get("name", {})
        if isinstance(name_node, dict):
            return name_node.get("value")
    if t == "AST_CALL":
        name_node = node.get("name", {})
        if isinstance(name_node, dict) and name_node.get("tag") == "name":
            return name_node.get("value")
    return None


def _handler_field_sym(name, handler_scopes, file_path, line_1):
    """Return a synthetic SymbolDef if name is a field exposed at line_1."""
    for scope in handler_scopes:
        if not _file_matches(scope.file, file_path):
            continue
        if scope.start_line <= line_1 <= scope.end_line:
            for fname, fsize in scope.fields:
                if fname == name:
                    return SymbolDef(
                        name=fname, kind=SymbolKind.Variable,
                        file=scope.file, line=scope.start_line,
                        col_start=0, col_end=0,
                        detail=f"bits({fsize}) {fname}",
                        doc=f"Named field of bitlayout {scope.layout_name}",
                    )
            if name == scope.argument:
                return SymbolDef(
                    name=name, kind=SymbolKind.Variable,
                    file=scope.file, line=scope.start_line,
                    col_start=0, col_end=0,
                    detail=f"bits({scope.layout_name}) {name}",
                    doc=f"Handler input for bitlayout {scope.layout_name}",
                )
    return None


def _file_matches(sym_file: str, file_path: str) -> bool:
    return (
        sym_file == file_path
        or file_path.endswith(sym_file)
        or sym_file.endswith(file_path)
    )


def _path_to_uri(sym_file: str, current_file_path: str, current_uri: str) -> str:
    """Convert a symbol's file path to a URI."""
    import os

    if os.path.isabs(sym_file):
        return _file_path_to_uri(sym_file)

    server_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(server_dir)
    abs_path = os.path.join(project_dir, sym_file)
    return _file_path_to_uri(abs_path)


def _file_path_to_uri(path: str) -> str:
    import urllib.parse
    import os

    if not os.path.isabs(path):
        server_dir = os.path.dirname(os.path.abspath(__file__))
        project_dir = os.path.dirname(server_dir)
        path = os.path.join(project_dir, path)
    return "file://" + urllib.parse.quote(path, safe="/:@")


# Built-in functions registered in interpreter.c:armlet_vm_init_builtins
_BUILTINS = [
    ("break",                       "break()",                              "Break into a debugger"),
    ("print",                       "print(...)",                                        "Print values to stdout"),
    ("inspect",                     "inspect(...)",                         "Print a detailed table of variables"),
    ("backtrace",                   "backtrace()",                          "Print the current call backtrace"),
    ("dispatch",                    "dispatch(value)",                      "Dispatch to the matching bitlayout handler"),
    ("serialize",                   "serialize(file_name, variable)",       "Serialize a value to a binary file"),
    ("deserialize",                 "deserialize(file_name)",               "Deserialize a value from a binary file"),
    ("set_bits_range_name",         "set_bits_range_name(target, name, end[, start])", "Name a bit range on a bits value"),
    ("bitlayout_to_json",           "bitlayout_to_json(layout[, file_name])", "Serialize one bitlayout to JSON (stdout or file)"),
    ("export_bitlayouts_json",      "export_bitlayouts_json([file_name])",  "Serialize all registered bitlayouts as a JSON array"),
    ("begin_implementation_defined","begin_implementation_defined(file_name)", "Begin recording implementation-defined values"),
    ("implementation_defined",      "implementation_defined(name, value)",  "Record an implementation-defined value"),
    ("end_implementation_defined",  "end_implementation_defined()",         "Finish recording implementation-defined values"),
    ("Log2",                        "integer Log2(integer v)",              "Compute floor(log2(v))"),
    ("Real",                        "real Real(value)",                     "Convert integer or real to real"),
    ("RoundUp",                     "integer RoundUp(real v)",              "Round real up to nearest integer"),
    ("RoundDown",                   "integer RoundDown(real v)",            "Round real down to nearest integer"),
]

_BUILTIN_BY_NAME = {
    name: SymbolDef(
        name=name, kind=SymbolKind.Function,
        file="<builtin>", line=0, col_start=0, col_end=0,
        detail=detail, doc=doc,
    )
    for name, detail, doc in _BUILTINS
}

_KIND_FUNCTION = 3
_KIND_FIELD = 5
_KIND_VARIABLE = 6
_KIND_CLASS = 7
_KIND_STRUCT = 22
_KIND_ENUM = 13
_KIND_ENUM_MEMBER = 20
_KIND_VALUE = 12

_SYMBOL_TO_COMPLETION_KIND = {
    SymbolKind.Function: _KIND_FUNCTION,
    SymbolKind.Variable: _KIND_VARIABLE,
    SymbolKind.Class: _KIND_CLASS,
    SymbolKind.Struct: _KIND_STRUCT,
    SymbolKind.Enum: _KIND_ENUM,
    SymbolKind.EnumMember: _KIND_ENUM_MEMBER,
}


def _member_access_prefix(line: str, char_0: int):
    """Return (identifier, is_struct_select) if cursor is after 'identifier.' or
    'identifier.<[fields,]', else None."""
    pos = char_0 - 1
    # Skip any partial member name already typed
    while pos >= 0 and (line[pos].isalnum() or line[pos] == '_'):
        pos -= 1
    # For multi-field select (foo.<f1, f2, ...>), skip back over previous fields and commas
    is_struct_select = False
    while pos >= 0:
        p = pos
        while p >= 0 and line[p] == ' ':
            p -= 1
        if p >= 0 and line[p] == ',':
            p -= 1
            while p >= 0 and (line[p].isalnum() or line[p] == '_'):
                p -= 1
            pos = p
            is_struct_select = True
        else:
            break
    # Optionally skip '<' for the foo.< syntax
    if pos >= 0 and line[pos] == '<':
        pos -= 1
        is_struct_select = True
    # Must be '.'
    if pos < 0 or line[pos] != '.':
        return None
    pos -= 1
    # Collect the identifier before '.'
    end = pos + 1
    while pos >= 0 and (line[pos].isalnum() or line[pos] == '_'):
        pos -= 1
    ident = line[pos + 1:end]
    return (ident, is_struct_select) if ident else None


def _in_print_interpolation(line: str, char_0: int) -> bool:
    """Return True if char_0 is inside a {name} interpolation in a print() string."""
    # Scan left from cursor for '{', stop at '"' or start of line
    pos = char_0 - 1
    while pos >= 0 and line[pos] not in ('{', '"'):
        pos -= 1
    if pos < 0 or line[pos] != '{':
        return False
    # Scan left from '{' for the opening '"'
    pos2 = pos - 1
    while pos2 >= 0 and line[pos2] != '"':
        pos2 -= 1
    if pos2 < 0:
        return False
    # Check that 'print(' appears somewhere before the opening quote
    return 'print(' in line[:pos2]


def completion(symbols, handler_scopes, file_path, line_0, char_0, current_line='',
               fields_by_type=None, var_type=None):
    """Handle textDocument/completion. Returns list of CompletionItem dicts.

    line_0 and char_0 are 0-indexed (LSP convention).
    """
    line_1 = line_0 + 1
    items = []
    seen = set()

    def add(label, kind, detail="", documentation=""):
        if label in seen:
            return
        seen.add(label)
        item = {"label": label, "kind": kind}
        if detail:
            item["detail"] = detail
        if documentation:
            item["documentation"] = {"kind": "plaintext", "value": documentation}
        items.append(item)

    # Member access: foo. or foo.<  → offer fields of foo's type only, nothing else
    member_result = _member_access_prefix(current_line, char_0)
    if member_result is not None:
        member_prefix, is_struct_select = member_result
        type_name = (var_type or {}).get(member_prefix)
        if type_name:
            for fname, ftype in (fields_by_type or {}).get(type_name, []):
                if is_struct_select and not ftype.startswith('bits'):
                    continue
                add(fname, _KIND_FIELD, detail=ftype)
        return items

    in_interpolation = _in_print_interpolation(current_line, char_0)

    for scope in handler_scopes:
        if not _file_matches(scope.file, file_path):
            continue
        if scope.start_line <= line_1 <= scope.end_line:
            for fname, fsize in scope.fields:
                add(
                    fname,
                    _KIND_FIELD,
                    detail=f"bits({fsize})",
                    documentation=f"Field of {scope.layout_name}",
                )
            if scope.argument:
                add(
                    scope.argument,
                    _KIND_VARIABLE,
                    detail="bits(...)",
                    documentation=f"Handler input for {scope.layout_name}",
                )

    for sym in symbols:
        kind = _SYMBOL_TO_COMPLETION_KIND.get(sym.kind, _KIND_VARIABLE)
        add(sym.name, kind, detail=sym.detail)

    if not in_interpolation:
        for name, detail, doc in _BUILTINS:
            add(name, _KIND_FUNCTION, detail=detail, documentation=doc)
        add("TRUE",  _KIND_VALUE, detail="boolean")
        add("FALSE", _KIND_VALUE, detail="boolean")

    return items
