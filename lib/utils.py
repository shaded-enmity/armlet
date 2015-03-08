#
# armLET Core Library
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

from datetime import date
import getopt, itertools, os, re
from time import time

def Enum(**enums):
    return type('Enum', (), enums)

LogCat = Enum(Message=0, Input=1, Section=2, Leaf=3, Marker=4, Link=5, Debug=6)
LogIndent, LogIndentLevel = 4, 0
LogCatPrefixes = ['!', '$', '+', '-', 'v', '@', '>']
Separators = {'-': '<>', '_': '[]', '=': '{}', '+': '()', '^': '\'\''}
BitPairs = ['.-', '01', 'oX']
WhiteSpacePattern = re.compile(r'\s+')
LeadingWhitespace = re.compile(r'^\s+')

def StringList(fromlist):
    return [str(f) for f in fromlist]

def IntList(string):
    return [ord(c) for c in string]

def RoundRobin(*iterables):
    "round-robin('ABC', 'D', 'EF') --> A D E B F C"
    pending = len(iterables)
    nexts = itertools.cycle(iter(it).next for it in iterables)
    while pending:
        try:
            for nxt in nexts:
                yield nxt()
        except StopIteration:
            pending -= 1
            nexts = itertools.cycle(itertools.islice(nexts, pending))

def ObjectDump(obj):
    return ' '.join(['\t' + n + '=' + str(v) + '\n'\
      for n, v in vars(obj).items() if n[0:2] != '__'])

def ClassName(var):
    if isinstance(var, str):
        return var
    return var.__class__.__module__ + '::' + var.__class__.__name__
    #return var.__class__.__name__

def Indent(val):
    global LogIndentLevel
    LogIndentLevel = LogIndentLevel + val
    return LogIndentLevel

def FormatBytes(num):
    for x in ['bytes', 'KB', 'MB', 'GB']:
        if num < 1024.0 and num > -1024.0:
            return "%3.1f%s" % (num, x)
        num /= 1024.0
    return "%3.1f%s" % (num, 'TB')

def Unary(value):
    return value and len(value) == 1

def Int(s):
    try:
	return int(s, 0)
    except ValueError:
	return None

def Add(a, b):
    return a + b

def Log(fstr, t, v=None, m=LogCat.Message):
    if m == LogCat.Debug:
        print (' ' * (LogIndentLevel * LogIndent)) +\
          ('[%s] %s %s' % (LogCatPrefixes[m], ClassName(v), ((fstr) % t)))
    else:
        print (' ' * (LogIndentLevel * LogIndent)) +\
          ('[%s] %s' % (LogCatPrefixes[m], ((fstr) % t)))

def Merge(a, b):
    for x in b:
        a.append(x)
    return a

def WipeWhitespace(line):
    return re.sub(WhiteSpacePattern, '', line)

def FormatData(dd, data):
    fstr, tmp, tbl, num, s, ext = '', '', [], 0, 0, False
    for dm in re.findall(r'(\d*?)([sdxbv][\-_=\+\^]?)', dd):
        num = 1 if dm[0] == '' else Int(dm[0])
        ext = len(dm[1]) > 1
        tmp = ''
        for dummy_x in xrange(0, num):
            if dm[1][0] == 's':
                tmp += ', "%s"'
                tbl.append(str(data[s]))
            elif dm[1][0] == 'd':
                tmp += ', %s'
                tbl.append("{:,}".format(Int(data[s])))
            elif dm[1][0] == 'x':
                tmp += ', %#08x' if data[s] != '0' else ', %i'
                tbl.append(Int(data[s]))
            elif dm[1][0] == 'b':
                tmp += ', %s'
                tbl.append(bin(Int(data[s])))
            elif dm[1][0] == 'v':
                tmp += ', %s'
                tbl.append(data[s])

            if not ext:
                tmp = tmp[2:]

            s += 1

        if ext:
            fstr += ' ' + Separators[dm[1][1]][0] + tmp[2:] +\
              Separators[dm[1][1]][1] + ' '
        else:
            fstr += tmp

    return (fstr % tuple(tbl)).strip()

