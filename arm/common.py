#
# armLET ARM Core
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

from lib import utils
import string, pprint, json

PageType = utils.Enum(Invalid=0, AArch32=1, SIMD32=2, AArch64=4, SIMD64=8,
                        Complementary=16, GPRTable=32)

class CoreVersion(object):
    (INVALID, PRE_V4, V4, V4T, V5T, V5TE, V5TEJ, V6, V6K, V6Z, V6T2, V7A,
     V7R, V7M, V6M, V6SM, V7S, V7, V_OBAN, V7EM, V8A, V8A_32, V8A_64,
     UNSAFE, PROTEUS, PROTEUS_EXT) = utils.BinaryRange(26)


class VFPNEONVersion(object):
    (INVALID, VFPV2, VFPV3, VFPV3_16, VFPV3_NEONV1, NEONV1_INTEGER, VFPV3_HPFP,
     VFPV3_HPFP_16, VFPV_HPFP_NEONV1_HPFP, VFPV2_SP, VFPV3_SP, VFPV3_SP_16,
     VFPV3_SP_HPFP, VFPV3_SP_HPFP_16, VFPV4, VFPV4_16, VFPV4_SP, VFPV4_SP_16,
     VFPV4_NEONV2, VFPV5_16, VFPV5_SP_16, VFPV_UNSAFE, VFPV_8A_FP,
     VFPV_8A_FP_NEON, VFPV_OBAN_FP, VFPV_OBAN_FP_NEON) = utils.BinaryRange(26)


class ArchExtension(object):
    (INVALID, THUMBEE, SECURITY, JAZELLE, MULTIPROCESSING, VIRTUALIZATION,
     THUMBDIVIDE, ARMDIVIDE, MARVELLDIVIDE, XSCALE, WMMX, WMMX2, FUSED_MAD,
     VMSAV7, PMSAV7, CRYPTOGRAPHY) = utils.BinaryRange(16)


class CoreProfile(object):
    (INVALID, A, R, M, S) = utils.BinaryRange(5)


class CoreMode(object):
    (INVALID, USER, FIQ, IRQ, SVC, MON, ABT, HYP, UND, SYS) = utils.BinaryRange(10)


class Variant(object):
    Invalid = 0

    ''' Base ISA Variants '''
    Arm = 1 << 0
    Thumb = 1 << 1
    Simd = 1 << 2

    ''' Bitness Variants '''
    Bits8 = 1 << 3
    Bits16 = 1 << 4
    Bits32 = 1 << 5
    Bits64 = 1 << 6
    Bits128 = 1 << 7
    BitsInvolved = Bits8 | Bits16 | Bits32 | Bits64 | Bits128

    ''' Addressing Modes Variants '''
    Offset = 1 << 8
    PreIndex = 1 << 9
    PostIndex = 1 << 10
    Literal = 1 << 11
    Relative = 1 << 12

    ''' Named Variants '''
    System = 1 << 13
    Vector = 1 << 14
    Scalar = 1 << 15

    ''' Numeric Variants '''
    Unsigned = 1 << 16
    Single = 1 << 17
    Double = 1 << 18
    Signed = 1 << 19

    ''' Offset Variants '''
    ZeroOffset = 1 << 20
    ImmediateOffset = 1 << 21
    RegisterOffset = 1 << 22
    HasOffset = ImmediateOffset | RegisterOffset

    ''' Register Variants '''
    SameRegisters = 1 << 23
    DiffRegisters = 1 << 24
    NumRegs1 = 1 << 25
    NumRegs2 = 1 << 26
    NumRegs3 = NumRegs1 | NumRegs2
    NumRegs4 = 1 << 27
    RegTable = 1 << 28

    ''' Conversion Variants '''
    BitsToFloating = 1 << 29
    FloatingToBits = 1 << 30

    ''' Special & Exotic stuff '''
    CRC32 = 1 << 31
    ZeroImmediate = 1 << 32
    Archetype = 1 << 33

    _Listed = ['Arm', 'Thumb', 'Simd', 'Bits8', 'Bits16', 'Bits32', 'Bits64', 'Bits128',
		'Offset', 'PreIndex', 'PostIndex', 'Literal', 'Relative', 'System', 'Scalar',
		'Vector', 'Unsigned', 'Single', 'Double', 'Signed', 'ZeroOffset', 'ImmediateOffset',
		'RegisterOffset', 'SameRegisters', 'DiffRegisters', 'NumRegs1', 'NumRegs2', 'NumRegs4',
		'RegTable', 'BitsToFloating', 'FloatingToBits', 'CRC32', 'ZeroImmediate', 'Archetype']

    ''' Archetypes '''
    AArch64_ARM = Arm | Bits64 | Archetype
    AArch64_SIMD = Simd | Bits64 | Archetype
    AArch32_ARM = Arm | Bits32 |  Archetype
    AArch32_SIMD = Simd | Bits32 | Archetype
    AArch32_SYS = System | Bits32 | Archetype
    AArch32_THUMB = Thumb | Bits32 | Archetype
    AArch32_THUMB16 = Thumb | Bits16 | Archetype

    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        return self.isVariant(other)

    def __hash__(self):
        return hash(self.value)

    def __repr__(self):
	lines = []
	for v in self._Listed:
		val = getattr(self, v)
		if self.value & val != 0:
			lines.append(v)
        return '[' + ' '.join(lines) + ' (0x%x)]' % self.value

    def isVariant(self, other):
        if isinstance(other, int):
            return self.value == other

        return self.value == other.value


