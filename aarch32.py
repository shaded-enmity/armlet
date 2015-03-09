from lib import utils
from arm import common
import string, pprint, json

class ARM32Data(common.DataStrings):
    EncodingMatcher = utils.RegexMatcher('^([AT])(\d+)$')
    MnemonicsMatcher = utils.RegexMatcher('F7.1.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')

    @staticmethod
    def getEncodingMatcher():
        return ARM32Data.EncodingMatcher

    @staticmethod
    def getMnemonicsMatcher():
        return ARM32Data.MnemonicsMatcher

    @staticmethod
    def getTargetCount():
        return 293

    @staticmethod
    def getPageHeader():
	return 'F7.1AlphabeticallistofT32andA32baseinstructionsetinstructions'
    
    @staticmethod
    def getPageFooter():
        return 'F7T32andA32BaseInstructionSetInstructionDescriptions'

    @staticmethod
    def getInstructionType():
        return ARM32Instruction

    @staticmethod
    def isTwobitOperand(insn_num, operand):
        TWOBITS=['type', 'imm2', 'imod', 'sz', 'opt', '!=11', 'rotate']
        return operand in TWOBITS


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
        a = utils.Int(archnum)
        if a != None:
            variant = common.EncodingVariant(isa, a)
        else:
            variant = common.EncodingVariant(isa, 1, support)
        variant.support.append(support)
        self.encodings.append(variant)
        return variant

    def _getmnemonics(self):
        return self.getEncoding().mnemonics

    def addMnemonicsVariant(self, name):
        self._getmnemonics().append(common.MnemonicsVariant(name))

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
        return ARM32JSONSerializer().serialize_instruction(self)

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

class ARM32JSONSerializer(object):
    def __init__(self):
        self.data = ''

    def serialize_instruction(self, insn):
        " :type insn: ARM32Instruction "
        names = [n.replace(',', '').strip() for n in insn.names]
        encodings = []
        for enc in insn.encodings:
            mnemonics = [{'name': mv.name, 'constraint': mv.constraint.string, 'mnemonics': [{'value': m.value, 'aliases': [{'target': a.value, 'constraint':a.constraint.string} for a in m.aliases]} for m in mv.mnemonics]} for mv in enc.mnemonics]
            encodings.append({'name': enc.getName(), 'variant': str(enc.isa), 
                'mnemonics': mnemonics, 'decode': enc.decode, 'bits': [{'name': bc.name, 'size': bc.length, 'type': bc.type} for bc in enc.bits]
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
            }
        }

        pprint.pprint(jsondict)
        return json.dumps(jsondict)

class ARM32Pager(common.PageCompleter):
    def __init__(self, data):
	super(ARM32Pager, self).__init__()
	self.PageFooters = [utils.SliceMatcher(data.getPageFooter(), pipe=utils.Pipes.WipeWhitespace)]
	self.PageHeader = utils.SliceMatcher(data.getPageHeader(), pipe=utils.Pipes.WipeWhitespace)

    def isHeader(self, line):
        r = self.PageHeader.match(line)
	return r

    def isFooter(self, line):
	r = any(h.match(line) for h in self.PageFooters)
	return r

class Stage(object):
    (Start, Name, Summary, Bitheader, Operands, Variant, Mnemonics,
     Pseudocode, Aliases, Symbols, Operation, Support, Components, Syntax,
     Decode, Equivalency) = utils.BinaryRange(16)

class ARM32Processor(common.Engine):
    def __init__(self):
        self.stage = Stage.Start
	self.insn = None
        self.last_instruction = None
	self.page_completer = ARM32Pager(self.getData())
	self.target_count = self.getData().getTargetCount()
	self.instructions = []
	self.num_map = {}

    def getData(self):
	return ARM32Data

    def getArchVariant(self):
        return self.ARM32

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
            s, l, t = header, 1, common.OperandType.INVALID
            if isinstance(header, tuple):
                s, l = header[1], header[0] - header[1] + 1
            if name.startswith('R'):
                t = common.OperandType.REGISTER
            elif name.startswith('imm') or name == 'i':
                t = common.OperandType.IMMEDIATE
            elif name.startswith('!='):
                t = common.OperandType.CONDITION
            elif len(name) < 3:
                t = common.OperandType.OPERAND
            return common.BitOperand(name, s, l, t)

        def _collapse((items, acc, first), value):
            if value.name in ['0', '1', '(0)', '(1)', 'x']:
                acc.append(value.name)
                return (items, acc, first if first != None else value)
            elif items and value.name == items[-1].name:
                if acc:
                    items.append(common.BitOperand(' '.join(acc), first, len(acc), common.OperandType.BITS))
                items[-1].length += value.length
                return (items, [], None)
            else:
                if acc:
                    items.append(common.BitOperand(' '.join(acc), first, len(acc), common.OperandType.BITS))
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
                final.extend([common.BitOperand(' '.join(joined[1]), 0, len(joined[1]), common.OperandType.BITS)])
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
            self.insn = self.getData().getInstructionType()()
            self.insn.setHeader(num, name, title)
            if common.IsVariant(self.getArchVariant().value, common.Variant.Bits64):
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
            if common.IsVariant(self.getArchVariant().value, common.Variant.Bits64):
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
		else:
			self.insn.metadata['Syntax'].append(line)

	elif self.stage == Stage.Operation:
		self.insn.metadata['Operation'].append(line)
