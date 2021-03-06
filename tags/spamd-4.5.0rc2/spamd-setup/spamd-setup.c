/*	$OpenBSD: spamd-setup.c,v 1.35 2008/10/03 18:58:52 jmc Exp $ */

/*
 * Copyright (c) 2003 Bob Beck.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#ifdef __OpenBSD__
#include <netinet/ip_ipsp.h>
#else
#define __dead
#endif
#include <netdb.h>
#include <zlib.h>

#ifdef __FreeBSD__
int 		  use_pf = 1;
int 		  ipfw_tabno = 2;
#include <net/if.h>
#include <sys/queue.h>
#include <netinet/ip_fw.h>
#endif

#define PATH_FTP		"/usr/bin/ftp"
#define PATH_PFCTL		"/sbin/pfctl"
#ifndef PATH_SPAMD_CONF
#define PATH_SPAMD_CONF		"/usr/local/etc/spamd/spamd.conf"
#endif
#define SPAMD_ARG_MAX		256 /* max # of args to an exec */

struct cidr {
	u_int32_t addr;
	u_int8_t bits;
};

struct bl {
	u_int32_t addr;
	int8_t b;
	int8_t w;
};

struct blacklist {
	char *name;
	char *message;
	struct bl *bl;
	size_t blc, bls;
	u_int8_t black;
	int count;
};

u_int32_t	  imask(u_int8_t);
u_int8_t	  maxblock(u_int32_t, u_int8_t);
u_int8_t	  maxdiff(u_int32_t, u_int32_t);
struct cidr	 *range2cidrlist(u_int32_t, u_int32_t);
void		  cidr2range(struct cidr, u_int32_t *, u_int32_t *);
char		 *atop(u_int32_t);
int		  parse_netblock(char *, struct bl *, struct bl *, int);
int		  open_child(char *, char **);
int		  fileget(char *);
int		  open_file(char *, char *);
char		 *fix_quoted_colons(char *);
void		  do_message(FILE *, char *);
struct bl	 *add_blacklist(struct bl *, size_t *, size_t *, gzFile, int);
int		  cmpbl(const void *, const void *);
struct cidr	**collapse_blacklist(struct bl *, size_t);
int		  configure_spamd(u_short, char *, char *, struct cidr **);
int		  configure_pf(struct cidr **);
#ifdef __FreeBSD__
int		  configure_ipfw(struct cidr **);
#endif
int		  getlist(char **, char *, struct blacklist *, struct blacklist *);
__dead void	  usage(void);

int		  debug;
int		  dryrun;
int		  greyonly = 1;

extern char 	 *__progname;

u_int32_t
imask(u_int8_t b)
{
	u_int32_t j = 0;
	int i;

	for (i = 31; i > 31 - b; --i)
		j |= (1 << i);
	return (j);
}

u_int8_t
maxblock(u_int32_t addr, u_int8_t bits)
{
	u_int32_t m;

	while (bits > 0) {
		m = imask(bits - 1);

		if ((addr & m) != addr)
			return (bits);
		bits--;
	}
	return (bits);
}

u_int8_t
maxdiff(u_int32_t a, u_int32_t b)
{
	u_int8_t bits = 0;
	u_int32_t m;

	b++;
	while (bits < 32) {
		m = imask(bits);

		if ((a & m) != (b & m))
			return (bits);
		bits++;
	}
	return (bits);
}

struct cidr *
range2cidrlist(u_int32_t start, u_int32_t end)
{
	struct cidr *list = NULL;
	size_t cs = 0, cu = 0;
	u_int8_t maxsize, diff;
	struct cidr *tmp;

	while (end >= start) {
		maxsize = maxblock(start, 32);
		diff = maxdiff(start, end);

		maxsize = MAX(maxsize, diff);
		if (cs <= cu + 1) {		/* one extra for terminator */
			tmp = realloc(list, (cs + 32) * sizeof(struct cidr));
			if (tmp == NULL)
				errx(1, "malloc failed");
			list = tmp;
			cs += 32;
		}
		list[cu].addr = start;
		list[cu].bits = maxsize;
		cu++;
		list[cu].addr = 0;
		list[cu].bits = 0;
		start = start + (1 << (32 - maxsize));
	}
	return (list);
}

