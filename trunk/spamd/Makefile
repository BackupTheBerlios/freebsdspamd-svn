#	$OpenBSD: Makefile,v 1.9 2007/03/04 03:19:41 beck Exp $

PROG=	spamd
SRCS=	spamd.c sdl.c grey.c sync.c

#if __FreeBSD_version < 601000
SRCS+=	strtonum.c
#endif

MAN=	spamd.8

CFLAGS+= -Wall -Wstrict-prototypes

LDADD+=	-lcrypto -lmd
DPADD+=	${LIBCRYPTO} ${LIBMD}

.include <bsd.prog.mk>