def IsVariant(a, b):
    return (a & b) != 0

OperandType = utils.Enum(INVALID=0, REGISTER=1, IMMEDIATE=2, OPERAND=3, CONDITION=4, BITS=5)
class BitOperand(object):
    def __init__(self, name, start, length, t=None):
        self.name = name
        self.start = start
        self.length = length
        self.type = t or OperandType.INVALID

    def __repr__(self):
        return '<%s> (%i)' % (self.name, self.length)

class EncodingVariant(utils.Test):
    def __init__(self, isa, num=1, name=''):
        self.name = name
        self.isa = isa
        self.num = num
        self.mnemonics = []
        self.support = []
        self.detail = []
        self.bits = []
        self.decode = []

    def getName(self):
        if self.name:
            return self.name
        if self.isa in [Variant.AArch32_THUMB, Variant.AArch32_THUMB16]:
            return 'T%i' % self.num
        return 'A%i' % self.num

    def getBitSize(self):
        if self.isa in [Variant.AArch32_THUMB16]:
            return 16
        return 32

    def getCoverage(self):
        return reduce(utils.Add, [c.length for c in self.bits], 0)

    def addOperand(self, op):
        self.bits.append(op)

    def addDecode(self, decode):
        self.decode.append(decode)

    def validate(self, context=None):
        if self.getCoverage() != self.getBitSize():
            return False
        return True

    def report(self):
        utils.Log(' --- [%i/%i bits covered per %i bitparts, %i assembly mnemonics]',
              (self.getCoverage(), self.getBitSize(), len(self.bits), len(self.mnemonics)), self)

        if self.getCoverage() != self.getBitSize():
            utils.Log(' --- insn %s components [%s]', (self.getName(),
              ', '.join([str(x) for x in self.bits])), self)

class Constraint(object):
    EMPTY,NEVER,ALWAYS=None,None,None
    def __init__(self, value=''):
        self.string = value

    def solve(self, context=None):
        return True

Constraint.EMPTY = Constraint()
Constraint.NEVER = Constraint('false')
Constraint.ALWAYS = Constraint('true')

class ConstrainedAlias(object):
    def __init__(self, m):
        self.value = m
        self.constraint = Constraint.EMPTY

class Mnemonics(object):
    def __init__(self, m, aliases=None):
        self.value = m
        self.aliases = aliases or []

    def addAlias(self, m):
        self.aliases.append(ConstrainedAlias(m))

    def setConstraint(self, cons):
        self.aliases[-1].constraint = Constraint(cons)

class MnemonicsVariant(object):
    def __init__(self, name='', constraint=None, mnemonics=None):
        self.name = name
        self.constraint = constraint or Constraint.EMPTY
        self.mnemonics = mnemonics or []
        self.aliases = []

    def addConstraint(self, fromstr):
        if self.constraint != Constraint.EMPTY:
            utils.Log(' -- multiple constraint for %s (old: %s) (new: %s)', (self.name, str(self.constraint), fromstr))
        self.constraint = Constraint(fromstr)

    def addMnemonics(self, mnemonics):
        self.mnemonics.append(Mnemonics(mnemonics))

