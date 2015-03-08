#!/usr/bin/python
# armLET LaTeX Serializer 
#
# Copyright (C) 2013 Pavel Odvody
#
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA
#

import sys, json

from arm import common
from lib import utils

def serialize(insn):
    insn, out = json.loads(insn), ''
    for e in insn['encodings']:
        out += '\\begin{instruction}{%s. %s [%s]}%%\n' % (insn['id'], ', '.join(insn['names']), e['name'])
        for bp in e['bits']:
            flags = ''
            if bp['type'] == common.OperandType.REGISTER:
                flags = ', register'
            elif bp['type'] == common.OperandType.IMMEDIATE:
                flags = ', opcode'
            elif bp['type'] == common.OperandType.CONDITION:
                flags = ', opcode'
            out += ' \\addpart[bits=%d%s] {%s};%%\n' % (bp['size'], flags, bp['name'])
        out += '\\end{instruction}%\n\n'
    return out
