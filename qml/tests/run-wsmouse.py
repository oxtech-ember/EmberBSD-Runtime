#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the real wsmouse adapter against constants from the selected OS."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host-prefix', required=True, type=Path)
parser.add_argument('--wscons-header', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
host = args.host_prefix.resolve(strict=True)
clang = (host / 'bin/clang').resolve(strict=True)
if not clang.is_relative_to(host):
    parser.error('compiler must be inside the selected host prefix')
header_text = args.wscons_header.read_text()
names = (
    'WSCONS_EVENT_MOUSE_UP', 'WSCONS_EVENT_MOUSE_DOWN',
    'WSCONS_EVENT_MOUSE_DELTA_X', 'WSCONS_EVENT_MOUSE_DELTA_Y',
    'WSCONS_EVENT_MOUSE_ABSOLUTE_X', 'WSCONS_EVENT_MOUSE_ABSOLUTE_Y',
    'WSCONS_EVENT_MOUSE_DELTA_Z', 'WSEVENT_VERSION',
)
macros = []
for name in names:
    match = re.search(r'^#define\s+' + name + r'\s+([0-9]+)\b', header_text, re.M)
    if match is None:
        parser.error('missing constant in selected OS header: ' + name)
    macros.append(f'#define {name} {match.group(1)}')

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='eqml-wsmouse-') as temporary:
    temporary = Path(temporary)
    compat = temporary / 'dev/wscons/wsconsio.h'
    compat.parent.mkdir(parents=True)
    compat.write_text('''#ifndef EQML_TEST_WSCONSIO_H
#define EQML_TEST_WSCONSIO_H
#include <stdint.h>
#include <time.h>
''' + '\n'.join(macros) + '''
#define WSMOUSE_EVENT_VERSION WSEVENT_VERSION
#define WSMOUSEIO_SETVERSION 0x574d0001UL
struct wscons_event { unsigned int type; int value; struct timespec time; };
#endif
''')
    executable = args.out.resolve()
    executable.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(clang), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-Dopen=eqml_test_open', '-Dioctl=eqml_test_ioctl',
        '-Dclock_gettime=eqml_test_clock_gettime',
        '-I' + str(temporary), '-I' + str(root),
        str(root / 'eqml.c'), str(root / 'eqml_input.c'),
        str(root / 'eqml_wsmouse.c'), str(root / 'tests/test_eqml_wsmouse.c'),
        '-o', str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