class Instruction(utils.Test):
    def __init__(self):
        self.num_id = 0
        self.names = []
        self.summary = []
	self.metadata = {"Syntax": [], "Operation": [], "Decode": []}
	self.encodings = []

    def setHeader(self, num, *names):
        self.num_id = num
        for n in names:
            self.names.append(n)
        return self

    def getName(self):
        return self.names[0] if self.names else '* unknown *'

    def addNames(self, names):
        if not isinstance(names, list):
            self.names.append(names)
        else:
            for n in names:
                self.names.append(n)
        return self

    def addSummary(self, line):
        self.summary.append(line)

    def getEncoding(self, isa=None, num=None):
        if isa == None and num == None:
            return self.encodings[-1]
        return [x for x in self.encodings\
                 if x.isa == isa and x.num == num][0] or None

    def setEncodingIsa(self, isa):
        enc = self.getEncoding()
        enc.isa = isa
        return self

    def setEncodingBits(self, bits):
        enc = self.getEncoding()
        enc.bits = bits
        return self

    def addEncoding(self, archnum, support=None, isa=Variant.Invalid):
        a = utils.Int(archnum)
        if a != None:
            variant = EncodingVariant(isa, a)
        else:
            variant = EncodingVariant(isa, 1, support)
        variant.support.append(support)
        self.encodings.append(variant)
        return variant

    def _getmnemonics(self):
        return self.getEncoding().mnemonics

    def addMnemonicsVariant(self, name):
        self._getmnemonics().append(MnemonicsVariant(name))

    def addConstraint(self, constr):
        self._getmnemonics()[-1].addConstraint(constr)

    def addMnemonics(self, mnem):
        self._getmnemonics()[-1].addMnemonics(mnem)

    def addMnemonicAlias(self, target):
        self._getmnemonics()[-1].mnemonics[-1].addAlias(target)

    def addMnemonicAliasConstraint(self, constr):
        self._getmnemonics()[-1].mnemonics[-1].setConstraint(constr)

    def addDecode(self, decode):
        self.getEncoding().addDecode(decode)

    def addEncodingSupport(self, support):
        if not isinstance(support, list):
            self.getEncoding().support.append(support)
        else:
            for m in support:
                self.getEncoding().support.append(m)
        return self

    def serialize(self):
        """
        print 'NUM:', self.num_id
        print 'NMS:', self.names
        print 'SUM:', self.summary
        print ''

        for encoding in self.encodings:
            print encoding.getName()
            print '', str(encoding.isa)
            print '', encoding.bits
            print ''
            for mv in encoding.mnemonics:
                print ' *', mv.name, mv.constraint.string
                print ' ' + '\n  *'.join(mv.mnemonics)
            print ''
            print ' Variant decode:'
            print '  ' + '\n  '.join(encoding.decode) 
            print ''   

        print ''
        print ' OPERATION: '
        # the pseudocode uses indentation just like Python for scoping
        # so make sure that we remove only the common leading whitespace
        remove = min([len(utils.LeadingWhitespace.match(x).group(0)) for x in self.metadata['Operation']])
        print ' ' + ' '.join([x[remove:] for x in self.metadata['Operation']])
        print ''
        print ' SYMBOLS: '
        remove = min([len(utils.LeadingWhitespace.match(x).group(0)) for x in self.metadata['Syntax']])
        print ' ' + ' '.join([x[remove:] for x in self.metadata['Syntax']])
        print ''
        """
        return JSONSerializer().serialize_instruction(self)

    def validate(self, context=None):
        if self.num_id != 0:
            failed = False
            for enc in self.encodings:
                if not enc.validate():
                    failed = True
                    enc.report()
            if failed:
                utils.Log('--- validating "%s (%s)" (%i) encodings',
                        ((self.names[0] if len(self.names) else 'UNKNOWN'),
                         self.num_id, len(self.encodings)), self)

                #ARM32JSONSerializer().serialize_instruction(self)
                utils.Log('*** FAILED !!! ', (), self)
                return False
            return True
        return False

