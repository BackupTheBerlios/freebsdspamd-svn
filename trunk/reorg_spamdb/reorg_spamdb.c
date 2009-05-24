/* $Id: reorg_spamdb.c,v 1.1 2009/05/22 13:02:27 ohauer Exp ohauer $ */

/*
 * Copyright (c) 2002-2007 Bob Beck.  All rights reserved.
 * Copyright (c) 2002 Theo de Raadt.  All rights reserved.
 * Copyright (c) 2009 Olli Hauer.     All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * description:
 *  reorg_spamdb tries to reorganize (decrease) a inflated spamd database
 *  by copying the content into a fresh hash database.
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <db.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "grey.h"
/* test databases, and dummy's */
/* #define PATH_SPAMD_DB "/var/db/spamd.btree" */
/* #define PATH_SPAMD_DB "/var/db/spamd.test" */
/* #define PATH_SPAMD_DB "/var/db/spamd" */

#define PATH_REORG_DB "/var/db/spamd.reorg"
#define DBBTREE  1
#define DBHASH   2


void     usage(void);

#ifdef __OpenBSD__
#define lchmod chmod
struct syslog_data sdata = SYSLOG_DATA_INIT;
#else
#define syslog_r(l, s, args...) syslog(l,args)
#define openlog_r(i, l, f, s)   openlog(i, l, f)
#define closelog_r(l)           closelog()
int sdata = 0;                  /* dummy */
#endif

struct passwd *pw;
struct stat statbuf_in, statbuf_out;
struct stat stat_indb, stat_outdb;
int replace = 0;
int verbose = 0;

char sfn[] = "/var/db/spamd.reorg_XXXXXXXXX";

void
usage(void)
{
    extern char *__progname;
    fprintf(stderr, "usage: %s [rv]\n", __progname);
    exit(1);
}

static void
convert_spamd_db(int dbformat)
{
    char*       tsfn = NULL;
    int         r, fd = -1;
    u_int32_t   count = 0;
    DB          *db1, *db2;
    BTREEINFO   btreeinfo;
    HASHINFO    hashinfo_odb, hashinfo_ndb;
    DBT         dbk, dbd;
    time_t      t_start, t_end;

    if (verbose)
        /* print details for input database */
        printf ("in : %-20s uid: %-4d gid: %-4d mode %04o format: %-5s "
           "size: %-10u (%.3f MiB)\n", PATH_SPAMD_DB, (int)statbuf_in.st_uid,
           (int)statbuf_in.st_gid, statbuf_in.st_mode & 0777,
           dbformat == DBHASH ? "hash" : "btree", (u_int)statbuf_in.st_size,
           (double)statbuf_in.st_size / (1024 * 1024));

    if (dbformat == DBBTREE){
        replace = 0; /* no replace if source db is in btree format */
        /* try to open the db as a BTREE */
        memset(&btreeinfo, 0, sizeof(btreeinfo));
        db1 = dbopen(PATH_SPAMD_DB, O_EXLOCK|O_RDWR, 0600, DB_BTREE, &btreeinfo);
    }
    else {
        /* DB is already in hash format */
        memset(&hashinfo_odb, 0, sizeof(hashinfo_odb));
        db1 = dbopen(PATH_SPAMD_DB, O_EXLOCK|O_RDWR, 0600, DB_HASH, &hashinfo_odb);
    }

    if (db1 == NULL) {
    if (errno == EACCES) {
        syslog_r(LOG_ERR, &sdata,
        "can't open %s in RW mode for UID %d (%m)", PATH_SPAMD_DB, geteuid() );
        err(1, "can't open %s in RW mode for UID %d", PATH_SPAMD_DB, geteuid() );
    }
    else
        syslog_r(LOG_ERR, &sdata,
        "corrupt db in %s, remove and restart (%m)", PATH_SPAMD_DB);
        err(1, "corrupt db in %s, remove and restart", PATH_SPAMD_DB );
    exit(1);
    }

    if ((fd = mkstemp(sfn)) == -1) {
        syslog_r(LOG_ERR, &sdata,
            "can't process %s: mkstemp failed. UID: %d (%m)", PATH_SPAMD_DB, geteuid());
        err(1, "can't process %s: mkstemp failed. UID: %d", PATH_SPAMD_DB, geteuid());
    }

    memset(&hashinfo_ndb, 0, sizeof(hashinfo_ndb));
    db2 = dbopen(sfn, O_EXLOCK|O_RDWR|O_CREAT, 0600, DB_HASH, &hashinfo_ndb);
    if (db2 == NULL) {
        unlink(sfn);
        syslog_r(LOG_ERR, &sdata,
            "can't convert %s: can't dbopen %s (%m)", PATH_SPAMD_DB, sfn);
        db1->close(db1);
        exit(1);
    }

    if (verbose) {
        if (dbformat == DBBTREE)
            printf("convert %s from btree to hash format\n", PATH_SPAMD_DB);
        else
            printf("start reorganization of %s...\n", PATH_SPAMD_DB);
    }

    time(&t_start);
    memset(&dbk, 0, sizeof(dbk));
    memset(&dbd, 0, sizeof(dbd));
    for (r = db1->seq(db1, &dbk, &dbd, R_FIRST); !r;
        r = db1->seq(db1, &dbk, &dbd, R_NEXT)) {
        if (db2->put(db2, &dbk, &dbd, 0)) {
            db2->sync(db2, 0);
            db2->close(db2);
            db1->close(db1);
            unlink(sfn);
            syslog_r(LOG_ERR, &sdata,
                "can't convert %s - remove and restart", PATH_SPAMD_DB);
            exit(1);
        }
        count++;
        if (verbose){
            if ((count % 100) == 0) {
                printf("\rprogress %7u records", count);
                fflush(stdout);
            }
        }
    }
    db2->sync(db2, 0);
    db2->close(db2);
    db1->sync(db1, 0);
    db1->close(db1);

    time(&t_end);
    syslog_r(LOG_INFO, &sdata, "progress %u records: in %.f second(s)",
        count, difftime(t_end, t_start) );

    if (verbose)
        printf("\rprogress %7u records: -> finish in %.f second(s)\n", count,
            difftime(t_end, t_start) );

    if (replace) {
        rename(sfn, PATH_SPAMD_DB);
        tsfn = PATH_SPAMD_DB;
    }
    else {
        rename(sfn, PATH_REORG_DB);
        tsfn = PATH_REORG_DB;
    }

    close(fd);

    /* change owner to _spamd */
    if (pw && (chown(tsfn, pw->pw_uid, pw->pw_gid) == -1)) {
        syslog_r(LOG_ERR, &sdata, "chown %s failed (%m)", tsfn);
        exit(1);
    }
    /* restore file access mode */
    if (lchmod(tsfn, statbuf_in.st_mode) == -1) {
        syslog_r(LOG_ERR, &sdata, "chmod %s failed (%m)", tsfn);
        printf ("chmod %s faild (%s)\n", tsfn, strerror(errno));
        exit(1);
    }

    r = lstat(tsfn, &statbuf_out);
    if (r != -1) {
        if (replace)
            syslog_r(LOG_INFO, &sdata, "replace %s with reorganized db. "
                "old size %u (%.3f MiB), new size: %u (%.3f MiB)",
                PATH_SPAMD_DB, (u_int)statbuf_in.st_size,
                (double)statbuf_in.st_size / (1024 * 1024),
                (u_int)statbuf_out.st_size,
                (double)statbuf_out.st_size / (1024 * 1024));
        else
            syslog_r(LOG_INFO, &sdata, "%s db finished, "
                "size %s : %u (%.3f MiB), new size %s: %u (%.3f MiB)",
                dbformat == DBHASH ? "reorganize" : "convert",
                PATH_SPAMD_DB, (u_int)statbuf_in.st_size,
                (double)statbuf_in.st_size / (1024 * 1024),
                tsfn, (u_int)statbuf_out.st_size,
                (double)statbuf_out.st_size / (1024 * 1024));

    if (verbose)
        /* print details for output database */
        printf ("out: %-20s uid: %-4d gid: %-4d mode %04o format: %-5s "
            "size: %-10u (%.3f MiB)\n", tsfn, (int)statbuf_out.st_uid,
            (int)statbuf_out.st_gid, statbuf_out.st_mode & 0777,
            "hash", (u_int)statbuf_out.st_size,
            (double)statbuf_out.st_size / (1024 * 1024));
    }
}


