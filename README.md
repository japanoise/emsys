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

## Why you might hate it

emsys is, like many in the ersatz-emacs sphere, "religious". This means:

* There's no configuration except through editing the source code (I may go for
  a config.h header like suckless)
* Lines in files are always separated by one linefeed (\n, ^J), including a
  final LF at the end of the file.
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

## Usage

`emsys` is not a 'modal' editor - text entered goes straight into the buffer,
without having to enter an insert mode.

Keybindings below are described in the usual Emacs format (`C-x` means
Control-x, `M-x` means Alt-x or Escape then x).

### Emacs Jargon

* Buffer = An open file or pseudo-file which can be typed into
* Window = A section on the screen displaying a buffer
* Mark = The start or end of the region (where the end or start is the
  current cursor position)
* Region = The area between the cursor and the mark

### Basic Commands

* `C-x C-s` - Save buffer
* `C-x C-f` - Open file
* `C-x C-c` - Quit
* `M-x ...` - Run named command
* `M-x version` - Display version information

### Cursor

* `C-n` or DOWN - Move cursor to *n*ext line
* `C-p` or UP - Move cursor to *p*revious line
* `C-f` or RIGHT - Move cursor *f*orward
* `C-b` or LEFT - Move cursor *b*ackward
* `M-f` - Move cursor *f*orward word
* `M-b` - Move cursor *b*ackward word
* `M-n` - Move cursor to *n*ext paragraph
* `M-p` - Move cursor to *p*revious paragraph
* `M-<` - Move cursor to start of buffer
* `M->` - Move cursor to end of buffer
* `C-a` or HOME - Move cursor to start of line
* `C-e` or END - Move cursor to end of line
* `C-v` or PGDN - Move cursor down a page/screen
* `C-z` or `M-v` or PGUP - Move cursor up a page/screen
* `C-s` - *S*earch
* `M-g` - *G*oto line number

### Text Editing

* `C-_` - Undo (this is Control-/ on most terminals)
*  `C-x C-_` - Redo (keep pressing `C-_` to keep redoing)
* `C-@` - Set mark (this is Control-SPACE on most terminals)
* `C-w` - Kill (cut) current region
* `M-w` - Copy current region
* `C-y` - Yank (paste)
* `M-|` - Filter region through shell command. This even works with pipes!
* `C-j` - Insert a newline and indent (indentation copied from previous line)
* BACKSPACE - Delete backwards
* `C-d` or DELETE - Delete forwards
* `M-BACKSPACE` - Delete backwards word
* `M-d` - Delete forwards word
* `C-u` - Delete to beginning of line
* `C-k` - Delete to end of line
* `M-%` - Query replace, that is, interactive search and replace. Emsys' version
  works very much like [GNU Emacs'][queryreplace], save for the fact that we
  don't have recursive editing, so `C-r` just replaces the current occurrence
  with the string prompted for without changing the replacement.
* `M-x replace-string` - Replace one string with another in the region
* `M-x indent-tabs` - Use tabs for indentation in current buffer (the default)
* `M-x indent-spaces` - Use spaces for indentation in current buffer. You will
  be prompted for the number of spaces to use.
* `M-x whitespace-cleanup` - Cleanup whitespace in current buffer. Beware, this
  clears the undos and redos.
* `C-t` - Transpose (swap) characters around cursor, e.g. `a|b` -> `b|a`
* `M-t` - Transpose words
* `M-u` - Uppercase word (`foo` -> `FOO`)
* `M-l` - Lowercase word (`FOO` -> `foo`)
* `M-c` - Capitalize word (`foo` -> `Foo`)
* `C-q` - Insert next character raw (allowing you to enter e.g. raw control
  characters - be careful with nulls!)

Note that commands dealing with capitalization only work for ASCII letters - any
other characters will be ignored.

If you want to change how paragraph and word endings are calculated, edit the
file `bound.c`.

[queryreplace]: https://www.gnu.org/software/emacs/manual/html_node/emacs/Query-Replace.html

### Windows

* `C-x b` - Switch buffer
* `C-x o` - Switch window
* `C-x 0` - Kill current window
* `C-x 1` - Kill other windows (make current the only *one*)
* `C-x 2` - Create new window
* `C-l` - Center cursor in window

### Advanced

* `C-x (` - Start defining keyboard macro
* `C-x )` - Stop defining keyboard macro
* `C-x e` - Execute macro (stopping definition if currently defining) - press
  `e` again to repeat the macro
* `C-x C-z` - Suspend emsys. Most of the time this will take you back to the
  shell, where you can run `fg` to return emsys to the *f*ore*g*round.
* `M-x revert` - Reload file on disk into current buffer. Useful if you want to
  use `sed`, `go fmt`, etc. to do text operations on files.
* `C-x =` - Describe cursor position (displays information about character at
  point)

## Forks

* [By Nicholas Carroll](https://github.com/nicholascarroll/emsys) - personal
  preference changes, pre-built Windows executable, some 'CUA'-type bindings
  (`C-v` for yank/paste, `C-z` for undo).

## Copying

Licensed MIT.
