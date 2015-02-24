from lib import utils
from arm import common

class ARM32Data(common.DataStrings):
    InstructionMnemonics = utils.RegexMatcher('F7.1.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')
    
    InstructionEncoding = utils.RegexMatcher('^Encoding ([AT])(\d+)\s*(.*)')
    
    InstructionDualEncoding = utils.RegexMatcher('^Encoding ([AT]+\d+/[AT]+\d+)\s*(.*)')
    
    InstructionComponents = utils.RegexMatcher('([A-Za-z0-9 \(\)_]*)')

    Variants = [
        common.LengthVariant('10', ['T2'], dict(Rm=4)),
        common.LengthVariant('26', ['T1'], dict(Rm=4)),
        common.LengthVariant('27', ['T1'], dict(Rm=4)),
        common.LengthVariant('64', ['A1'], dict(register_list=16)),
        common.LengthVariant('65', ['A1'], dict(register_list=16)),
        common.LengthVariant('66', ['T1'], dict(register_list=13)),
        common.LengthVariant('66', ['A1'], dict(register_list=16)),
        common.LengthVariant('67', ['A1'], dict(register_list=16)),
        common.LengthVariant('103', ['A1', 'T1', 'A2', 'T2'], dict(opc1=3)),
        common.LengthVariant('108', ['T1'], dict(Rm=4)),
        common.LengthVariant('112', ['A1', 'T1', 'A2', 'T2'], dict(opc1=3)),
        common.LengthVariant('116', ['A1'], dict(mask=2)),
        common.LengthVariant('117', ['A1', 'T1'], dict(mask=2)),
        common.LengthVariant('137', ['A1'], dict(register_list=16)),
        common.LengthVariant('138', ['A1'], dict(register_list=16)),
        common.LengthVariant('182', ['A1'], dict(Rn=4)),
        common.LengthVariant('185', ['A1'], dict(Rn=4)),
        common.LengthVariant('194', ['A1'], dict(Rn=4)),
        common.LengthVariant('200', ['A1', 'T1'], dict(sat_imm=4)),
        common.LengthVariant('212', ['A1'], dict(register_list=16)),
        common.LengthVariant('213', ['A1'], dict(register_list=16)),
        common.LengthVariant('214', ['A1'], dict(register_list=16)),
        common.LengthVariant('215', ['A1'], dict(register_list=16)),
        common.LengthVariant('279', ['A1', 'T1'], dict(sat_imm=4)),
    ] 
    
    OpcodeProxies = {39: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        [1, 2])),
                 40: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_ARM,
                                                        [108, 109])),
                 49: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        3)),
                 51: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        4)),
                 110: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_ARM,
                                                        [16, 17, 99, 100])),
                 115: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        9)),
                 118: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        10)),
                 123: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_ARM,
                                                        157)),
                 153: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        13)),
                 181: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        14)),
                 198: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        [15, 16])),
                 240: common.InstructionProxy(common.ProxyTransform.ImmediateBitwiseInv,
                                       common.ProxyTarget(common.Variant.AArch32_SYS,
                                                        [19, 20]))
                     }


class ARM32Instruction(common.Instruction):
    def __init__(self):
	super(ARM32Instruction, self).__init__()
	self.encodings = []

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

    def addEncoding(self, archnum, support=None, isa=common.Variant.Invalid):
        variant = common.EncodingVariant(isa, int(archnum))
        variant.support.append(support)
        self.encodings.append(variant)
        return variant

    def addEncodingMnemonics(self, mnemonics):
        if not isinstance(mnemonics, list):
            self.getEncoding().mnemonics.append(mnemonics)
        else:
            for m in mnemonics:
                self.getEncoding().mnemonics.append(m)
        return self

    def addEncodingSupport(self, support):
        if not isinstance(support, list):
            self.getEncoding().support.append(support)
        else:
            for m in support:
                self.getEncoding().support.append(m)
        return self

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
                utils.Log('*** FAILED !!! ', (), self)
                return False
            return True
        return False

class ARM32Pager(common.PageCompleter):
    PageHeader = utils.SliceMatcher('F7.1AlphabeticallistofT32andA32baseinstructionsetinstructions', pipe=utils.Pipes.WipeWhitespace)
    PageFooters = [utils.SliceMatcher('F7T32andA32BaseInstructionSetInstructionDescriptions', pipe=utils.Pipes.WipeWhitespace)]

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

thumb16_header_seq = num_seq(15)
thumb32_header_seq = num_seq(15) + num_seq(15)
arm_header_seq = num_seq(31)

