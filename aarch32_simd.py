from lib import utils
from arm import common
from aarch32 import ARM32Processor, Stage, ARM32Instruction, thumb32_header_seq, thumb16_header_seq, arm_header_seq

class ARM32SIMDData(common.DataStrings):
    InstructionMnemonics = utils.RegexMatcher('F8.1.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')
    
    InstructionEncoding = utils.RegexMatcher('^Encoding ([AT])(\d+)\s*(.*)')
    
    InstructionDualEncoding = utils.RegexMatcher('^Encoding ([AT]+\d+/[AT]+\d+)\s*(.*)')
    
    InstructionComponents = utils.RegexMatcher('([A-Za-z0-9 \(\)_]*)')

    GeneralPurposeRegisters = utils.RegexMatcher('([VR][tndm]{1,4})')

    Variants = [
      common.LengthVariant('29', ['T1', 'A1'], dict(op=2)),
      common.LengthVariant('44', ['T1', 'A1'], dict(op=2)),
      common.LengthVariant('45', ['T1', 'A1'], dict(opc2=3)),
      common.LengthVariant('83', ['T1', 'A1'], dict(opc2=2)),
      common.LengthVariant('84', ['T1', 'A1'], dict(opc2=2)),
      common.LengthVariant('116', ['T1', 'A1'], dict(op=2)),
      common.LengthVariant('128', ['T1', 'A1'], dict(op=2)),
      common.LengthVariant('130', ['T1', 'A1'], dict(op=3))
    ]

    OpcodeProxies = {26: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                           common.ProxyTarget(common.Variant.AArch32_SIMD,
                                                            27)),
                     37: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                           common.ProxyTarget(common.Variant.AArch32_SIMD,
                                                            32)),
                     40: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                           common.ProxyTarget(common.Variant.AArch32_SIMD,
                                                            34)),
                     100: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                           common.ProxyTarget(common.Variant.AArch32_SIMD,
                                                            101))}
 
    InstructionOperands = {
      'size': 2, 'D': 1, 'M': 1, 'Q': 1, 'N': 1,
      'sz': 1, 'E': 1, 'op': 1, 'cond': 4, 'sf': 1,
      'sx': 1, 'RM': 2, 'T': 1, 'Rt': 4, 'B': 1,
      'align': 2, 'type': 4, 'index_align': 4, 'cc': 2,
      'cmode': 4, 'F': 1, 'opc2': 4, 'U': 1, 'i': 1,
      '(0)': 1, 'a': 1, 'P': 1, 'W': 1, 'L': 1, 'len': 2,
      'opc1': 2
    }

class ARM32SIMDPager(common.PageCompleter):
    PageHeader = utils.SliceMatcher('F8.1Alphabeticallistoffloating-pointandAdvancedSIMDinstructions', pipe=utils.Pipes.WipeWhitespace)
    PageFooters = [utils.SliceMatcher('F8T32andA32AdvancedSIMDandfloating-pointInstructionDescriptions', pipe=utils.Pipes.WipeWhitespace)]

    def isHeader(self, line):
        r = self.PageHeader.match(line)
	return r

    def isFooter(self, line):
	r = any(h.match(line) for h in self.PageFooters)
	return r