void
cidr2range(struct cidr cidr, u_int32_t *start, u_int32_t *end)
{
	*start = cidr.addr;
	*end = cidr.addr + (1 << (32 - cidr.bits)) - 1;
}

char *
atop(u_int32_t addr)
{
	struct in_addr in;

	memset(&in, 0, sizeof(in));
	in.s_addr = htonl(addr);
	return (inet_ntoa(in));
}

int
parse_netblock(char *buf, struct bl *start, struct bl *end, int white)
{
	char astring[16], astring2[16];
	unsigned maskbits;
	struct cidr c;

	/* skip leading spaces */
	while (*buf == ' ')
		buf++;
	/* bail if it's a comment */
	if (*buf == '#')
		return (0);
	/* otherwise, look for a netblock of some sort */
	if (sscanf(buf, "%15[^/]/%u", astring, &maskbits) == 2) {
		/* looks like a cidr */
		memset(&c.addr, 0, sizeof(c.addr));
		if (inet_net_pton(AF_INET, astring, &c.addr, sizeof(c.addr))
		    == -1)
			return (0);
		c.addr = ntohl(c.addr);
		if (maskbits > 32)
			return (0);
		c.bits = maskbits;
		cidr2range(c, &start->addr, &end->addr);
		end->addr += 1;
	} else if (sscanf(buf, "%15[0123456789.]%*[ -]%15[0123456789.]",
	    astring, astring2) == 2) {
		/* looks like start - end */
		memset(&start->addr, 0, sizeof(start->addr));
		memset(&end->addr, 0, sizeof(end->addr));
		if (inet_net_pton(AF_INET, astring, &start->addr,
		    sizeof(start->addr)) == -1)
			return (0);
		start->addr = ntohl(start->addr);
		if (inet_net_pton(AF_INET, astring2, &end->addr,
		    sizeof(end->addr)) == -1)
			return (0);
		end->addr = ntohl(end->addr) + 1;
		if (start > end)
			return (0);
	} else if (sscanf(buf, "%15[0123456789.]", astring) == 1) {
		/* just a single address */
		memset(&start->addr, 0, sizeof(start->addr));
		if (inet_net_pton(AF_INET, astring, &start->addr,
		    sizeof(start->addr)) == -1)
			return (0);
		start->addr = ntohl(start->addr);
		end->addr = start->addr + 1;
	} else
		return (0);

	if (white) {
		start->b = 0;
		start->w = 1;
		end->b = 0;
		end->w = -1;
	} else {
		start->b = 1;
		start->w = 0;
		end->b = -1;
		end->w = 0;
	}
	return (1);
}

int
open_child(char *file, char **argv)
{
	int pdes[2];

	if (pipe(pdes) != 0)
		return (-1);
	switch (fork()) {
	case -1:
		close(pdes[0]);
		close(pdes[1]);
		return (-1);
	case 0:
		/* child */
		close(pdes[0]);
		if (pdes[1] != STDOUT_FILENO) {
			dup2(pdes[1], STDOUT_FILENO);
			close(pdes[1]);
		}
		execvp(file, argv);
		_exit(1);
	}

	/* parent */
	close(pdes[1]);
	return (pdes[0]);
}

int
fileget(char *url)
{
	char *argv[6];

	argv[0] = "ftp";
	argv[1] = "-V";
	argv[2] = "-o";
	argv[3] = "-";
	argv[4] = url;
	argv[5] = NULL;

	if (debug)
		fprintf(stderr, "Getting %s\n", url);

	return (open_child(PATH_FTP, argv));
}

int
open_file(char *method, char *file)
{
	char *url;
	char **ap, **argv;
	int len, i, oerrno;

	if ((method == NULL) || (strcmp(method, "file") == 0))
		return (open(file, O_RDONLY));
	if ((strcmp(method, "http") == 0) ||
	    strcmp(method, "ftp") == 0) {
		asprintf(&url, "%s://%s", method, file);
		if (url == NULL)
			return (-1);
		i = fileget(url);
		free(url);
		return (i);
	} else if (strcmp(method, "exec") == 0) {
		len = strlen(file);
		argv = calloc(len, sizeof(char *));
		if (argv == NULL)
			errx(1, "malloc failed");
		for (ap = argv; ap < &argv[len - 1] &&
		    (*ap = strsep(&file, " \t")) != NULL;) {
			if (**ap != '\0')
				ap++;
		}
		*ap = NULL;
		i = open_child(argv[0], argv);
		oerrno = errno;
		free(argv);
		errno = oerrno;
		return (i);
	}
	errx(1, "Unknown method %s", method);
	return (-1); /* NOTREACHED */
}

