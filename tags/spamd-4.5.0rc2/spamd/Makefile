#	$OpenBSD: Makefile,v 1.9 2007/03/04 03:19:41 beck Exp $

PROG=	spamd
SRCS=	spamd.c sdl.c grey.c sync.c
MAN=	spamd.8

.include "${.CURDIR}/../osver.mk"

CFLAGS+= -Wall -Wstrict-prototypes

LDADD+=	-lcrypto -lmd
DPADD+=	${LIBCRYPTO} ${LIBMD}

.include <bsd.prog.mk>
