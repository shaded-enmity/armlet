#!/usr/bin/env python3
"""Armlet LSP server — communicates over stdio using JSON-RPC 2.0."""
import os
import re
import sys
import shutil
import subprocess
import tempfile
import urllib.parse
import logging

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import transport
import ast_index
import features
import diagnostics

logging.basicConfig(
    filename=os.path.join(os.path.dirname(__file__), 'server.log'),
    level=logging.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s',
)
log = logging.getLogger(__name__)

_server_dir = os.path.dirname(os.path.abspath(__file__))
_project_dir = os.path.dirname(_server_dir)
_local_bin = os.path.join(_project_dir, 'armlet')
armlet_bin = _local_bin if os.path.isfile(_local_bin) else (shutil.which('armlet') or 'armlet')

_cache: dict = {}


def _uri_to_path(uri: str) -> str:
    if uri.startswith('file://'):
        return urllib.parse.unquote(uri[len('file://'):])
    return uri


def run_armlet(file_path: str, text: str):
    """Write text to a temp file, run armlet twice: once for AST (-P -L) and once
    for diagnostics (stderr only). Returns (ast_json_str or None, stderr_str)."""
    suffix = os.path.splitext(file_path)[1] or '.aml'
    file_dir = os.path.dirname(file_path) or _project_dir
    with tempfile.NamedTemporaryFile(mode='w', suffix=suffix, dir=file_dir, delete=False) as tf:
        tf.write(text)
        tmp_path = tf.name
    try:
        common = dict(cwd=_project_dir, capture_output=True, text=True, timeout=10)
        extra = ['-I', file_dir] if file_dir != _project_dir else []
        r_ast  = subprocess.run([armlet_bin, '-P', '-L'] + extra + [tmp_path], **common)
        r_diag = subprocess.run([armlet_bin] + extra + [tmp_path], **common)

        ast_json = r_ast.stdout.replace(tmp_path, file_path) if r_ast.stdout.strip() else None
        stderr   = r_diag.stderr.replace(tmp_path, file_path)
        return ast_json, stderr
    except Exception as e:
        log.error('armlet failed: %s', e)
        return None, str(e)
    finally:
        os.unlink(tmp_path)


# Match 'identifier.' at end of line (no field name yet, or just whitespace/semicolon)
_INCOMPLETE_DOT   = re.compile(r'(\w)\.([ \t]*;?[ \t]*)$', re.MULTILINE)
# Match 'identifier.<' with optional partial field list (identifiers, commas, spaces) to end of line
_INCOMPLETE_DOTLT = re.compile(r'(\w)\.<[\w, \t]*(;?[ \t]*)$', re.MULTILINE)

def _patch_incomplete_member_access(text: str) -> str:
    """Replace trailing 'foo.' / 'foo.<...' with syntactically valid placeholders."""
    text = _INCOMPLETE_DOT.sub(r'\1.placeholder_;', text)
    text = _INCOMPLETE_DOTLT.sub(r'\1.<placeholder_>;', text)
    return text


def update_cache(uri: str, text: str):
    """Re-run armlet on the document and update the index + publish diagnostics."""
    file_path = _uri_to_path(uri)
    json_str, stderr = run_armlet(file_path, _patch_incomplete_member_access(text))
    log.debug('armlet stderr: %s', stderr[:500] if stderr else '')

    diags = diagnostics.parse_diagnostics(stderr or '', file_path)
    transport.write_message({
        'jsonrpc': '2.0',
        'method': 'textDocument/publishDiagnostics',
        'params': {'uri': uri, 'diagnostics': diags},
    })

    # Always refresh lines so completion sees current text even if parse fails
    if uri in _cache:
        ast, syms, by_name, handler_scopes, fp, _lines, fields_by_type, var_type = _cache[uri]
        _cache[uri] = (ast, syms, by_name, handler_scopes, fp, text.splitlines(), fields_by_type, var_type)

    if json_str:
        try:
            ast, syms, by_name, handler_scopes, fields_by_type, var_type = ast_index.build_index(json_str)
            _cache[uri] = (ast, syms, by_name, handler_scopes, file_path, text.splitlines(), fields_by_type, var_type)
            log.debug('Indexed %d symbols, %d handler scopes for %s',
                      len(syms), len(handler_scopes), uri)
        except Exception as e:
            log.error('Index build failed: %s', e)


