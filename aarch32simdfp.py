from lib import utils
from aarch32 import ARM32Data, ARM32Processor

class ARM32SIMDData(ARM32Data): 
    MnemonicsMatcher = utils.RegexMatcher('F8.1.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')

    @staticmethod
    def getMnemonicsMatcher():
        return ARM32SIMDData.MnemonicsMatcher

    @staticmethod
    def getTargetCount():
        return 235

    @staticmethod
    def getPageHeader():
        return 'F8.1Alphabeticallistoffloating-pointandAdvancedSIMDinstructions'
    
    @staticmethod
    def getPageFooter():
        return 'F8T32andA32AdvancedSIMDandfloating-pointInstructionDescriptions'

    @staticmethod
    def isTwobitOperand(insn_num, operand):
        TWOBITS=['imm2', '!=11', 'align', 'size', 'opc1', 'opc2', 'cc', 'len']
        if insn_num in ['58', '159']:
            TWOBITS=['imm2', '!=11', 'align', 'size', 'op']
        return operand in TWOBITS


class ARM32SIMDProcessor(ARM32Processor):
    def getArchVariant(self):
        return self.ARM32SIMD

    def getData(self):
	return ARM32SIMDData
