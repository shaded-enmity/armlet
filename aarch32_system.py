from lib import utils
from arm import common
from aarch32 import ARM32Processor, Stage, ARM32Instruction, thumb32_header_seq, thumb16_header_seq, arm_header_seq

class ARM32SystemData(common.DataStrings):
    InstructionMnemonics = utils.RegexMatcher('F7.4.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')
    
    InstructionEncoding = utils.RegexMatcher('^Encoding ([AT])(\d+)\s*(.*)')
    
    InstructionDualEncoding = utils.RegexMatcher('^Encoding ([AT]+\d+/[AT]+\d+)\s*(.*)')
    
    InstructionComponents = utils.RegexMatcher('([A-Za-z0-9 \(\)_]*)')

    Variants = [common.LengthVariant('5', ['A1'], dict(register_list=15)),
                common.LengthVariant('6', ['A1'], dict(register_list=15)),
		common.LengthVariant('17', ['A1'], dict(register_list=16))]

    OpcodeProxies = {8: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_ARM,
                                                        [77, 89, 93, 98])),
                     19: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        [222, 232, 233])) }

class ARM32SystemPager(common.PageCompleter):
    PageHeader = utils.SliceMatcher('F7.4Alphabeticallistofsysteminstructions', pipe=utils.Pipes.WipeWhitespace)
    PageFooters = [utils.SliceMatcher('F7T32andA32BaseInstructionSetInstructionDescriptions', pipe=utils.Pipes.WipeWhitespace)]

    def isHeader(self, line):
        r = self.PageHeader.match(line)
	return r

    def isFooter(self, line):
	r = any(h.match(line) for h in self.PageFooters)
	return r

class ARM32SystemProcessor(ARM32Processor):
    ARCH_MAP={thumb16_header_seq: common.Engine.THUMB16, thumb32_header_seq: common.Engine.THUMB, 
            arm_header_seq: common.Engine.ARM32}
    def __init__(self):
        self.stage = Stage.Start
	self.insn = None
	self.page_completer = ARM32SystemPager()
	self.target_count = 22
	self.instructions = []
	self.num_map = {}

    def getArchVariant(self):
        return self.ARM32System

    def getProxies(self):
	return ARM32SystemData.OpcodeProxies

    def _parse_mnemonics(self, line):
	    if ARM32SystemData.InstructionMnemonics.match(line):
		    num, name, title = ARM32SystemData.InstructionMnemonics.getMatched()

		    #if not num in ARM32SystemData.BadOpcodes:
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
	    new = page.page_lines[3].startswith('F7.4.')
	    
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
        for v in ARM32SystemData.Variants:
            vs = v.getVariants(num, encoding)
            if vs and reg in vs:
                return vs[reg]
        return None


    def process_line(self, line):
	"""

	:param line: A manual page line
	:type line: str
	"""
        linesws = utils.WipeWhitespace(line)
	linesr = line.strip()

	if self.insn.num_id != 0:
		if self.stage != Stage.Syntax:
			if ARM32SystemData.InstructionDualEncoding.match(line):
				self.stage = Stage.Support
				e = ARM32SystemData.InstructionDualEncoding.getMatched()
				self.insn.addEncoding(e[0][1], e[1])
				return

			elif ARM32SystemData.InstructionEncoding.match(line):
				self.stage = Stage.Support
				e = ARM32SystemData.InstructionEncoding.getMatched()
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
		if linesr == 'DM':
			self.insn.getEncoding().bits.insert(1, common.BitOperand('DM', 0, 1))

		if linesr == 'DN':
			self.insn.getEncoding().bits.insert(1, common.BitOperand('DN', 0, 2))

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