static void
check_spamd_db(void)
{
    HASHINFO hashinfo;
    DB *db;

    /* check if /var/db/spamd exists, if not, exit */
    memset(&hashinfo, 0, sizeof(hashinfo));
    db = dbopen(PATH_SPAMD_DB, O_EXLOCK, 0600, DB_HASH, &hashinfo);

    if (db == NULL) {
        switch (errno) {
        case EFTYPE:
            /*
             * db may be old BTREE instead of HASH, attempt to
             * convert.
             */
            convert_spamd_db(DBBTREE);
            /* not reached */
            exit(1);
            break;
        default:
            syslog_r(LOG_ERR, &sdata, "open of %s failed (%m)", PATH_SPAMD_DB);
            exit(1);
        }
    }
    db->sync(db, 0);
    db->close(db);
    /* DB format is hash, reorganize */
    convert_spamd_db(DBHASH);
}

static void
sig_term(int sig)
{
    unlink(sfn);
    syslog_r(LOG_ERR, &sdata, "killed by signal %d, unlink temporarily %s", sig, sfn);
    fprintf(stderr, "\nkilled by signal %d, unlink temporarily %s\n", sig, sfn);
    _exit(1);
}

int
main(int argc, char *argv[])
{
    extern char *__progname;
    struct sigaction sa;
    int ch, r;

    while ((ch = getopt(argc, argv, "rv")) != -1) {
        switch (ch) {
        case 'r':
            replace = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            usage();
            break;
        }
    }

    openlog_r("reorg_spamdb", LOG_PID | LOG_NDELAY, LOG_DAEMON, &sdata);

    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sig_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    if ((pw = getpwnam("_spamd")) == NULL)
        errx(1, "no such user _spamd");

    /* check if PATH_SPAMD_DB exist and is a regular file */
    r = lstat(PATH_SPAMD_DB, &statbuf_in);
    if ( r == 0 && !S_ISREG(statbuf_in.st_mode)) {
        syslog_r(LOG_ERR, "exit \"%s\" : Not a regular file", PATH_SPAMD_DB);
        errx(1, "exit \"%s\" : Not a regular file", PATH_SPAMD_DB);
    }
    if (r == -1) {
        syslog_r(LOG_ERR, &sdata, "%s (%m)", PATH_SPAMD_DB);
        err(1, "%s", PATH_SPAMD_DB);
    }
    check_spamd_db();
    closelog_r(&sdata);
    exit(0);
}
