from lib import utils
from arm import common

class ARM64Data(common.DataStrings):
    InstructionMnemonics = utils.RegexMatcher('C5\.6\.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')
    
    InstructionComponents = utils.RegexMatcher('([A-Za-z0-9 \(\)_]*)')

    InstructionOperands = {
        'sf': 1, 'shift': 2, 'CRn': 4, 'CRm': 4,
        'op': 1, 'S': 1, 'op1': 3, 'op2': 3, 'L': 1,
        'size': 2, 'o1': 1, 'o2': 1, 'o0': 1, 'opc': 2,
        'imms': 6, 'immr': 6, 'option': 3, 'cond': 4,
        'b5': 1, 'b40': 5, 'U': 1, 'immlo': 2, 'immhi': 19,
	'x': 1, 'x0': 2, 'x1': 2, 'N': 1, 'nzcv': 4, 'sz': 2,
	'hw': 2
    }

    OpcodeProxies = ()

    BitComponent = utils.RegexMatcher('([\(\)01]*)')
    
    InstructionOpcodes = ['op', 'S', 'sfopc', 'N', 'L', 'LL', 'o0',
                          'o1', 'U', 'Ra', 'opc', 'op2', 'o2',
                          'C', 'size', 'Rs', 'Rt2', 'o2Lo1', 'sf', 'Rt']
    
    GeneralPurposeRegisters = utils.RegexMatcher('([R][mandts2]{1,4})')
    
    CRCMatcher = utils.RegexMatcher('CRC32+C?[BHWX]+ variant \((sf = [01]+, sz = [01]{2})\)')
    
    ImmediateMatcher = utils.RegexMatcher('imm(\d+).*')

    Pseudocode = ['Shareddecodeforallvariants', 'Operation']

    AlternateDisassembly = ['andisthepreferreddisassemblywhen',
                            'andisalwaysthepreferreddisassembly']


class ARM64Pager(common.PageCompleter):
    PageHeader = utils.SliceMatcher('C5.6Alphabeticallistofinstructions', pipe=utils.Pipes.WipeWhitespace)
    PageFooters = [utils.SliceMatcher('C5A64BaseInstructionDescriptions', pipe=utils.Pipes.WipeWhitespace)]

    def isHeader(self, line):
        r = self.PageHeader.match(line)
	return r

    def isFooter(self, line):
	r = any(h.match(line) for h in self.PageFooters)
	return r


def num_seq(upto):
    return ''.join(str(a) for a in reversed(range(upto+1)))

class Stage(object):
    (Start, Name, Summary, Bitheader, Operands, Variant, Mnemonics,
     Pseudocode, Aliases, Symbols, Operation, Support, Components, Syntax) = utils.BinaryRange(14)

class ARM64Instruction(common.Instruction):
    def __init__(self):
	super(ARM64Instruction, self).__init__()
	self.decoder = common.EncodingClassDecoder()
	self.finished = False

    def setFinished(self, val):
	self.finished = val

    def validate(self, context=None):
        if self.num_id != 0:
            encodings, failed = self.decoder.getDecodes(), False
            if len(encodings) > 1:
                ''' skip default encoding if we got something more '''
                encodings = encodings[1:]
            for (k, v) in encodings:
                failed = not v.bit_components.validate()
                if failed:
                    utils.Log('decoding (%s) failed for class (%i) with coverage (%i)',
                               (self.num_id, k, v.bit_components.getCoverage()),
                               self)
                    with utils.Indentation(1):
                        utils.Log('components: <%s>', (', '.join([str(x) for x in v.bit_components.components])))
            return not failed
        return False

ParseStage = utils.Enum(Invalid=0, Num=1, Names=2, Desc=3,
                          Components=4, Opcodes=5, Headers=6, Alias=7,
                          AltMnemonics=8, Asm=9, Code=10,
                          AltDisasm=11, Mnemonics=12)

