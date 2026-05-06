import json
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional, List, Tuple


class SymbolKind(IntEnum):
    Function = 12
    Variable = 13
    Class = 5
    Struct = 23
    Enum = 10
    EnumMember = 22


@dataclass
class HandlerScope:
    """Named fields exposed inside a bitlayout handler body."""

    layout_name: str
    argument: str
    fields: List[Tuple[str, int]]
    file: str
    start_line: int
    end_line: int


@dataclass
class SymbolDef:
    name: str
    kind: SymbolKind
    file: str
    line: int
    col_start: int
    col_end: int
    detail: str = ""
    doc: str = ""


def _armlet_decoder(pairs):
    """Handle duplicate 'type' key in AST_VAR_DEF nodes."""
    result = {}
    for k, v in pairs:
        if k in result:
            result[f"__dup_{k}"] = v
        else:
            result[k] = v
    return result


def _format_type_spec(ts):
    if not isinstance(ts, dict):
        return "?"
    t = ts.get("type", "")
    if t == "AST_TUPLE":
        parts = ", ".join(_format_type_spec(e) for e in ts.get("elements", []))
        return f"({parts})"
    name_node = ts.get("name", {})
    name = name_node.get("value", "?") if isinstance(name_node, dict) else "?"
    size_node = ts.get("size")
    if size_node and isinstance(size_node, dict):
        size_val = size_node.get("value", "?")
        return f"{name}({size_val})"
    return name


def _format_params(params):
    parts = []
    for p in params:
        if not isinstance(p, dict):
            continue
        type_spec = p.get("type")
        name_node = p.get("name", {})
        pname = name_node.get("value", "?") if isinstance(name_node, dict) else "?"
        ptype = _format_type_spec(type_spec) if isinstance(type_spec, dict) else "?"
        parts.append(f"{ptype} {pname}")
    return ", ".join(parts)


def _build_fundef_detail(node):
    rt = node.get("return_type")
    ret = _format_type_spec(rt) if rt is not None else None
    name_node = node.get("name", {})
    name = name_node.get("value", "?") if isinstance(name_node, dict) else "?"
    kind = node.get("kind", "func")
    params = _format_params(node.get("params", []))
    if kind == "type":
        return f"builtin {name}({params})"
    return f"{ret} {name}({params})" if ret else f"{name}({params})"


def _build_bitlayout_detail(node):
    name = node.get("name", "?")
    members = node.get("members", [])
    total = node.get("total_bits", "?")
    parts = []
    for m in members:
        if not isinstance(m, dict):
            continue
        kind = m.get("kind")
        if kind == "named":
            parts.append(f"{m.get('name')}: {m.get('size')}")
        elif kind == "immediate":
            parts.append(f"'{m.get('bits')}'")
    member_str = ", ".join(parts)
    return f"bitlayout {name} is ({member_str})  // {total} bits"


def build_index(ast_json: str):
    """Parse AST JSON string, return (ast_obj, symbols, symbols_by_name, handler_scopes,
    fields_by_type, var_type).
    fields_by_type: type name → [(field_name, type_str)]
    var_type: variable name → type name
    """
    ast = json.loads(ast_json, object_pairs_hook=_armlet_decoder)
    symbols = []
    handler_scopes = []
    fields_by_type = {}
    var_type = {}
    _walk(ast, symbols, handler_scopes, fields_by_type, var_type)
    by_name = {}
    for sym in symbols:
        by_name.setdefault(sym.name, []).append(sym)
    return ast, symbols, by_name, handler_scopes, fields_by_type, var_type


def _span_of(node):
    span = node.get("span", {})
    if not isinstance(span, dict):
        return None
    return span


def _max_line(node) -> int:
    """Return the maximum span line number found anywhere under node."""
    best = 0
    if isinstance(node, dict):
        span = node.get("span")
        if isinstance(span, dict):
            best = max(best, span.get("line", 0))
        for v in node.values():
            best = max(best, _max_line(v))
    elif isinstance(node, list):
        for item in node:
            best = max(best, _max_line(item))
    return best


