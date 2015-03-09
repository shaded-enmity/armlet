from lib import utils
from aarch32 import ARM32Data, ARM32Processor

class ARM64SIMDData(ARM32Data): 
    MnemonicsMatcher = utils.RegexMatcher('C7\.3\.(\d*)\s*([A-Z1-9]{1,8})\s*(.*)')
    EncodingMatcher = utils.RegexMatcher('^(Scalar|Vector|Nooffset|Pre-index|Post-index|Unsignedoffset|Signedoffset)()$')

    @staticmethod
    def getEncodingMatcher():
        return ARM64SIMDData.EncodingMatcher

    @staticmethod
    def getMnemonicsMatcher():
        return ARM64SIMDData.MnemonicsMatcher

    @staticmethod
    def getTargetCount():
        return 350

    @staticmethod
    def getPageHeader():
        return 'C7.3Alphabeticallistoffloating-pointandAdvancedSIMDinstructions'
    
    @staticmethod
    def getPageFooter():
        return 'C7A64AdvancedSIMDandFloating-pointInstructionDescriptions'

    @staticmethod
    def isTwobitOperand(insn_num, operand):
        TWOBITS=['size', 'opc', 'type', 'len']
        return operand in TWOBITS


class ARM64SIMDProcessor(ARM32Processor):
    def getArchVariant(self):
        return self.ARM64SIMD

    def getData(self):
	return ARM64SIMDData
