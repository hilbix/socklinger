/* Execute quick hack shell scripts connected to a socket.
 *
 * This program now is far too complex.  Most shall be done
 * implicitely by tinolib.  And it is far too much hacked, so it needs
 * a rewrite.
 *
 * Copyright (C)2004-2016 by Valentin Hilbig <webmaster@scylla-charybdis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#if 0
#define	TINO_DP_all	TINO_DP_ON
#endif

#include "tino/alarm.h"
#include "tino/file.h"
#include "tino/procsock.h"
#include "tino/privilege.h"
#include "tino/getopt.h"
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
    int		running;	/* Running total	*/
    int		max;		/* Max running nr	*/
    pid_t	*pids;		/* List of childs	*/
    int		dodelay;	/* Issue delay before fork	*/
    time_t	now;		/* Last timestamp	*/

    /* Settings
     */
    int		fd;		/* File descriptor, -1=0/1, else only this FD	*/
    char	*address;	/* Socket argument (listen address or connect address)	*/
    char	*connect;	/* NULL: accept(); else: connect.  If nonempty: bind arg	*/
    char	**argv;		/* program argument list: program [args]	*/
    int		count;		/* Max number of accepts, connects etc.	*/
    int		flag_oldstyle;	/* Enable old commandline support and behavior	*/
    int		flag_newstyle;	/* Suppress old commandline support and behavior	*/
    int		delay;		/* Delay forking a number of seconds	*/
    int		timeout;	/* Maximum connect timeout	*/
    int		ignerr;		/* Ignore errors in loops	*/
    int		verbose;	/* verbose level	*/
    int		lingertime;	/* Maximum time to linger (in seconds)	*/
    unsigned long lingersize;	/* Max bytes to read while lingering	*/
    int		rotate;		/* do rotation	*/
    int		prepend, utc;	/* prepend timestamp, prepend UTC	*/
    int		maxwait;	/* Maximum lingering waiting time	*/

    /* Helpers
     */
    long	pid;
    char	note_buf[80];
    char	timestring[20];	/* YYYYMMDD-HHMMSS	*/
  };

