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

ManualBadLines = ['If<Rd>isthePC,mustbeoutsideorlastinITblock.']

PageType = utils.Enum(Invalid=0, AArch32=1, SIMD32=2, AArch64=4, SIMD64=8,
                        Complementary=16, GPRTable=32)

ProxyTransform = utils.Enum(Invalid=0, ImmediateBitwiseInv=1,
                              OperandsReversed=2, InstructionLink=3)

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


class EncodingClass(object):
    LastID, TypeToId = 0, {}

    def __init__(self, name, patterns=None):
        self.id = EncodingClass.LastID
        self.variant_type = Variant.Invalid
        self.name = name
        self.patterns = patterns or []
        self.cached_match = None
        EncodingClass.LastID += 1

    def getName(self):
        return self.name

    def getId(self):
        return self.id

    def getVariantType(self):
        return self.variant_type

    def mapToVariant(self, variant_type):
        if variant_type not in EncodingClass.TypeToId:
            EncodingClass.TypeToId[variant_type] = self.getId()
            return True
        return False

    def matchString(self, line):
        self.cached_match = [x for x in self.patterns if x.match(line)]
        return self.cached_match

    def extractArguments(self):
        if self.cached_match:
            arguments = []
            for m in self.cached_match:
                utils.Merge(arguments, m.getMatched())
            return arguments
        return None


class ClassList(object):
    def __init__(self):
        self.items = []

    def append(self, classes):
        for (k, c) in classes.items():
            if k != 'default':
		""" Skip the default class """
                utils.Merge(self.items, c)
        return self.items

    def getItems(self):
        return self.items

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
    def __init__(self, isa, num):
        self.isa = isa
        self.num = num
        self.mnemonics = []
        self.support = []
        self.detail = []
        self.bits = []
        self.decode = []

    def getName(self):
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
    EMPTY=None
    def __init__(self, value=''):
        self.string = value

    def solve(self, context=None):
        return context
Constraint.EMPTY = Constraint()

class MnemonicsVariant(object):
    def __init__(self, name='', constraint=None, mnemonics=None):
        self.name = name
        self.constraint = constraint or Constraint.EMPTY
        self.mnemonics = mnemonics or []

    def addConstraint(self, fromstr):
        if self.constraint != Constraint.EMPTY:
            utils.Log(' -- multiple constraint for %s (old: %s) (new: %s)', (self.name, str(self.constraint), fromstr))
        self.constraint = Constraint(fromstr)

    def addMnemonics(self, mnemonics):
        self.mnemonics.append(mnemonics)

class LengthVariant(object):
        def __init__(self, nid, nisa, variants):
                self.nid = nid
                self.isa = nisa
                self.variants = variants
        
        def match(self, nid, enc):
            return self.nid == nid and enc.getName() in self.isa

        def getVariants(self, nid, encoding):
            if not self.match(nid, encoding):
                return None
            return self.variants

class FieldVariant(utils.Test):
    def __init__(self, ttype, fields=None):
        self.type = ttype
        self.fields = fields or {}

class Instruction(utils.Test):
    def __init__(self):
        self.num_id = 0
        self.names = []
        self.summary = []
        self.metadata = {"Syntax": [], "Operation": []}

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

class ProxyTarget(object):
    def __init__(self, engine, num):
        self.target_arch = engine
        self.instruction_num = num

    def getTarget(self, factory):
        return factory.fromVariant(self.target_arch).num_map[self.instruction_num]

class InstructionProxy(Instruction):
    def __init__(self, ttype, target):
        super(InstructionProxy, self).__init__()
        self.type = ttype
        self.target = target

    def proxy(self):
        utils.Log('proxying (%i)', (self.target.instruction_num), self)
        pass

class BitComponent(object):
    def __init__(self, name, size):
        self.name = name
        self.size = size

    def __repr__(self):
        return '"%s": %i' % (self.name, self.size)