/*
 * fix_quoted_colons walks through a buffer returned by cgetent.  We
 * look for quoted strings, to escape colons (:) in quoted strings for
 * getcap by replacing them with \C so cgetstr() deals with it correctly
 * without having to see the \C bletchery in a configuration file that
 * needs to have urls in it. Frees the buffer passed to it, passes back
 * another larger one, with can be used with cgetxxx(), like the original
 * buffer, it must be freed by the caller.
 * This should really be a temporary fix until there is a sanctioned
 * way to make getcap(3) handle quoted strings like this in a nicer
 * way.
 */
char *
fix_quoted_colons(char *buf)
{
	int in = 0;
	size_t i, j = 0;
	char *newbuf, last;

	/* Allocate enough space for a buf of all colons (impossible). */
	newbuf = malloc(2 * strlen(buf) + 1);
	if (newbuf == NULL)
		return (NULL);
	last = '\0';
	for (i = 0; i < strlen(buf); i++) {
		switch (buf[i]) {
		case ':':
			if (in) {
				newbuf[j++] = '\\';
				newbuf[j++] = 'C';
			} else
				newbuf[j++] = buf[i];
			break;
		case '"':
			if (last != '\\')
				in = !in;
			newbuf[j++] = buf[i];
			break;
		default:
			newbuf[j++] = buf[i];
		}
		last = buf[i];
	}
	free(buf);
	newbuf[j] = '\0';
	return (newbuf);
}

void
do_message(FILE *sdc, char *msg)
{
	size_t i, bs = 0, bu = 0, len;
	ssize_t n;	
	char *buf = NULL, last, *tmp;
	int fd;

	len = strlen(msg);
	if (msg[0] == '"' && msg[len - 1] == '"') {
		/* quoted msg, escape newlines and send it out */
		msg[len - 1] = '\0';
		buf = msg + 1;
		bu = len - 2;
		goto sendit;
	} else {
		/*
		 * message isn't quoted - try to open a local
		 * file and read the message from it.
		 */
		fd = open(msg, O_RDONLY);
		if (fd == -1)
			err(1, "Can't open message from %s", msg);
		for (;;) {
			if (bu == bs) {
				tmp = realloc(buf, bs + 8192);
				if (tmp == NULL)
					errx(1, "malloc failed");
				bs += 8192;
				buf = tmp;
			}

			n = read(fd, buf + bu, bs - bu);
			if (n == 0) {
				goto sendit;
			} else if (n == -1) {
				err(1, "Can't read from %s", msg);
			} else
				bu += n;
		}
		buf[bu]='\0';
	}
 sendit:
	fprintf(sdc, ";\"");
	last = '\0';
	for (i = 0; i < bu; i++) {
		/* handle escaping the things spamd wants */
		switch (buf[i]) {
		case 'n':
			if (last == '\\')
				fprintf(sdc, "\\\\n");
			else
				fputc('n', sdc);
			last = '\0';
			break;
		case '\n':
			fprintf(sdc, "\\n");
			last = '\0';
			break;
		case '"':
			fputc('\\', sdc);
			/* FALLTHROUGH */
		default:
			fputc(buf[i], sdc);
			last = '\0';
		}
	}
	fputc('"', sdc);
	if (bs != 0)
		free(buf);
}

