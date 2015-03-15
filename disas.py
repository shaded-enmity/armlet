#!/usr/bin/python
# armLET Disassembler
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

from arm import common
import sys, json

def get_arch_instructions(*arch):
    deser = common.JSONSerializer()
    normal, simd = [[deser.deserialize_instruction(i) for i in a['instructions']] for a in arch]
    return normal + simd

def get_encodings(encodings, softvar):
    return [e for e in encodings if common.IsVariant(e.isa, softvar)]

def select_arch(arch, data):
    if arch == 'aarch32':
        return get_arch_instructions(data[0], data[1])
    elif arch == 'aarch64':
        return get_arch_instructions(data[2], data[3])
    raise Exception('Unknown architecture: ' + arch)

def format_name(insn, cond):
    name = insn.getName()
    if cond[1] == 1:
        name += ".NE"
    return name

def register_name(rnum):
    if rnum < 13:
        return 'r%d' % rnum
    elif rnum == 13:
        return 'sp'
    elif rnum == 14:
        return 'lr'
    elif rnum == 15:
        return 'pc'

def get_registers(bits):
    regs = []
    for b in bits:
        if b[0].type == common.OperandType.REGISTER:
            regs.append(register_name(b[1]))
    if not regs:
        return ''
    if len(regs) < 2:
        return regs[0]
    r1, r0 = regs[:2]
    regs[0] = r0
    regs[1] = r1
    return ', '.join(regs)

def signed_imm(val):
    v = bin(val)[2:]
    return int('1'*len(v), 2) - val

def get_branch_target(val, pc):
    return pc - signed_imm(val)*2 - 4

def get_immediate(bits):
    for b in bits:
        if b[0].type == common.OperandType.IMMEDIATE:
            return b[1]
    return None

def disassemble(opcodes, insns, pc=0):
    thumb, PC = False, pc
    for opcode in opcodes:
        candidates = []
        #print hex(opcode)
        for insn in insns:
            variant = common.Variant.Thumb if thumb else common.Variant.Arm
            for e in get_encodings(insn.encodings, variant):
                if not e.mask:
                    continue
                if opcode & e.mask == e.mask:
                    candidates.append((insn, e))
        candidates.sort(key=lambda k: k[1].mask, reverse=True)
        enc = candidates[0][1]
        bits = []
        for o in enc.bits:
            v = (opcode >> (o.start - o.length)) & ((1 << o.length) - 1)
            bits.append((o, v))

        registers = get_registers(bits)
        immediate = get_immediate(bits)

        fmt = "0x%08X" % PC + ' ' + format_name(candidates[0][0], bits[0]) + '\t'
        if registers:
            fmt += ' ' + registers
        if immediate != None:
            if candidates[0][0].getName() != 'B':
                fmt += ' #%i' % immediate
            else:
                fmt += ' 0x%08x' % get_branch_target(immediate, PC)
        print fmt
        PC += enc.getBitSize() / 8
                    
def main():
    args = sys.argv[1:]
    datafile, disasfile, arch = args
    insndata = json.load(open(datafile))
    instructions = select_arch(arch, insndata['InstructionData'])
    disassemble([int(s.strip(), 16) for s in open(disasfile).readlines()], instructions)

main()
