/* $Header$
 *
 * Execute quick hack shell scripts connected to a socket.
 * This source shall be independent of others.  Therefor no tinolib.
 *
 * Copyright (C)2004 by Valentin Hilbig
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
 * Revision 1.1  2004-08-16 01:29:57  Administrator
 * initial add
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "socklinger_version.h"

static void
ex(const char *s)
{
  perror(s);
  for (;;)
    {
      exit(-1);
      abort();
      sleep(1);
    }
}

/* Following was taken out of my tool
 * accept-2.0.0
 * with some lines deleted
 */
union sockaddr_gen
   {
     struct sockaddr	sa;
     struct sockaddr_un	un;
     struct sockaddr_in	in;
  };

static int
getaddr(union sockaddr_gen *sin, const char *adr)
{
  char		*s, *host;
  int		max;
  size_t	len;

  len	= strlen(adr)+1;
  host	= alloca(len);
  memcpy(host, adr, len);

  memset(sin, 0, sizeof *sin);

  for (s=host; *s; s++)
    if (*s==':')
      {
	*s	= 0;

	sin->in.sin_family	= AF_INET;
	sin->in.sin_addr.s_addr	= htonl(INADDR_ANY);
	sin->in.sin_port	= htons(atoi(s+1));

	if (s!=host && !inet_aton(host, &sin->in.sin_addr))
	  {
	    struct hostent	*he;

	    if ((he=gethostbyname(host))==0)
	      ex(host);
	    if (he->h_addrtype!=AF_INET || he->h_length!=sizeof sin->in.sin_addr)
	      ex("unsupported host address");
	    memcpy(&sin->in.sin_addr, he->h_addr, sizeof sin->in.sin_addr);
	  }
	return sizeof *sin;
      }

  sin->un.sun_family	= AF_UNIX;

  max = strlen(host);
  if (max > sizeof(sin->un.sun_path)-1)
    max = sizeof(sin->un.sun_path)-1;
  strncpy(sin->un.sun_path, host, max);

  return max + sizeof sin->un.sun_family;
}

static int
sock_tcp_listen(const char *s)
{
  union sockaddr_gen	sin;
  int			on, len;
  int			sock;

  len	= getaddr(&sin, s);

  sock	= socket(sin.sa.sa_family, SOCK_STREAM, 0);
  if (sock<0)
    ex("socket");

  on = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on))
    ex("setsockopt reuse");

  if (bind(sock, &sin.sa, len))
    ex("bind");

  if (listen(sock, 16))
    ex("listen");

  return sock;
}
/* END COPY
 */

void
socklinger(int fi, int fo, char **argv)
{
  struct linger         linger;
  int			on;
  pid_t			chld;
  char			buf[BUFSIZ*10];
  int			n;

  /* set some default socket options
   */
  linger.l_onoff        = 1;
  linger.l_linger       = 65535;
  if (setsockopt(fo, SOL_SOCKET, SO_LINGER, &linger, sizeof linger) && fo!=1)
    ex("linger");

  on    = 102400;
  if (setsockopt(fi, SOL_SOCKET, SO_RCVBUF, &on, sizeof on) && fi)
    ex("rcvbuf");
  on    = 102400;
  if (setsockopt(fo, SOL_SOCKET, SO_SNDBUF, &on, sizeof on) && fo!=1)
    ex("sndbuf");

  /* fork off the child
   */
  if ((chld=fork())==0)
    {
      if (fi)
	dup2(fi, 0);
      if (fo!=1)
	dup2(fo, 1);
      if (fi!=0)
	close(fi);
      if (fo!=1 && fo!=fi)
	close(fo);

      /* Warning: stderr(2) stays as it is,
       * so it might be the socket (inetd-mode)
       * or the tty (acceptloop mode).
       */
      execvp(*argv, argv);
      exit(-1);
    }
  if (chld==(pid_t)-1)
    ex("fork");

  /* well, this is funny
   * no convenience here anywhere, please?
   */
  for (;;)
    {
      pid_t		pid;
      int		status;

      pid	= wait(&status);
      if (pid==chld)
        break;
      if (pid!=(pid_t)-1)
        continue;
      if (errno!=EINTR)
	ex("wait");
    }

  /* Now the socket belongs to us, and nobody else
   * Shutdown the writing side,
   * and flush the reading side
   * until the other side closes the socket, too.
   *
   * Thus, so the lingering.
   */
  shutdown(fo, SHUT_WR);
  while ((n=read(fi, buf, sizeof buf))!=0)
    if (n<0)
      if (errno!=EINTR && errno!=EAGAIN)
	ex("read");
}

int
main(int argc, char **argv)
{
  int	sock;
  long	pid;

  if (argc<3)
    {
      fprintf(stderr, "Usage: %s -|[host]:port|unixsocket program [args...]\n"
		"\tVersion " SOCKLINGER_VERSION " compiled at " __DATE__ "\n"
		"\tYou probably need this for simple TCP shell scripts:\n"
		"\tif - is given, use stdout(!) as socket (inetd/tcpserver)\n"
		"\telse the given socket (TCP or unix) is opened and listened on\n"
		"\tand each connection is served by the progam (max. 1 in parallel):\n"
		"\tExec program with args with stdin/stdout connected to socket\n"
		"\tWait for program termination.\n"
		"\tShutdown the stdout side of the socket.\n"
		"\tWait (linger) on stdin until EOF is received.\n", argv[0]);
      return 1;
    }
  if (!*argv[1] || !strcmp(argv[1],"-"))
    {
      socklinger(0, 1, argv+2);
      return 0;
    }

  sock	= sock_tcp_listen(argv[1]);
  pid	= getpid();
  for (;;)
    {
      int	fd;

      printf("[%ld] wait on socket %s\n", pid, argv[1]);
      fd	= accept(sock, NULL, NULL);
      if (fd<0)
	{
	  if (errno==EINTR)
	    continue;
	  ex("accept");
	}
      printf("[%ld] run program for connection %d\n", pid, fd);
      socklinger(fd, fd, argv+2);
      if (close(fd))
	ex("close");
    }
  return 0;
}