class JSONSerializer(object):
    def __init__(self):
        self.data = ''

    def serialize_instruction(self, insn):
        " :type insn: Instruction "
        names = [n.replace(',', '').strip() for n in insn.names]
        encodings = []
        for enc in insn.encodings:
	    counter, num = enc.getBitSize(), 0
	    for x in enc.bits:
		counter -= x.length
		pnum = 0
		if x.type == OperandType.BITS:
		    ln = x.name.count(' ')
		    for n, y in enumerate(x.name.split(' ')):
		        if y in ['1', '(1)']:
			    pnum |= 1 << (ln - n)
		    num |= pnum << counter
		    
            mnemonics = [{'name': mv.name, 'constraint': mv.constraint.string, 'mnemonics': [{'value': m.value, 'aliases': [{'target': a.value, 'constraint':a.constraint.string} for a in m.aliases]} for m in mv.mnemonics]} for mv in enc.mnemonics]
            encodings.append({'name': enc.getName(), 'variant': str(enc.isa), 
		    'mnemonics': mnemonics, 'mask': '0x%08x'%num, 'decode': enc.decode, 'bits': [{'name': bc.name, 'size': bc.length, 'type': bc.type} for bc in enc.bits]
                })
        if insn.metadata['Operation']:
	   remove = min([len(utils.LeadingWhitespace.match(x).group(0)) for x in insn.metadata['Operation']])
           operation = [x[remove:].strip('\n\r') for x in insn.metadata['Operation']]
	else:
	   operation = []
	if insn.metadata['Syntax']:
	   remove = min([len(utils.LeadingWhitespace.match(x).group(0)) for x in insn.metadata['Syntax']])
           symbols = [''.join([c for c in x[remove:].strip('\n\r') if c in string.printable]) for x in insn.metadata['Syntax']]
	else:
           symbols = []
	if insn.metadata['Decode']:
	   remove = min([len(utils.LeadingWhitespace.match(x).group(0)) for x in insn.metadata['Decode']])
           decode = [''.join([c for c in x[remove:].strip('\n\r') if c in string.printable]) for x in insn.metadata['Decode']]
	else:
           decode = []


        jsondict = {
            'id': insn.num_id,
            'names': names,
            'summary': {
              'lines': insn.summary
            },
            'encodings': encodings,
            'operation': {
              'lines': operation
            },
            'symbols': {
              'lines': symbols
            },
	    'decode': {
	      'lines': decode
	    }
        }

	return json.dumps(jsondict, sort_keys=True, indent=2, separators=(',', ': '))

class ManualPage(object):
    def __init__(self, lines, pt=PageType.Invalid):
        self.page_type = pt
        self.page_lines = lines
        self.num = 0

    def parseLines(self):
        return PageType.Invalid

class PageCompleter(object):
    NoMatch = 0
    PageStarted = 1
    PageCompleted = 2

    def __init__(self, profiler=None):
        self.profiler = profiler
        self.page_start = 0
        self.pages_list = []
        self.pages = []

    def createPage(self, lines):
        rng = self.pages_list[-1]
        page = ManualPage(lines[rng[0]:rng[1] - 3])
        page.num = len(self.pages_list)
        self.pages.append(page)
        return page

    def isHeader(self, line):
        return False

    def isFooter(self, line):
        return False

    def execute(self, line, profiler):
        if self.page_start != 0:
            if self.isFooter(line):
                page = (self.page_start, profiler.getCounter('total_lines'))
                self.pages_list.append(page)
                self.page_start = 0
                profiler.counter('pages')
                return self.PageCompleted
        if self.isHeader(line):
            self.page_start = profiler.getCounter('total_lines')
            return self.PageStarted
        return self.NoMatch

class DataStrings(object):
    def getStrings(self):
        return []

class Pager(PageCompleter):
    def __init__(self, data):
	super(Pager, self).__init__()
	self.PageFooters = [utils.SliceMatcher(data.getPageFooter(), pipe=utils.Pipes.WipeWhitespace)]
	self.PageHeader = utils.SliceMatcher(data.getPageHeader(), pipe=utils.Pipes.WipeWhitespace)

    def isHeader(self, line):
        r = self.PageHeader.match(line)
	return r

    def isFooter(self, line):
	r = any(h.match(line) for h in self.PageFooters)
	return r