class ARM32SIMDProcessor(ARM32Processor):
    ARCH_MAP={thumb16_header_seq: common.Engine.THUMB16, thumb32_header_seq: common.Engine.THUMB, 
            arm_header_seq: common.Engine.ARM32}
    def __init__(self):
        self.stage = Stage.Start
	self.insn = None
	self.page_completer = ARM32SIMDPager()
	self.target_count = 172
	self.instructions = []
	self.num_map = {}

    def getArchVariant(self):
        return self.ARM32SIMD

    def getProxies(self):
	return ARM32SIMDData.OpcodeProxies

    def _parse_mnemonics(self, line):
	    if ARM32SIMDData.InstructionMnemonics.match(line):
		    num, name, title = ARM32SIMDData.InstructionMnemonics.getMatched()
		    self.insn.setHeader(num, name, title)
		    self.stage = Stage.Summary
		    return True

	    return False
    
    def _parse_support(self, line):
	    """ :type line: str """
	    if line.startswith('ARMv'):
		    self.insn.addEncodingSupport(line)
		    return True
	    else:
		    return False

    def _parse_encoding_mnemonics(self, line):
	    """ :type line: str """
	    #if line[:2] == self.insn.getName()[:2]:
	    self.insn.addEncodingMnemonics(line)
	    return True
	    #	    return True
	    #else:
	    #	    return False

    def processPage(self, page):
	    """Process all pages

	    :param page: A page
	    :type page: arm.common.ManualPage
	    """
	    new = page.page_lines[3].startswith('F8.1.')
	    
	    if new:
	    	self.insn = ARM32Instruction()
	    else:
		if not self.insn:
	    		self.insn = ARM32Instruction()
		        self.stage = Stage.Start
			new = True
		else:
			self.stage = Stage.Operands

	    for line in page.page_lines:
		    if utils.WipeWhitespace(line) != '':
		    	self.process_line(line)

	    self.current_instruction = self.insn
            self.stage = Stage.Start

	    return (new, self.insn.validate())

    def variantLength(self, num, encoding, reg):
        for v in ARM32SIMDData.Variants:
            vs = v.getVariants(num, encoding)
            if vs and reg in vs:
                return vs[reg]
        return None
 
    def getComponentList(self, comps):
        bitgroup, components, insn = [], [], self.insn
        for c in comps.split(' '):
	    if c.strip() == '':
		continue
            bits = self.getRawBits(c)
            if bits:
                for b in bits:
                    bitgroup.append(b)
            else:
                self.flushBitGroup(bitgroup, components)
                fixup, rsize = self.variantLength(insn.num_id,
                                             insn.getEncoding(), c), 0
                if fixup:
                    rsize = fixup
                elif len(c) > 3 and c[0:3] == 'imm':
                    rsize = int(utils.Pipes.WipeAlphabet.execute(c[3:]))
                elif c in ARM32SIMDData.InstructionOperands:
                    rsize = ARM32SIMDData.InstructionOperands[c]
                elif ARM32SIMDData.GeneralPurposeRegisters.match(c):
                    rsize = 4
                elif c == 'U1':
                    rsize, c = 1, c[0]
                    bitgroup.append('1')
                else:
                    utils.Log('unknown operand (%s) - %i chars [%s]',
                               (c, len(c), comps), self)

                components.append(common.BitOperand(c, 0, rsize))
        self.flushBitGroup(bitgroup, components)
        return components

    def process_line(self, line):
	"""

	:param line: A manual page line
	:type line: str
	"""
        linesws = utils.WipeWhitespace(line)
	linesr = line.strip()

	if self.insn.num_id != 0:
		if self.stage != Stage.Syntax:
			if ARM32SIMDData.InstructionDualEncoding.match(line):
				self.stage = Stage.Support
				e = ARM32SIMDData.InstructionDualEncoding.getMatched()
				self.insn.addEncoding(e[0][1], e[1])
				return

			elif ARM32SIMDData.InstructionEncoding.match(line):
				self.stage = Stage.Support
				e = ARM32SIMDData.InstructionEncoding.getMatched()
				self.insn.addEncoding(e[1], e[2])
				return

        if self.stage == Stage.Start:
		if self._parse_mnemonics(line):
			pass

	elif self.stage == Stage.Symbols:
		header = self.scanBitHeader(linesr.split(' '), linesws)
		if header != self.INVALID:
			self.insn.setEncodingIsa(header)
			self.stage = Stage.Components
		else:
			self.insn.addEncodingMnemonics(linesr)

        elif self.stage == Stage.Summary:
		self.insn.addSummary(line)

	elif self.stage == Stage.Support:
		if linesr.startswith('ARMv'):
			self.insn.addEncodingSupport(linesr)
			self.stage = Stage.Symbols
		else:
			self.insn.addEncodingMnemonics(linesr)
			self.stage = Stage.Symbols

	elif self.stage == Stage.Operands:
		if '=' in linesr or '|' in linesr:
			self.insn.getEncoding().detail.append(linesr)
		else:
			if linesws == 'Assemblersyntax':
				self.stage = Stage.Syntax
			else: 
				header = self.scanBitHeader(linesr.split(' '), linesws)
				if header != self.INVALID:
					self.insn.setEncodingIsa(header)
					self.stage = Stage.Components

        elif self.stage == Stage.Components:
		self.insn.setEncodingBits(self.getComponentList(linesr))
		self.stage = Stage.Operands

	elif self.stage == Stage.Syntax:
		if linesws == 'Operation':
			self.stage = Stage.Operation
		else:
			self.insn.metadata['Syntax'].append(linesr)

	elif self.stage == Stage.Operation:
		self.insn.metadata['Operation'].append(linesr)