class ARM64Processor(common.Engine):
    def __init__(self):
        self.stage = Stage.Start
	self.insn = None
	self.page_completer = ARM64Pager()
	self.target_count = 224
	self.instructions = []
	self.num_map = {}

    def getArchVariant(self):
        return self.ARM64

    def getProxies(self):
	return ARM64Data.OpcodeProxies
  
    def scanBitHeader(self, parts, linesws):
        if linesws.startswith('31302928'):
            return self.ARM64
	return self.INVALID

    def decodeClass(self, line):
        return self.insn.decoder.decodeLine(line)

    def decodeVariant(self, line):
        return self.getCurrentClass().variant_components.decodeLine(line)

    def decode(self, line):
        decoded = self.decodeClass(line)
        if decoded:
            return decoded
        else:
            return self.decodeVariant(line)

    def getCurrentClass(self):
        decoder = self.insn.decoder
        return decoder.getLastDecode()

    def getCurrentVariant(self):
        decoder = self.getCurrentClass().variant_components
        return decoder.getLastDecode()

    def flushBitGroup(self, bits):
        if len(bits) > 0:
            self.getCurrentClass().createComponent(' '.join(bits), len(bits))
            bits[:] = []

    def getComponentList(self, line):
        components, bits, bl = [], [], 0
        for part in line.split(' '):
	    if part.strip() == '':
		continue

            if part in ARM64Data.InstructionOperands:
                self.flushBitGroup(bits)
                self.getCurrentClass().createComponent(part,
                                        ARM64Data.InstructionOperands[part])

            elif ARM64Data.GeneralPurposeRegisters.match(part):
                self.flushBitGroup(bits)
                size, last = 5, self.getCurrentClass().getLastComponent()
                if part == 'Rm' and last.name == 'M':
                    size = 4
                self.getCurrentClass().createComponent(part, size)

            elif ARM64Data.ImmediateMatcher.match(part):
                self.flushBitGroup(bits)
                size = int(ARM64Data.ImmediateMatcher.getMatched()[0])
                self.getCurrentClass().createComponent(part, size)

            else:
                if ARM64Data.BitComponent.match(part):
                    utils.Merge(bits, self.getRawBits(part))
                else:
                    utils.Log('unknown simd operand -- `%s`', (part), self)
                    self.flushBitGroup(bits)
		    
        self.flushBitGroup(bits)
        return components

    def parseOffsetSection(self, line):
        if self.decodeClass(line):
            return ParseStage.Desc
        return None

    def parseSecondStage(self, line, fromStep):
        if self.decodeClass(line):
            ''' Sections as defined in :NA64PageParser.NSections:  '''
            return ParseStage.Desc

        if self.decodeVariant(line):
            ''' Headers as defined in :NA64PageParser.NHeaders:  '''
            return ParseStage.Headers

        wiped = utils.WipeWhitespace(line)
        if wiped in ARM64Data.Pseudocode:
            ''' :NA64PageParser.NPseudocode: markers '''
            #utils.Log('arm64-mnem -- pseudocode: `%s`', (line), self)
            return ParseStage.Code

        elif wiped == 'AssemblerSymbols':
            ''' Further lines contain ARM Assembly '''
            #utils.Log('arm64-mnem -- asm symbols: `%s`', (line), self)
            self.insn.setFinished(True)
            return ParseStage.Asm

        elif wiped == 'Aliasconditions':
            ''' Alias conditions for the current :AArch64.Instruction: '''
            #utils.Log('arm64-mnem -- alias condition: `%s`', (line), self)
            return ParseStage.Alias

        if line[:2] == self.insn.getName()[:2]:
            ''' Line contains ASM variant of the instruction '''
            #utils.Log('arm64-mnem -- assembly: `%s`', (line), self)
            self.getCurrentVariant().createMnemonicVariant(line)
            return ParseStage.Mnemonics

        elif wiped == 'isequivalentto':
            ''' Maps relationships between ASM mnemonics '''
            #utils.Log('arm64-mnem -- alt-mnemonics: `%s`', (line), self)
            return ParseStage.AltMnemonics
        elif wiped in ARM64Data.AlternateDisassembly:
            #utils.Log('simd64-mnem -- preferred disassembly: `%s`', (line), self)
            return ParseStage.AltDisasms

        else:
            #utils.Log('arm64-mnem -- appending description: `%s`', (line), self)
            self.getCurrentClass().addOperands(line)
            return ParseStage.Headers

        return None

    def processPage(self, page):
        step = ParseStage.Num
        bitpattern = []

        self.insn = ARM64Instruction()
	self.current_instruction = self.insn

        for line in page.page_lines:
            pl, wiped = line.strip(), utils.WipeWhitespace(line)
            if step == ParseStage.Num:
                if ARM64Data.InstructionMnemonics.match(line):
                    a, b, c = ARM64Data.InstructionMnemonics.getMatched()
                    self.insn.setHeader(a, b, c)
                    step = ParseStage.Desc

            elif step == ParseStage.Names:
                if pl != '':
                    self.insn.addNames(pl)
                    step = ParseStage.Desc

            elif step == ParseStage.Desc:
                if pl != '':
                    if self.decodeClass(line):
                        step = ParseStage.Desc
		    elif self.scanBitHeader(pl.split(' '), wiped) != self.INVALID: 
		        step = ParseStage.Components
                    else:
                        self.insn.addSummary(pl)

            elif step == ParseStage.Components:
                if pl != '' and ARM64Data.InstructionComponents.match(pl):
                    self.getComponentList(pl)
                    bitpattern = []
                    step = ParseStage.Opcodes

            elif step == ParseStage.Opcodes:
                if pl != '':
                    ops = [x for x in pl.split(' ') if x in ARM64Data.InstructionOpcodes]
                    #if self.validateOpcodeList(ops, len(wiped)):
                    #    pass
                    #else:
                    step = self.parseSecondStage(pl, step)
                    if step == ParseStage.Asm:
                       self.insn.setFinished(True)
		       return (True, self.insn.validate())

            elif step == ParseStage.Alias:
                if pl != '':
                    if pl[:5] == 'Alias':
                        step = ParseStage.Desc
                        #utils.Log('arm64-mnem -- alias: (%s)', (pl), self)
                    #else:
                        #utils.Log('arm64-mnem -- invalid alias: (%s)', (pl), self)

            elif step >= ParseStage.Headers:
                if pl != '':
                    second = self.parseSecondStage(pl, step)
                    if second == None:
                        utils.Log('arm64-mnem -- residual line (%s)', (pl), self)
                    else:
                        step = second
                        if step == ParseStage.Asm:
                            self.insn.setFinished(True)
                            ''' do not process anything beyond
                                the `Assembler Symbols` header '''
                            return (True, self.insn.validate())

        return (True, self.insn.validate())
