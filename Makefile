BIN     := armlet
SRC_DIR := source
OBJDIR  := build/objs
DEPDIR  := build/deps

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRCS = main.c $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*/*.c)
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
DEPS = $(patsubst %.c,$(DEPDIR)/%.d,$(SRCS))

CFLAGS += -ggdb -Wall -Wextra -Wformat=2 -Wformat-security \
          -fstack-protector-strong -Wno-unused-parameter

EXTRA_LIBS =

# Optional tree-sitter syntax highlighting: make HAVE_TREE_SITTER=1
ifdef HAVE_TREE_SITTER
  CFLAGS += -DHAVE_TREE_SITTER
  TS_SRCS = tree-sitter/src/parser.c tree-sitter/src/scanner.c
  TS_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(TS_SRCS))
  TS_DEPS = $(patsubst %.c,$(DEPDIR)/%.d,$(TS_SRCS))
  EXTRA_LIBS += $(shell pkg-config --libs tree-sitter)
else
  TS_OBJS =
  TS_DEPS =
endif

all: $(BIN)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@) $(dir $(DEPDIR)/$*.d)
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEPDIR)/$*.d -c $< -o $@

$(OBJDIR)/tree-sitter/src/%.o: tree-sitter/src/%.c
	@mkdir -p $(dir $@) $(dir $(DEPDIR)/tree-sitter/src/$*.d)
	$(CC) $(CFLAGS) -w -MMD -MP -MF $(DEPDIR)/tree-sitter/src/$*.d -c $< -o $@

$(BIN): $(OBJS) $(TS_OBJS)
	$(CC) $(CFLAGS) -std=c11 $^ \
	  -lm $(shell pkg-config --libs gmp) \
	  $(shell pkg-config --libs ncurses) \
	  $(EXTRA_LIBS) \
	  -Wl,-z,relro,-z,now \
	  -o $@

install: $(BIN)
	install -d '$(DESTDIR)$(BINDIR)'
	install -m755 $(BIN) '$(DESTDIR)$(BINDIR)'/$(BIN)

clean:
	$(RM) -r $(OBJDIR) $(DEPDIR) $(BIN)

-include $(DEPS) $(TS_DEPS)

.PHONY: all install clean
