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

.if ${OSVERSION} < 601000
SRCS+=	strtonum.c
.endif