def Bits(items, s=0):
    if not hasattr(items, '__contains__'):
        return '[]'
    return '[' + ''.join([BitPairs[s][0] for unused_i in items[::-1]]) + ']'

def YearRange(a, b):
    if a == b:
        return '%i' % a
    return '%i - %i' % (a, b)

def BasicDesc(manifest):
    name = ('='*12)+'> '+manifest['name'] + ' <' + ('='*(58-len(manifest['name']))) + ' ** '
    pivot = (len(manifest['name']) / 2) + 13
    dstr = YearRange(manifest['year'], date.today().year) + ('(C) [%s]' % (manifest['license']))
    datestring = ((pivot - len(dstr) / 2) * ' ') + dstr
    datestring = datestring +  + (74-len(datestring))*' ' + ' **'
    astr = 'by ' + manifest['author_ref']
    author = ((pivot - len(astr) / 2) * ' ') + astr
    author = author + (74-len(author))*' ' + ' **'
    print '/' + ('*' * 80)
    print ' ** %s\n ** %s\n ** %s\n ** %s\n ** %s **' %\
      (manifest['logo'], name,\
        datestring, author, ' '*74)
    print ' ' + ('*' * 79) + '/\n'
    Log('cwd "%s"', (os.getcwd()),
          manifest['space'])

def Usage(filename, manifest):
    print '    USAGE: %s %s\n' % (filename, manifest['arg_str'])
    print '     %s\n' % manifest['usage_str']
    return False

def FileParser(args, optformat, manifest):
    try:
        opts, dummy_args = getopt.getopt(args[1:], optformat + ':')
        filenames = Merge([], [v for (k, v) in opts if k == '-' + optformat])
        if len(filenames) == 0:
            return Usage(args[0], manifest)
        BasicDesc(manifest)
        return filenames
    except getopt.GetoptError:
        return Usage(args[0], manifest)

def PropGetter(i_object, i_method):
    try:
        return getattr(i_object, i_method)
    except AttributeError:
        return None

class Indentation(object):
    def __init__(self, level=1):
        self.level = level

    def __enter__(self):
        Indent(self.level)

    def __exit__(self, wtype, value, tb):
        Indent(-self.level)

class Profiler(object):
    class ProfilerEntry(object):
        def __init__(self, name, time):
            self.entry_name = name
            self.runtime = time

    class ProfilerEntrySet(object):
        def __init__(self, name):
            self.base = Profiler.ProfilerEntry(name, 0)
            self.entries = []

        def addEntry(self, entry):
            if entry.entry_name == self.base.entry_name:
                self.base.runtime += entry.runtime
                self.entries.append(entry)
                return True
            return False

    class ProfilerData(object):
        def __init__(self):
            self.entries = {}

        def getDataPoints(self, point):
            if point not in self.entries:
                self.entries[point] = Profiler.ProfilerEntrySet(point)
            return self.entries[point]

        def addDataEntry(self, entry):
            entry_set = self.getDataPoints(entry)
            entry_set.addEntry(entry)
            return self

        def addDataPoint(self, name, runtime):
            return self.addDataEntry(Profiler.ProfilerEntry(name, runtime))

        def getRuntime(self, name):
            return self.getDataPoints(name).base.runtime

    class Probe(object):
        def __init__(self, name, parent):
            now = time()
            self.name = name
            self.start_time = now
            self.end_time = None
            self.parent = parent

        def begin(self):
            self.start_time = time()
            self.end_time = None

        def checkpoint(self):
            now = self.end_time or time()
            return Profiler.ProfilerEntry(self.name, now - self.start_time)

        def end(self):
            self.end_time = time()

        def __enter__(self):
            self.begin()

        def __exit__(self, wtype, value, tb):
            self.end()
            if self.parent:
                self.parent.killProbe(self)

    def __init__(self):
        self.start_time = time()
        self.event_data = Profiler.ProfilerData()
        self.probes = []
        self.counters = {}

    def counter(self, name, num=1):
        if name not in self.counters:
            self.counters[name] = 0
        self.counters[name] += num
        return self.counters[name]

    def getCounter(self, name):
        return 0 if name not in self.counters else self.counters[name]

    def getCounters(self, *names):
        return tuple(self.getCounter(name) for name in names)

    def getRuntime(self):
        return time() - self.start_time

    def getEntryStats(self, name):
        return self.event_data.getDataPoints(name)

    def probe(self, name):
        probe = Profiler.Probe(name, self)
        self.probes.append(probe)
        return probe

    def killProbe(self, probe):
        entry = probe.checkpoint()
        self.probes.remove(probe)
        self.event_data.addDataEntry(entry)
        return entry

