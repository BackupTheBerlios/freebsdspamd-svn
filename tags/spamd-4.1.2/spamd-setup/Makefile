#	$OpenBSD: Makefile,v 1.4 2005/05/24 22:23:04 millert Exp $

PROG=	spamd-setup
SRCS=	spamd-setup.c
MAN=	spamd-setup.8
LDADD=	-lz
DPADD=	${LIBZ}
CFLAGS+= -Wall -Wstrict-prototypes -D_NO_NAMESPACE_POLLUTION

.include <bsd.prog.mk>
