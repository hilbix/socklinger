$Header$

What it does: See file DESCRIPTION
How to use:   See file example/proxy.sh

To compile, just type
	make

This now is based on tinolib to improve further development.

Additional notes:
	The Apache developers wrote a good source about lingering:
	http://httpd.apache.org/docs/misc/fin_wait_2.html
	This is true for HTTPD but is true for many other cases as well.

-Tino, webmaster@scylla-charybdis.com
$Log$
Revision 1.2  2005-08-19 04:26:39  tino
release socklinger 1.3.0

Revision 1.1  2004/08/16 01:29:57  Administrator
initial add
