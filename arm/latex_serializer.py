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

import sys, json, re

from arm import common
from lib import utils

def tokenize(mnem):
    name = re.match('^\w+',  mnem).group(0)
    mnem = mnem[len(name):]
    code = '\\newmnemonics['
    for m in re.compile(r'{(.*?)}').finditer(mnem):
        innercomma = m.group(1).find(',')
        span = m.span()
        val = m.group(1).replace(',', '')
        code += 'open curly, operand={' + val + '}, '
        if innercomma != -1:
            code += 'comma, '
        code += 'close curly, '
        mnem = mnem[:span[0]] + ' '*(span[1]-span[0]) + mnem[span[1]:]
    mnem = mnem.strip()
    for m in re.compile(r'#?<.*?>,?').finditer(mnem):
        innercomma = m.group(0).find(',')
        span = m.span()
        cpref = m.group(0).startswith('#')
        val = m.group(0).replace(',', '').replace('#', '\\#')
        code += 'operand={' + val + '}, '
        if innercomma != -1:
            code += 'comma, '
        mnem = mnem[:span[0]] + ' '*(span[1]-span[0]) + mnem[span[1]:]
    code = code[:-2]
    code += ']{%s};%%' % name
    return code

def serialize(insn):
    insn, out = json.loads(insn), ''
    for e in insn['encodings']:
        out += '\\begin{instruction}{%s. %s [%s]}%%\n' % (insn['id'], ', '.join(insn['names']), e['name'])
        for bp in reversed(e['bits']):
            flags = ''
            if bp['type'] == common.OperandType.REGISTER:
                flags = ', register'
            elif bp['type'] == common.OperandType.IMMEDIATE:
                flags = ', opcode, color=cyan!60!'
            elif bp['type'] == common.OperandType.CONDITION:
                flags = ', opcode, color=green'
            elif bp['type'] == common.OperandType.BITS:
                bp['name'] = ', '.join(bp['name'].split(' '))
            elif bp['type'] == common.OperandType.OPERAND:
                if len(bp['name']) == 1:
                    flags = ', opcode, color=red, name={%s}' % bp['name']
                elif len(bp['name']) == 2:
                    flags = ', opcode, color=orange'
            out += ' \\addpart[bits=%d%s] {%s};%%\n' % (bp['size'], flags, bp['name'])
        for mv in e['mnemonics']:
            cn, cc = '', ''
            if mv['constraint']:
                cn, cc = mv['constraint'].strip().strip('.').split('=', 1)
                cc = '=' + cc
            out += '\\newvariant{%s}{%s}{%s};%%\n' % (mv['name'], cn.strip(), cc)
            out += tokenize(mv['mnemonics'][0]) + '\n'

        out += '\\end{instruction}%\n\n'
    return out