def handle(msg):
    method = msg.get('method', '')
    msg_id = msg.get('id')
    params = msg.get('params', {}) or {}

    def respond(result):
        if msg_id is not None:
            transport.write_message({'jsonrpc': '2.0', 'id': msg_id, 'result': result})

    def error(code, message):
        if msg_id is not None:
            transport.write_message({'jsonrpc': '2.0', 'id': msg_id,
                                     'error': {'code': code, 'message': message}})

    if method == 'initialize':
        respond({
            'capabilities': {
                'textDocumentSync': {'openClose': True, 'change': 1},
                'hoverProvider': True,
                'definitionProvider': True,
                'documentSymbolProvider': True,
                'completionProvider': {'triggerCharacters': ['.', '<']},
            },
            'serverInfo': {'name': 'armlet-lsp', 'version': '0.1.0'},
        })

    elif method == 'initialized':
        pass

    elif method == 'shutdown':
        respond(None)

    elif method == 'exit':
        sys.exit(0)

    elif method == 'textDocument/didOpen':
        td = params.get('textDocument', {})
        update_cache(td.get('uri', ''), td.get('text', ''))

    elif method == 'textDocument/didChange':
        td = params.get('textDocument', {})
        changes = params.get('contentChanges', [])
        if changes:
            update_cache(td.get('uri', ''), changes[-1].get('text', ''))

    elif method == 'textDocument/didClose':
        uri = params.get('textDocument', {}).get('uri', '')
        _cache.pop(uri, None)

    elif method == 'textDocument/hover':
        uri = params.get('textDocument', {}).get('uri', '')
        pos = params.get('position', {})
        line_0 = pos.get('line', 0)
        char_0 = pos.get('character', 0)
        if uri in _cache:
            ast, syms, by_name, handler_scopes, file_path, lines, fields_by_type, var_type = _cache[uri]
            result = features.hover(ast, by_name, handler_scopes, uri, file_path, line_0, char_0)
            respond(result)
        else:
            respond(None)

    elif method == 'textDocument/definition':
        uri = params.get('textDocument', {}).get('uri', '')
        pos = params.get('position', {})
        line_0 = pos.get('line', 0)
        char_0 = pos.get('character', 0)
        if uri in _cache:
            ast, syms, by_name, handler_scopes, file_path, lines, fields_by_type, var_type = _cache[uri]
            result = features.definition(ast, by_name, uri, file_path, line_0, char_0)
            respond(result)
        else:
            respond([])

    elif method == 'textDocument/documentSymbol':
        uri = params.get('textDocument', {}).get('uri', '')
        if uri in _cache:
            ast, syms, by_name, handler_scopes, file_path, lines, fields_by_type, var_type = _cache[uri]
            result = features.document_symbol(syms, file_path)
            respond(result)
        else:
            respond([])

    elif method == 'textDocument/completion':
        uri = params.get('textDocument', {}).get('uri', '')
        pos = params.get('position', {})
        line_0 = pos.get('line', 0)
        char_0 = pos.get('character', 0)
        if uri in _cache:
            ast, syms, by_name, handler_scopes, file_path, lines, fields_by_type, var_type = _cache[uri]
            current_line = lines[line_0] if line_0 < len(lines) else ''
            result = features.completion(syms, handler_scopes, file_path, line_0, char_0, current_line, fields_by_type, var_type)
            respond(result)
        else:
            respond([])

    elif msg_id is not None:
        error(-32601, f'Method not found: {method}')


def main():
    log.info('armlet-lsp starting, binary: %s, project: %s', armlet_bin, _project_dir)
    while True:
        try:
            msg = transport.read_message()
            if msg is None:
                break
            log.debug('→ %s id=%s', msg.get('method'), msg.get('id'))
            handle(msg)
        except Exception as e:
            log.exception('Unhandled error: %s', e)


if __name__ == '__main__':
    main()
