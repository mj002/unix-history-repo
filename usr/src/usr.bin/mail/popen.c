/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
static char sccsid[] = "@(#)popen.c	5.11 (Berkeley) %G%";
#endif /* not lint */

#include "rcv.h"
#include <sys/signal.h>
#include <sys/wait.h>

#define READ 0
#define WRITE 1
static int *pid;

FILE *
Popen(cmd, mode)
	char *cmd;
	char *mode;
{
	int p[2];
	int myside, hisside, fd0, fd1;

	if (pid == 0)
		pid = (int *) malloc((unsigned) sizeof (int) * getdtablesize());
	if (pipe(p) < 0)
		return NULL;
	if (*mode == 'r') {
		myside = p[READ];
		fd0 = -1;
		hisside = fd1 = p[WRITE];
	} else {
		myside = p[WRITE];
		hisside = fd0 = p[READ];
		fd1 = -1;
	}
	if ((pid[myside] = start_command(cmd, 0, fd0, fd1, NOSTR)) < 0) {
		close(p[READ]);
		close(p[WRITE]);
		return NULL;
	}
	close(hisside);
	return fdopen(myside, mode);
}

Pclose(ptr)
	FILE *ptr;
{
	int i;
	int omask;

	i = fileno(ptr);
	fclose(ptr);
	omask = sigblock(sigmask(SIGINT)|sigmask(SIGHUP));
	i = wait_command(pid[i]);
	sigsetmask(omask);
	return i;
}

/*
 * Run a command without a shell, with optional arguments and splicing
 * of stdin and stdout.  The command name can be a sequence of words.
 * Signals must be handled by the caller.
 * "Mask" contains the signals to ignore in the new process.
 * SIGINT is enabled unless it's in the mask.
 */
/*VARARGS4*/
run_command(cmd, mask, infd, outfd, a0, a1, a2)
	char *cmd;
	int infd, outfd;
	char *a0, *a1, *a2;
{
	int pid;

	if ((pid = start_command(cmd, mask, infd, outfd, a0, a1, a2)) < 0)
		return -1;
	return wait_command(pid);
}

/*VARARGS4*/
start_command(cmd, mask, infd, outfd, a0, a1, a2)
	char *cmd;
	int infd, outfd;
	char *a0, *a1, *a2;
{
	int pid;

	if ((pid = vfork()) < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		char *argv[100];
		int i = getrawlist(cmd, argv, sizeof argv / sizeof *argv);

		if ((argv[i++] = a0) != NOSTR &&
		    (argv[i++] = a1) != NOSTR &&
		    (argv[i++] = a2) != NOSTR)
			argv[i] = NOSTR;
		if (infd >= 0)
			dup2(infd, 0);
		if (outfd >= 0)
			dup2(outfd, 1);
		for (i = getdtablesize(); --i > 2;)
			close(i);
		for (i = 1; i <= NSIG; i++)
			if (mask & sigmask(i))
				(void) signal(i, SIG_IGN);
		if ((mask & sigmask(SIGINT)) == 0)
			(void) signal(SIGINT, SIG_DFL);
		(void) sigsetmask(0);
		execvp(argv[0], argv);
		perror(argv[0]);
		_exit(1);
	}
	return pid;
}

wait_command(pid)
	int pid;
{
	union wait status;
	int r;

	while ((r = wait(&status)) >= 0 && r != pid)
		;
	if (r < 0)
		return -1;
	if (status.w_status != 0) {
		printf("Fatal error in process.\n");
		return -1;
	}
	return 0;
}