class BitComponentList(utils.Test):
    def __init__(self, size=32):
        self.size = size
        self.components = []

    def addComponent(self, bit_component):
        self.components.append(bit_component)

    def createComponent(self, name, size):
        self.addComponent(BitComponent(name, size))

    def getCoverage(self):
        return reduce(utils.Add, [c.size for c in self.components], 0)

    def validate(self, context=None):
        return self.size == self.getCoverage()


class VariantDecoder(object):
    def __init__(self):
        self.encoding_map = {0: self.createDecode()}
        self.last_decode = self.encoding_map[0]

    def getLastDecode(self):
        return self.last_decode

    def createDecode(self, args=None):
        return None

    def getClassList(self):
        return []

    def getDecodes(self):
        return self.encoding_map.items()

    def decodeLine(self, line):
        classes = [cls for cls in self.getClassList() if cls.matchString(line)]
        if utils.Unary(classes):
            encoding_class = classes[0]
            arguments = encoding_class.extractArguments()
            if self.addVariant(encoding_class, self.createDecode(arguments)):
                return encoding_class
        return None

    def addVariant(self, encoding_class, vobj):
        uid = encoding_class.getId()
        if uid not in self.encoding_map:
            self.encoding_map[uid] = vobj
            self.last_decode = vobj
            return True
        return False

    def getDefault(self):
        return self.getVariantById(0)

    def getVariantById(self, variant_id):
        return None if variant_id not in self.encoding_map\
            else self.encoding_map[variant_id]

    def getVariantByType(self, variant_type):
        variant_id = None if variant_type not in EncodingClass.TypeToId \
			else EncodingClass.TypeToId[variant_type]
        return self.getVariantById(variant_id)


class EncodingPropertyDecoder(VariantDecoder):
    class _Container(object):
        def __init__(self, condition=None):
            self.mnemonics = []
            self.condition = condition

        def createMnemonicVariant(self, mnem):
            self.mnemonics.append(mnem)

    def createDecode(self, args=None):
        return EncodingPropertyDecoder._Container(args)

    def getClassList(self):
        return VariantEncodings.getItems()


class EncodingClassDecoder(VariantDecoder):
    class _Container(object):
        def __init__(self):
            self.bit_components = BitComponentList()
            self.variant_components = EncodingPropertyDecoder()
            self.operands = []
            self.metadata = {}

        def getLastComponent(self):
            return self.bit_components.components[-1]

        def addOperands(self, op):
            self.operands.append(op)

        def createComponent(self, name, size):
            self.bit_components.createComponent(name, size)

    def createDecode(self, args=None):
        return EncodingClassDecoder._Container()

    def getClassList(self):
        return ClassEncodings.getItems()


DecoderClasses = dict(default=None, addressing=None, simd=None)
DecoderProperties = dict(default=None)
ClassEncodings = ClassList()
VariantEncodings = ClassList()

def DefaultClassDecoder():
    return DecoderClasses.get('default')[0]

def DecoderRegexp(class_name):
    return '^' + class_name + '\s?(?:\((.*)\))?'

def DecoderCreate(class_name, class_type, class_patterns=None):
    encoding = EncodingClass(class_name)

    if IsVariant(class_type, Variant.BitsInvolved | Variant.HasOffset):
        encoding.patterns.append(utils.RegexMatcher(DecoderRegexp(class_name), utils.Pipes.TrimWhitespace))
    elif not class_patterns:
        encoding.patterns.append(utils.PipedMatcher(class_name, utils.Pipes.TrimWhitespace))
    else:
        encoding.patterns = class_patterns

    encoding.mapToVariant(class_type)
    return encoding

