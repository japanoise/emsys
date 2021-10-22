#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "emsys.h"
#include "region.h"
#include "pipe.h"

static uint8_t *cmd;
static char *buf;

static uint8_t *transformerPipeCmd(uint8_t *input) {
	struct timeval tv;
	int pin[2], pout[2], perr[2], nr = 1, nerr = 1, nw, written;
	int iw = 0, closed = 0, exstatus;
	char *ebuf = NULL;
	/* Filepos auxp; */
	fd_set fdI, fdO;
	pid_t pid = -1;

	/* XXX: Returning input is potentially unsafe (double-free?) */
	if(cmd[0] == '\0')
		return input;
	if(pipe(pin) == -1)
		return input;
	if(pipe(pout) == -1) {
		close(pin[0]);
		close(pin[1]);
		return input;
	}
	if(pipe(perr) == -1) {
		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		return input;
	}

	/* i_sortpos(&fsel, &fcur); */

	/* Things I will undo or free at the end of this function */
	/* s = i_gettext(fsel, fcur); */

	if((pid = fork()) == 0) {
		dup2(pin[0], 0);
		dup2(pout[1], 1);
		dup2(perr[1], 2);
		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		close(perr[0]);
		close(perr[1]);
		/* I actually like it with sh so I can input pipes et al. */
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		fprintf(stderr, "sandy: execl sh -c %s", cmd);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}

	if(pid > 0) {
		close(pin[0]);
		close(pout[1]);
		close(perr[1]);

		fcntl(pin[1], F_SETFL, O_NONBLOCK);
		fcntl(pout[0], F_SETFL, O_NONBLOCK);
		fcntl(perr[0], F_SETFL, O_NONBLOCK);

		ebuf = calloc(1, BUFSIZ + 1);

		FD_ZERO(&fdO);
		FD_SET(pin[1], &fdO);
		FD_ZERO(&fdI);
		FD_SET(pout[0], &fdI);
		FD_SET(perr[0], &fdI);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		nw = strlen((char*)input);
		while(select(FD_SETSIZE, &fdI, &fdO, NULL, &tv) > 0 &&
		     (nw > 0 || nr > 0)) {
			fflush(NULL);
			if(FD_ISSET(pout[0], &fdI) && nr > 0) {
				nr = read(pout[0], buf, BUFSIZ);
				if(nr >= 0)
					buf[nr] = '\0';
				else
					break; /* ...not seen it yet */

				if (nr) {
					editorSetStatusMessage("nr %i buf %s", nr, buf);
				}
			} else if(nr > 0) {
				FD_SET(pout[0], &fdI);
			} else {
				FD_CLR(pout[0], &fdI);
			}
			if(FD_ISSET(perr[0], &fdI) && nerr > 0) {
				/* Blatant TODO: take last line of stderr and copy as tmptitle */
				ebuf[0] = '\0';
				nerr = read(perr[0], ebuf, BUFSIZ);
				if(nerr == -1)
					editorSetStatusMessage("WARNING! command reported an error!!!");
				if(nerr < 0)
					break;
			} else if(nerr > 0) {
				FD_SET(perr[0], &fdI);
			} else {
				FD_CLR(perr[0], &fdI);
			}
			if(FD_ISSET(pin[1], &fdO) && nw > 0) {
				written = write(pin[1], &(input[iw]),
				    (nw < BUFSIZ ? nw : BUFSIZ));
				if(written < 0)
					break; /* broken pipe? */
				iw += (nw < BUFSIZ ? nw : BUFSIZ);
				nw -= written;
			} else if(nw > 0) {
				FD_SET(pin[1], &fdO);
			} else {
				if(!closed++)
					close(pin[1]);
				FD_ZERO(&fdO);
			}
		}
		free(ebuf);
		if(!closed)
			close(pin[1]);
		waitpid(pid, &exstatus, 0); /* We don't want to close the pipe too soon */
		close(pout[0]);
		close(perr[0]);
	} else {
		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		close(perr[0]);
		close(perr[1]);
	}
	return (uint8_t*) buf;
}

void editorPipe(struct editorConfig *ed, struct editorBuffer *bf) {
	buf = calloc(1, BUFSIZ + 1);
	cmd = NULL;
	cmd = editorPrompt(bf, (uint8_t*)"Pipe command: %s", NULL);
	if (cmd == NULL) {
		editorSetStatusMessage("Canceled pipe command.");
		return;
	}

	editorTransformRegion(ed, bf, transformerPipeCmd);

	free(cmd);
}
