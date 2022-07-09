# Hacking on emsys

## Debugging

Run `make clean && make debug` to get a build with debug symbols for use with
your favorite debugger.

gcc knows roughly what to do with programs that mess with the terminal, so don't
be afraid to `gdb ./emsys` with a debug build. When it crashes or hits your
breakpoint, it will unmess with the terminal for you. If it still looks fucky,
run `shell reset`.

emsys has the following preprocessor `#define`s to help you with various
scenarios:

* `EMSYS_DEBUG_UNDO` - Shows the current undo in the status bar. Define
  `EMSYS_DEBUG_REDO` *as well* to show redos instead.
* `EMSYS_DEBUG_MACROS` - Shows the current macro in the status bar. Note that
  this can get long and cause you to crash with stack smashing detected, so you
  may want to bump the size of the status bar macro.
* `EMSYS_DEBUG_PROMPT` - Shows function variables when `editorPrompt` is shown.

## Static builds

You can build emsys statically with musl using (arch linux with [just][just]):

1. `just install musl`
2. `make clean && make CC="musl-gcc -static"`
3. `ldd ./emsys` should say "not a dynamic executable"

It should now be a static executable you can safely copy to other distros and
expect to Just Work.

[just]: https://github.com/japanoise/neo-dotfiles/blob/master/bin/just

## Generated code

The emoji block in wcwidth.c is generated like such:

    grep 'Basic_Emoji' < txt/emoji-sequences.txt | sed -e '/^#/d' -e '/ FE0F/d' -e 's/ .*//' -e 's/^\([A-Fa-f0-9]*\)$/ucs == 0x\1 ||/' -e 's/^\([A-Fa-f0-9]*\)..\([A-Fa-f0-9]*\)$/(0x\1 <= ucs \&\& ucs <= 0x\2) ||/'

emoji-sequences.txt was retrieved from [unicode.org](https://www.unicode.org/Public/emoji/13.1/)