def InitializeDecoder():
    DecoderClasses['default'] = [DecoderCreate('Default',  Variant.Invalid)]

    DecoderClasses['addressing'] = [DecoderCreate('No offset',  Variant.Offset |  Variant.ZeroOffset),
                  DecoderCreate('Unsigned offset',  Variant.Offset |  Variant.Unsigned),
                  DecoderCreate('Signed offset',  Variant.Offset |  Variant.Signed),
                  DecoderCreate('Pre-index',  Variant.PreIndex),
                  DecoderCreate('Post-index',  Variant.PostIndex)]

    DecoderClasses['simd'] = [DecoderCreate('Scalar',  Variant.Scalar),
                 DecoderCreate('Vector',  Variant.Vector)]

    DecoderProperties['default'] = [DecoderCreate('Default',  Variant.Invalid)]

    DecoderProperties['bit_variant'] = [DecoderCreate('8-bit variant',  Variant.Bits8),
                                  DecoderCreate('16-bit variant', Variant.Bits16),
                                  DecoderCreate('32-bit variant', Variant.Bits32),
                                  DecoderCreate('64-bit variant', Variant.Bits64),
                                  DecoderCreate('128-bit variant', Variant.Bits128)]

    DecoderProperties['offset_variant'] = [DecoderCreate('No offset variant', Variant.ZeroOffset),
                                  DecoderCreate('Post-index variant', Variant.PostIndex),
                                  DecoderCreate('Pre-index variant', Variant.PreIndex),
                                  DecoderCreate('Signed offset variant', Variant.Offset | Variant.Signed),
                                  DecoderCreate('Unsigned offset variant', Variant.Offset | Variant.Unsigned),
                                  DecoderCreate('Integer variant', Variant.Signed)]

    DecoderProperties['crc32_variant'] = [DecoderCreate('CRC32B variant', Variant.Bits8 | Variant.CRC32),
                                  DecoderCreate('CRC32H variant', Variant.Bits16 | Variant.CRC32),
                                  DecoderCreate('CRC32W variant', Variant.Bits32 | Variant.CRC32),
                                  DecoderCreate('CRC32X variant', Variant.Bits64 | Variant.CRC32),
                                  DecoderCreate('CRC32CB variant', Variant.Bits8 | Variant.CRC32),
                                  DecoderCreate('CRC32CH variant', Variant.Bits16 | Variant.CRC32),
                                  DecoderCreate('CRC32CW variant', Variant.Bits32 | Variant.CRC32),
                                  DecoderCreate('CRC32CX variant', Variant.Bits64 | Variant.CRC32)]

    DecoderProperties['relative_variant'] = [DecoderCreate('26-bit signed PC-relative branch offset variant',
                                          Variant.Offset | Variant.Relative | Variant.Signed),
                                  DecoderCreate('19-bit signed PC-relative branch offset variant',
                                          Variant.Offset | Variant.Relative | Variant.Signed),
                                  DecoderCreate('14-bit signed PC-relative branch offset variant',
                                          Variant.Offset | Variant.Relative | Variant.Signed)]

    DecoderProperties['common_variant'] = [DecoderCreate('System variant', Variant.System),
                                            DecoderCreate('Literal variant', Variant.Literal)]

    DecoderProperties['commond_simd'] = [DecoderCreate('Scalar variant', Variant.Scalar | Variant.Simd),
                                  DecoderCreate('Vector variant', Variant.Vector | Variant.Simd),
                                  DecoderCreate('Advanced SIMD variant', Variant.Simd),
                                  DecoderCreate('Single-precision variant', Variant.Single | Variant.Bits32),
                                  DecoderCreate('Single-precision, zero variant', Variant.Single | Variant.ZeroImmediate | Variant.Bits32),
                                  DecoderCreate('Double-precision variant', Variant.Double | Variant.Bits64),
                                  DecoderCreate('Double-precision, zero variant', Variant.Double | Variant.ZeroImmediate | Variant.Bits64)]
    DecoderProperties['three_reg_simd'] = [DecoderCreate('Three registers of the same type variant',
                                          Variant.SameRegisters | Variant.Simd | Variant.NumRegs3),
                                  DecoderCreate('Three registers, not all the same type variant',
                                          Variant.DiffRegisters | Variant.Simd | Variant.NumRegs3)]

    DecoderProperties['imm_offset_simd'] = [DecoderCreate('8-bit, immediate offset variant',
                                          Variant.Bits8 | Variant.ImmediateOffset | Variant.Simd),
                                  DecoderCreate('16-bit, immediate offset variant',
                                          Variant.Bits16 | Variant.ImmediateOffset | Variant.Simd),
                                  DecoderCreate('32-bit, immediate offset variant',
                                          Variant.Bits32 | Variant.ImmediateOffset | Variant.Simd),
                                  DecoderCreate('64-bit, immediate offset variant',
                                          Variant.Bits64 | Variant.ImmediateOffset | Variant.Simd),
                                  DecoderCreate('Immediate offset variant', Variant.ImmediateOffset | Variant.Simd)]

    DecoderProperties['reg_offset_simd'] = [DecoderCreate('8-bit, register offset variant',
                                          Variant.Bits8 | Variant.RegisterOffset | Variant.Simd),
                                  DecoderCreate('16-bit, register offset variant',
                                          Variant.Bits16 | Variant.RegisterOffset | Variant.Simd),
                                  DecoderCreate('32-bit, register offset variant',
                                          Variant.Bits32 | Variant.RegisterOffset | Variant.Simd),
                                  DecoderCreate('64-bit, register offset variant',
                                          Variant.Bits64 | Variant.RegisterOffset | Variant.Simd),
                                  DecoderCreate('Register offset variant', Variant.RegisterOffset | Variant.Simd)]

    DecoderProperties['convert_simd'] = [DecoderCreate('32-bit to single-precision variant',
                                          Variant.BitsToFloating | Variant.Bits32 | Variant.Single),
                                  DecoderCreate('Single-precision to 32-bit variant', Variant.FloatingToBits | Variant.Bits32 | Variant.Single),
                                  DecoderCreate('Single-precision to 64-bit variant', Variant.FloatingToBits | Variant.Bits64 | Variant.Single),
                                  DecoderCreate('Double-precision to 32-bit variant', Variant.FloatingToBits | Variant.Bits32 | Variant.Double),
                                  DecoderCreate('Double-precision to 64-bit variant', Variant.FloatingToBits | Variant.Bits64 | Variant.Double)]

    ClassEncodings.append(DecoderClasses)
    VariantEncodings.append(DecoderProperties)

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


