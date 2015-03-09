from lib import utils
from aarch32 import ARM32Data, ARM32Processor

class ARM64Data(ARM32Data): 
    MnemonicsMatcher = utils.RegexMatcher('C6\.6\.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')
    EncodingMatcher = utils.RegexMatcher('^(Pre-index|Post-index|Unsignedoffset)()$')

    @staticmethod
    def getEncodingMatcher():
        return ARM64Data.EncodingMatcher

    @staticmethod
    def getMnemonicsMatcher():
        return ARM64Data.MnemonicsMatcher

    @staticmethod
    def getTargetCount():
        return 223

    @staticmethod
    def getPageHeader():
        return 'C6.6Alphabeticallistofinstructions'
    
    @staticmethod
    def getPageFooter():
        return 'C6A64BaseInstructionDescriptions'

    @staticmethod
    def isTwobitOperand(insn_num, operand):
        TWOBITS=['imm2', '!=11', 'shift', 'immlo', 'sz', 'op1', 'op2', 'hw']
        if insn_num in ['18', '56', '68', '71', '208', '129', '130', '131', '204', '205']:
            TWOBITS=['imm2', '!=11', 'shift', 'immlo', 'sz']
        return operand in TWOBITS


class ARM64Processor(ARM32Processor):
    def getArchVariant(self):
        return self.ARM64

    def getData(self):
	return ARM64Data