class DataPump(object):
    def __init__(self, filename):
        self.fp = open(filename, 'rb')
        self.lines = None
        self.engines = EngineManager()

    def registerEngines(self, *engines):
        for engine in engines:
            self.engines.fromVariant(engine.getArchVariant(), engine)

    def loadRawData(self):
        self.lines = self.fp.readlines()
        if self.lines:
            utils.Log('successfully loaded (%i) lines', (len(self.lines)),
                   self, utils.LogCat.Input)
            return True
        else:
            utils.NLog('data loading failed',
                       (), self, utils.LogCat.Leaf)
            return False

    def execute(self, profiler):
        for line in self.lines:
            profiler.counter('total_lines')
            if line.strip() == '':
                profiler.counter('empty_lines')
            else:
                for p in self.engines.getEngines():
                    pagexec = p.page_completer.execute(line, profiler)
                    if pagexec == PageCompleter.PageCompleted:
                        p.page_completer.createPage(self.lines)
                        break
                    elif pagexec == PageCompleter.PageStarted:
                            break

        utils.Log('=====================================' * 2, (),
                   self, utils.LogCat.Marker)
        for p in self.engines.getEngines():
            #utils.NLog('Executing over %i pages', (len(p.page_completer.pages)), self)
            p.execute()
        utils.Log('=====================================' * 2, (),
                   self, utils.LogCat.Marker)


class EngineManager(object):
    _engine_variants = [Variant.AArch32_ARM, Variant.AArch32_SIMD,
                        Variant.AArch32_SYS, Variant.AArch64_ARM,
                        Variant.AArch64_SIMD]

    def __init__(self, prepared=None):
        self.engines = prepared or {}
        self.ordered = self.engines.values() or []

    def getEngines(self):
        return self.ordered

    def fromVariant(self, variant, eset=None):
        if variant not in EngineManager._engine_variants:
            return None
        if variant in self.engines:
            return self.engines[variant]
        else:
            self.engines[variant] = eset
            self.ordered.append(eset)
            return eset

class Stage(object):
    (Start, Name, Summary, Bitheader, Operands, Variant, Mnemonics,
     Pseudocode, Aliases, Symbols, Operation, Support, Components, Syntax,
     Decode, Equivalency, SharedDecode) = utils.BinaryRange(17)