def _walk(node, symbols, handler_scopes, fields_by_type, var_type):
    if not isinstance(node, dict):
        return
    t = node.get("type", "")
    if not isinstance(t, str):
        return
    span = _span_of(node)

    if t == "AST_FUNDEF" and span:
        name_node = node.get("name", {})
        name = name_node.get("value", "") if isinstance(name_node, dict) else ""
        if name:
            symbols.append(
                SymbolDef(
                    name=name,
                    kind=SymbolKind.Function,
                    file=span.get("file", ""),
                    line=span.get("line", 0),
                    col_start=span.get("col_start", 0),
                    col_end=span.get("col_end", 0),
                    detail=_build_fundef_detail(node),
                )
            )

    elif t == "AST_BITLAYOUT" and span:
        name = node.get("name", "")
        if name:
            symbols.append(
                SymbolDef(
                    name=name,
                    kind=SymbolKind.Struct,
                    file=span.get("file", ""),
                    line=span.get("line", 0),
                    col_start=span.get("col_start", 0),
                    col_end=span.get("col_end", 0),
                    detail=_build_bitlayout_detail(node),
                )
            )
            named_members = [
                (m["name"], m.get("size", "?"))
                for m in node.get("members", [])
                if isinstance(m, dict) and m.get("kind") == "named"
            ]
            if named_members:
                fields_by_type[name] = [
                    (fname, f"bits({fsize})") for fname, fsize in named_members
                ]
            handler = node.get("handler")
            if isinstance(handler, dict):
                h_span = _span_of(handler)
                start = h_span.get("line", 0) if h_span else 0
                end = _max_line(handler)
                handler_scopes.append(
                    HandlerScope(
                        layout_name=name,
                        argument=node.get("argument", ""),
                        fields=named_members,
                        file=span.get("file", ""),
                        start_line=start,
                        end_line=end,
                    )
                )

    elif t == "AST_TYPE" and span:
        name = node.get("name", "")
        if name:
            field_list = []
            for f in node.get("fields", []):
                if not isinstance(f, dict):
                    continue
                type_spec = f.get("__dup_type")
                fname_node = f.get("name", {})
                fname = fname_node.get("value", "") if isinstance(fname_node, dict) else ""
                ftype = _format_type_spec(type_spec) if isinstance(type_spec, dict) else "?"
                if fname:
                    field_list.append((fname, ftype))
            fields_by_type[name] = field_list
            field_str = ", ".join(f"{ftype} {fname}" for fname, ftype in field_list)
            symbols.append(
                SymbolDef(
                    name=name,
                    kind=SymbolKind.Struct,
                    file=span.get("file", ""),
                    line=span.get("line", 0),
                    col_start=span.get("col_start", 0),
                    col_end=span.get("col_end", 0),
                    detail=f"type {name} is ({field_str})",
                )
            )

    elif t == "AST_TYPE_ALIAS" and span:
        from_node = node.get("from", {})
        name = from_node.get("value", "") if isinstance(from_node, dict) else ""
        if name:
            to_spec = node.get("to")
            type_str = _format_type_spec(to_spec) if isinstance(to_spec, dict) else "?"
            symbols.append(
                SymbolDef(
                    name=name,
                    kind=SymbolKind.Class,
                    file=span.get("file", ""),
                    line=span.get("line", 0),
                    col_start=span.get("col_start", 0),
                    col_end=span.get("col_end", 0),
                    detail=f"type {name} = {type_str}",
                )
            )

    elif t == "AST_ENUM" and span:
        name = node.get("name", "")
        if name:
            variants = node.get("variants", [])
            detail = f"enum {name} {{ {', '.join(variants)} }}"
            symbols.append(
                SymbolDef(
                    name=name,
                    kind=SymbolKind.Enum,
                    file=span.get("file", ""),
                    line=span.get("line", 0),
                    col_start=span.get("col_start", 0),
                    col_end=span.get("col_end", 0),
                    detail=detail,
                )
            )
            for variant in variants:
                symbols.append(
                    SymbolDef(
                        name=variant,
                        kind=SymbolKind.EnumMember,
                        file=span.get("file", ""),
                        line=span.get("line", 0),
                        col_start=span.get("col_start", 0),
                        col_end=span.get("col_end", 0),
                        detail=f"{name}.{variant}",
                        doc=f"Variant of enum {name}",
                    )
                )

    elif t == "AST_VAR_DEF" and span:
        name_node = node.get("name", {})
        name = name_node.get("value", "") if isinstance(name_node, dict) else ""
        type_spec = node.get("__dup_type")
        # Extract the bare type name for member-access completion
        type_name = None
        if isinstance(type_spec, dict):
            tname_node = type_spec.get("name", {})
            if isinstance(tname_node, dict):
                type_name = tname_node.get("value") or None
        if name:
            type_str = (
                _format_type_spec(type_spec) if isinstance(type_spec, dict) else ""
            )
            detail = f"{type_str} {name}" if type_str else name
            symbols.append(
                SymbolDef(
                    name=name,
                    kind=SymbolKind.Variable,
                    file=span.get("file", ""),
                    line=span.get("line", 0),
                    col_start=span.get("col_start", 0),
                    col_end=span.get("col_end", 0),
                    detail=detail,
                )
            )
            if type_name:
                var_type[name] = type_name
        # Handle multi-variable declarations: `bits(1) Aa, Bb, Cc;`
        for n_node in node.get("names", []):
            if not isinstance(n_node, dict):
                continue
            n = n_node.get("value", "")
            if not n:
                continue
            type_str = (
                _format_type_spec(type_spec) if isinstance(type_spec, dict) else ""
            )
            symbols.append(
                SymbolDef(
                    name=n,
                    kind=SymbolKind.Variable,
                    file=span.get("file", ""),
                    line=span.get("line", 0),
                    col_start=span.get("col_start", 0),
                    col_end=span.get("col_end", 0),
                    detail=f"{type_str} {n}" if type_str else n,
                )
            )
            if type_name:
                var_type[n] = type_name

    for v in node.values():
        if isinstance(v, dict):
            _walk(v, symbols, handler_scopes, fields_by_type, var_type)
        elif isinstance(v, list):
            for item in v:
                _walk(item, symbols, handler_scopes, fields_by_type, var_type)


def find_node_at(node, line_1: int, col_1: int, file: str = None) -> Optional[dict]:
    """Find innermost AST node whose single-line span contains (line_1, col_1).
    Positions are 1-indexed (armlet convention).
    If file is given, only nodes whose span.file matches are considered."""
    best = [None]
    _find_at(node, line_1, col_1, file, best)
    return best[0]


def _find_at(node, line, col, file, best):
    if not isinstance(node, dict):
        return
    span = node.get("span")
    if isinstance(span, dict):
        sl = span.get("line", 0)
        sc = span.get("col_start", 0)
        ec = span.get("col_end", 0)
        sf = span.get("file", "")
        if sl == line and sc <= col <= ec:
            if file is None or sf == file or sf.endswith(file) or file.endswith(sf):
                best[0] = node
    for v in node.values():
        if isinstance(v, dict):
            _find_at(v, line, col, file, best)
        elif isinstance(v, list):
            for item in v:
                _find_at(item, line, col, file, best)