def SetValue(mine, theirs, defaults):
    if not mine:
        mine = theirs or defaults
    return mine

def BinaryRange(n):
    return [0] + [1 << x for x in range(n-1)]

class StringData(object):
    def __init__(self, *lines):
        self.strings = []
        self.strings.append(lines)

    def getDefault(self):
        return self.strings[0] if self.strings else None

    def append(self, *lines):
        self.strings.append(lines)
        return self

class DataGroups:
    Whitespace = '\s+'
    Alphanumeric = '[A-Za-z0-9]+'
    Alphabet = '[A-Za-z]+'
    Numbers = '[0-9]+'

class PatternMatcher(object):
    def __init__(self, pattern):
        self.pattern = pattern
        self.line = None
        self.result = None

    def preMatch(self, line):
        self.line = line

    def _match(self):
        return self.line == self.pattern

    def postMatch(self, line):
        self.line = None

    def match(self, line):
        self.preMatch(line)
        self.result = self._match()
        self.postMatch(line)
        return self.result

class Pipe(object):
    def execute(self, line):
        return line

class WipingPipe(Pipe):
    def __init__(self, what):
        self.wipe = what

    def execute(self, line):
        return re.sub(self.wipe, '', line)

class TrimmingPipe(Pipe):
    def __init__(self, what):
        self.trim = what

    def execute(self, line):
        w = re.sub('^' + self.trim, '', line)
        w = re.sub(self.trim + '$', '', w)
        return w

class ListOf(object):
    def __init__(self):
        self.items = []
        self.type = NClassName(self)

    def addItem(self, *item):
        for i in item:
            self.items.append(i)

class Pipes(ListOf):
    Passthrough = Pipe()
    TrimWhitespace = TrimmingPipe(DataGroups.Whitespace)
    WipeWhitespace = WipingPipe(DataGroups.Whitespace)
    WipeAlphabet = WipingPipe(DataGroups.Alphabet)

    def __init__(self):
        super(NPipes, self).__init__()
        self.addItem(NPipes.Passthrough,
                     NPipes.TrimWhitespace,
                     NPipes.WipeWhitespace,
                     NPipes.WipeAlphabet)

class PipedMatcher(PatternMatcher):
    def __init__(self, pattern, pipe=Pipes.Passthrough):
        super(PipedMatcher, self).__init__(pattern)
        self.pipe = pipe

    def preMatch(self, line):
        self.line = self.pipe.execute(line)

    def getMatched(self):
        #print self.line, self.pattern
	return [self.line]

class RegexMatcher(PipedMatcher):
    def __init__(self, pattern, pipe=Pipes.TrimWhitespace):
        super(RegexMatcher, self).__init__(pattern, pipe)
        self.matched = None

    def getMatched(self):
        return self.matched.groups() or self.matched.group(0)

    def _match(self):
        self.matched = re.search(self.pattern, self.line)
        return self.matched and self.line != ''

class SliceMatcher(PipedMatcher):
    def __init__(self, pattern, aslice=slice(0, None),
                 pipe=Pipes.TrimWhitespace):
        super(SliceMatcher, self).__init__(pattern, pipe)
        if isinstance(aslice, list):
            aslice = slice(aslice[0], aslice[1])
        self.slice = aslice

    def _match(self):
        return self.line[self.slice] == self.pattern

class Test(object):
    def getCaseName(self):
        return ClassName(self)

    def getMessage(self):
        return "Test '%s' passed!" % self.getCaseName()

    def validate(self, context):
        return True

if __name__ == '__main__':
    pass
