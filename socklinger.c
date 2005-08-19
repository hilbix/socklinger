/* $Header$
 *
 * Execute quick hack shell scripts connected to a socket.
 * This is now based on tinolib.  Only the first version was independent.
 *
 * Copyright (C)2004-2005 by Valentin Hilbig
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Log$
 * Revision 1.3  2005-08-19 04:26:39  tino
 * release socklinger 1.3.0
 *
 * Revision 1.2  2004/08/16 14:03:16  Administrator
 * Support for handling more than one connection (a fixed number) in parallel
 *
 * Revision 1.1  2004/08/16 01:29:57  Administrator
 * initial add
 */

#include "tino/sock.h"
#include "tino/privilege.h"
#include "tino/getopt.h"
#include "tino/proc.h"

#include "socklinger_version.h"

static int
socklinger(int fi, int fo, char **argv)
{
  struct linger         linger;
  int			on;
  char			buf[BUFSIZ*10];
  int			n;

  /* set some default socket options
   */
  linger.l_onoff        = 1;
  linger.l_linger       = 65535;
  if (setsockopt(fo, SOL_SOCKET, SO_LINGER, &linger, sizeof linger) && fo!=1)
    return 1;

  on    = 102400;
  if (setsockopt(fi, SOL_SOCKET, SO_RCVBUF, &on, sizeof on) && fi)
    return 1;
  on    = 102400;
  if (setsockopt(fo, SOL_SOCKET, SO_SNDBUF, &on, sizeof on) && fo!=1)
    return 1;

  /* fork off the child
   * Warning: stderr(2) stays as it is,
   * so it might be the socket (inetd-mode)
   * or the tty (acceptloop mode).
   */
  tino_wait_child(tino_fork_exec(fi, fo, -1, argv), -1l, NULL);

  /* Now the socket belongs to us, and nobody else
   * Shutdown the writing side,
   * and flush the reading side
   * until the other side closes the socket, too.
   *
   * Thus, do a typical lingering.
   */
  if (fo!=fi)
    close(fo);
  if (shutdown(fi, SHUT_WR) && errno!=ENOTCONN)
    return 1;
  while ((n=read(fi, buf, sizeof buf))!=0)
    if (n<0)
      if (errno!=EINTR && errno!=EAGAIN)
	return 1;
  return 0;
}

static void
socklinger_accept(int sock, char **argv)
{
  long	pid;

  pid	= getpid();
  for (;;)
    {
      int	fd;

      printf("[%05ld] socket wait %s\n", pid, argv[1]);
      fd	= accept(sock, NULL, NULL);
      if (fd<0)
	{
	  if (errno==EINTR)
	    continue;
	  tino_exit("accept");
	}
      printf("[%05ld] run program for connection %d\n", pid, fd);
      if (socklinger(fd, fd, argv+2))
        perror("socklinger");
      if (close(fd))
	tino_exit("close");
    }
}

int
main(int argc, char **argv)
{
  int	sock;
  long	cnt;
  char	*end;

  
  if (argc<3)
    {
      fprintf(stderr, "Usage: %s [n@]-|[host]:port|unixsocket program [args...]\n"
		"\tVersion " SOCKLINGER_VERSION " compiled at " __DATE__ "\n"
		"\tYou probably need this for simple TCP shell scripts:\n"
		"\tif - or '' is given, use stdout(!) as socket (inetd/tcpserver)\n"
		"\telse the given socket (TCP or unix) is opened and listened on\n"
		"\tand each connection is served by the progam (max. n in parallel):\n"
		"\tExec program with args with stdin/stdout connected to socket\n"
		"\tWait for program termination.\n"
		"\tShutdown the stdout side of the socket.\n"
		"\tWait (linger) on stdin until EOF is received.\n", argv[0]);
      return 1;
    }
  if (!*argv[1] || !strcmp(argv[1],"-"))
    {
      tino_privilege_end_elevation();
      socklinger(0, 1, argv+2);
      return 0;
    }
  if ((cnt=strtol(argv[1],&end,0))>0 && *end=='@')
    argv[1]	= end+1;

  sock	= tino_sock_tcp_listen(argv[1]);
  /* give up effective UID
   * against privilege escalation
   */
  tino_privilege_end_elevation();
  if (cnt<=1)
    socklinger_accept(sock, argv);
  else
    {
      int	n;
      pid_t	pid;

      for (n=cnt; --n>=0; )
	if (!fork())
	  socklinger_accept(sock, argv);
      while ((pid=wait(NULL))==(pid_t)-1)
	if (errno!=EINTR && errno!=EAGAIN)
	  tino_exit("main-wait");
      tino_exit("child came home");
    }
  return 0;
}
