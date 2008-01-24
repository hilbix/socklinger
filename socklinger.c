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
 * Revision 1.19  2008-01-24 23:36:54  tino
 * Typo fixes in comments
 *
 * Revision 1.18  2007-05-08 03:30:09  tino
 * See ChangeLog, commit for dist
 *
 * Revision 1.17  2007/04/04 05:30:12  tino
 * Delay corrected, help text improved
 *
 * Revision 1.16  2007/04/04 03:47:16  tino
 * Cleanups before dist
 *
 * Revision 1.15  2007/04/04 03:45:08  tino
 * Internal rewrite to support option -i
 *
 * Revision 1.14  2007/04/03 02:42:38  tino
 * commit for dist (working version as it seems)
 *
 * Revision 1.13  2007/04/03 02:07:27  tino
 * delay function shall work now (and 3 ideas into getopt added but disabled)
 *
 * Revision 1.12  2007/04/02 17:13:42  tino
 * Again some changes, see ChangeLog
 *
 * Revision 1.11  2007/03/27 01:13:03  tino
 * New intermediate version.  Shall work, but untested
 *
 * Revision 1.10  2007/03/26 23:51:29  tino
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

#include "tino/file.h"
#include "tino/sock.h"
#include "tino/privilege.h"
#include "tino/getopt.h"
#include "tino/proc.h"
#include "tino/str.h"
#include "tino/hup.h"
#include "tino/sleep.h"

#include "socklinger_version.h"

#undef CONF
#define	CONF	struct socklinger_conf *conf

