#!/usr/bin/python
# armLET ARM CLI System 
#
# Copyright (C) 2013 Pavel Odvody
#
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA
#

import sys

from arm import common, latex_serializer
from lib import utils

from aarch32 import ARM32Processor
from aarch32simdfp import ARM32SIMDProcessor
from aarch64 import ARM64Processor
from aarch64simdfp import ARM64SIMDProcessor

#from aarch64 import ARM64Processor
#from aarch64_simd import ARM64SIMDProcessor

def Manifest():
    _N_project_descr = {'license': 'GNU/GPL 2.0', 'year': 2013,
      'author_ref': 'Pavel Odvody',
      'name': 'armLET CLI',
      'arg_str': '[-f file] [-o outfile] [-h | --help]',
      'usage_str': '',
      'space': 'armLET::cli',
      'logo': '''
                 __    __.__ __.__
   _._ _._ __.__|  |  |   __|_   _|
  | .'|  _|     |  |__|   __| | |  
  |__,|_| |_|_|_|_____|_____| |_|  
      '''}
    return _N_project_descr

def main(argv):
    manifest, profiler = Manifest(), utils.Profiler()
    filenames = utils.FileParser(argv, 'f', manifest)
    if filenames:
        ''' measure the time it takes '''

        with profiler.probe('file_read'):
            utils.Log('processing filename: %s', (filenames[0][0]),
                       manifest['space'], utils.LogCat.Section)

            with utils.Indentation(1):
                pump = common.DataPump(filenames[0][0])

                if pump.loadRawData():
                    pump.registerEngines(ARM32Processor(), ARM32SIMDProcessor(), 
				    ARM64Processor(), ARM64SIMDProcessor())
                    pump.execute(profiler)

        utils.Log('finished running in (%f seconds)', (profiler.getRuntime()),
                   manifest['space'], utils.LogCat.Leaf)

        if filenames[1]:
	    ''' produce strictly valid JSON '''

            with open(filenames[1][0], 'a+') as outf:
                outf.truncate()
                outf.write('{"InstructionData": [\n')
                for engine in pump.engines.getEngines():
                    outf.write('{"name": "%s", "instructions": [\n' % str(engine.__class__.__name__))
                    for insn in engine.instructions:
                        outf.writelines(insn.serialize())
                        if insn != engine.instructions[-1]:
                            outf.write(',\n')
                    if engine != pump.engines.getEngines()[-1]:
                        outf.write(']},\n')
                    else:
                        outf.write(']}')
                outf.write(']}\n')

if __name__ == '__main__':
    sys.exit(main(sys.argv))
