# New ports collection makefile for:	greyscanner
# Date created:		05 Mai 2007
# Whom:			Olli Hauer
#
# $FreeBSD$
#

PORTNAME=	greyscanner
PORTVERSION=	4.2
CATEGORIES=	mail
MASTER_SITES=	# none
DISTFILES=	# none

MAINTAINER=	ohauer@gmx.de
COMMENT=	Script that runs as daemon to cleanup OpenBSD spamd database

RUN_DEPENDS= ${LOCALBASE}/libexec/spamd:${PORTSDIR}/mail/spamd \
	${SITE_PERL}/Email/Valid.pm:${PORTSDIR}/mail/p5-Email-Valid

NO_BUILD=	yes
WRKSRC=		${WRKDIR}/src
PLIST_FILES=	libexec/greyscanner
SUB_FILES=	pkg-message
PORTEXAMPLES=	spamd_rrd

USE_RC_SUBR=	greyscanner.sh
SUB_LIST=	PERL=${PERL}
USE_PERL5_RUN=	yes

SRC=		${.CURDIR}/src

do-fetch:
	@${DO_NADA}

pre-patch:
	@${CP} -R ${SRC} ${WRKDIR}

post-patch:
	@${REINPLACE_CMD} -e 's|/usr/bin/perl|${PERL}|' \
	-e 's|%%PREFIX%%|${PREFIX}|g' \
	${WRKSRC}/greyscanner
	@${REINPLACE_CMD} -e 's|%%PREFIX%%|${PREFIX}|g' \
	${WRKSRC}/spamd_rrd

do-install:
	${INSTALL_SCRIPT} -m 740 ${WRKSRC}/greyscanner ${PREFIX}/libexec
	@${MKDIR} ${EXAMPLESDIR}
	${INSTALL_SCRIPT} -m 740 ${WRKSRC}/spamd_rrd ${EXAMPLESDIR}/

post-install:
	@${CAT} ${PKGMESSAGE}
.include <bsd.port.mk>