EngineModule = utils.Enum(Invalid=0, PageCompleter=1, PageSet=2,
                            InstructionVariants=4)

class DataStrings(object):
    def getStrings(self):
        return []

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


class Engine(object):
    INVALID=Variant(Variant.Archetype | Variant.Invalid)
    ARM32=Variant(Variant.AArch32_ARM)
    ARM32System=Variant(Variant.AArch32_SYS)
    ARM32SIMD=Variant(Variant.AArch32_SIMD)
    THUMB=Variant(Variant.AArch32_THUMB)
    THUMB16=Variant(Variant.AArch32_THUMB16)
    ARM64=Variant(Variant.AArch64_ARM)
    ARM64SIMD=Variant(Variant.AArch64_SIMD)

    def getArchVariant(self):
        return self.INVALID

    def getProxies(self):
        return {}

    def flushBitGroup(self, bits, comps):
        if len(bits) > 0:
            comps.append(BitOperand(' '.join(bits), 0, len(bits)))
            bits[:] = []

    def getRawBits(self, part):
        bits = []
        for p in part:
            if p in ['(', ')']:
                continue
            if p in ['1', '0', 'x']:
                bits.append(p)
            else:
                return None
        return bits

    def processPage(self, page):
        return False

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

        if self.insn.validate():
            self.instructions.append(self.insn)
            self.num_map[utils.Int(self.insn.num_id)] = self.insn

        utils.Log('', (), self, utils.LogCat.Debug)
	with utils.Indentation(1):
	        utils.Log('%i/%i\tinstructions\t[%i, %i, %f]',
        	       (len(self.instructions), self.target_count,
        	        self.target_count - len(self.instructions), num_proxies,
        	        len(self.instructions) / float(self.target_count)),
        	       self)

    def __init__(self):
        self.page_completer = None
        self.variant_lengths = None
        self.instructions = None
        self.current_instruction = None
        self.num_map = {}
        self.target_count = 0
