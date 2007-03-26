/* $Header$
 *
 * Execute quick hack shell scripts connected to a socket.
 *
 * This program now is far too complex.  Most shall be done
 * implicitely by tinolib.  And it is far too much hacked, so it needs
 * a rewrite.
 *
 * Copyright (C)2004-2007 by Valentin Hilbig <webmaster@scylla-charybdis.com>
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
 * Revision 1.10  2007-03-26 23:51:29  tino
 * Intermediate checkin: Moved to new CONF instead of function args
 *
 * Revision 1.9  2006/12/12 11:52:17  tino
 * New version with editing in the old edits to do prefork and connect
 *
 * Revision 1.8  2006/12/12 10:36:42  tino
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

#undef CONF
#define	CONF	struct socklinger_conf *conf

struct socklinger_conf
  {
    /* Work area
     */
    int		sock;		/* Running socket	*/
    int		nr;		/* Running number	*/

    /* Settings
     */
    char	*address;	/* Socket argument (listen address or connect address)	*/
    char	*connect;	/* NULL: accept(); else: connect.  If nonempty: bind arg	*/
    char	**argv;		/* program argument list: program [args]	*/
    int		count;		/* Max number of accepts, connects etc.	*/

    /* Helpers
     */
    long	pid;
    char	note_buf[80];
  };

static const char *
note_str(CONF, const char *s)
{
  if (!conf->pid)
    conf->pid	= getpid();
  snprintf(conf->note_buf, sizeof conf->note_buf, "[%d][%05ld] %s", conf->nr, conf->pid, s);
  return conf->note_buf;
}

static void
note(CONF, const char *s, ...)
{
  va_list	list;

  printf("%s", note_str(conf, ""));
  va_start(list, s);
  vprintf(s, list);
  va_end(list);
  printf("\n");
}

static int
socklinger(CONF, int fi, int fo)
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
  env[0]	= tino_str_printf("SOCKLINGER_NR=%d", conf->nr);
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
  keepfd[0]	= conf->sock+1;	/* close from 2..sock	*/
  keepfd[1]	= 0;

  tino_hup_ignore(1);

  note(conf, "run %s", conf->argv[0]);
  pid		= tino_fork_exec(fi, fo, 2, conf->argv, env, 1, keepfd);

  /* Free environment and wait for the child
   */
  free(env[2]);
  free(env[1]);
  free(env[0]);

  tino_wait_child_exact(pid, &cause);
  note(conf, "%s, lingering", cause);
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

static int
socklinger_dosock(CONF)
{
  int	fd;

  if (conf->connect)
    {
      note(conf, (*conf->connect ? "connect %s as %s" : "connect %s"), conf->address, conf->connect);
      fd	= tino_sock_tcp_connect(conf->address, *conf->connect ? conf->connect : NULL);
      /* As the connect may bind to privilege ports
       * end the privilege eleveation here
       */
      tino_privilege_end_elevation();
    }
  else
    {
      note(conf, "accept %s", conf->address);
      fd	= accept(conf->sock, NULL, NULL);
    }
  if (fd<0)
    {
      if (errno!=EINTR)
	perror(note_str(conf, conf->connect ? "connect" : "accept"));
      sleep(1);
    }
  return fd;
}

static void
socklinger_run(CONF)
{
  tino_hup_start(note_str(conf, "HUP received"));
  for (;;)
    {
      int	fd;

      tino_hup_ignore(0);
      fd	= socklinger_dosock(conf);
      if (fd<0)
	continue;
      if (socklinger(conf, fd, fd))
        perror(note_str(conf, "socklinger"));
      if (close(fd))
	tino_exit(note_str(conf, "close"));
    }
}

static void
socklinger_postfork(CONF)
{
  int	n;
  pid_t	*pids;

  if (conf->count<0)
    conf->count	= -conf->count;

  000;	/* missing: if parent dies, send something like HUP to all childs	*/

  pids	= tino_alloc0(conf->count * sizeof *pids);

  tino_hup_start(note_str(0, "HUP received"));
  for (;;)
    {
      int	fd, status;
      pid_t	pid;

      conf->nr	= 0;

      tino_hup_ignore(0);
      for (n=0; n<conf->count && pids[n]; n++);

      if (n>=conf->count)
	note(0, "wait for childs");
      pid	= waitpid((pid_t)-1, &status, (n<conf->count ? WNOHANG : 0));
      if (pid==(pid_t)-1)
	{
	  if (errno!=EINTR && errno!=EAGAIN && errno!=ECHILD)
	    tino_exit(note_str(conf, "postfork"));
	}
      else if (pid)
	{
	  for (n=0; n<conf->count; n++)
	    if (pid==pids[n])
	      {
		char	*cause;

		pids[n]	= 0;
		cause	= tino_wait_child_status_string(status, NULL);
		note(conf, "child %ul %s", (unsigned long)pid, cause);
		free(cause);
	      }
	  continue;
	}
      conf->nr	= n;
      fd	= socklinger_dosock(conf);
      if (fd<0)
	continue;
      if ((pid=fork())==0)
	{
	  /* conf->n already set	*/
	  if (conf->sock>=0)
	    close(conf->sock);
	  /* keep conf->sock for close() action above	*/
	  if (socklinger(conf, fd, fd))
	    perror(note_str(conf, "socklinger"));
	  if (close(fd))
	    tino_exit(note_str(conf, "close"));
	  exit(0);
	}
      if (pid==(pid_t)-1)
	{
	  perror(note_str(conf, "fork"));
	  continue;
	}
      close(fd);
      pids[n]	= pid;
    }
}

