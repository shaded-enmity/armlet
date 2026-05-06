/**
 * @file ARM pseudocode parser
 * @author Pavel Odvody <pavel@redhat.com>
 * @license MIT
 */

const PREC = {
  compare: 2,
  concat: 3,
  expression: 4,
  tuple: 5,
  return_tuple: 6,
  parenthesized: 4,
  shift: 17,
  plus: 18,
  times: 19,
  unary: 20,
  power: 21,
  call: 22,
  bitselect: 23,
};

module.exports = grammar({
  name: "armlet",

  extras: $ => [
    /[\s\f\uFEFF\u2060\u200B]|\r?\n/,
    MAIN_BUILD ? $._comment : $.comment
  ],

  inline: $ => [
    $.inline, $.indented
  ],

  externals: $ => [
    $._NEWLINE,
    $._INDENT,
    $._DEDENT,
  ],

  conflicts: $ => [
    [$.value, $.target],
    [$.value, $.type_spec, $.target],
    [$.value, $.lhs_tuple, $.type_spec],
    [$.value, $.lhs_tuple],
    [$.value, $.array_access],
    [$.argument_list, $.argument_list_def],
    [$.setter, $.array_access],
    [$.dereference],
  ],

  reserved: {
    global: _ => [
      'if', 'else', 'elsif', 'then',
      'case', 'of', 'when', 'otherwise',
      'IN', 'assert', 'return', 'constant',
      'while', 'do', 'for', 'to', 'downto',
      'repeat', 'until', 'type', 'is', 'enumeration',
      'array', 'TRUE', 'FALSE', 'import', 'use',
      'UNKNOWN', 'IMPLEMENTATION_DEFINED', 'SEE',
      'UNDEFINED', 'UNPREDICTABLE', 'bitlayout',
    ]
  },

  word: $ => $.name,

  rules: {
    suite: $ => repeat($.statement),

    statement: $ => choice($.func_expr, $.func_def, $.simple_statement, $.compound_statement, $.type_definition, $.enum_def, $.getter, $.setter, $.type_alias, $.import, $.bitlayout),

    simple_statement: $ => choice($.assignment, $.var_def, $.expr_statement, $.return, $.assert, $.array_definition, $.use, $.func),
    compound_statement: $ => choice($.if_then, $.case_when, $.while_loop, $.repeat_until, $.for_loop),

    use: $ => seq('use', choice($.name, $.dereference), ';'),

    import: $ => seq(
      'import', $.string, ';'
    ),

    while_loop: $ => seq(
      'while', $.expression, 'do', $.block
    ),

    repeat_until: $ => seq(
      'repeat', choice($.block, $.simple_statement), 'until', $.expression, ';'
    ),

    for_loop: $ => seq(
      'for', $.name, '=', $.expression, choice('to', 'downto'), $.expression, $.block
    ),

    assert: $ => seq('assert', $.expression, ';'),
    return: $ => seq('return', optional($.expression), ';'),

    expr_statement: $ => seq($.expression, ';'),

    inline_if: $ => seq(
      'if', $.expression, 'then', $.expression,
      repeat(seq('elsif', $.expression, 'then', $.expression)),
      'else', $.expression,
    ),

    if_then: $ =>
      seq('if',
        field('condition', $.expression),
        'then',
        field('consequence', $.block),
        repeat(
          field('alternative',
            seq('elsif', field('condition', $.expression), 'then', field('consequence', $.block)))
        ),
        optional(
          seq('else',
            field('else_alternative', $.block)
          )
        )
      ),

    case_when: $ => seq(
      'case',
      field('condition', $.expression),
      'of', $._NEWLINE,
      $._INDENT,
      field('case', repeat1($.when)),
      field('otherwise', optional(seq('otherwise', $.block))),
      $._DEDENT,
    ),

    when: $ => seq('when', $.value, $.block),

    block: $ => choice(
      $.indented,
      $.inline,
    ),

    indented: $ => seq(
      $._NEWLINE,
      $._INDENT,
      repeat1(choice(
        seq($.simple_statement, optional($._NEWLINE)),
        $.compound_statement,
      )),
      $._DEDENT,
    ),

    inline: $ => seq(repeat($.simple_statement), $._NEWLINE),

    float: _ => {
      const digits = repeat1(/[0-9]+_?/);
      const exponent = seq(/[eE][\+-]?/, digits);

      return token(seq(
        choice(
          seq(digits, '.', digits, optional(exponent)),
          seq(digits, exponent),
        ),
        optional(/[jJ]/),
      ));
    },

    integer: _ => token(choice(
      seq(
        choice('0x', '0X'),
        repeat1(/_?[A-Fa-f0-9]+/),
      ),
      seq(
        repeat1(/[0-9]+_?/),
      ),
    )),

    value: $ => choice($.immediate, $.name, $.dereference, $.struct_select, $.bit_select, $.array_access, $.array_like_literal),

    immediate: $ => choice($.bitstring, $.integer, $.float, $.boolean_value, $.string),

    bitstring: _ => seq("'", repeat1(/[01xX ]+?/), "'"),

    string: $ => seq(
      '"',
      repeat(choice(
        token.immediate(prec(1, /[^"{}\n\\]+/)),
        $.escape_sequence,
        $.interpolation,
      )),
      token.immediate('"'),
    ),

    escape_sequence: _ => token.immediate(seq(
      '\\', choice('"', "\\")
    )),

    interpolation: $ => seq(
      token.immediate('{'),
      $.name,
      token.immediate('}'),
    ),

    dereference: $ => prec.right(
      seq(
        field('first', $.name),
        repeat(seq('.', field('middle', $.name))),
        '.',
        field('last', $.name),
      )
    ),

    name: _ => /[a-zA-Z_]\w*/,

    bit: _ => 'bit',

    bits: $ => seq('bits', '(', $.expression, ')'),

    unary_operator: $ => prec.left(PREC.unary, seq(choice('-', '!'), $.expression)),

    expression: $ => prec(PREC.expression, choice(
      $.value,
      $.unary_operator,
      $.comparison_operator,
      $.boolean_operator,
      $.binary_operator,
      $.parenthesized,
      $.tuple,
      $.func,
      $.set,
      $.inline_if,
      $.unknown,
      $.implementation_defined,
      $.see,
      'UNPREDICTABLE',
      'UNDEFINED'
    )),

    implementation_defined: $ => seq($.type_spec, 'IMPLEMENTATION_DEFINED', $.string),

    see: $ => seq('SEE', $.string),

    unknown: $ => seq(
      choice($.type_spec), 'UNKNOWN'
    ),

    qualifier: _ => 'constant',

    parenthesized: $ => seq('(', $.expression, ')'),

    lhs_tuple: $ => seq(
      '(', commaSep(choice('-', $.name, $.target, $.bit_select, $.struct_select, $.bit_concat, $.lhs_tuple)), ')'
    ),

    bit_concat: $ => prec(PREC.bitselect, seq('<', commaSep1($.name, $.dereference), '>')),

    tuple: $ => prec(PREC.tuple, seq('(', commaSep($.expression), ')')),

    type_spec: $ => choice($.name, $.bits, $.bit, $.array_variable, $.dereference),

    target: $ => choice(
      $.dereference, $.array_access
    ),

    var_def: $ => seq(
      field('type', $.type_spec), commaSep1(field('name', $.name)), ';',
    ),

    typed_lhs: $ => seq(
      field('type', $.type_spec),
      field('name', choice($.name, $.dereference)),
    ),

    maybe_qualified: $ => seq(
      optional($.qualifier),
      choice(
        $.typed_lhs,
        $.lhs_tuple,
        $.name,
      ),
    ),

    left_hand_side: $ => choice(
      $.maybe_qualified,
      $.target,
      $.struct_select,
      $.bit_select,
      $.bit_concat,
    ),

    argument_list: $ => seq('(', optional(commaSep1($.expression)), ')'),

    func: $ => prec(PREC.call, seq(
      field('name', choice($.name, $.dereference)),
      $.argument_list,
    )),

    func_expr: $ => seq($.func, ';'),

    getter: $ => seq(
      $.type_spec,
      field('name', choice($.name, $.dereference)),
      field('arguments', optional(seq('[', optional(commaSep1(seq($.type_spec, $.name))), ']'))),
      $.block
    ),

    setter: $ => seq(
      field('name', choice($.name, $.dereference)),
      field('arguments', seq('[', optional(commaSep1(seq($.type_spec, $.name))), ']')), '=', $.type_spec, field('set_name', $.name), $.block
    ),

    argument_list_def: $ => seq('(', optional(commaSep1(seq($.type_spec, $.name))), ')'),

    return_tuple: $ => seq('(', commaSep(choice($.type_spec, $.return_tuple)), ')'),

    func_def: $ => seq(
      optional(choice($.type_spec, $.return_tuple)), choice($.name, $.dereference), $.argument_list_def, $.block
    ),

    array_variable: $ => seq(
      'array', '[', $.expression, '..', $.expression, ']', 'of', $.type_spec, choice($.name, $.dereference),
    ),

    array_definition: $ => seq(
      'array', $.type_spec, choice($.name, $.dereference), '[', $.expression, '..', $.expression, ']', ';',
    ),

    array_access: $ => seq(
      field('name', choice($.name, $.dereference)),
      field('arguments', seq('[', optional(commaSep1($.expression)), ']')),
    ),

    struct_select: $ => seq(
      choice($.dereference, $.name, $.array_access), '.', '<', commaSep1($.name), '>'
    ),

    bit_select: $ => prec(PREC.bitselect, seq(
      $.expression, token.immediate('<'), field('bit_select_inner', commaSep1($.expression)), alias(token(prec(1, '>')), '>'),
    )),

    enum_def: $ => seq(
      'enumeration', $.name, '{', field('enum_variant', commaSep1($.name)), optional(','), '}', ';'
    ),

    set: $ => seq(
      '{', commaSep1(choice($.expression)), '}'
    ),

    array_like_literal: $ => seq(
      '[', commaSep1(choice($.expression)), ']'
    ),

    type_alias: $ => seq(
      'type',
      choice(
        $.name,
        $.dereference
      ),
      '=',
      choice(
        $.type_spec,
        $.return_tuple,
      ),
      ';'
    ),

    type_definition: $ => seq(
      'type', $.name, 'is', '(',
      field('fields', commaSep1(choice(seq(
        $.type_spec, $.name,
      ), $.array_variable))),
      optional(','),
      ')', optional(';')
    ),

    bit_layout_handler: $ => seq('then', 'do', optional($.name), $.indented),

    bit_layout_spec: $ => seq($.name, ':', $.integer),

    bitlayout: $ => seq(
      'bitlayout', $.name, optional('is'), '(',
      commaSep1(choice(
        $.bitstring,
        $.bit_layout_spec,
      )), optional(','),
      ')', optional(';'),
      optional($.bit_layout_handler), 
    ),

    assignment: $ => seq(
      $.left_hand_side,
      '=',
      $.expression,
      ';',
    ),

    comparison_operator: $ => prec.left(PREC.compare, seq(
      $.expression,
      choice(
        '<',
        '<=',
        '==',
        '!=',
        '>=',
        '>',
        'IN',
      ),
      $.expression,
    )),

    boolean_operator: $ => choice(
      prec.left(1, seq(
        field('left', $.expression),
        field('operator', '&&'),
        field('right', $.expression),
      )),
      prec.left(1, seq(
        field('left', $.expression),
        field('operator', '||'),
        field('right', $.expression),
      )),
    ),

    boolean_value: _ => choice('TRUE', 'FALSE'),

    binary_operator: $ => {
      const table = [
        [prec.left, '+', PREC.plus],
        [prec.left, '-', PREC.plus],
        [prec.left, ':', PREC.concat],
        [prec.left, '+:', PREC.power],
        [prec.left, '*', PREC.times],
        [prec.left, 'DIV', PREC.times],
        [prec.left, '/', PREC.times],
        [prec.left, 'MOD', PREC.times],
        [prec.left, '^', PREC.power],
        [prec.left, '>>', PREC.shift],
        [prec.left, '<<', PREC.shift],
        [prec.left, 'EOR', PREC.shift],
        [prec.left, 'AND', PREC.shift],
        [prec.left, 'OR', PREC.shift],
      ];

      return choice(...table.map(([fn, operator, precedence]) => fn(precedence, seq(
        field('left', $.expression),
        field('operator', operator),
        field('right', $.expression),
      ))));
    },

    comment: _ => token(choice(
      seq('//', /.*/),
      seq(
        '/*',
        /[^*]*\*+([^/*][^*]*\*+)*/,  // not greedy
        '/'
      )
    )),
  }
});

/**
 * Creates a rule to match one or more of the rules separated by a comma
 *
 * @param {RuleOrLiteral} rule
 *
 * @returns {SeqRule}
 */
function commaSep1(rule) {
  return sep1(rule, ',');
}

/**
 * Creates a rule to match one or more occurrences of `rule` separated by `sep`
 *
 * @param {RuleOrLiteral} rule
 *
 * @param {RuleOrLiteral} separator
 *
 * @returns {SeqRule}
 */
function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

function commaSep(rule) {
  return sep(rule, ',');
}

function sep(rule, separator) {
  return seq(rule, repeat1(seq(separator, rule)));
}