class ARM32Processor(common.Engine):
    ARCH_MAP={thumb16_header_seq: common.Engine.THUMB16, thumb32_header_seq: common.Engine.THUMB, 
            arm_header_seq: common.Engine.ARM32}
    def __init__(self):
        self.stage = Stage.Start
	self.insn = None
	self.page_completer = ARM32Pager()
	self.target_count = 291
	self.instructions = []
	self.num_map = {}

    def getArchVariant(self):
        return self.ARM32

    def getProxies(self):
	return ARM32Data.OpcodeProxies
  
    def scanBitHeader(self, parts, linesws):
        if linesws in self.ARCH_MAP:
            return self.ARCH_MAP[linesws]
	return self.INVALID

    def _parse_mnemonics(self, line):
	    if ARM32Data.InstructionMnemonics.match(line):
		    num, name, title = ARM32Data.InstructionMnemonics.getMatched()

		    if not int(num) in ARM32Data.OpcodeProxies:
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
	    new = page.page_lines[3].startswith('F7.1.')
	    
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
        for v in ARM32Data.Variants:
            vs = v.getVariants(num, encoding)
            if vs and reg in vs:
                return vs[reg]
        return None

    def getComponentList(self, comps):
	"""

	:type comps: str
	"""
        bitgroup, components, insn, fixups = [], [], self.insn, []
        isize = insn.getEncoding().getBitSize()
        for c in comps.split(' '):
	    if c.strip() == '':
		    continue
            bits = self.getRawBits(c)
            if bits:
                for b in bits:
                    bitgroup.append(b)
            else:
                self.flushBitGroup(bitgroup, components)

                if len(c) > 3 and c[0:3] == 'imm':
                    rsize = int(utils.Pipes.WipeAlphabet.execute(c[3:]))
                    components.append(common.BitOperand(c, 0, rsize))

                elif c in ['S', 'i', '(0)', '(1)', '(S)', 'J1', 'J2', 'H',
                           'op', 'C', 'P', 'U', 'D', 'W', 'M', 'tb', 'T',
                           'R', 'E', 'sh', 'A', 'I', 'F', 'im']:
                    components.append(common.BitOperand(c, 0, 1))

                elif c in ['DN', 'DM', 'N']:
                    fixups.append(c)
                    components.append(common.BitOperand(c, 0, 1))

                elif len(c) > 1 and c[0] == 'R' and c[1] in ['n', 'd', 'm', 's', 't', 'a']:
                    rsize = 4 if isize == 32 else 3
                    if 'DN' in fixups:
                        rsize = 3 if c == 'Rdn' else 4
                    elif 'DM' in fixups:
                        rsize = 3 if c == 'Rdm' else 4
                    elif 'N' in fixups:
                        rsize = 3 if c == 'Rn' else 4

                    fixup = self.variantLength(insn.num_id, insn.getEncoding(), c)
                    if fixup:
                        rsize = fixup
                    components.append(common.BitOperand(c, 0, rsize))


                elif c in ['cond', 'opc1', 'CRn', 'CRd', 'CRm', 'coproc',
                           'option', 'firstcond', 'mask', 'M1', 'opcode', 'reg']:
                    rsize = 4
                    fixup = self.variantLength(insn.num_id, insn.getEncoding(), c)
                    if fixup:
                        rsize = fixup
                    components.append(common.BitOperand(c, 0, rsize))

                elif c in ['opc2']:
                    components.append(common.BitOperand(c, 0, 3))

                elif c in ['type', 'sz', 'opt', '(0)(0)', 'rotate', 'imod']:
                    components.append(common.BitOperand(c, 0, 2))

                elif c in ['msb', 'lsb', 'widthm1', 'sat_imm', 'mode']:
                    rsize = 5
                    fixup = self.variantLength(insn.num_id, insn.getEncoding(), c)
                    if fixup:
                        rsize = fixup
                    components.append(common.BitOperand(c, 0, rsize))

                elif c == 'register_list':
                    reglist_size = 13 if isize == 32 else 8
                    fixup = self.variantLength(insn.num_id, insn.getEncoding(), c)
                    if fixup:
                        reglist_size = fixup
                    components.append(common.BitOperand(c, 0, reglist_size))

                else:
                    utils.Log('unknown operand (%s) - %i chars [%s]',
                               (c, len(c), comps), self)
                    continue
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
			if ARM32Data.InstructionDualEncoding.match(line):
				self.stage = Stage.Support
				e = ARM32Data.InstructionDualEncoding.getMatched()
				self.insn.addEncoding(e[0][1], e[1])
				return

			elif ARM32Data.InstructionEncoding.match(line):
				self.stage = Stage.Support
				e = ARM32Data.InstructionEncoding.getMatched()
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
