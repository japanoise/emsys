#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<termios.h>
#include<unistd.h>

#define ESC "\033"
#define CSI ESC"["
#define CRLF "\r\n"
#define ISCTRL(c) ((0 < c && c < 0x20) || c == 0x7f)

struct termios orig_termios;

void die(const char *s) {
        perror(s);
        exit(1);
}

void disableRawMode() {
        if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios)==-1)
                die("disableRawMode tcsetattr");
        if(write(STDOUT_FILENO, CSI"?1049l", 8)==-1)
                die("disableRawMode write");
}

void enableRawMode() {
        /* Saves the screen and switches to an alt screen */
        if(write(STDOUT_FILENO, CSI"?1049h", 8)==-1)
                die("disableRawMode write");
        /* 
         * I looked into it. It's possible, but not easy, to do it
         * without termios. Basically you'd have to hand-hack and send
         * off your own bits. Check out busybox vi and that rabbithole
         * for an implementation. 
         */
        if(tcgetattr(STDIN_FILENO, &orig_termios)==-1)
                die("tcgetattr");
        atexit(disableRawMode);

        struct termios raw = orig_termios;
        raw.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
        if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)==-1)
                die("enableRawMode tcsetattr");
}

int main(int argc, char *argv[]) {
        enableRawMode();
        char c;
        while (1) {
                if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
                        die("read");
                if (ISCTRL(c)) {
                        printf("%d"CRLF, c);
                } else {
                        printf("%d ('%c')"CRLF, c, c);
                }
                if (c == 'q') break;
        }
        return 0;
}

