/* $Header$
 *
 * Execute quick hack shell scripts connected to a socket.
 *
 * This is now based on tinolib.  Only the first version was
 * independent.
 *
 * This program now is far too complex.  Most shall be done
 * implicitely by tinolib.
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
 * Revision 1.8  2006-12-12 10:36:42  tino
 * See ChangeLog
 *
 * Revision 1.7  2006/02/09 12:53:47  tino
 * one more close was forgotten in the main program
 *
 * Revision 1.6  2006/02/09 11:27:10  tino
 * output generalization and bug fixed as I forgot to
 * close the socket which accepts the connection for the forked program.
 *
 * Revision 1.5  2006/01/25 03:31:13  tino
 * dist 1.3.1
 *
 * Revision 1.4  2005/10/30 03:24:22  tino
 * additional possibilities
 *
 * Revision 1.3  2005/08/19 04:26:39  tino
 * release socklinger 1.3.0
 *
 * Revision 1.2  2004/08/16 14:03:16  Administrator
 * Support for handling more than one connection (a fixed number) in parallel
 *
 * Revision 1.1  2004/08/16 01:29:57  Administrator
 * initial add
 */

#if 0
#define	TINO_DP_all	TINO_DP_ON
#else
#define TINO_DP_main	TINO_DP_OFF
#endif

#include "tino/sock.h"
#include "tino/privilege.h"
#include "tino/getopt.h"
#include "tino/proc.h"
#include "tino/str.h"
#include "tino/hup.h"

#include "socklinger_version.h"

static const char *
note_str(int n, const char *s)
{
  static char	buf[80];
  static long	pid;

  if (!pid)
    pid	= getpid();
  snprintf(buf, sizeof buf, "[%d][%05ld] %s", n, pid, s);
  return buf;
}

static void
note(int n, const char *s, ...)
{
  va_list	list;
  static long	pid;

  if (!pid)
    pid	= getpid();

  printf("%s", note_str(n, ""));
  va_start(list, s);
  vprintf(s, list);
  va_end(list);
  printf("\n");
}

static int
socklinger(int fi, int fo, char **argv, int nr, int sock)
{
  char	buf[BUFSIZ*10], *peer, *name;
  int	n;
  char	*env[4], *cause;
  int	keepfd[2];
  pid_t	pid;

  /* set some default socket options
   */
  if ((tino_sock_linger_err(fo, 65535) && fo!=1) ||
      (tino_sock_rcvbuf_err(fi, 102400) && fi) ||
      (tino_sock_sndbuf_err(fo, 102400) && fo!=1))
    return 1;

  tino_sock_reuse(fi, 1);
  tino_sock_keepalive(fi, 1);
  if (fi!=fo)
    {
      tino_sock_reuse(fo, 1);
      tino_sock_keepalive(fo, 1);
    }
  /* Prepare the environment
   * This is a hack today, shall be done better in future
   */
  peer	= tino_sock_get_peername(fi);
  if (!peer)
    peer	= tino_sock_get_peername(fo);
  name	= tino_sock_get_sockname(fi);
  if (!name)
    name	= tino_sock_get_sockname(fo);
  env[0]	= tino_str_printf("SOCKLINGER_NR=%d", nr);
  env[1]	= tino_str_printf("SOCKLINGER_PEER=%s", (peer ? peer : ""));
  env[2]	= tino_str_printf("SOCKLINGER_SOCK=%s", (name ? name : ""));
  env[3]	= 0;
  if (name)
    free(name);
  if (peer)
    free(peer);
  /* fork off the child
   * Warning: stderr(2) stays as it is,
   * so it might be the socket (inetd-mode) 
  * or the tty (acceptloop mode).
   */
  keepfd[0]	= sock+1;	/* close from 2..sock	*/
  keepfd[1]	= 0;

  tino_hup_ignore(1);

  note(nr, "run %s", argv[0]);
  pid		= tino_fork_exec(fi, fo, 2, argv, env, 1, keepfd);

  /* Free environment and wait for the child
   */
  free(env[2]);
  free(env[1]);
  free(env[0]);

  tino_wait_child_exact(pid, &cause);
  note(nr, "%s, lingering", cause);
  free(cause);

  tino_hup_ignore(0);

  /* Now the socket belongs to us, and nobody else
   * Shutdown the writing side,
   * and flush the reading side
   * until the other side closes the socket, too.
   *
   * Thus, do a typical lingering.
   */
  if (fo!=fi)
    close(fo);
  if (shutdown(fi, SHUT_WR))
    return 1;

  000;	/* add timeout and maximum data amount read away	*/

  while ((n=read(fi, buf, sizeof buf))!=0)
    if (n<0 && (errno!=EINTR && errno!=EAGAIN))
      return 1;
  return 0;
}

static void
socklinger_accept(int sock, char **argv, int n)
{
  tino_hup_start(note_str(n, "HUP received"));
  for (;;)
    {
      int	fd;

      tino_hup_ignore(0);
      note(n, "wait %s", argv[1]);
      fd	= accept(sock, NULL, NULL);
      if (fd<0)
	{
	  if (errno==EINTR)
	    continue;
	  perror(note_str(n, "accept"));
	  sleep(1);
	  continue;
	}
      else if (socklinger(fd, fd, argv+2, n, sock))
        perror(note_str(n, "socklinger"));
      if (close(fd))
	tino_exit(note_str(n, "close"));
    }
}

/* It shall get a real commandline
 */
int
main(int argc, char **argv)
{
  int	sock;
  long	cnt;
  char	*end;

  if (argc<3)
    {
      fprintf(stderr, "Usage: %s [n@][host]:port|[n@]unixsocket|- program [args...]\n"
	      "\tVersion " SOCKLINGER_VERSION " compiled at " __DATE__ "\n"
	      "\tYou probably need this for simple TCP shell scripts:\n"
	      "\tif - or '' is given, use stdout(!) as socket (inetd/tcpserver)\n"
	      "\telse the given socket (TCP or unix) is opened and listened on\n"
	      "\tand each connection is served by the progam (max. n in parallel):\n"
	      "\t1) Exec program with args with stdin/stdout connected to socket\n"
	      "\t   env var SOCKLINGER_NR=-1 (inetd), 0 (single) or instance-nr\n"
	      "\t   env var SOCKLINGER_PEER is set to the peer's IP:port\n"
	      "\t2) Wait for program termination.\n"
	      "\t3) Shutdown the stdout side of the socket.\n"
	      "\t   Wait (linger) on stdin until EOF is received.\n"
	      , argv[0]);
      return 1;
    }
  if (!*argv[1] || !strcmp(argv[1],"-"))
    {
      tino_privilege_end_elevation();
      socklinger(0, 1, argv+2, -1, -1);
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
    socklinger_accept(sock, argv, 0);
  else
    {
      int	n;
      pid_t	pid;

      000;	/* missing: if parent dies, send something like HUP to all childs	*/
      for (n=cnt; n>0; n--)
	if ((pid=fork())==0)
	  socklinger_accept(sock, argv, n);
	else if (pid==(pid_t)-1)
	  tino_exit(note_str(0, "fork"));
      close(sock);	/* socket no more needed below	*/
      while ((pid=wait(NULL))==(pid_t)-1)
	if (errno!=EINTR && errno!=EAGAIN)
	  tino_exit(note_str(0, "main-wait"));
      tino_exit(note_str(0, "child came home"));
    }
  return 0;
}
