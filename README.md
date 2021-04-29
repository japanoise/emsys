# emsys - emacs-family editor for msys

[Gomacs][gomacs]' little sister - lighter, more compatible. As before, based
on the [tutorial][tutorial].

## Why you might want to try it

emsys gives you an editor with Emacs keybindings without having to use full-on
Emacs - useful if you're used to the Vi(m) way of relying more on the shell than
the editor's features for advanced tasks, but hate modal editors or prefer Emacs
bindings.

emsys is very hackable. Being based on [Build Your Own Text Editor][tutorial],
the basic internals of the editor are very well documented and a small community
exists around hacking on very similar editors. The code is laid out quite
logically, and I'm working on moving it into separate files with a logical
layout so there's no hunt-the-function rubbish going on. It uses the very simple
and logical "array of rows" data structure rather than the confusing ropes, gap
buffer, or mmap data structures.

emsys supports UTF-8 editing out of the box. UTF-8 bugs are the highest priority
tickets to fix besides crashes or corruption.

emsys is compatible with any unix-like environment that uses VT100/xterm escape
characters. The name actually comes from its intended use-case in my own setup -
as the "em" that will turn up on msys2. msys2 compatibility is thus another high
priority. If you've tried to use Godit, Gomacs, or uemacs on msys2 and run into
trouble, this is the editor for you!

If you care about that sort of thing, emsys is very small - as of 1bd6009 only
41k unstripped on my x86_64 Linux box, a little less than ed (the standard text
editor) but a little more than the world's smallest,
[e](https://github.com/japanoise/e).  A feature planned in future will be
release builds that you can easily wget onto a box run by a luddite sysadmin
that only installed Vi(m).

On topic with that, emsys has very little in the way of dependencies - to build,
just a C compiler, standard library, and a termios - none for runtime. I'm
thinking maybe of using PCRE2 for more advanced features, but that's installed
basically everywhere anyway. Other than that, any new features added will
endeavor to only introduce single-header libraries at most.

Features not supported by current version, but planned:

* Open multiple files
* +linum syntax (like LESSEDIT)
* Vertical splits
* Undo/Redo
* Indentation QOL, including ^J and indent-spaces

## Why you might hate it

emsys is, like many in the ersatz-emacs sphere, "religious". This means:

* There's no configuration except through editing the source code (I may go for
  a config.h header like suckless)
* Files are always separated by one linefeed (\n, ^J), including a final LF at
  the end of the file.
* Files and keyboard input are always in the One True Encoding, UTF-8.
* Files are plain text and do not contain nulls (though they may contain other
  control characters)
* Files are not outrageously huge (there's specialist tools for those)
* Features are "put up or hack up" - I am very welcoming of good PRs. Let's work
  together to get the new feature you wrote into the editor - but I will not do
  it for you.
  
emsys currently has some minor bugs around rendering; nothing serious, they
could likely be fixed by a steady hand that took the time to understand the
code. They don't bother me too much for now, so I'm leaving them alone until
they do.

Features I don't currently plan to support:

- Syntax highlighting - too complex, would bloat the executable. I am open to a
  good PR, though.
- Vi(m) keys - just use vim. If you care about space or dependencies or
  whatever, you can use elvis or busybox vi or something. If you fork and make a
  vi version, however, open a ticket and I will link it here.
- Lisp - just use Real Emacs™ or [Gomacs][gomacs].
- Horizontal splits - it would complicate the rendering code, especially if I
  continue to support multilines. Explore the [Gomacs][gomacs] commit/issue
  history if you don't believe me!

## Installation

### Windows (msys2)

You will need to be running on msys2 proper, not the mingw64 version, because
you need termios (sorry, but it's a must). Install the compliers and libraries:

    pacman -S msys2-devel msys2-runtime-devel
    
Then, `make && make install` and you should be laughing.

### Unix

No dependencies (yet), so long as you have `make` and a C compiler you should
be able to just `make && sudo make install`.

[gomacs]: https://github.com/japanoise/gomacs
[tutorial]: https://viewsourcecode.org/snaptoken/kilo/index.html

## Copying

Licensed MIT.
