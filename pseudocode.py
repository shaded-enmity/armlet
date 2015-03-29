from arm import common
from lib import utils
import sys, math

"""
   PSEUDOCODE TESTS
   ================

   1. Assignments, Concatentaions, Function calls

     python pseudocode.py <(echo "Rd = '11'; i = '0'; d = UInt(Rd:'01':i);")

     ASSIGN-[]
      VAR-['Rd'] BITSTRING-['11']
     ENDSTMT-[]
     ASSIGN-[]
       VAR-['i'] BITSTRING-['0']
     ENDSTMT-[]
     ASSIGN-[]
      VAR-['d'] CALL-['UInt']
       CONCAT-[]
        CONCAT-[] VAR-['i']
         VAR-['Rd'] BITSTRING-['01']
     ENDSTMT-[]

"""

node = utils.Enum(INVALID=0, ENDSTMT=1, ASSIGN=1<<1, COMPARE=1<<2, CALL=1<<3, VAR=1<<4, IMMEDIATE=1<<5, BITSTRING=1<<6, TYPE=1<<7, CONCAT=1<<8)
_node_names = [  'INVALID', 'ENDSTMT', 'ASSIGN',    'COMPARE',    'CALL',    'VAR',    'IMMEDIATE',    'BITSTRING',    'TYPE',    'CONCAT']

token = utils.Enum(INVALID=0, LPAREN=1, RPAREN=1<<1, LANGLE=1<<2, RANGLE=1<<3, LSQUARE=1<<4, RSQUARE=1<<5,
        EQUALS=1<<6, COMMA=1<<7, DOT=1<<8, COL=1<<9, SCOL=1<<10, EXCLAM=1<<11, PIPE=1<<12, AND=1<<13, 
        SQUOTE=1<<14, LCURLY=1<<15, RCURLY=1<<16, PLUS=1<<17, MINUS=1<<18, STAR=1<<19, CARET=1<<20, NEWLINE=1<<21)
tokens = ['', '(', ')', '<', '>', '[', ']', '=', ',', '.', ':', ';', '!', '|', '&', '\'', '{', '}', '+', '-', '*', '^', '\n']

keyword = utils.Enum(INVALID=0, IF=1, THEN=1<<1, ELSE=1<<2, ELSIF=1<<3, DIV=1<<4, EOR=1<<5, FOR=1<<6,
                     TO=1<<7, UNDEF=1<<8, UNPRED=1<<9, UNKNOWN=1<<10, CASE=1<<11, OF=1<<12, WHEN=1<<13,
                     MOD=1<<14, AND=1<<15, OR=1<<16, OTHERWISE=1<<17, TRUE=1<<18, FALSE=1<<19, ASSERT=1<<20,
                     IN=1<<21, TYPE=1<<22, IS=1<<23, SEE=1<<24, REPEAT=1<<25, UNTIL=1<<26, WHILE=1<<27, 
                     DO=1<<28, ENUM=1<<29, REAL=1<<30, RETURN=1<<31, ARRAY=1<<32, BIT=1<<33, BITS=1<<34,
                     DOWNTO=1<<35, CONST=1<<36)
keywords = ['keywords.INVALID', 'if', 'then', 'else', 'elsif', 'DIV', 'EOR', 'for', 'to', 'UNDEFINED', 'UNPREDICTABLE', 
            'UNKNOWN', 'case', 'of', 'when', 'MOD', 'AND', 'OR', 'otherwise', 'TRUE', 'FALSE', 'assert',
            'IN', 'type', 'is', 'SEE', 'repeat', 'until', 'while', 'do', 'enumeration', 'real', 'return'
            'array', 'bit', 'bits', 'downto', 'constant']

typeclass = utils.Enum(INVALID=0, INTEGER=1, BOOLEAN=1<<1, BITS=1<<2, STRUCT=1<<3, ENUM=1<<4, ARRAY=1<<5, FUNC=1<<6, TUPLE=1<<7,
                       SETTER=1<<8, GETTER=1<<9, CONST=1<<10)

class TypeVar(object):
    def __init__(self, name, value):
        self.name = name
        self.value = value

class TypeDescriptor(object):
    def __init__(self, tclass, name, size=32, typevars=None):
        self.type_class = tclass
        self.name = name
        self.size = size
        self.vars = [] if not typevars else typevars

    def clone_resized(self, newsize):
        return TypeDescriptor(self.type_class, self.name, newsize)

