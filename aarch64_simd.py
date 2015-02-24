from lib import utils
from arm import common
from aarch64 import ARM64Processor, ARM64Instruction, ParseStage

class ARM64SIMDData(common.DataStrings):
    InstructionMnemonics = utils.RegexMatcher('C6\.3\.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')
    
    InstructionComponents = utils.RegexMatcher('([A-Za-z0-9 \(\)_]*)')

    InstructionOperands = {'sz': 1, 'L': 1, 'M': 1, 'H': 1, 'S': 1,
                           'Q': 1, 'sf': 1, 'cond': 4, 'nzcv': 4,
                           'size': 2, 'immh': 4, 'immb': 3, 'cmode': 4,
                           'scale': 6, 'opc': 2, 'option': 3, 'len': 2,
                           'a': 1, 'b': 1, 'c': 1, 'd': 1, 'e': 1,
                           'f': 1, 'g': 1, 'h': 1, 'type': 2, 'op': 1}

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


class ARM64SIMDPager(common.PageCompleter):
    PageHeader = utils.SliceMatcher('C6.3Alphabeticallistoffloating-pointandAdvancedSIMDinstructions', pipe=utils.Pipes.WipeWhitespace)
    PageFooters = [utils.SliceMatcher('C6A64SIMDandFloating-pointInstructionDescriptions', pipe=utils.Pipes.WipeWhitespace)]

    def isHeader(self, line):
        r = self.PageHeader.match(line)
	return r

    def isFooter(self, line):
	r = any(h.match(line) for h in self.PageFooters)
	return r

class ARM64SIMDProcessor(ARM64Processor):
    def __init__(self):
	self.stage = ParseStage.Num
	self.insn = None
	self.page_completer = ARM64SIMDPager()
	self.target_count = 350
	self.instructions = []
	self.num_map = {}

    def getArchVariant(self):
        return self.ARM64SIMD

    def getProxies(self):
	return ARM64SIMDData.OpcodeProxies
  
    def scanBitHeader(self, parts, linesws):
        if linesws.startswith('31302928'):
            return self.ARM64SIMD
	return self.INVALID

    def getComponentList(self, line):
        components, bits, bl = [], [], 0
        for part in line.split(' '):
	    if part.strip() == '':
		continue

            if part in ARM64SIMDData.InstructionOperands:
                self.flushBitGroup(bits)
                self.getCurrentClass().createComponent(part,
                                        ARM64SIMDData.InstructionOperands[part])

            elif ARM64SIMDData.GeneralPurposeRegisters.match(part):
                self.flushBitGroup(bits)
                size, last = 5, self.getCurrentClass().getLastComponent()
                if part == 'Rm' and last.name == 'M':
                    size = 4
                self.getCurrentClass().createComponent(part, size)

            elif ARM64SIMDData.ImmediateMatcher.match(part):
                self.flushBitGroup(bits)
                size = int(ARM64SIMDData.ImmediateMatcher.getMatched()[0])
                self.getCurrentClass().createComponent(part, size)

            else:
                if ARM64SIMDData.BitComponent.match(part):
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
        if wiped in ARM64SIMDData.Pseudocode:
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
        elif wiped in ARM64SIMDData.AlternateDisassembly:
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
                if ARM64SIMDData.InstructionMnemonics.match(line):
                    a, b, c = ARM64SIMDData.InstructionMnemonics.getMatched()
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
                if pl != '' and ARM64SIMDData.InstructionComponents.match(pl):
                    self.getComponentList(pl)
                    bitpattern = []
                    step = ParseStage.Opcodes

            elif step == ParseStage.Opcodes:
                if pl != '':
                    ops = [x for x in pl.split(' ') if x in ARM64SIMDData.InstructionOpcodes]
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
