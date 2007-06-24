#	$OpenBSD: Makefile,v 1.6 2007/03/04 03:19:41 beck Exp $

# from the bsd.port.mk
AWK?=		/usr/bin/awk
SYSCTL?=	/sbin/sysctl
# Get __FreeBSD_version
.if !defined(OSVERSION)
.if exists(/usr/include/sys/param.h)
OSVERSION!=	${AWK} '/^\#define __FreeBSD_version/ {print $$3}' < /usr/include/sys/param.h
.elif exists(/usr/src/sys/sys/param.h)
OSVERSION!=	${AWK} '/^\#define __FreeBSD_version/ {print $$3}' < /usr/src/sys/sys/param.h
.else
OSVERSION!=	${SYSCTL} -n kern.osreldate
.endif
.endif

PROG=	spamlogd
SRCS=	spamlogd.c sync.c
MAN=	spamlogd.8

.if ${OSVERSION} < 601000
SRCS+=	../spamd/strtonum.c
.endif

CFLAGS+= -Wall -Wstrict-prototypes -I${.CURDIR}/../spamd
LDADD+= -lpcap -lcrypto -lmd
DPADD+= ${LIBPCAP} ${LIBMD}
.PATH:  ${.CURDIR}/../spamd

.include <bsd.prog.mk>