static void
conf_set_time(CONF)
{
  struct tm	*tm;

  time(&conf->now);
  tm	= (conf->utc ? gmtime : localtime)(&conf->now);
  snprintf(conf->timestring, sizeof conf->timestring, "%04d%02d%02d-%02d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static const char *
note_str(CONF, const char *s)
{
  conf_set_time(conf);
  if (conf->prepend)
    snprintf(conf->note_buf, sizeof conf->note_buf, "[%s][%d][%05ld] %s", conf->timestring, conf->nr, conf->pid, s);
  else
    snprintf(conf->note_buf, sizeof conf->note_buf, "[%d][%05ld] %s", conf->nr, conf->pid, s);

  return conf->note_buf;
}

static void
vnote(CONF, TINO_VA_LIST list)
{
  printf("%s", note_str(conf, ""));
  tino_vfprintf(stdout, list);
  printf("\n");
}

static void
always(CONF, const char *s, ...)
{
  tino_va_list	list;

  tino_va_start(list, s);
  vnote(conf, &list);
  tino_va_end(list);
}

static void
note(CONF, const char *s, ...)
{
  tino_va_list	list;

  if (conf->verbose<0)
    return;

  tino_va_start(list, s);
  vnote(conf, &list);
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
  vnote(conf, &list);
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
  char	*env[8], *cause;
  int	keepfd[3], max, *fds;
  pid_t	pid;
  time_t	start;

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
  peer	= tino_sock_get_peernameN(fi);
  if (!peer)
    peer	= tino_sock_get_peernameN(fo);
  if (!peer)
    peer	= tino_strdupO(conf->address);
  name	= tino_sock_get_socknameN(fi);
  if (!name)
    name	= tino_sock_get_socknameN(fo);
  if (!name)
    name	= tino_strdupO(conf->connect);

  /* A second hack is to preset peer and name in case of process sockets.
   * This really should be handled by the subsystem correctly.
   */
  if (!peer || *conf->address=='|')
    {
      tino_freeO(peer);
      peer	= tino_strdupO(conf->address);
    }
  if (!name || *conf->address=='|')
    {
      tino_freeO(name);
      name	= tino_strdupO(conf->connect);
    }

  n		= 0;
  env[n++]	= tino_str_printf("SOCKLINGER_NR=%d", conf->nr);
  env[n++]	= tino_str_printf("SOCKLINGER_PEER=%s", (peer ? peer : ""));
  env[n++]	= tino_str_printf("SOCKLINGER_SOCK=%s", (name ? name : ""));
  env[n++]	= tino_str_printf("SOCKLINGER_MAX=%d", conf->max);
  env[n++]	= tino_str_printf("SOCKLINGER_COUNT=%d", conf->running);
  env[n++]	= tino_str_printf("SOCKLINGER_PID=%ld", conf->pid);
  always(conf, "peer %s via %s", peer, name);	/* sets conf->timestring	*/
  env[n++]	= tino_str_printf("SOCKLINGER_NOW=%s", conf->timestring);
  env[n]	= 0;
  if (name)
    tino_freeO(name);
  if (peer)
    tino_freeO(peer);

  /* fork off the child
   * Warning: stderr(2) stays as it is,
   * so it might be the socket (inetd-mode) 
   * or the tty (acceptloop mode).
   */
  keepfd[0]	= conf->sock+1;	/* close from 2..sock	*/
  keepfd[1]	= 0;
  keepfd[2]	= 0;

  max	= conf->fd+1;
  if (max<3)
    max	= 3;
  fds		= tino_allocO(max*sizeof *fds);
  fds[0]	= fi;
  fds[1]	= fo;
  fds[2]	= 2;
  if (conf->fd>=0)
    {
      TINO_FATAL_IF(fi!=fo);
      fds[0]		= 0;
      fds[1]		= 1;
      fds[conf->fd]	= fi;
      keepfd[1]		= conf->fd;
    }

  tino_hup_ignoreO(1);

  note(conf, "run %s", conf->argv[0]);
  pid		= tino_fork_execO(fds,max, conf->argv, env, 1, keepfd);

  /* Free environment and wait for the child
   */
  while (--n>=0)
    tino_freeO(env[n]);

  tino_wait_child_exact(pid, &cause);
  always(conf, "%s, lingering", cause);
  tino_freeO(cause);

  tino_hup_ignoreO(0);

  /* Now the socket belongs to us, and nobody else.
   *
   * Shutdown the writing side,
   * and flush the reading side
   * until the other side closes the socket, too.
   *
   * Thus, do a typical lingering.
   */
  if (fo!=fi)
    tino_file_closeE(fo);
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

  time(&start);
  start++;
  for (;;)
    {
      char	buf[BUFSIZ*10];
      int	wasalarm;
      int	tmp;

      tmp = 0;
      if (conf->lingertime || conf->maxwait)
	{
	  if (conf->maxwait)
	    {
	      tmp	 = conf->maxwait-(time(NULL)-start);
	      if (tmp<=0)
		{
		  always(conf, "max linger time exceeded");
		  break;
		}
	      if (conf->lingertime && tmp>conf->lingertime)
		tmp	= conf->lingertime;
	    }
	  else
	    tmp	= conf->lingertime;
	  tino_alarm_set(tmp, NULL, NULL);
	}
      n	= tino_file_readI(fi, buf, sizeof buf);
      wasalarm	= tino_alarm_is_pending();
      if (tmp)
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

/* give up effective UID
 * against privilege escalation
 */
static void
drop_privileges(CONF)
{
  int	e;

#ifdef HORRIBLY_INSECURE_NEVER_DO_THIS
  if (conf->keep_privileges)
    return;
#endif
  e	= errno;
  if (tino_privilege_end_elevationE()<0)
    socklinger_error(conf, "error dropping privileges");
  errno	= e;
}

static int
socklinger_dosock(CONF)
{
  int	fd, wasalarm;

  wasalarm	= 0;
  if (conf->connect)
    {
      note(conf, (*conf->connect ? "connect %s as %s" : "connect %s"), conf->address, conf->connect);
      if (conf->timeout)
	tino_alarm_set(conf->timeout, NULL, NULL);
      fd	= tino_proc_sock(conf->address, "|", conf->connect);
      wasalarm	= tino_alarm_is_pending();
      if (conf->timeout)
	tino_alarm_stop(NULL, NULL);

      /* As the connect may bind to privilege ports
       * end the privilege eleveation here
       */
      if (*conf->connect)
        drop_privileges(conf);
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
      else if (wasalarm)
	perror(note_str(conf, conf->connect ? "connect timeout" : "accept timeout"));
      tino_relax();
    }
  return fd;
}

static int
socklinger_close(CONF, int fd)
{
  if (fd<0)
    return -1;
  if (socklinger(conf, fd, fd))
    {
      perror(note_str(conf, "socklinger"));
      tino_file_close_ignE(fd);
      return 1;
    }
  if (tino_file_nonblockE(fd))
    perror(note_str(conf, "unblock"));
  if (tino_file_closeE(fd))
    tino_exit(note_str(conf, "close"));
  return 0;
}

static int
socklinger_run(CONF)
{
  tino_hup_ignoreO(0);
  return socklinger_close(conf, socklinger_dosock(conf));
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
  conf->pids	= tino_alloc0O(conf->count * sizeof *conf->pids);
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

  /* Only reduce in slow motion	*/
  if (conf->max && !conf->pids[conf->max-1])
    conf->max--;
  if (conf->rotate)
    {
      if (conf->rotate>conf->count)
	conf->rotate	= 1;
      n	= conf->rotate-1;
      if (!conf->pids[n])
	return n+1;
    }
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
  int	flags;

  conf->nr	= 0;
  n		= socklinger_child_findfree(conf);
  flags		= 0;
  if (!n)
    note(conf, "wait for childs");
  else if (conf->dodelay)		/* send signal in delay secs	*/
    {
      verbose(conf, "delaying %ds", conf->delay);
      tino_alarm_set(conf->delay, NULL, NULL);
      if (!conf->running)
	{
	  pause();
	  flags	= WNOHANG;
	}
    }
  else
    flags	= WNOHANG;
  pid	= waitpid((pid_t)-1, &status, flags);
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
	    conf->running--;
	    /* Max corrected on next loop	*/
	    break;
	  }
      cause	= tino_wait_child_status_string(status, NULL);
      note(conf, "child %lu %s%s", (unsigned long)pid, cause, n>=0 ? "" : " (foreign child!)");
      tino_freeO(cause);
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

  if (n>conf->max)
    conf->max	= n;
  if (n<=0)
    return 1;
  if ((pid=fork())==0)
    {
      conf->running++;
      return 1;
    }
  if (pid==(pid_t)-1)
    {
      socklinger_error(conf, "fork()");
      return -1;
    }
  if (conf->delay)
    conf->dodelay	= 1;	/* Delay the next fork()	*/
  conf->pids[n-1]	= pid;
  conf->running++;
  if (conf->rotate)
    conf->rotate++;
  return 0;
}

static void
socklinger_postfork(CONF)
{
  int	fd;

  socklinger_alloc_pids(conf);

  tino_hup_startO(note_str(conf, "HUP received"));
  conf->dodelay	= 0;
  for (;;)
    {
      int	n;

      tino_hup_ignoreO(0);

      n	= socklinger_waitchild(conf);
      if (n<=0)
	{
	  tino_relax();
	  continue;
	}

      fd	= socklinger_dosock(conf);
      if (fd<0)
        {
	  conf->dodelay	= 1;	/* something failed, delay!	*/
	  continue;
	}
      if (socklinger_forkchild(conf, n)>0)
	break;
      tino_file_closeE(fd);
    }

  /* forked child code
   */

  /* conf->n already set	*/
  if (conf->sock>=0)
    tino_file_closeE(conf->sock);
  /* keep conf->sock for close() action above	*/

  exit(socklinger_close(conf, fd));
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

  tino_hup_startO(note_str(conf, "HUP received"));
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
		      " [host]:port|unix|/unix|@abstact|-|'|script' program [args...]\n"
		      "\tYou probably need this for simple TCP shell scripts:\n"
		      "\tif - or '' is given, use stdin/out as socket (inetd/tcpserver)\n"
		      "\telse the given socket (TCP or unix) is opened and listened on\n"
		      "\tand each connection is served by the progam (max. N in parallel):\n"
		      "\t1) Exec program with args with stdin/stdout connected to socket\n"
		      "\t   env var SOCKLINGER_NR=-1 (inetd), 0 (single) or instance-nr\n"
		      "\t   env var SOCKLINGER_PEER is set to the peername\n"
		      "\t   env var SOCKLINGER_SOCK is set to the sockname\n"
		      "\t   env var SOCKLINGER_PID gives the forking socklinger PID\n"
		      "\t   env var SOCKLINGER_NOW is set to YYYYMMDD-HHMMSS (see -u)\n"
		      "\tThe following is only meaningful for postforking:\n"
		      "\t   env var SOCKLINGER_MAX is set to maximum running NR\n"
		      "\t   env var SOCKLINGER_COUNT is set to the currently running tasks\n"
		      "\t2) Wait for program termination.\n"
		      "\t3) Shutdown the stdout side of the socket.\n"
		      "\t   Wait (linger) on stdin until EOF is received."
		      ,

		      TINO_GETOPT_USAGE
		      "h	This help"
		      ,
#if 0
		      TINO_GETOPT_FLAG
		      "4	use IPv4 (default: autodetect)"
		      , &conf->ipv4,

		      TINO_GETOPT_FLAG
		      "6	use IPv6 (default: autodetect)"
		      , &conf->ipv6,
#endif
		      TINO_GETOPT_STRING
		      "b addr	bind to address for connect, implies option -c\n"
		      "		For process sockets ('|'-type) this is the environment\n"
		      "		like -b 'HELLO=\"\\\"hello world\\\"\" ANOTHER=var'"
		      , &conf->connect,

		      TINO_GETOPT_FLAG
		      "c	use connect instead of accept\n"
		      "		Implied for scripts ('|script') or option -b"
		      , &flag_connect,

		      TINO_GETOPT_INT
		      TINO_GETOPT_TIMESPEC
		      "d secs	delay forking.  In preforking socklinger sleeps after\n"
		      "		accept/connect, in postforking it does not fork\n"
		      "		additional childs for the given time."
		      , &conf->delay,

		      TINO_GETOPT_INT
		      TINO_GETOPT_DEFAULT
		      "f nr	Set fd for socket, default sets it for 0/1"
		      , &conf->fd,
		      -1,

		      TINO_GETOPT_FLAG
		      "i	ignore errors (stay in loop if fork() fails etc.)"
		      , &conf->ignerr,
#ifdef HORRIBLY_INSECURE_NEVER_DO_THIS
		      TINO_GETOPT_FLAG
		      "k	keep privileges (do not drop privilege escalation)\n"
		      "		WARNING, USE THAT WITH CARE!"
		      , &conf->keep_privileges,
#endif
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
		      "		Defaults to 0 which is equivalent to 1 unless option -s.\n"
		      "		preforking (N>0) or postforking (N<0) is done.\n"
		      "		Under CygWin preforking is not available for accept mode"
		      , &conf->count,

		      TINO_GETOPT_FLAG
		      "p	prepend timestamp [YYYYMMDD-HHMMSS] to log lines"
		      , &conf->prepend,

		      TINO_GETOPT_FLAG
		      "o	old style prefix [n@[[src]>] to first param.\n"
		      "		'n@' is option -n and '[src]>' are option -b and -c\n"
		      , &conf->flag_oldstyle,

		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      TINO_GETOPT_MAX
		      "q	be more quiet (only tell important things)\n"
		      "		If not quiet enough, try >/dev/null (errs go to stderr)"
		      , &conf->verbose,
		      1,
		      -1,

		      TINO_GETOPT_FLAG
		      "r	rotate slots (only meaningful with postforking)"
		      , &conf->rotate,

		      TINO_GETOPT_FLAG
		      "s	single shot: for N=0 (default) terminate after first accept\n"
		      "		Note: option -o is ignored if option -s present."
		      , &conf->flag_newstyle,

		      TINO_GETOPT_INT
		      TINO_GETOPT_TIMESPEC
		      "t secs	Maximum connect timeout (default=0: system timeout)"
		      , &conf->timeout,

		      TINO_GETOPT_FLAG
		      "u	use UTC time (see -p and env)"
		      , &conf->utc,

		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      TINO_GETOPT_MAX
		      "v	be more verbose (opposite of -q)"
		      , &conf->verbose,
		      -1,
		      1,

		      TINO_GETOPT_INT
		      TINO_GETOPT_TIMESPEC
		      "w secs	Maximum waiting time on lingering (default=0:forever)"
		      , &conf->maxwait,

		      NULL);

  if (argn<=0)
    exit(1);

  if (!conf->lingertime)
    verbose(conf, "unlimited lingering, perhaps try option -l");

  /* We need the PID for the environment, so set it here.
   * Pherhaps this changes as we might be called via chaining.
   * This is not yet supported, but it might be required in future.
   */
  conf->pid	= getpid();

  conf->address	= argv[argn];
  conf->argv	= argv+argn+1;
  conf->sock	= -1;	/* important: must be -1 such that keepfd[0] above becomes 0.  XXX is this correct?	*/
  conf->nr	= -1;

  if ((flag_connect || *conf->address=='|') && !conf->connect)
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

  if (conf->flag_oldstyle && !conf->flag_newstyle)
    {
      /* Check for sane option values?
       */
      conf->count	= strtol(conf->address,&end,0);
      if (end!=conf->address && *end=='@')
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

  tino_sock_error_fn	= tino_sock_error_fn_ignore;

  process_args(conf, argc, argv);
  tino_sigdummy(SIGCHLD);		/* interrupt process on SIGCHLD	*/

  /* Connection comes from stdin/stdout:
   */
  if (!conf->address)
    {
      drop_privileges(conf);
      if (conf->fd>=0)
	{
	  perror(note_str(conf, "cannot use option -f here"));
	  return 3;
	}
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

      drop_privileges(conf);

#ifdef __CYGWIN__
      /* Under CygWin preforking isn't possible as it is impossible to
       * do an accept() on the same socket from within different
       * processes
       */
      if (conf->count>0)
	conf->count	= -conf->count;
#endif
   }
  else if (!*conf->connect)
    drop_privileges(conf);	/* drop privileges early	*/

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
      tino_hup_startO(note_str(conf, "HUP received"));
      while (socklinger_run(conf)<0 || !conf->flag_newstyle)
        tino_relax();
    }
  return 0;
}