/* retrieve a list from fd. add to blacklist bl */
struct bl *
add_blacklist(struct bl *bl, size_t *blc, size_t *bls, gzFile gzf, int white)
{
	int i, n, start, bu = 0, bs = 0, serrno = 0;
	char *buf = NULL, *tmp;
	struct bl *blt;

	for (;;) {
		/* read in gzf, then parse */
		if (bu == bs) {
			tmp = realloc(buf, bs + (1024 * 1024) + 1);
			if (tmp == NULL) {
				free(buf);
				buf = NULL;
				bs = 0;
				serrno = errno;
				goto bldone;
			}
			bs += 1024 * 1024;
			buf = tmp;
		}

		n = gzread(gzf, buf + bu, bs - bu);
		if (n == 0)
			goto parse;
		else if (n == -1) {
			serrno = errno;
			goto bldone;
		} else
			bu += n;
	}
 parse:
	start = 0;
	for (i = 0; i <= bu; i++) {
		if (*blc == *bls) {
			*bls += 1024;
			blt = realloc(bl, *bls * sizeof(struct bl));
			if (blt == NULL) {
				*bls -= 1024;
				serrno = errno;
				goto bldone;
			}
			bl = blt;
		}
		if (i == bu || buf[i] == '\n') {
			buf[i] = '\0';
			if (parse_netblock(buf + start,
			    bl + *blc, bl + *blc + 1, white))
				*blc += 2;
			start = i + 1;
		}
	}
	if (bu == 0)
		errno = EIO;
 bldone:
	if (buf)
		free(buf);
	if (serrno)
		errno = serrno;
	return (bl);
}

int
cmpbl(const void *a, const void *b)
{
	if (((struct bl *)a)->addr > ((struct bl *) b)->addr)
		return (1);
	if (((struct bl *)a)->addr < ((struct bl *) b)->addr)
		return (-1);
	return (0);
}

/*
 * collapse_blacklist takes blacklist/whitelist entries sorts, removes
 * overlaps and whitelist portions, and returns netblocks to blacklist
 * as lists of nonoverlapping cidr blocks suitable for feeding in
 * printable form to pfctl or spamd.
 */
struct cidr **
collapse_blacklist(struct bl *bl, size_t blc)
{
	int bs = 0, ws = 0, state=0, cli, i;
	u_int32_t bstart = 0;
	struct cidr **cl;
	int laststate;
	u_int32_t addr;

	if (blc == 0)
		return (NULL);
	cl = calloc(((blc / 2) + 1), sizeof(struct cidr));
	if (cl == NULL) {
		return (NULL);
	}
	qsort(bl, blc, sizeof(struct bl), cmpbl);
	cli = 0;
	cl[cli] = NULL;
	for (i = 0; i < blc;) {
		laststate = state;
		addr = bl[i].addr;

		do {
			bs += bl[i].b;
			ws += bl[i].w;
			i++;
		} while (bl[i].addr == addr);
		if (state == 1 && bs == 0)
			state = 0;
		else if (state == 0 && bs > 0)
			state = 1;
		if (ws > 0)
			state = 0;
		if (laststate == 0 && state == 1) {
			/* start blacklist */
			bstart = addr;
		}
		if (laststate == 1 && state == 0) {
			/* end blacklist */
			cl[cli++] = range2cidrlist(bstart, addr - 1);
			cl[cli] = NULL;
		}
		laststate = state;
	}
	return (cl);
}

int
configure_spamd(u_short dport, char *name, char *message,
    struct cidr **blacklists)
{
	int lport = IPPORT_RESERVED - 1, s;
	struct sockaddr_in sin;
	FILE* sdc;

	s = rresvport(&lport);
	if (s == -1)
		return (-1);
	memset(&sin, 0, sizeof sin);
	sin.sin_len = sizeof(sin);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(dport);
	if (connect(s, (struct sockaddr *)&sin, sizeof sin) == -1)
		return (-1);
	sdc = fdopen(s, "w");
	if (sdc == NULL) {
		close(s);
		return (-1);
	}
	fprintf(sdc, "%s", name);
	do_message(sdc, message);
	while (*blacklists != NULL) {
		struct cidr *b = *blacklists;
		while (b->addr != 0) {
			fprintf(sdc, ";%s/%u", atop(b->addr), (b->bits));
			b++;
		}
		blacklists++;
	}
	fputc('\n', sdc);
	fclose(sdc);
	close(s);
	return (0);
}