struct socklinger_conf
  {
    /* Work area
     */
    int		sock;		/* Running socket	*/
    int		nr;		/* Running number	*/
    pid_t	*pids;		/* List of childs	*/
    int		dodelay;	/* Issue delay before fork	*/

    /* Settings
     */
    char	*address;	/* Socket argument (listen address or connect address)	*/
    char	*connect;	/* NULL: accept(); else: connect.  If nonempty: bind arg	*/
    char	**argv;		/* program argument list: program [args]	*/
    int		count;		/* Max number of accepts, connects etc.	*/
    int		flag_newstyle;	/* Suppress old commandline support and behavior	*/
    int		delay;		/* Delay forking a number of seconds	*/
    int		timeout;	/* Maximum connect timeout	*/
    int		ignerr;		/* Ignore errors in loops	*/
    int		verbose;	/* verbose level	*/
    int		lingertime;	/* Maximum time to linger (in seconds)	*/
    unsigned long lingersize;	/* Max bytes to read while lingering	*/

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
vnote(CONF, const char *s, TINO_VA_LIST list)
{
  printf("%s", note_str(conf, ""));
  tino_vfprintf(stdout, s, list);
  printf("\n");
}

static void
always(CONF, const char *s, ...)
{
  tino_va_list	list;

  tino_va_start(list, s);
  vnote(conf, s, &list);
  tino_va_end(list);
}

static void
note(CONF, const char *s, ...)
{
  tino_va_list	list;

  if (conf->verbose<0)
    return;

  tino_va_start(list, s);
  vnote(conf, s, &list);
  tino_va_end(list);
}

/* No sideeffects on errno
 */
static void
verbose(CONF, const char *s, ...)
{
  tino_va_list	list;
  int		e;

  if (conf->verbose<=0)
    return;

  e	= errno;
  tino_va_start(list, s);
  vnote(conf, s, &list);
  tino_va_end(list);
  errno	= e;
}

static void
socklinger_error(CONF, const char *s)
{
  if (!conf->ignerr)
    tino_exit(note_str(conf, s));
  perror(note_str(conf, s));
  tino_relax();
}

static int
socklinger(CONF, int fi, int fo)
{
  char	*peer, *name;
  int	n;
  char	*env[4], *cause;
  int	keepfd[2];
  pid_t	pid;

  /* set some default socket options
   */
  if ((tino_sock_lingerE(fo, 65535) && fo!=1) ||
      (tino_sock_rcvbufE(fi, 102400) && fi) ||
      (tino_sock_sndbufE(fo, 102400) && fo!=1))
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
  always(conf, "peer %s via %s", peer, name);
  if (name)
    tino_free(name);
  if (peer)
    tino_free(peer);
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
  tino_free(env[2]);
  tino_free(env[1]);
  tino_free(env[0]);

  tino_wait_child_exact(pid, &cause);
  always(conf, "%s, lingering", cause);
  tino_free(cause);

  tino_hup_ignore(0);

  /* Now the socket belongs to us, and nobody else.
   *
   * Shutdown the writing side,
   * and flush the reading side
   * until the other side closes the socket, too.
   *
   * Thus, do a typical lingering.
   */
  if (fo!=fi)
    tino_file_close(fo);
  if (tino_sock_shutdownE(fi, SHUT_WR))
    return 1;

  /* This shall become a library routine as correct lingering is
   * somewhat generic.
   *
   * However for this it must be more detailed and the alarm handling
   * must be better designed, too.
   *
   * Note that I consider lingering a design fault of TCP.  When you
   * close a TCP socket the close should be delayed until all data is
   * correctly delivered to the other side.  So a close shall shutdown
   * the writing side and wait, while lingering, until all data has
   * been delivered to the application.  However most modern TCP
   * stacks not even wait until data has reached the other side, they
   * already close the socket before the remaining data in the TCP
   * queue buffers are really flushed.
   *
   * Closing a socket nonlingering could be archived easliy through
   * O_NDELAY (so first fcntl() and then close()).  If this is not
   * enough as you want flushing but timeout you can set an alarm()
   * around it.  However such a behavior is likely to break a lot of
   * programs, so perhaps a "close_linger()" syscall would be
   * convenient or a matching socket flag.  As there is nothing
   * portable (I did not hear about it) one has to re_invent the wheel
   * (note that SO_LINGER is mostly ignored by many systems today).
   * So that's exactly what I want to have: A generic
   * "tino_sock_close()" and a "tino_sock_close_timeout()" which makes
   * sure that all data is flushed to the other side before closing
   * the socket.
   *
   * Also please see my tino_file_close() implementation, it knows
   * about EINTR.
   *
   * Is there any way to - portably - find out if all data has been
   * sent?  Note that then there still might be a problem, as incoming
   * data usually is kept in the input buffer of the receiving side,
   * and this does not mean that the data has reached the application.
   * If this application (the other side) still is writing to the
   * socket which already is closed on our side, it will see an error,
   * and many applications do not handle write() and read() errors
   * differently, this means, they bail out before reading what's in
   * the socket input buffers.  This is deadly for file transfers, as
   * this cuts the end of a file, for instance.  Socklinger was
   * designed for "quickhacks", so it must assume that the other side
   * misbehaves in TCP things, so this lingering is essential.
   * Socklinger must linger until the other side sees the EOF on the
   * input and therefor closes the write side.
   *
   * OTOH this might lead to other problems, where TCP connections
   * just starve.  This is, the sending side does not shutdown the
   * socket at all, perhaps because of a network loss.  In this case
   * socklinger hangs endlessly waiting for the socket to terminate.
   * Even SO_KEEPALIVE cannot help in each case, as the other side
   * might wait until we close our reading side (which never happens).
   * For this situation, there is the timeout.  If you haven't seen
   * data for a long time while lingering you can consider the
   * connection dead and tear it down.  However what a "long time" is
   * cannot be hardcoded.  On ethernet 10s is a long time, while with
   * smallband long distance connections even 1 hour can be a short
   * period.
   */
  for (;;)
    {
      char	buf[BUFSIZ*10];
      int	wasalarm;

      if (conf->lingertime)
	tino_alarm_set(conf->lingertime, NULL, NULL);
      n	= tino_file_read_intr(fi, buf, sizeof buf);
      wasalarm	= tino_alarm_is_pending();
      if (conf->lingertime)
	tino_alarm_stop(NULL, NULL);
      if (!n)
	break;
      if (n<0)
	{
	  if (errno!=EINTR && errno!=EAGAIN)
	    return 1;
	  if (wasalarm)
	    {
	      always(conf, "linger timeout");
	      break;
	    }
	  tino_relax();
	}
      else if (conf->lingersize)
	{
	  if (conf->lingersize<=n)
	    {
	      always(conf, "linger size exeeded");
	      break;
	    }
	  conf->lingersize	-= n;
	}
    }
  return 0;
}

static int
socklinger_dosock(CONF)
{
  int	fd;

  if (conf->connect)
    {
      int	e;

      note(conf, (*conf->connect ? "connect %s as %s" : "connect %s"), conf->address, conf->connect);
      fd	= tino_sock_tcp_connect(conf->address, *conf->connect ? conf->connect : NULL);
      /* As the connect may bind to privilege ports
       * end the privilege eleveation here
       */
      e	= errno;
      tino_privilege_end_elevation();
      errno	= e;
    }
  else
    {
      note(conf, "accept %s", conf->address);
      fd	= tino_sock_acceptI(conf->sock);
    }
  if (fd<0)
    {
      if (errno!=EINTR)
	perror(note_str(conf, conf->connect ? "connect" : "accept"));
      tino_relax();
    }
  return fd;
}

static int
socklinger_run(CONF)
{
  int	fd;

  tino_hup_ignore(0);
  fd	= socklinger_dosock(conf);
  if (fd<0)
    return 1;
  if (socklinger(conf, fd, fd))
    perror(note_str(conf, "socklinger"));
  if (tino_file_close(fd))
    tino_exit(note_str(conf, "close"));
  return 0;
}

static void
socklinger_alloc_pids(CONF)
{
  /* missing: if parent terminates, send something like HUP to all
   *  childs
   */
  000;

  if (conf->count<0)
    conf->count	= -conf->count;
  conf->pids	= tino_alloc0(conf->count * sizeof *conf->pids);
}

/* Find a free child number
 *
 * 0	all childs taken
 * <0	error
 * >0	free child slot
 */
static int
socklinger_child_findfree(CONF)
{
  int	n;

  for (n=0; n<conf->count; n++)
    if (!conf->pids[n])
      return n+1;
  return 0;
}

/* Wait for a free child slot
 *
 * 0	all childs taken or loop needed
 * <0	child came home
 * >0	free child slot
 */
static int
socklinger_waitchild(CONF)
{
  int	n, e, status;
  pid_t	pid;

  conf->nr	= 0;
  n		= socklinger_child_findfree(conf);
  if (!n)
    note(conf, "wait for childs");
  else if (conf->dodelay)		/* send signal in delay secs	*/
    {
      verbose(conf, "delaying %ds", conf->delay);
      tino_alarm_set(conf->delay, NULL, NULL);
    }
  pid	= waitpid((pid_t)-1, &status, (!conf->dodelay && n ? WNOHANG : 0));
  e	= errno;
  if (n && conf->dodelay)
    tino_alarm_stop(NULL, NULL);
  conf->dodelay	= 0;	/* If processes return, immediately allow another connect	*/
  if (pid==(pid_t)-1)
    {
      if (e!=EINTR && e!=EAGAIN && e!=ECHILD)
	{
	  errno	= e;
	  socklinger_error(conf, "waitpid()");
	  return 0;
	}
    }
  else if (pid)
    {
      char	*cause;

      for (n=conf->count; --n>=0; )
	if (pid==conf->pids[n])
	  {
	    conf->pids[n]	= 0;
	    conf->nr		= n+1;	/* is nulled above again	*/
	    break;
	  }
      cause	= tino_wait_child_status_string(status, NULL);
      note(conf, "child %lu %s%s", (unsigned long)pid, cause, n>=0 ? "" : " (foreign child!)");
      tino_free(cause);
      return -1;
    }
  conf->nr	= n;
  return n;
}

/* Fork a child slot
 *
 * <0	error
 * 0	parent
 * >0	child
 */
static int
socklinger_forkchild(CONF, int n)
{
  pid_t	pid;

  if (n<=0 || (pid=fork())==0)
    return 1;
  if (pid==(pid_t)-1)
    {
      socklinger_error(conf, "postfork fork()");
      return -1;
    }
  if (conf->delay)
    conf->dodelay	= 1;	/* Delay the next fork()	*/
  conf->pids[n-1]	= pid;
  return 0;
}

static void
socklinger_postfork(CONF)
{
  int	fd;

  socklinger_alloc_pids(conf);

  tino_hup_start(note_str(conf, "HUP received"));
  conf->dodelay	= 0;
  for (;;)
    {
      int	n;

      tino_hup_ignore(0);

      n	= socklinger_waitchild(conf);
      if (n<=0)
	{
	  tino_relax();
	  continue;
	}

      fd	= socklinger_dosock(conf);
      if (fd<0)
	continue;
      if (socklinger_forkchild(conf, n)>0)
	break;
      tino_file_close(fd);
    }

  /* forked child code
   */

  /* conf->n already set	*/
  if (conf->sock>=0)
    tino_file_close(conf->sock);
  /* keep conf->sock for close() action above	*/
  if (socklinger(conf, fd, fd))
    {
      perror(note_str(conf, "socklinger"));
      exit(1);
    }
  if (tino_file_close(fd))
    tino_exit(note_str(conf, "close"));
  exit(0);
}

static void
socklinger_prefork(CONF)
{
  socklinger_alloc_pids(conf);

  for (;;)
    {
      int	n;

      n	= socklinger_waitchild(conf);
      if (n<0)
	socklinger_error(conf, "child came home");
      if (socklinger_forkchild(conf, n)>0)
	break;
    }

  /* forked child code (this is run by the fork()==0)
   */

  tino_hup_start(note_str(conf, "HUP received"));
  for (;;)
    if (socklinger_run(conf))
      tino_relax();
    else if (conf->delay)
      tino_sleep(conf->delay);
  exit(-1);
}

static void
process_args(CONF, int argc, char **argv)
{
  int	argn, flag_connect;
  char	*end;

  argn	= tino_getopt(argc, argv, 2, 0,
		      TINO_GETOPT_VERSION(SOCKLINGER_VERSION)
		      " [host]:port|unixsocket|- program [args...]\n"
		      "\tYou probably need this for simple TCP shell scripts:\n"
		      "\tif - or '' is given, use stdin/out as socket (inetd/tcpserver)\n"
		      "\telse the given socket (TCP or unix) is opened and listened on\n"
		      "\tand each connection is served by the progam (max. N in parallel):\n"
		      "\t1) Exec program with args with stdin/stdout connected to socket\n"
		      "\t   env var SOCKLINGER_NR=-1 (inetd), 0 (single) or instance-nr\n"
		      "\t   env var SOCKLINGER_PEER is set to the peername\n"
		      "\t   env var SOCKLINGER_SOCK is set to the sockname\n"
		      "\t2) Wait for program termination.\n"
		      "\t3) Shutdown the stdout side of the socket.\n"
		      "\t   Wait (linger) on stdin until EOF is received."
		      ,

		      TINO_GETOPT_USAGE
		      "h	This help"
		      ,

		      TINO_GETOPT_STRING
		      "b	bind to address for connect, implies option -c"
		      , &conf->connect,

		      TINO_GETOPT_FLAG
		      "c	use connect instead of accept"
		      , &flag_connect,

		      TINO_GETOPT_INT
		      TINO_GETOPT_TIMESPEC
		      "d secs	delay forking.  In preforking socklinger sleeps after\n"
		      "		accept/connect, in preforking it does not fork more\n"
		      "		connections in the given time."
		      , &conf->delay,

		      TINO_GETOPT_FLAG
		      "i	ignore errors (stay in loop if fork() fails etc.)"
		      , &conf->ignerr,

		      TINO_GETOPT_INT
		      TINO_GETOPT_TIMESPEC
		      "l secs	Maximum time to linger (default=0: forever)\n"
		      "		Note that arriving data restarts the timeout, see -m"
		      , &conf->lingertime,

		      TINO_GETOPT_ULONGINT
		      TINO_GETOPT_SUFFIX
		      "m byte	Maximum data to read while lingering (default=0: any)"
		      , &conf->lingersize,

		      TINO_GETOPT_INT
		      "n N	number of parallel connections to serve\n"
		      "		if missing or 0 socklinger does 1 connect without loop\n"
		      "		else preforking (N>0) or postforking (N<0) is done.\n"
		      "		Under CygWin preforking is not available for accept mode"
		      , &conf->count,

		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      TINO_GETOPT_MAX
		      "q	be more quiet (only tell important things)\n"
		      "		If not quiet enough, try >/dev/null (errs go to stderr)"
		      , &conf->verbose,
		      1,
		      -1,

		      TINO_GETOPT_FLAG
		      "s	suppress old style prefix [n@[[src]>] to first param.\n"
		      "		'n@' is option -n and '[src]>' are option -b and -c\n"
		      "		For N=0 only a single accept is done (else it loops!)."
		      , &conf->flag_newstyle,
#if 0
		      TINO_GETOPT_INT
		      TINO_GETOPT_TIMESPEC
		      "t secs	Maximum connect timeout (default=0: system timeout)"
		      , &conf->timeout,
#endif
		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      TINO_GETOPT_MAX
		      "v	be more verbose (opposit of -q)"
		      , &conf->verbose,
		      -1,
		      1,

		      NULL);

  if (argn<=0)
    exit(1);

  if (!conf->lingertime)
    verbose(conf, "unlimited lingering, perhaps try option -l");

  conf->address	= argv[argn];
  conf->argv	= argv+argn+1;
  conf->sock	= -1;	/* important: must be -1 such that keepfd[0] above becomes 0.  XXX is this correct?	*/
  conf->nr	= -1;

  if (flag_connect && !conf->connect)
    conf->connect	= "";

  if (!*conf->address || !strcmp(conf->address,"-"))
    {
      /* Check for sane option values?
       */
      conf->address	= 0;
      return;
    }

  if (conf->count || conf->connect)
    conf->flag_newstyle	= 1;

  if (!conf->flag_newstyle)
    {
      /* Check for sane option values?
       */
      conf->count	= strtol(conf->address,&end,0);
      if (*end=='@')
	conf->address	= end+1;
      else
	conf->count	= 0;

      /* conf->sock	= -1;	already done above	*/
      if ((end=strchr(conf->address,'>'))!=0)
	{
	  conf->connect	= conf->address;
	  *end++	= 0;
	  conf->address	= end;

	  /* Still ugly and shall be more OO like
	   * conf->sock is -1
	   */
	}
    }
}

/* Due to the "connect" case and the alternate fork method this routine got too complex.
 */
int
main(int argc, char **argv)
{
  static struct socklinger_conf	config;	/* may be big and must be preset 0	*/
  CONF	= &config;

  process_args(conf, argc, argv);
  tino_sigdummy(SIGCHLD);		/* interrupt process on SIGCHLD	*/

  /* Connection comes from stdin/stdout:
   */
  if (!conf->address)
    {
      tino_privilege_end_elevation();
      if (socklinger(conf, 0, 1))
	{
	  perror(note_str(conf, "socklinger"));
	  return 2;
	}
      return 0;
    }

  if (!conf->connect)
    {
      conf->sock	= tino_sock_tcp_listen(conf->address);

      /* give up effective UID
       * against privilege escalation
       */
      tino_privilege_end_elevation();

#ifdef __CYGWIN__
      /* Under CygWin preforking isn't possible as it is impossible to
       * do an accept() on the same socket from within different
       * processes
       */
      if (conf->count>0)
	conf->count	= -conf->count;
#endif
   }

  conf->nr	= 0;

  /* I am not completely satisfied with following solution:
   *
   * This shall somehow magically be combined into one single thing.
   */

  if (conf->count<0)
    socklinger_postfork(conf);
  else if (conf->count>0)
    socklinger_prefork(conf);
  else if (conf->connect)
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
    }
  else
    {
      /* For old style we do a loop, for new style, only a single
       * accept() is done.  However repeat the accept() if accept
       * returns an error (there are spurious connects out there).
       */
      tino_hup_start(note_str(conf, "HUP received"));
      while (socklinger_run(conf) || !conf->flag_newstyle);
    }
  return 0;
}