static void
socklinger_prefork(CONF)
{
  int	n;
  pid_t	pid;

  000;	/* missing: if parent dies, send something like HUP to all childs	*/

  for (n=conf->count; n>0; n--)
    if ((pid=fork())==0)
      {
	conf->nr	= n;
	socklinger_run(conf);
      }
    else if (pid==(pid_t)-1)
      {
	conf->nr	= n;	/* report child number	*/
	tino_exit(note_str(conf, "fork"));
      }

  close(conf->sock);	/* socket no more needed now	*/

  while ((pid=wait(NULL))==(pid_t)-1)
    if (errno!=EINTR && errno!=EAGAIN)
      tino_exit(note_str(conf, "main-wait"));
}

/* It shall get a real commandline
 *
 * Due to the "connect" case and the alternate fork method this routine got too complex.
 */
int
main(int argc, char **argv)
{
  static struct socklinger_conf	config;	/* may be big and must be preset 0	*/
  CONF				= &config;
  char				*end;
  int				argn;

  argn	= tino_getopt(argc, argv, 2, 0,
		      TINO_GETOPT_VERSION(SOCKLINGER_VERSION)
		      " [n@[src>]][host]:port|[n@[>]]unixsocket|- program [args...]\n"
	      "\tYou probably need this for simple TCP shell scripts:\n"
	      "\tif - or '' is given, use stdin/out as socket (inetd/tcpserver)\n"
	      "\telse the given socket (TCP or unix) is opened and listened on\n"
	      "\tand each connection is served by the progam (max. n in parallel):\n"
	      "\t1) Exec program with args with stdin/stdout connected to socket\n"
	      "\t   env var SOCKLINGER_NR=-1 (inetd), 0 (single) or instance-nr\n"
	      "\t   env var SOCKLINGER_PEER is set to the peer's IP:port\n"
	      "\t   env var SOCKLINGER_SOCK is set to the socket arg\n"
	      "\t2) Wait for program termination.\n"
	      "\t3) Shutdown the stdout side of the socket.\n"
	      "\t   Wait (linger) on stdin until EOF is received.\n"
	      "Specials:\n"
	      "\tn<0 alternate fork method (default for CygWin).\n"
	      "\t> does a connect instead of listen/accept.  This loops if n given.\n"
	      "\tsrc, if given, is the address to bind to for connects.\n"

		      TINO_GETOPT_USAGE
		      "h	This help"
		      ,

		      NULL);

  if (argn<=0)
    return 1;

  conf->address	= argv[argn];
  conf->argv	= argv+argn+1;
  conf->sock	= -1;	/* important: must be -1 such that keepfd[0] above becomes 0.  XXX is this correct?	*/
  conf->nr	= -1;

  if (!*conf->address || !strcmp(conf->address,"-"))
    {
      tino_privilege_end_elevation();
      if (socklinger(conf, 0, 1))
	{
	  perror(note_str(conf, "socklinger"));
	  return 2;
	}
      return 0;
    }

  conf->count	= strtol(conf->address,&end,0);
  if (*end=='@')
    conf->address	= end+1;
  else
    conf->count	= 0;

  /* conf->sock	= -1;	already done above	*/
  if ((end=strchr(conf->address,'>'))!=0)
    {
      conf->connect	= conf->address;
      *end++		= 0;
      conf->address	= end;

      /* Still ugly and shall be more OO like
       * conf->sock is -1
       */
    }
  else
    {
      conf->sock	= tino_sock_tcp_listen(conf->address);

      /* give up effective UID
       * against privilege escalation
       */
      tino_privilege_end_elevation();
   }

  if (!conf->count)
    {
      conf->nr	= 0;

      if (conf->connect)
	{
	  int	fd;

	  /* Just do a single connect
	   */
	  fd	= socklinger_dosock(conf);
	  if (fd<0)
	    return 3;
	  if (socklinger(conf, fd, fd))
	    {
	      perror(note_str(conf, "socklinger"));
	      return 2;
	    }
	  return 0;
	}
      socklinger_run(conf);
      return 0;
    }
#ifdef __CYGWIN__
  if (!conf->connect && conf->count>0)
    conf->count	= -conf->count;
#endif

  if (conf->count<0)
    socklinger_postfork(conf);
  else
    socklinger_prefork(conf);

  tino_exit(note_str(conf, "child came home"));
  return 0;
}