int
configure_pf(struct cidr **blacklists)
{
	char *argv[9]= {"pfctl", "-q", "-t", "spamd", "-T", "replace",
	    "-f" "-", NULL};
	static FILE *pf = NULL;
	int pdes[2];

	if (pf == NULL) {
		if (pipe(pdes) != 0)
			return (-1);
		switch (fork()) {
		case -1:
			close(pdes[0]);
			close(pdes[1]);
			return (-1);
		case 0:
			/* child */
			close(pdes[1]);
			if (pdes[0] != STDIN_FILENO) {
				dup2(pdes[0], STDIN_FILENO);
				close(pdes[0]);
			}
			execvp(PATH_PFCTL, argv);
			_exit(1);
		}

		/* parent */
		close(pdes[0]);
		pf = fdopen(pdes[1], "w");
		if (pf == NULL) {
			close(pdes[1]);
			return (-1);
		}
	}
	while (*blacklists != NULL) {
		struct cidr *b = *blacklists;

		while (b->addr != 0) {
			fprintf(pf, "%s/%u\n", atop(b->addr), (b->bits));
			b++;
		}
		blacklists++;
	}
	return (0);
}

#ifdef __FreeBSD__
int
configure_ipfw(struct cidr **blacklists)
{
	static int s = -1;
	ipfw_table_entry ent;

	if (s == -1)
		s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (s < 0)
	{
		err(1, "IPFW socket unavailable");
		return (-1);
	}

	/* flush the table */
	ent.tbl = ipfw_tabno;
	if (setsockopt(s, IPPROTO_IP, IP_FW_TABLE_FLUSH,  &ent.tbl, sizeof(ent.tbl)) < 0)
	{
		err(1, "IPFW setsockopt(IP_FW_TABLE_FLUSH)");
		return (-1);
	}

	while (*blacklists != NULL) {
		struct cidr *b = *blacklists;

		while (b->addr != 0) {
			/* add b to ipfw_tabno */
			ent.tbl = ipfw_tabno;
			ent.masklen = b->bits;
			ent.value = 0;
			inet_aton(atop(b->addr), (struct in_addr *)&ent.addr);
			if (setsockopt(s, IPPROTO_IP, IP_FW_TABLE_ADD,  &ent, sizeof(ent)) < 0)
			{
				err(1, "IPFW setsockopt(IP_FW_TABLE_ADD)");
				return (-1);
			}
			b++;
		}
		blacklists++;
	}

	return(0);
}
#endif

int
getlist(char ** db_array, char *name, struct blacklist *blist,
    struct blacklist *blistnew)
{
	char *buf, *method, *file, *message;
	int fd, black = 0;
	size_t blc, bls;
	struct bl *bl = NULL;
	gzFile gzf;

	if (cgetent(&buf, db_array, name) != 0)
		err(1, "Can't find \"%s\" in spamd config", name);
	buf = fix_quoted_colons(buf);
	if (cgetcap(buf, "black", ':') != NULL) {
		/* use new list */
		black = 1;
		blc = blistnew->blc;
		bls = blistnew->bls;
		bl = blistnew->bl;
	} else if (cgetcap(buf, "white", ':') != NULL) {
		/* apply to most recent blacklist */
		black = 0;
		blc = blist->blc;
		bls = blist->bls;
		bl = blist->bl;
	} else
		errx(1, "Must have \"black\" or \"white\" in %s", name);

	switch (cgetstr(buf, "msg", &message)) {
	case -1:
		if (black)
			errx(1, "No msg for blacklist \"%s\"", name);
		break;
	case -2:
		errx(1, "malloc failed");
	}

	switch (cgetstr(buf, "method", &method)) {
	case -1:
		method = NULL;
		break;
	case -2:
		errx(1, "malloc failed");
	}

	switch (cgetstr(buf, "file", &file)) {
	case -1:
		errx(1, "No file given for %slist %s",
		    black ? "black" : "white", name);
	case -2:
		errx(1, "malloc failed");
	default:
		fd = open_file(method, file);
		if (fd == -1)
			err(1, "Can't open %s by %s method",
			    file, method ? method : "file");
		free(method);
		free(file);
		gzf = gzdopen(fd, "r");
		if (gzf == NULL)
			errx(1, "gzdopen");
	}
	free(buf);
	bl = add_blacklist(bl, &blc, &bls, gzf, !black);
	gzclose(gzf);
	if (bl == NULL) {
		warn("Could not add %slist %s", black ? "black" : "white",
		    name);
		return (0);
	}
	if (black) {
		blistnew->message = message;
		blistnew->name = name;
		blistnew->black = black;
		blistnew->bl = bl;
		blistnew->blc = blc;
		blistnew->bls = bls;
	} else {
		/* whitelist applied to last active blacklist */
		blist->bl = bl;
		blist->blc = blc;
		blist->bls = bls;
	}
	if (debug)
		fprintf(stderr, "%slist %s %zu entries\n",
		    black ? "black" : "white", name, blc / 2);
	return (black);
}

