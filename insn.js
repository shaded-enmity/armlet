var PART_TYPE_BITS = 0, PART_TYPE_REGISTER = 1,
    PART_TYPE_OPCODE = 2, PART_TYPE_OTHER = 3;

function DefaultRenderer() {}
DefaultRenderer.prototype = {
  constructor: DefaultRenderer(),
  /* render single tick */
  tick: function(name) {
    return '<div class="insn-' + name + '-tick-container">' +
             '<div class="insn-' + name + '-tick"></div>' +
           '</div>';
  },
  /* render top, middle and bottom ticks between instructions */
  ticks: function() {
    return this.tick('top') + this.tick('middle') + this.tick('bottom');
  },
  /* filler space for nodes without ticks */
  filler: function () {
    return '<div class="insn-filler">&nbsp;</div>';
  },
  /* header block with given value */
  header_block: function(value) {
    return '<div class="insn-no-space insn-block insn-height-30">' + value + '</div>';
  },
  /* place label in the middle of instruction part
     size is the number of bits in the instruction part */
  label: function(size, value) {
    return '<div class="insn-block-label insn-height-70" style="width: ' + (size * 1.5) + 'em">' + value + '</div>';
  },
  /* bit block with given bit value and color class */
  bit_block: function(color, value) {
    return '<div class="insn-no-space insn-block insn-height-70 ' + color + '">' + value + '</div>';
  },
  /* add instruction part */
  part: function (parent, classes, inner) {
    return parent.append('<div class="insn-border insn-no-space insn-block insn-part ' + classes + '">' + inner() + '</div>');
  }
};

function Instruction(parts, hextet) {
  this.parts = [];
  if (parts !== undefined) {
    this.add_parts(parts);
  }
  if (hextet === undefined) {
    this.hextet_header = false;
  } else {
    this.hextet_header = hextet;
  }
}
Instruction.prototype = {
  constructor: Instruction,
  /* add instruction parts and update total size */
  add_parts: function(parts) {
    this.parts = this.parts.concat(parts);
    this.size = this.parts.reduce(function(p, c) {
      return p + c.size;
    }, 0);
  },
  /* render instruction in parent using renderer */
  render: function(parent, renderer) {
    if (renderer === undefined) {
        renderer = new DefaultRenderer();
    }

    var bitcount = this.size, first = false;
    for (var i = this.parts.length-1; i>=0; i--) {
      bitcount -= this.parts[i].size;
      var c = bitcount;
      /* offset by -16 if hextet header is set and we're over 16 bits */
      c -= 16 * (this.hextet_header && c > 15);
      this.parts[i].render(parent, c, renderer, first);
      /* mark that we're past first element */
      first = true;
    }
  }
}

function InstructionPart(type, size, data) {
  this.type = type;
  this.size = size;
  this.data = data;
}
InstructionPart.prototype = {
  constructor: InstructionPart,
  /* maps each PART_TYPE_* to two distinct color classes */
  color_map: [
    ["insn-even-bit", "insn-odd-bit"],
    ["insn-even-register", "insn-odd-register"],
    ["insn-even-opcode", "insn-odd-opcode"],
    ["insn-even-other", "insn-odd-other"]
  ],
  render: function(parent, start, renderer, openLeft, openRight, renderAll) {
    if (openLeft === undefined) {
      openLeft = false;
    }
    if (openRight === undefined) {
      openRight = false;
    }
    if (renderAll === undefined) {
      renderAll = false;
    }

    var colors = this.color_map[this.type], first_val = "",
        last_val = "", fix_size = this.size & 1 ? 1 : 0, i = 1,
        _s = this.size, _t = this.type, _d = this.data;
    if (this.type == PART_TYPE_BITS) {
      /* we're rendering bits */
      if (this.size > 1) {
        /* the size is 2+, meaning that we have enough data
           to set first and last value */
        first_val = this.data[0];
        last_val = this.data[this.data.length - 1];
      } else {
        /* render a single bit */
        renderer.part(parent, openLeft ? ' insn-no-left' : '', function() {
            return renderer.filler() + renderer.header_block(start+(_s-1)) +
              renderer.bit_block(colors[0+fix_size], _d[0])
        });
        return;
      }
    } else {
      /* handle single bit wide register, opcode or other */
      if (this.size == 1) {
        renderer.part(parent, openLeft ? ' insn-no-left' : '', function() {
            return renderer.filler() + renderer.header_block(start+(_s-1)) +
              renderer.bit_block(colors[0+fix_size], '') + renderer.label(_s, _d)
        });
        return;
      }
    }

    /* render first */
    renderer.part(parent, openLeft ? ' insn-no-right insn-no-left' : ' insn-no-right', function() {
      return renderer.ticks() + renderer.header_block(start + (_s - 1)) +
        renderer.bit_block(colors[0 + fix_size], first_val) +
        (_t != PART_TYPE_BITS ? renderer.label(_s, _d) : '')
    });

    for (i=(this.size - 1); i > 1 ; i--) {
      /* we render left->right but bits are specified right->left */
      var block_value = function() {
        return _t == PART_TYPE_BITS ? _d[(_s - 1) - i + 1] : ''
      };
      var header_value = function() {
        return renderAll || _t == PART_TYPE_BITS ? (start + i - 1) : ''
      };
      /* render intermediary nth */
      renderer.part(parent, 'insn-no-sides', function() {
        return renderer.ticks() + renderer.header_block(header_value()) +
          renderer.bit_block(colors[i & 1], block_value());
      });
    }

    /* render last */
    renderer.part(parent, openRight ? ' insn-no-sides' : ' insn-no-left', function() {
      return renderer.filler() + renderer.header_block(start) +
        renderer.bit_block(colors[i & 1], last_val)
    });
  }
};
