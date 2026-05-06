import sys
import json


def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        line = line.decode('utf-8').strip()
        if not line:
            break
        if ': ' in line:
            k, v = line.split(': ', 1)
            headers[k] = v
    length = int(headers.get('Content-Length', 0))
    if length == 0:
        return None
    body = sys.stdin.buffer.read(length)
    return json.loads(body)


def write_message(obj):
    body = json.dumps(obj).encode('utf-8')
    header = f'Content-Length: {len(body)}\r\n\r\n'.encode('utf-8')
    sys.stdout.buffer.write(header + body)
    sys.stdout.buffer.flush()