void
send_blacklist(struct blacklist *blist, in_port_t port)
{
	struct cidr **cidrs, **tmp;

	if (blist->blc > 0) {
		cidrs = collapse_blacklist(blist->bl, blist->blc);
		if (cidrs == NULL)
			errx(1, "malloc failed");
		if (!dryrun) {
			if (configure_spamd(port, blist->name,
			    blist->message, cidrs) == -1)
				err(1, "Can't connect to spamd on port %d",
				    port);
#ifndef __FreeBSD__					
			if (!greyonly && configure_pf(cidrs) == -1)
				err(1, "pfctl failed");
#else
		if(use_pf){
			if (!greyonly && configure_pf(cidrs) == -1)
				err(1, "pfctl failed");
		}
		else{
			if (!greyonly && configure_ipfw(cidrs) == -1)
				err(1, "ipfw failed");
		}
#endif
		}
		for (tmp = cidrs; *tmp != NULL; tmp++)
			free(*tmp);
		free(cidrs);
		free(blist->bl);
	}
}

__dead void
usage(void)
{

#ifndef __FreeBSD__
	fprintf(stderr, "usage: %s [-bDdn]\n", __progname);
#else	
	fprintf(stderr, "usage: %s [-bDdnmt]\n", __progname);
#endif
	exit(1);
}

int
main(int argc, char *argv[])
{
	size_t dbs, dbc, blc, bls, black, white;
	char **db_array, *buf, *name;
	struct blacklist *blists;
	struct servent *ent;
	int daemonize = 0, i, ch;

#ifndef __FreeBSD__
	while ((ch = getopt(argc, argv, "bdDn")) != -1) {
#else
	while ((ch = getopt(argc, argv, "bdDnm:t:")) != -1) {
#endif
		switch (ch) {
		case 'n':
			dryrun = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'b':
			greyonly = 0;
			break;
#ifdef __FreeBSD__
		case 't':
			ipfw_tabno = atoi(optarg);
			break;
		case 'm':
			if (strcmp(optarg, "ipfw") == 0)
				use_pf=0;
			break;
#endif
		case 'D':
			daemonize = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage();

	if (daemonize)
		daemon(0, 0);

	if ((ent = getservbyname("spamd-cfg", "tcp")) == NULL)
		errx(1, "cannot find service \"spamd-cfg\" in /etc/services");
	ent->s_port = ntohs(ent->s_port);

	dbs = argc + 2;
	dbc = 0;
	db_array = calloc(dbs, sizeof(char *));
	if (db_array == NULL)
		errx(1, "malloc failed");

	db_array[dbc]= PATH_SPAMD_CONF;
	dbc++;
	for (i = 1; i < argc; i++)
		db_array[dbc++] = argv[i];

	blists = NULL;
	blc = bls = 0;
	if (cgetent(&buf, db_array, "all") != 0)
		err(1, "Can't find \"all\" in spamd config");
	name = strsep(&buf, ": \t"); /* skip "all" at start */
	blc = 0;
	while ((name = strsep(&buf, ": \t")) != NULL) {
		if (*name) {
			/* extract config in order specified in "all" tag */
			if (blc == bls) {
				struct blacklist *tmp;

				bls += 32;
				tmp = realloc(blists,
				    bls * sizeof(struct blacklist));
				if (tmp == NULL)
					errx(1, "malloc failed");
				blists = tmp;
			}
			if (blc == 0)
				black = white = 0;
			else {
				white = blc - 1;
				black = blc;
			}
			memset(&blists[black], 0, sizeof(struct blacklist));
			black = getlist(db_array, name, &blists[white],
			    &blists[black]);
			if (black && blc > 0) {
				/* collapse and free previous blacklist */
				send_blacklist(&blists[blc - 1], ent->s_port);
			}
			blc += black;
		}
	}
	/* collapse and free last blacklist */
	if (blc > 0)
		send_blacklist(&blists[blc - 1], ent->s_port);
	return (0);
}