class Engine(object):
    INVALID=Variant(Variant.Archetype | Variant.Invalid)
    ARM32=Variant(Variant.AArch32_ARM)
    ARM32System=Variant(Variant.AArch32_SYS)
    ARM32SIMD=Variant(Variant.AArch32_SIMD)
    THUMB=Variant(Variant.AArch32_THUMB)
    THUMB16=Variant(Variant.AArch32_THUMB16)
    ARM64=Variant(Variant.AArch64_ARM)
    ARM64SIMD=Variant(Variant.AArch64_SIMD)

    def __init__(self):
        self.stage = Stage.Start
	self.insn = None
        self.last_instruction = None
	self.page_completer = Pager(self.getData())
	self.target_count = self.getData().getTargetCount()
	self.instructions = []
	self.num_map = {}

    def getData(self):
	return None

    def getArchVariant(self):
        return self.INVALID

    def getProxies(self):
        return {}

    def _parse_variant(self, line):
        if line.endswith('variant'):
            return line
        return None

    def _parse_bitheader(self, line):
        " Require at least N integers on line to qualify "
        NUM_PARTS_THRESHOLD = 6

        def _collapse((gaps, last), value):
            if last - 1 == value or last == 0:
                if not gaps:
                    gaps.append(last)
                gaps.append(value)
                return (gaps, value)
            if len(gaps) > 1:
                gaps.pop()
            gaps.append((last, value))
            return (gaps, value)

        vals = [utils.Int(x) for x in filter(None, line.split(' '))]
        filtered = filter(lambda x: x != None, vals)

        if len(filtered) != len(vals):
            return None
        
        if len(filtered) > NUM_PARTS_THRESHOLD:
            gaps = reduce(_collapse, filtered[1:], ([], filtered[0]))
            return gaps[0]
        
        return None

    def _unpack_header(self, header):
        def _mapper(item):
            if isinstance(item, tuple):
                return [item[0], item[1]]
            return [item]
        a = []
        for i in map(_mapper, header):
            a.extend(i)
        return a

    def _parse_components(self, line):
        TWOBITS=['type', 'imm2', 'imod', 'sz', 'opt', '!=11', 'rotate']

        def _create((name, header)):
            s, l, t = header, 1, OperandType.INVALID
            if isinstance(header, tuple):
                s, l = header[1], header[0] - header[1] + 1
            if name.startswith('R'):
                t = OperandType.REGISTER
            elif name.startswith('imm') or name == 'i':
                t = OperandType.IMMEDIATE
            elif name.startswith('!='):
                t = OperandType.CONDITION
            elif len(name) < 3:
                t = OperandType.OPERAND
            return BitOperand(name, s, l, t)

        def _collapse((items, acc, first), value):
            if value.name in ['0', '1', '(0)', '(1)', 'x']:
                acc.append(value.name)
                return (items, acc, first if first != None else value)
            elif items and value.name == items[-1].name:
                if acc:
                    items.append(BitOperand(' '.join(acc), first, len(acc), OperandType.BITS))
                items[-1].length += value.length
                return (items, [], None)
            else:
                if acc:
                    items.append(BitOperand(' '.join(acc), first, len(acc), OperandType.BITS))
                items.append(value)
                return (items, [], None)

        vals = [s.strip() for s in line.split(' ') if s.strip()]
	fixups = [s for s in vals if self.getData().isTwobitOperand(self.insn.num_id, s)]
        if fixups:
            for f in fixups:
                vals.insert(vals.index(f)+1, f)

        if len(vals) == len(self.header):
            merged = map(_create, zip(vals, self.header))
            joined = reduce(_collapse, merged, ([], [], None))
            final = joined[0]
            if joined[1]:
                final.extend([BitOperand(' '.join(joined[1]), 0, len(joined[1]), OperandType.BITS)])
            return final
        else:
            print vals
            print self.header
            utils.Log(' -- at: %s', (self.insn.num_id))
            utils.Log(' -- component length mismatch: %d / %d', (len(vals), len(self.header)))

    def _parse_mnemonics(self, line):
        if self.getData().getMnemonicsMatcher().match(line):
            num, name, title = self.getData().getMnemonicsMatcher().getMatched()
            #utils.Log('-- new instruction: %s. %s%s', (num, name, title))
            self.last_instruction = self.insn
            self.insn = Instruction()
            self.insn.setHeader(num, name, title)
            if IsVariant(self.getArchVariant().value, Variant.Bits64):
                self.insn.addEncoding(0)
            self.stage = Stage.Bitheader
            return True

        return False

    def processPage(self, page):
        """Process all pages

        :param page: A page
        :type page: arm.common.ManualPage
        """
        
        for line in page.page_lines:
                if utils.WipeWhitespace(line) != '':
                    self.process_line(line)

        if self.last_instruction:
            if IsVariant(self.getArchVariant().value, Variant.Bits64):
                if len(self.last_instruction.encodings) > 1:
                    self.last_instruction.encodings = self.last_instruction.encodings[1:]
            retval = (True, self.last_instruction.validate())
            self.current_instruction = self.last_instruction
            self.last_instruction = None
            return retval

        return (False, False)

    def process_line(self, line):
	"""

	:param line: A manual page line
	:type line: str
	"""
        linesws = utils.WipeWhitespace(line)
	linesr = line.strip()

	if self.insn and self.insn.num_id != 0:
            if self.stage != Stage.Syntax:
                if self.getData().getEncodingMatcher().match(linesws):
                    self.stage = Stage.Bitheader
                    e = self.getData().getEncodingMatcher().getMatched()
                    self.insn.addEncoding(e[1], e[0])
                    #with utils.Indentation(1):
                    #    utils.Log(' -- %s', (str(e)))
                    return

        if self._parse_mnemonics(line):
            pass

        elif self.stage == Stage.Bitheader:
            header = self._parse_bitheader(linesr)
            if header:
                self.stage = Stage.Components
                hdr = self._unpack_header(header)
                arm32 = hdr[0] == 31 and hdr[-1] == 0
                thumb32 = len([x for x in hdr if x == 0 or x == 15]) == 4
                thumb16 = hdr[0] == 15 and hdr[-1] == 0

                self.header = header

                if arm32:
                    self.insn.setEncodingIsa(self.getArchVariant())
                elif thumb32:
                    self.insn.setEncodingIsa(self.THUMB)
                elif thumb16:
                    self.insn.setEncodingIsa(self.THUMB16)
                else:
                    utils.Log(' -- at: %s', (self.insn.num_id))
                    utils.Log(' -- unknown bit header: [%s]', (' '.join([str(x) for x in header])))
            else:
                self.insn.addSummary(filter(lambda x: x in string.printable, line.strip()))

	elif self.stage == Stage.Mnemonics:
            variant = self._parse_variant(linesr)
            if not variant:
                prefix = 'Applies when'
                if linesr.startswith(prefix):
                    self.insn.addConstraint(linesr[len(prefix):])
                elif linesr == 'is equivalent to':
                    self.stage = Stage.Equivalency
                elif linesr.startswith('Decode for'):
                    self.stage = Stage.Decode
                elif linesr.startswith('Assembler symbols'):
                    self.stage = Stage.Syntax
                else:
                    self.insn.addMnemonics(linesr)
            else:
                self.insn.addMnemonicsVariant(variant)

        elif self.stage == Stage.Equivalency:
            if linesr.startswith('and is'):
                self.insn.addMnemonicAliasConstraint(linesr)
                self.stage = Stage.Mnemonics
            else:
                self.insn.addMnemonicAlias(linesr)

        elif self.stage == Stage.Decode:
            if linesr.startswith('Notes for'):
                self.stage = Stage.Pseudocode
            elif linesr == 'Assembler symbols':
		self.stage = Stage.Syntax
            else:
                self.insn.addDecode(linesr)

        elif self.stage == Stage.Summary:
            self.insn.addSummary(filter(lambda x: x in string.printable, line.strip()))

	elif self.stage == Stage.Support:
            variant = self._parse_variant(linesr)
            if variant:
                self.stage = Stage.Mnemonics
                #utils.Log(' -- variant: %s', (variant))
                self.insn.addMnemonicsVariant(variant)

        elif self.stage == Stage.Components:
            components = self._parse_components(line)
            if components:
                #with utils.Indentation(1):
                #    utils.Log(' -- %s', (str(components)))

                self.insn.setEncodingBits(components)
                self.stage = Stage.Support
            else:
                raise Exception('Invalid components: ' + line)

        elif self.stage == Stage.Pseudocode:
            # don't care about the notes right now :/
            if linesr.startswith('Assembler symbols'):
                self.stage = Stage.Syntax

	elif self.stage == Stage.Syntax:
		if linesws == 'Operationforallencodings' or linesws == 'Operation':
			self.stage = Stage.Operation
		elif linesr.startswith('Shared decode'):
			self.stage = Stage.SharedDecode
		else:
			self.insn.metadata['Syntax'].append(line)

	elif self.stage == Stage.Operation:
		self.insn.metadata['Operation'].append(line)

	elif self.stage == Stage.SharedDecode:
		if linesws == 'Operationforallencodings' or linesws == 'Operation':
		    self.stage = Stage.Operation
		else:
		    self.insn.metadata['Decode'].append(line)

    def execute(self):
        last_num, num_proxies = 0, 0
        for page in self.page_completer.pages:
            if self.processPage(page) == (True, True):
                self.instructions.append(self.current_instruction)
                number = utils.Int(self.current_instruction.num_id)
                if not number:
                    self.instructions.pop()
                    continue
                self.num_map[number] = self.current_instruction
                distance = (number - last_num)
                if distance > 1:
                    skipped = xrange(last_num + 1, number)
                    really_missed = [n for n in skipped\
                                      if n not in self.getProxies()]
                    if len(really_missed) > 0:
                        missed = ', '.join([str(s) for s in\
                                       skipped])
                        with utils.Indentation(1):
                            utils.Log('missed (%i): %s', (distance - 1, missed),
                                       self, utils.LogCat.Debug)
                    else:
                        proxies = [(n, self.getProxies()[n]) for n in skipped]
                        if len(proxies) == (distance - 1):
                            for (num, proxy) in proxies:
                                proxy.setHeader(num, 'Proxy')
                                self.instructions.append(proxy)
                                num_proxies += 1
                last_num = number

        if self.insn and self.insn.validate():
            self.instructions.append(self.insn)
            self.num_map[utils.Int(self.insn.num_id)] = self.insn

        utils.Log('', (), self, utils.LogCat.Debug)
	with utils.Indentation(1):
	        utils.Log('%i/%i\tinstructions\t[%i, %i, %f]',
        	       (len(self.instructions), self.target_count,
        	        self.target_count - len(self.instructions), num_proxies,
        	        len(self.instructions) / float(self.target_count)),
        	       self)
