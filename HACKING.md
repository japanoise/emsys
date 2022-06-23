# Hacking on emsys

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
