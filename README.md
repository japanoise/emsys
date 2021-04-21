# emsys - emacs-family editor for msys

[Gomacs][gomacs]' little sister - lighter, more compatible. As before, based
on the [tutorial][tutorial].

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