class TypedExpression(object):
    def __init__(self, t, expr):
        self.type = t
        self.expression = expr

class FunctionSignature(object):
    def __init__(self):
        self.return_type = None
        self.args = {}
        self.name = ''
        self.type = typeclass.FUNC

    def set_array_func(self, getter):
        self.type |= typeclass.GETTER if getter else typeclass.SETTER

    def set_return_type(self, td):
        " :type td: TypeDescriptor "
        self.return_type = TypedExpression(td, 'return')

    def add_function_arg(self, td, name):
        " :type td: TypeDescriptor "
        if name in self.args:
            print 'W0100: Function "{0}" already has an argument named "{1}"'.format(self.name, name)
        self.args[name] = TypedExpression(td, name)

class VariableCache(object):
    def __init__(self):
        self.cache = {}

    def add_variable(self, tv, td):
        """" 
        :type tv: TypeVar 
        :type td: TypeDescriptor 
        """
        self.cache[tv.name] = TypedExpression(td, tv)

    def get_variable(self, name):
        return self.cache[name] if name in self.cache else None

_memop_vars = [TypeVar('MemOp_Load', 0), TypeVar('MemOp_Store', 1)]

types = {'integer':     TypeDescriptor(typeclass.INTEGER, 'integer'),
         'boolean':     TypeDescriptor(typeclass.BOOLEAN, 'boolean'),
         'bits':        TypeDescriptor(typeclass.BITS | typeclass.ARRAY, 'bits'),
         'MemOp':       TypeDescriptor(typeclass.ENUM, 'MemOp', typevars=_memop_vars)}

class FunctionCache(object):
    def __init__(self):
        self.cache = {}

    def add_function(self, sig):
        " :type sig: FunctionSignature "
        self.cache[sig.name] = sig

    def lookup_func(self, name):
        return self.cache[name] if name in self.cache else None

    def lookup_signature(self, name, args):
        """ :type sig: FunctionSignature
            :type name: str """
        s = self.cache[name]
        if sig.return_type == s.return_type:
            if not s.args and not args:
                return s
            if all(map(lambda (x, y): x.type == y, zip(s.args.values(), args))):
                return s
        return None

funcs = {}

state = utils.Enum(DEFAULT=0, FCALL=1, TYPE=1<<1, ASSIGN=1<<2, CONCAT=1<<3)
numbers = utils.RegexMatcher('^\d+$')

class State(object):
    def __init__(self, state):
        self.state = state
        self.nodes = []

class StateManager(object):
    def __init__(self):
        self.states = [State(state.DEFAULT)]
        self.popped = []

    def push_state(self, state):
        self.states.append(State(state))

    def peek_state(self):
        return self.states[-1]

    def pop_state(self):
        statenodes = self.states[-1].nodes
        s = self.states.pop()
        self.states[-1].nodes[-1].children.extend(statenodes)
        self.popped.append(s)
        return s

    def compare_state(self, state):
        return self.peek_state().state == state

    def get_top_node(self):
        return self.peek_state().nodes[-1]

    def add_node(self, node):
        return self.peek_state().nodes.append(node)

    def add_data(self, data):
        return self.get_top_node().data.append(data)

    def typecheck_nodes(self, *types):
        if len(self.peek_state().nodes) < len(types):
            return False
        return all([self.peek_state().nodes[-(i + 1)].type == t for i, t in enumerate(types)])
            

class Expression(object):
    def __init__(self):
        self.nodes = []

    def add_node(self, node):
        self.nodes.append(node)

    def get_rank(self, node):
        """ Rank is the amount of values that will be unpacked. As
          values in tuples are separated with comma ',' we just
          compute the number of commas in the expression and add 
          default 1.

            (a, b, c) = GetTriplet();

          Here the rank of the left-hand side would be 3.
          """
        return 1 + sum([int(n.type == token.COMMA) for n in self.nodes])

class Token(object):
    def __init__(self, value, _type):
        self.value = value
        self.type = _type

    def __repr__(self):
        return '"{0}" = 0x{1:04x}'.format(self.value, self.type)

