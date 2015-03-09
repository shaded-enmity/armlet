from lib import utils
from arm import common

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
    def isTwobitOperand(insn_num, operand):
        TWOBITS=['type', 'imm2', 'imod', 'sz', 'opt', '!=11', 'rotate']
        return operand in TWOBITS


class ARM32Processor(common.Engine):
    def getData(self):
	return ARM32Data

    def getArchVariant(self):
        return self.ARM32
