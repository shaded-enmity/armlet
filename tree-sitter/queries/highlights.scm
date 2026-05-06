(qualifier) @keyword.storage.modifier

(bits (expression) @variable)

(bit_select bit_select_inner: (expression) @attribute)

[
  (integer)
  (float)
] @constant.numeric

(return_tuple [(type_spec) (integer) (float)] @type.builtin)

(type_spec) @type.builtin

(argument_list (expression) @variable.parameter)

(argument_list_def (name) @variable.parameter)

(boolean_value) @constant.builtin.boolean

(func_def [(name) (dereference)] @function)

(func [(name) (dereference)] @function)

(left_hand_side) @variable

(enum_def enum_variant: (name) @type.enum.variant)

(enum_def (name) @type.enum)

[
  (string)
  (bitstring)
] @string

(interpolation) @string.special
(interpolation (name) @variable)

(getter (name) @variable.parameter)
(getter name: [(name) (dereference)] @function)

(setter (name) @variable.parameter)
(setter name: [(name) (dereference)] @function)

(comment) @comment

(array_access name: [(name) (dereference)] @function)
(array_access arguments: (expression (_)) @variable.parameter)

[
  ";"
  ","
  ":"
  "."
] @punctuation.delimiter

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
]  @punctuation.bracket

[
  "-"
  "!="
  "*"
  "&&"
  "||"
  "MOD"
  "DIV"
  "^"
  "+"
  "<"
  "<<"
  "<="
  "="
  "=="
  ">"
  ">="
  ">>"
  "||"
  "IN"
  "EOR"
  "AND"
  "OR"
  ":"
  "+:"
  "/"
] @operator

(bit_select ["<" ">"] @punctuation.bracket)

[
  "type"
  "assert"
  "for"
  "to"
  "downto"
  "repeat"
  "until"
  "while"
  "if"
  "then"
  "elsif"
  "else"
  "case"
  "of"
  "when"
  "otherwise"
  "return"
  "do"
  "is"
  "enumeration"
  "array"
  "import"
  "use"
  "UNKNOWN"
  "IMPLEMENTATION_DEFINED"
  "SEE"
  "UNDEFINED"
  "UNPREDICTABLE"
  "bitlayout"
] @keyword

((func (name) @function.builtin) (#match? @function.builtin
  "^(print|break|backtrace|inspect|set_bits_range_name|dispatch|serialize|deserialize|bitlayout_to_json|export_bitlayouts_json|begin_implementation_defined|implementation_defined|end_implementation_defined|Log2|Real|RoundUp|RoundDown)$"))