class TokenStream(object):
    def __init__(self):
        self.tokens = []
        self.cursor = -1

    def add_token(self, value, _type):
        self.tokens.append(Token(value, _type))

    def next_token(self):
        self.cursor += 1
        l = len(self.tokens)
        if self.cursor >= len(self.tokens):
            raise StopIteration()
        return self.tokens[self.cursor]

    def peek_next(self, n=1):
        if len(self.tokens) < (self.cursor + n):
            return None
        return self.tokens[self.cursor + n]

    def left(self, n=1):
        if 0 > (self.cursor - n):
            return None
        return self.tokens[self.cursor - n]

    def right(self, n=1):
        return self.peek_next(n)

    def left_slice(self, n=1, shift=False):
        sl=[]
        for x in xrange(n):
            sl.append(self.left(x+1))

        if shift:
            self.cursor -= len(filter(None, sl))
        return sl

    def right_slice(self, n=1, shift=False):
        sl=[]
        for x in xrange(n):
            sl.append(self.right(x+1))

        if shift:
            self.cursor += len(filter(None, sl))
        return sl

    def find_right(self, t):
        token, n = self.right(1), 1
        while token != None:
            if token.type == t:
                return (token, n)
            n += 1
            token = self.right(n)
        return (None, 0)

def handle_eq_token(wq, sm):
    print wq[-1],'='

def handle_lparen_token(wq, sm):
    " :type sm: StateManager "
    preword = wq[-1]
    if len(preword) > 1:
        sm.push_state(state.FCALL)
        sm.add_node(AstNode(node.CALL))
        sm.add_data(preword.strip())
        print 'func call beg:', preword.strip()

def handle_rparen_token(wq, sm):
    " :type sm: StateManager "
    preword = wq[-1]
    if sm.compare_state(state.FCALL):
        if len(preword) > 1:
            sm.add_node(AstNode())
            sm.add_data(preword.strip())
            print 'func call end:', preword
        else:
            print 'no args'

        sm.pop_state()

class AstNode(object):
    def __init__(self, _type=node.INVALID):
        self.type = _type
        self.children = []
        self.parent = None
        self.data = []

    def addChildren(self, *nodes):
        " :type _node: AstNode  "
        for n in nodes:
            self.children.append(n)
            n.parent = self

def is_call(word):
    pass

def handle_word(word, endtoken=(0,''), wq=[]):
    if endtoken[0] == token.EQUALS:
        handle_eq_token(wq)
    elif endtoken[0] == token.LPAREN:
        handle_lparen_token(wq)
    elif endtoken[0] == token.RPAREN:
        handle_rparen_token(wq)
    else:
        print word

