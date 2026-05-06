import re

_ANSI_RE = re.compile(r'\x1b\[[0-9;]*m')
_ERR_RE = re.compile(r'^(.+?):(\d+):(\d+):(\d+):\s+ERROR:\s+(.+)')


def parse_diagnostics(stderr_text: str, file_path: str):
    """Parse armlet stderr output into LSP diagnostic objects.

    Returns list of {range, severity, message} dicts for errors in file_path.
    """
    diagnostics = []
    clean = _ANSI_RE.sub('', stderr_text)
    for line in clean.splitlines():
        m = _ERR_RE.match(line)
        if not m:
            continue
        err_file, err_line, col_start, col_end, message = m.groups()
        if not (err_file == file_path or file_path.endswith(err_file) or err_file.endswith(file_path)):
            continue
        ln = int(err_line) - 1
        cs = int(col_start) - 1
        ce = int(col_end)
        diagnostics.append({
            'range': {
                'start': {'line': ln, 'character': cs},
                'end':   {'line': ln, 'character': ce},
            },
            'severity': 1,
            'source': 'armlet',
            'message': message.strip(),
        })
    return diagnostics
