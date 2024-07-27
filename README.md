# emsys - ersatz-emacs text editor

An ersatz-emacs text editor for unix-like systems (and msys, hence the name!)
with no dependencies. It's not much, but it punches above its weight class!

## Pros

* Small terminal executable editor with Emacs keybindings
* Supports many useful Emacs features like infinite undo, filtering regions,
  keyboard macros, etc.
* Simple codebase based on the venerable tutorial [Build Your Own Text
  Editor][tutorial].
* Decent UTF-8 support out of the box with no configuration.
* No dependencies, just a VT100/xterm-type terminal and termios.h to build.
* C99 and POSIX 2001 compliant

## Cons

* No configuration except through editing the source code
* Lines in files are always separated by one linefeed (\n, ^J), including a
  final LF at the end of the file.
* Files and keyboard input are always in the One True Encoding, UTF-8.
* Files are plain text and do not contain nulls (though they may contain other
  control characters)
* Files are loaded entirely into memory using the "array of lines" data
  structure, making emsys unsuitable for large files or those that don't fit in
  memory (use [joe](https://joe-editor.sourceforge.io/) in Emacs emulation mode)

## Anti-features

- Syntax highlighting - too complex, would bloat the executable. I am open to a
  good PR, though.
- Vi(m) keys - just use vim. If you care about space or dependencies or
  whatever, you can use elvis or busybox vi or something. If you fork and make a
  vi version, however, open a ticket and I will link it here.
- Lisp - just use [Real Emacs™](https://www.gnu.org/software/emacs/) or
  [Gomacs][gomacs].
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
* Point = The position of the cursor in the file
* Mark = A position in the file marked using `C-@`
* Region = The area between point and mark

### Basic Commands

* `C-x C-s` - Save buffer
* `C-x k`   - Kill buffer
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
* `C-x C-x` - Swap mark and point
* `C-x h` - Mark the entire buffer
* `C-w` - Kill (cut) current region
* `M-w` - Copy current region
* `C-y` - Yank (paste)
* `M-|` - Filter region through shell command. This even works with pipes!
* `C-j` - Insert a newline and indent (indentation copied from previous line)
* `C-o` - Insert a newline but do not move the point
* `C-i` (TAB) - Indent current line by either one tab or the current space
  indentation level. To insert a literal tab, `C-q C-i`
* SHIFT-TAB/BACKTAB - Unindent current line by either one tab or the current
  space indentation level.
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
* `M-x replace-regexp` - Replace first match of given regular expression per
  line in the region with given string
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
* `M-/` - Autocomplete current "word" (one or more alphanumeric or unicode
  characters). E.g. `foo -> foobar`.

Note that commands dealing with capitalization only work for ASCII letters - any
other characters will be ignored.

If you want to change how paragraph and word endings are calculated, edit the
file `bound.c`.

[queryreplace]: https://www.gnu.org/software/emacs/manual/html_node/emacs/Query-Replace.html

### Windows

* `C-x b` - Switch buffer
* `C-x <left>` - Previous buffer
* `C-x <right>` - Next buffer
* `C-x o` - Switch window
* `C-x 0` - Kill current window
* `C-x 1` - Kill other windows (make current the only *one*)
* `C-x 2` - Create new window
* `C-l` - Center cursor in window
* `M-x toggle-truncate-lines` or `C-x x t` - Toggles line wrap off/on

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
* `M-0` to `M-9` - Type in universal argument (in most cases, this just repeats
  the next command typed N times). E.g. `M-8 M-0 *` will type 80 asterisks.
  
#### Registers

Registers let you store a point, string (region), rectangle (see below), number,
or keyboard macro. Every register has a name, which must be one ASCII character
(including control characters, except `C-g` since that's the universal cancel
button).

* `C-x r j` - *J*ump to point stored in register
* `C-x r a` - Store m*a*cro in register. Also `C-x r m`, for now at least.
* `C-x r n` - Store *n*umber (i.e. universal argument entered with alt-numbers)
  in register.
* `C-x r C-@` or `C-x r SPACE` - Store point in register.
* `C-x r s` - Store region (*s*tring) in register.
* `C-x r +` - Increment number in register, or add region to string in register.
* `C-x r v` or `M-x view-register` - View contents of register.
* `C-x r r` - Copy rectangle to register.

#### Rectangles

Rectangles are the rectangular space between point and mark; all characters from
the leftmost column to the rightmost column within the topmost line to the
bottommost line.

* `C-x r t` - string-rectangle: Replace rectangle contents with given string on
  each line.
* `C-x r k` or `C-x r C-w` - kill-rectangle: Kill (cut) rectangle.
* `C-x r M-w` - copy-rectangle: Copy rectangle
* `C-x r y` - yank-rectangle: Yank (paste) rectangle.
* `C-x r r` - rectangle-to-register: Copy rectangle to register.

### Regular Expression Syntax

emsys uses [kokke's tiny-regex-c][tiny-regex] for regular expression support,
and has the following syntax for regular expressions:

  -  `.`         Dot, matches any character
  -  `^`         Start anchor, matches beginning of string
  -  `$`         End anchor, matches end of string
  -  `*`         Asterisk, match zero or more (greedy)
  -  `+`         Plus, match one or more (greedy)
  -  `?`         Question, match zero or one (non-greedy)
  -  `[abc]`     Character class, match if one of {'a', 'b', 'c'}
  -  `[^abc]`   Inverted class, match if NOT one of {'a', 'b', 'c'}
  -  `[a-zA-Z]` Character ranges, the character set of the ranges { a-z | A-Z }
  -  `\s`       Whitespace, \t \f \r \n \v and spaces
  -  `\S`       Non-whitespace
  -  `\w`       Alphanumeric, [a-zA-Z0-9_]
  -  `\W`       Non-alphanumeric
  -  `\d`       Digits, [0-9]
  -  `\D`       Non-digits

[tiny-regex]: https://github.com/kokke/tiny-regex-c

## Forks

* [By Nicholas Carroll](https://github.com/nicholascarroll/emsys) 

## Copying

Licensed MIT.