def parse(stream):
    s = iter(stream)

    tokenq, wb = TokenStream(), ''
    while True:
        try:
            c = next(s)
            if c in ['\t']:
                pass
                #tokenq.add_token(0, wb)
            elif c == ' ':
                # accumulate only leading space
                spaces = len(wb) == wb.count(' ')
                if spaces:
                    wb += c

            elif c in tokens:
                t = 1 << (tokens.index(c) - 1)
                if wb:
                    if len(wb) >= 2:
                        if wb[0] == ' ' and wb[1] != ' ':
                            wb = wb[1:]
                    if wb != ' ':
                        tokenq.add_token(wb, 0)
                tokenq.add_token(c, t)
                #handle_word(wb, (t, c), wq)
                wb = ''

            else:
                wb += c
                #print 'Unhandled token: %s (0x%X)' % (c, ord(c))

        except StopIteration:
            break

    funcs = {'UInt': AstNode(node.CALL)}
    tok, sm = None, StateManager()
    while True:
        try:
            tok = tokenq.next_token()
            if tok.type == token.INVALID:
                if sm.compare_state(state.DEFAULT) or sm.compare_state(state.ASSIGN):
                    if tok.value in funcs:
                        sm.add_node(AstNode(node.CALL))
                        sm.add_data(tok.value)
                        sm.push_state(state.FCALL)
                        print 'state.FCALL',
                    elif tok.value in types:
                        sm.push_state(state.TYPE)
                        sm.add_node(AstNode(node.TYPE))
                        print 'state.TYPE',
                    else:
                        sm.add_node(AstNode(node.VAR))
                        sm.add_data(tok.value)
                        print 'node.VAR',
                elif sm.compare_state(state.FCALL):
                    sm.add_node(AstNode(node.VAR))
                    sm.add_data(tok.value)
                    print 'arg:', tok.value
                else:
                    print 'Error: %i %s' % (sm.peek_state().state, tok.value)
            elif tok.type == token.EQUALS:
                n = sm.peek_state().nodes.pop()
                sm.add_node(AstNode(node.ASSIGN))
                sm.get_top_node().addChildren(n)
                sm.push_state(state.ASSIGN)
                print 'node.EQUALS',
            elif tok.type == token.LPAREN:
                if sm.compare_state(state.FCALL):
                    pass
                else:
                    if tokenq.cursor == 1 or tokenq.tokens[tokenq.cursor].type != token.INVALID:
                        # if it's a first token or an outstanding lparen 
                        # we try to create a tuple
                        pass # but not now
            elif tok.type == token.RPAREN:
                if sm.peek_state().state == state.FCALL:
                    sm.pop_state()
            elif tok.type == token.SQUOTE:
                if not sm.typecheck_nodes(node.VAR, node.BITSTRING) and not sm.typecheck_nodes(node.CONCAT):
                    sm.add_node(AstNode(node.BITSTRING))
                    nn = tokenq.peek_next()
                    if nn.type == token.INVALID:
                        sm.add_data(nn.value)
                        tokenq.next_token()
                        if tokenq.peek_next().type != token.SQUOTE:
                            print 'E002: Error trying to find matching \' for bitstring'
                        else:
                            tokenq.next_token()
            elif tok.type == token.COL:
                lhs = sm.get_top_node().type
                nn = tokenq.peek_next()
                if lhs == node.VAR:
                    nl, nr = sm.get_top_node(), AstNode(node.BITSTRING)
                    sm.peek_state().nodes.pop()
                    nc = AstNode(node.CONCAT)
                    nc.addChildren(nl, nr)
                    sm.add_node(nc)
                    #sm.add_node(nr)
                    tokenq.next_token()
                    #print 'token.COL: LHS - VAR'
                    if nn.type == token.SQUOTE:
                        if tokenq.right(2).type != token.SQUOTE:
                            print 'E001: Expected \' got %s' % tokenq.right(2).value
                        else:
                            nr.data.append(tokenq.peek_next().value)
                            tokenq.tokens.remove(tokenq.right(2))
                            tokenq.next_token()
                    elif nn.type == token.INVALID:
                        nr.type = node.VAR
                        nr.data.append(nn.value)
                        #print 'token.COL: RHS - VARIABLE'
                elif lhs == node.BITSTRING:
                    pass
                elif lhs == node.CONCAT:
                    n = sm.peek_state().nodes.pop()
                    nx = AstNode(node.VAR)
                    nc = AstNode(node.CONCAT)
                    nc.addChildren(n, nx)
                    sm.add_node(nc)
                    if nn.type == token.SQUOTE:
                        if tokenq.right(2).type != token.SQUOTE:
                                print 'E001: Expected \' got %s' % tokenq.right(2).value
                        else:
                            nr.data.append(tokenq.peek_next().value)
                            tokenq.tokens.remove(tokenq.right(2))
                            tokenq.next_token()
                    elif nn.type == token.INVALID:
                        nx.data.append(nn.value)
                    tokenq.next_token()
                else:
                    print 'token.COL: LHS unknown:', lhs
            elif tok.type == token.SCOL:
                sm.pop_state()
                sm.add_node(AstNode(node.ENDSTMT))
            print tok
        except StopIteration:
            # this is not very robust
            if tok.type not in [token.SCOL, token.NEWLINE]:
                print 'ERROR: Unfinished token stream ', tok.type
            break


    def walk_nodes(s, t):
        if not t:
            print '%s-%s' % (_node_names[int(math.log(s.type, 2)) + 1], str(s.data))
        if s.children:
            print ' '*t, ' '.join(['%s-%s' % (_node_names[int(math.log(n.type, 2)) + 1], str(n.data)) for n in s.children])
            for n in s.children:
                walk_nodes(n, t+1)

    #for s in sm.states:
    #    print 'state', s.state
    #    print 'nodes', ' '.join(['%s-%s' % (_node_names[int(math.log(n.type, 2)) + 1], str(n.data)) for n in s.nodes])

    for n in sm.states[0].nodes:
        walk_nodes(n, 0)

    #for n in sm.states[0].nodes:
    #    if n.type == node.CALL:
    #        print n.children

    #for s in sm.popped:
    #    print 'popped state', s.state
    #    print 'nodes', ' '.join(['%s-%s' % (_node_names[int(math.log(n.type, 2)) + 1], str(n.data)) for n in s.nodes])

def main():
    f = sys.argv[1]
    cont = open(f).read()
    parse(cont)

main()
