[![Socklinger Build Status](https://api.cirrus-ci.com/github/hilbix/socklinger.svg)](https://cirrus-ci.com/github/hilbix/socklinger/)

[Old description](DESCRIPTION)

See also [archive.org version of Apache on lingering](http://web.archive.org/web/19970615083850/http://www.apache.org:80/docs/misc/fin_wait_2.html)

# socklinger

This is a tool to, basically, listen on a socket and call a script to serve the request.

It can do connects, too, or connect two programs with a socketpair to communicate with each other.

The interesting part here is, that it lingers on the socket before the connection is closed,
such that the other (receiving) side has enough time to read all data from the socket.
Without this, some last data might get lost.

> For a more versatile program see `socat`.
> However it is not easy to use.

## Usage

	git clone https://github.com/hilbix/socklinger.git
	cd socklinger
	git submodule update --init
	make
	sudo make install

	socklinger -h

	socklinger socket command

## Notes

`socklinger [options] socket command args..`

- Option `-s` to run command only a single time if `-n0` (default) is used.
- Option `-n-X` to increase the number of parallel running `command`s to `X`
- Option `-nX` to prefork, that is `socklinger` forks before the `accept()`.
- If you use

- `killall -1 socklinger` gracefully shuts down socklinger
- 
- Environment in the forked program:
  - `SOCKLINGER_NR` internal number of the connection
  - `SOCKLINGER_PEER=%s", (peer ? peer : ""));
  SOCKLINGER_SOCK=%s", (name ? name : ""));
  SOCKLINGER_MAX=%d", conf->max);
  SOCKLINGER_COUNT=%d", conf->running);
  SOCKLINGER_PID=%ld", conf->pid);
  er, name);   /* sets conf->timestring        */
  SOCKLINGER_NOW=%s", conf->timestring);

