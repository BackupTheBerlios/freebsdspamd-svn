#	$OpenBSD: Makefile,v 1.9 2007/03/04 03:19:41 beck Exp $


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

PROG=	spamd
SRCS=	spamd.c sdl.c grey.c sync.c

.if ${OSVERSION} < 601000
SRCS+=	strtonum.c
.endif

MAN=	spamd.8

CFLAGS+= -Wall -Wstrict-prototypes

LDADD+=	-lcrypto -lmd
DPADD+=	${LIBCRYPTO} ${LIBMD}

.include <bsd.prog.mk>
