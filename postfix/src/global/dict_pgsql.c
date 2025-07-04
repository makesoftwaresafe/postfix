/*++
/* NAME
/*	dict_pgsql 3
/* SUMMARY
/*	dictionary manager interface to PostgreSQL databases
/* SYNOPSIS
/*	#include <dict_pgsql.h>
/*
/*	DICT	*dict_pgsql_open(name, open_flags, dict_flags)
/*	const char *name;
/*	int	open_flags;
/*	int	dict_flags;
/* DESCRIPTION
/*	dict_pgsql_open() creates a dictionary of type 'pgsql'.  This
/*	dictionary is an interface for the postfix key->value mappings
/*	to pgsql.  The result is a pointer to the installed dictionary,
/*	or a null pointer in case of problems.
/*
/*	The pgsql dictionary can manage multiple connections to
/*	different sql servers for the same database.  It assumes that
/*	the underlying data on each server is identical (mirrored) and
/*	maintains one connection at any given time.  If any connection
/*	fails,  any other available ones will be opened and used.
/*	The intent of this feature is to eliminate a single point of
/*	failure for mail systems that would otherwise rely on a single
/*	pgsql server.
/* .PP
/*	Arguments:
/* .IP name
/*	Either the path to the PostgreSQL configuration file (if it
/*	starts with '/' or '.'), or the prefix which will be used to
/*	obtain main.cf configuration parameters for this search.
/*
/*	In the first case, the configuration parameters below are
/*	specified in the file as \fIname\fR=\fIvalue\fR pairs.
/*
/*	In the second case, the configuration parameters are
/*	prefixed with the value of \fIname\fR and an underscore,
/*	and they are specified in main.cf.  For example, if this
/*	value is \fIpgsqlsource\fR, the parameters would look like
/*	\fIpgsqlsource_user\fR, \fIpgsqlsource_table\fR, and so on.
/* .IP other_name
/*	reference for outside use.
/* .IP open_flags
/*	Must be O_RDONLY.
/* .IP dict_flags
/*	See dict_open(3).
/* SEE ALSO
/*	dict(3) generic dictionary manager
/*	pgsql_table(5) PostgreSQL client configuration
/* AUTHOR(S)
/*	Aaron Sethman
/*	androsyn@ratbox.org
/*
/*	Based upon dict_mysql.c by
/*
/*	Scott Cotton
/*	IC Group, Inc.
/*	scott@icgroup.com
/*
/*	Joshua Marcus
/*	IC Group, Inc.
/*	josh@icgroup.com
/*--*/

/* System library. */

#include "sys_defs.h"

#ifdef HAS_PGSQL
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>

#include <postgres_ext.h>
#include <libpq-fe.h>

/* Utility library. */

#include "dict.h"
#include "msg.h"
#include "mymalloc.h"
#include "argv.h"
#include "vstring.h"
#include "split_at.h"
#include "myrand.h"
#include "events.h"
#include "stringops.h"
#include "valid_uri_scheme.h"

/* Global library. */

#include "cfg_parser.h"
#include "db_common.h"

/* Application-specific. */

#include "dict_pgsql.h"

#define STATACTIVE			(1<<0)
#define STATFAIL			(1<<1)
#define STATUNTRIED			(1<<2)

#define TYPEUNIX			(1<<0)
#define TYPEINET			(1<<1)
#define TYPECONNSTR			(1<<2)

#define RETRY_CONN_MAX			100
#define DEF_RETRY_INTV			60	/* 1 minute */
#define DEF_IDLE_INTV			60	/* 1 minute */

typedef struct {
    PGconn *db;
    char   *hostname;
    char   *name;
    char   *port;
    unsigned type;			/* TYPEUNIX | TYPEINET | TYPECONNSTR */
    unsigned stat;			/* STATUNTRIED | STATFAIL | STATCUR */
    time_t  ts;				/* used for attempting reconnection */
} HOST;

typedef struct {
    int     len_hosts;			/* number of hosts */
    HOST  **db_hosts;			/* hosts on which databases reside */
    char   *non_uri_target;		/* require dbname to be specified */
} PLPGSQL;

typedef struct {
    DICT    dict;
    CFG_PARSER *parser;
    char   *query;
    char   *result_format;
    void   *ctx;
    int     expansion_limit;
    char   *username;
    char   *password;
    char   *dbname;
    char   *encoding;
    int     retry_interval;
    int     idle_interval;
    char   *table;
    ARGV   *hosts;
    PLPGSQL *pldb;
    HOST   *active_host;
} DICT_PGSQL;


/* Just makes things a little easier for me.. */
#define PGSQL_RES PGresult

/* internal function declarations */
static PLPGSQL *plpgsql_init(ARGV *);
static PGSQL_RES *plpgsql_query(DICT_PGSQL *, const char *, VSTRING *);
static void plpgsql_dealloc(PLPGSQL *);
static void plpgsql_close_host(HOST *);
static void plpgsql_down_host(HOST *, int);
static void plpgsql_connect_single(DICT_PGSQL *, HOST *);
static const char *dict_pgsql_lookup(DICT *, const char *);
DICT   *dict_pgsql_open(const char *, int, int);
static void dict_pgsql_close(DICT *);
static HOST *host_init(const char *);

/* dict_pgsql_quote - escape SQL metacharacters in input string */

static void dict_pgsql_quote(DICT *dict, const char *name, VSTRING *result)
{
    DICT_PGSQL *dict_pgsql = (DICT_PGSQL *) dict;
    HOST   *active_host = dict_pgsql->active_host;
    char   *myname = "dict_pgsql_quote";
    size_t  len = strlen(name);
    size_t  buflen;
    int     err = 1;

    if (active_host == 0)
	msg_panic("%s: bogus dict_pgsql->active_host", myname);

    /*
     * We won't get arithmetic overflows in 2*len + 1, because Postfix input
     * keys have reasonable size limits, better safe than sorry.
     */
    if (len > (SSIZE_T_MAX - VSTRING_LEN(result) - 1) / 2)
	msg_panic("%s: arithmetic overflow in %lu+2*%lu+1",
		  myname, (unsigned long) VSTRING_LEN(result),
		  (unsigned long) len);
    buflen = 2 * len + 1;

    /*
     * XXX Workaround: stop further processing when PQescapeStringConn()
     * (below) fails. A more proper fix requires invasive changes, not
     * suitable for a stable release.
     */
    if (active_host->stat == STATFAIL)
	return;

    /*
     * Escape the input string, using PQescapeStringConn(), because the older
     * PQescapeString() is not safe anymore, as stated by the documentation.
     * 
     * From current libpq (8.1.4) documentation:
     * 
     * PQescapeStringConn writes an escaped version of the from string to the to
     * buffer, escaping special characters so that they cannot cause any
     * harm, and adding a terminating zero byte.
     * 
     * ...
     * 
     * The parameter from points to the first character of the string that is to
     * be escaped, and the length parameter gives the number of bytes in this
     * string. A terminating zero byte is not required, and should not be
     * counted in length.
     * 
     * ...
     * 
     * (The parameter) to shall point to a buffer that is able to hold at least
     * one more byte than twice the value of length, otherwise the behavior
     * is undefined.
     * 
     * ...
     * 
     * If the error parameter is not NULL, then *error is set to zero on
     * success, nonzero on error ... The output string is still generated on
     * error, but it can be expected that the server will reject it as
     * malformed. On error, a suitable message is stored in the conn object,
     * whether or not error is NULL.
     */
    VSTRING_SPACE(result, buflen);
    PQescapeStringConn(active_host->db, vstring_end(result), name, len, &err);
    if (err == 0) {
	VSTRING_SKIP(result);
    } else {

	/*
	 * PQescapeStringConn() failed. According to the docs, we still have
	 * a valid, null-terminated output string, but we need not rely on
	 * this behavior.
	 */
	msg_warn("dict pgsql: (host %s) cannot escape input string: %s",
		 active_host->hostname, PQerrorMessage(active_host->db));
	active_host->stat = STATFAIL;
	VSTRING_TERMINATE(result);
    }
}

/* dict_pgsql_lookup - find database entry */

static const char *dict_pgsql_lookup(DICT *dict, const char *name)
{
    const char *myname = "dict_pgsql_lookup";
    PGSQL_RES *query_res;
    DICT_PGSQL *dict_pgsql;
    static VSTRING *query;
    static VSTRING *result;
    int     i;
    int     j;
    int     numrows;
    int     numcols;
    int     expansion;
    const char *r;
    int     domain_rc;

    dict_pgsql = (DICT_PGSQL *) dict;

#define INIT_VSTR(buf, len) do { \
	if (buf == 0) \
	    buf = vstring_alloc(len); \
	VSTRING_RESET(buf); \
	VSTRING_TERMINATE(buf); \
    } while (0)

    INIT_VSTR(query, 10);
    INIT_VSTR(result, 10);

    dict->error = 0;

    /*
     * Don't frustrate future attempts to make Postfix UTF-8 transparent.
     */
#ifdef SNAPSHOT
    if ((dict->flags & DICT_FLAG_UTF8_ACTIVE) == 0
	&& !valid_utf8_stringz(name)) {
	if (msg_verbose)
	    msg_info("%s: %s: Skipping lookup of non-UTF-8 key '%s'",
		     myname, dict_pgsql->parser->name, name);
	return (0);
    }
#endif

    /*
     * Optionally fold the key.
     */
    if (dict->flags & DICT_FLAG_FOLD_FIX) {
	if (dict->fold_buf == 0)
	    dict->fold_buf = vstring_alloc(10);
	vstring_strcpy(dict->fold_buf, name);
	name = lowercase(vstring_str(dict->fold_buf));
    }

    /*
     * If there is a domain list for this map, then only search for addresses
     * in domains on the list. This can significantly reduce the load on the
     * server.
     */
    if ((domain_rc = db_common_check_domain(dict_pgsql->ctx, name)) == 0) {
	if (msg_verbose)
	    msg_info("%s: Skipping lookup of '%s'", myname, name);
	return (0);
    }
    if (domain_rc < 0)
	DICT_ERR_VAL_RETURN(dict, domain_rc, (char *) 0);

    /*
     * Suppress the actual lookup if the expansion is empty.
     * 
     * This initial expansion is outside the context of any specific host
     * connection, we just want to check the key pre-requisites, so when
     * quoting happens separately for each connection, we don't bother with
     * quoting...
     */
    if (!db_common_expand(dict_pgsql->ctx, dict_pgsql->query,
			  name, 0, query, 0))
	return (0);

    /* do the query - set dict->error & cleanup if there's an error */
    if ((query_res = plpgsql_query(dict_pgsql, name, query)) == 0) {
	dict->error = DICT_ERR_RETRY;
	return 0;
    }
    numrows = PQntuples(query_res);
    if (msg_verbose)
	msg_info("%s: retrieved %d rows", myname, numrows);
    if (numrows == 0) {
	PQclear(query_res);
	return 0;
    }
    numcols = PQnfields(query_res);

    for (expansion = i = 0; i < numrows && dict->error == 0; i++) {
	for (j = 0; j < numcols; j++) {
	    r = PQgetvalue(query_res, i, j);
	    if (db_common_expand(dict_pgsql->ctx, dict_pgsql->result_format,
				 r, name, result, 0)
		&& dict_pgsql->expansion_limit > 0
		&& ++expansion > dict_pgsql->expansion_limit) {
		msg_warn("%s: %s: Expansion limit exceeded for key: '%s'",
			 myname, dict_pgsql->parser->name, name);
		dict->error = DICT_ERR_RETRY;
		break;
	    }
	}
    }
    PQclear(query_res);
    r = vstring_str(result);
    return ((dict->error == 0 && *r) ? r : 0);
}

/* dict_pgsql_check_stat - check the status of a host */

static int dict_pgsql_check_stat(HOST *host, unsigned stat, unsigned type,
				         time_t t)
{
    if ((host->stat & stat) && (!type || host->type & type)) {
	/* try not to hammer the dead hosts too often */
	if (host->stat == STATFAIL && host->ts > 0 && host->ts >= t)
	    return 0;
	return 1;
    }
    return 0;
}

/* dict_pgsql_find_host - find a host with the given status */

static HOST *dict_pgsql_find_host(PLPGSQL *PLDB, unsigned stat, unsigned type)
{
    time_t  t;
    int     count = 0;
    int     idx;
    int     i;

    t = time((time_t *) 0);
    for (i = 0; i < PLDB->len_hosts; i++) {
	if (dict_pgsql_check_stat(PLDB->db_hosts[i], stat, type, t))
	    count++;
    }

    if (count) {
	idx = (count > 1) ?
	    1 + count * (double) myrand() / (1.0 + RAND_MAX) : 1;

	for (i = 0; i < PLDB->len_hosts; i++) {
	    if (dict_pgsql_check_stat(PLDB->db_hosts[i], stat, type, t) &&
		--idx == 0)
		return PLDB->db_hosts[i];
	}
    }
    return 0;
}

/* dict_pgsql_get_active - get an active connection */

static HOST *dict_pgsql_get_active(DICT_PGSQL *dict_pgsql, PLPGSQL *PLDB)
{
    const char *myname = "dict_pgsql_get_active";
    HOST   *host;
    int     count = RETRY_CONN_MAX;

    /* try the active connections first; prefer the ones to UNIX sockets */
    if ((host = dict_pgsql_find_host(PLDB, STATACTIVE, TYPEUNIX)) != NULL ||
	(host = dict_pgsql_find_host(PLDB, STATACTIVE, TYPEINET)) != NULL ||
     (host = dict_pgsql_find_host(PLDB, STATACTIVE, TYPECONNSTR)) != NULL) {
	if (msg_verbose)
	    msg_info("%s: found active connection to host %s", myname,
		     host->hostname);
	return host;
    }

    /*
     * Try the remaining hosts. "count" is a safety net, in case the loop
     * takes more than RETRY_CONN_INTV and the dead hosts are no longer
     * skipped.
     */
    while (--count > 0 &&
	   ((host = dict_pgsql_find_host(PLDB, STATUNTRIED | STATFAIL,
					 TYPEUNIX)) != NULL ||
	    (host = dict_pgsql_find_host(PLDB, STATUNTRIED | STATFAIL,
					 TYPEINET)) != NULL ||
	    (host = dict_pgsql_find_host(PLDB, STATUNTRIED | STATFAIL,
					 TYPECONNSTR)) != NULL)) {
	if (msg_verbose)
	    msg_info("%s: attempting to connect to host %s", myname,
		     host->hostname);
	plpgsql_connect_single(dict_pgsql, host);
	if (host->stat == STATACTIVE)
	    return host;
    }

    /* bad news... */
    return 0;
}

/* dict_pgsql_event - callback: close idle connections */

static void dict_pgsql_event(int unused_event, void *context)
{
    HOST   *host = (HOST *) context;

    if (host->db)
	plpgsql_close_host(host);
}

/*
 * plpgsql_query - process a PostgreSQL query.  Return PGSQL_RES* on success.
 *			On failure, log failure and try other db instances.
 *			on failure of all db instances, return 0;
 *			close unnecessary active connections
 */

static PGSQL_RES *plpgsql_query(DICT_PGSQL *dict_pgsql,
				        const char *name,
				        VSTRING *query)
{
    PLPGSQL *PLDB = dict_pgsql->pldb;
    HOST   *host;
    PGSQL_RES *res = 0;
    ExecStatusType status;

    while ((host = dict_pgsql_get_active(dict_pgsql, PLDB)) != NULL) {

	/*
	 * The active host is used to escape strings in the context of the
	 * active connection's character encoding.
	 */
	dict_pgsql->active_host = host;
	VSTRING_RESET(query);
	VSTRING_TERMINATE(query);
	db_common_expand(dict_pgsql->ctx, dict_pgsql->query,
			 name, 0, query, dict_pgsql_quote);
	dict_pgsql->active_host = 0;

	/* Check for potential dict_pgsql_quote() failure. */
	if (host->stat == STATFAIL) {
	    plpgsql_down_host(host, dict_pgsql->retry_interval);
	    continue;
	}

	/*
	 * Submit a command to the server. Be paranoid when processing the
	 * result set: try to enumerate every successful case, and reject
	 * everything else.
	 * 
	 * From PostgreSQL 8.1.4 docs: (PQexec) returns a PGresult pointer or
	 * possibly a null pointer. A non-null pointer will generally be
	 * returned except in out-of-memory conditions or serious errors such
	 * as inability to send the command to the server.
	 */
	if ((res = PQexec(host->db, vstring_str(query))) != 0) {

	    /*
	     * XXX Because non-null result pointer does not imply success, we
	     * need to check the command's result status.
	     * 
	     * Section 28.3.1: A result of status PGRES_NONFATAL_ERROR will
	     * never be returned directly by PQexec or other query execution
	     * functions; results of this kind are instead passed to the
	     * notice processor.
	     * 
	     * PGRES_EMPTY_QUERY is being sent by the server when the query
	     * string is empty. The sanity-checking done by the Postfix
	     * infrastructure makes this case impossible, so we need not
	     * handle this situation explicitly.
	     */
	    switch ((status = PQresultStatus(res))) {
	    case PGRES_TUPLES_OK:
	    case PGRES_COMMAND_OK:
		/* Success. */
		if (msg_verbose)
		    msg_info("dict_pgsql: successful query from host %s",
			     host->hostname);
		event_request_timer(dict_pgsql_event, (void *) host,
				    dict_pgsql->idle_interval);
		return (res);
	    case PGRES_FATAL_ERROR:
		msg_warn("pgsql query failed: fatal error from host %s: %s",
			 host->hostname, PQresultErrorMessage(res));
		break;
	    case PGRES_BAD_RESPONSE:
		msg_warn("pgsql query failed: protocol error, host %s",
			 host->hostname);
		break;
	    default:
		msg_warn("pgsql query failed: unknown code 0x%lx from host %s",
			 (unsigned long) status, host->hostname);
		break;
	    }
	} else {

	    /*
	     * This driver treats null pointers like fatal, non-null result
	     * pointer errors, as suggested by the PostgreSQL 8.1.4
	     * documentation.
	     */
	    msg_warn("pgsql query failed: fatal error from host %s: %s",
		     host->hostname, PQerrorMessage(host->db));
	}

	/*
	 * XXX An error occurred. Clean up memory and skip this connection.
	 */
	if (res != 0)
	    PQclear(res);
	plpgsql_down_host(host, dict_pgsql->retry_interval);
    }

    return (0);
}

/*
 * plpgsql_connect_single -
 * used to reconnect to a single database when one is down or none is
 * connected yet. Log all errors and set the stat field of host accordingly
 */
static void plpgsql_connect_single(DICT_PGSQL *dict_pgsql, HOST *host)
{
    if (host->type == TYPECONNSTR) {
	host->db = PQconnectdb(host->name);
    } else {
	host->db = PQsetdbLogin(host->name, host->port, NULL, NULL,
				dict_pgsql->dbname, dict_pgsql->username,
				dict_pgsql->password);
    }
    if (host->db == NULL || PQstatus(host->db) != CONNECTION_OK) {
	msg_warn("connect to pgsql server %s: %s",
		 host->hostname, PQerrorMessage(host->db));
	plpgsql_down_host(host, dict_pgsql->retry_interval);
	return;
    }
    if (PQsetClientEncoding(host->db, dict_pgsql->encoding) != 0) {
	msg_warn("dict_pgsql: cannot set the encoding to %s, skipping %s",
		 dict_pgsql->encoding, host->hostname);
	plpgsql_down_host(host, dict_pgsql->retry_interval);
	return;
    }
    if (msg_verbose)
	msg_info("dict_pgsql: successful connection to host %s",
		 host->hostname);
    /* Success. */
    host->stat = STATACTIVE;
}

/* plpgsql_close_host - close an established PostgreSQL connection */

static void plpgsql_close_host(HOST *host)
{
    if (host->db)
	PQfinish(host->db);
    host->db = 0;
    host->stat = STATUNTRIED;
}

/*
 * plpgsql_down_host - close a failed connection AND set a "stay away from
 * this host" timer.
 */
static void plpgsql_down_host(HOST *host, int retry_interval)
{
    if (host->db)
	PQfinish(host->db);
    host->db = 0;
    host->ts = time((time_t *) 0) + retry_interval;
    host->stat = STATFAIL;
    event_cancel_timer(dict_pgsql_event, (void *) host);
}

/* pgsql_parse_config - parse pgsql configuration file */

static void pgsql_parse_config(DICT_PGSQL *dict_pgsql, const char *pgsqlcf)
{
    const char *myname = "pgsql_parse_config";
    CFG_PARSER *p = dict_pgsql->parser;
    char   *hosts;
    VSTRING *query;
    char   *select_function;

    dict_pgsql->username = cfg_get_str(p, "user", "", 0, 0);
    dict_pgsql->password = cfg_get_str(p, "password", "", 0, 0);
    dict_pgsql->dbname = cfg_get_str(p, "dbname", "", 0, 0);
    dict_pgsql->encoding = cfg_get_str(p, "encoding", "UTF8", 1, 0);
    dict_pgsql->retry_interval = cfg_get_int(p, "retry_interval",
					     DEF_RETRY_INTV, 1, 0);
    dict_pgsql->idle_interval = cfg_get_int(p, "idle_interval",
					    DEF_IDLE_INTV, 1, 0);
    dict_pgsql->result_format = cfg_get_str(p, "result_format", "%s", 1, 0);

    /*
     * XXX: The default should be non-zero for safety, but that is not
     * backwards compatible.
     */
    dict_pgsql->expansion_limit = cfg_get_int(dict_pgsql->parser,
					      "expansion_limit", 0, 0, 0);

    if ((dict_pgsql->query = cfg_get_str(p, "query", 0, 0, 0)) == 0) {

	/*
	 * No query specified -- fallback to building it from components (
	 * old style "select %s from %s where %s" )
	 */
	query = vstring_alloc(64);
	select_function = cfg_get_str(p, "select_function", 0, 0, 0);
	if (select_function != 0) {
	    vstring_sprintf(query, "SELECT %s('%%s')", select_function);
	    myfree(select_function);
	} else
	    db_common_sql_build_query(query, p);
	dict_pgsql->query = vstring_export(query);
    }

    /*
     * Must parse all templates before we can use db_common_expand()
     */
    dict_pgsql->ctx = 0;
    (void) db_common_parse(&dict_pgsql->dict, &dict_pgsql->ctx,
			   dict_pgsql->query, 1);
    (void) db_common_parse(0, &dict_pgsql->ctx, dict_pgsql->result_format, 0);
    db_common_parse_domain(p, dict_pgsql->ctx);

    /*
     * Maps that use substring keys should only be used with the full input
     * key.
     */
    if (db_common_dict_partial(dict_pgsql->ctx))
	dict_pgsql->dict.flags |= DICT_FLAG_PATTERN;
    else
	dict_pgsql->dict.flags |= DICT_FLAG_FIXED;
    if (dict_pgsql->dict.flags & DICT_FLAG_FOLD_FIX)
	dict_pgsql->dict.fold_buf = vstring_alloc(10);

    hosts = cfg_get_str(p, "hosts", "", 0, 0);

    dict_pgsql->hosts = argv_split(hosts, CHARS_COMMA_SP);
    if (dict_pgsql->hosts->argc == 0) {
	argv_add(dict_pgsql->hosts, "localhost", ARGV_END);
	argv_terminate(dict_pgsql->hosts);
	if (msg_verbose)
	    msg_info("%s: %s: no hostnames specified, defaulting to '%s'",
		     myname, pgsqlcf, dict_pgsql->hosts->argv[0]);
    }
    /* Don't blacklist the load balancer! */
    if (dict_pgsql->hosts->argc == 1)
	argv_add(dict_pgsql->hosts, dict_pgsql->hosts->argv[0], (char *) 0);
    myfree(hosts);
}

/* dict_pgsql_open - open PGSQL data base */

DICT   *dict_pgsql_open(const char *name, int open_flags, int dict_flags)
{
    DICT_PGSQL *dict_pgsql;
    CFG_PARSER *parser;

    /*
     * Sanity check.
     */
    if (open_flags != O_RDONLY)
	return (dict_surrogate(DICT_TYPE_PGSQL, name, open_flags, dict_flags,
			       "%s:%s map requires O_RDONLY access mode",
			       DICT_TYPE_PGSQL, name));

    /*
     * Open the configuration file.
     */
    if ((parser = cfg_parser_alloc(name)) == 0)
	return (dict_surrogate(DICT_TYPE_PGSQL, name, open_flags, dict_flags,
			       "open %s: %m", name));

    dict_pgsql = (DICT_PGSQL *) dict_alloc(DICT_TYPE_PGSQL, name,
					   sizeof(DICT_PGSQL));
    dict_pgsql->dict.lookup = dict_pgsql_lookup;
    dict_pgsql->dict.close = dict_pgsql_close;
    dict_pgsql->dict.flags = dict_flags;
    dict_pgsql->parser = parser;
    pgsql_parse_config(dict_pgsql, name);
    dict_pgsql->active_host = 0;
    dict_pgsql->pldb = plpgsql_init(dict_pgsql->hosts);
    if (dict_pgsql->pldb == NULL)
	msg_fatal("couldn't initialize pldb!\n");
    if (msg_verbose && dict_pgsql->pldb->non_uri_target == 0
	&& dict_pgsql->dbname[0] != 0)
	msg_info("%s:%s table ignores 'dbname' field -- "
		 "all 'hosts' targets are URIs",
		 DICT_TYPE_PGSQL, name);
    if (dict_pgsql->pldb->non_uri_target && dict_pgsql->dbname[0] == 0) {
	DICT   *ret;

	ret = dict_surrogate(DICT_TYPE_PGSQL, name, open_flags, dict_flags,
			   "%s:%s host target '%s' requires dbname setting",
			     DICT_TYPE_PGSQL, name,
			     dict_pgsql->pldb->non_uri_target);
	dict_pgsql_close(&dict_pgsql->dict);
	return (ret);
    }
    dict_pgsql->dict.owner = cfg_get_owner(dict_pgsql->parser);
    return (&dict_pgsql->dict);
}

/* plpgsql_init - initialize a PGSQL database */

static PLPGSQL *plpgsql_init(ARGV *hosts)
{
    PLPGSQL *PLDB;
    int     i;

    PLDB = (PLPGSQL *) mymalloc(sizeof(PLPGSQL));
    PLDB->len_hosts = hosts->argc;
    PLDB->db_hosts = (HOST **) mymalloc(sizeof(HOST *) * hosts->argc);
    PLDB->non_uri_target = 0;
    for (i = 0; i < hosts->argc; i++) {
	PLDB->db_hosts[i] = host_init(hosts->argv[i]);
	if (PLDB->db_hosts[i]->type != TYPECONNSTR)
	    PLDB->non_uri_target = PLDB->db_hosts[i]->name;
    }

    return PLDB;
}


/* host_init - initialize HOST structure */

static HOST *host_init(const char *hostname)
{
    const char *myname = "pgsql host_init";
    HOST   *host = (HOST *) mymalloc(sizeof(HOST));
    const char *d = hostname;

    host->db = 0;
    host->hostname = mystrdup(hostname);
    host->stat = STATUNTRIED;
    host->ts = 0;

    /*
     * Modern syntax: connection URI.
     */
    if (valid_uri_scheme(d)) {
	host->type = TYPECONNSTR;
	host->name = mystrdup(d);
	host->port = 0;
    }

    /*
     * Historical syntax: "unix:/pathname" and "inet:host:port". Strip the
     * "unix:" and "inet:" prefixes. Look at the first character, which is
     * how PgSQL historically distinguishes between UNIX and INET.
     */
    else {
	if (strncmp(d, "unix:", 5) == 0 || strncmp(d, "inet:", 5) == 0)
	    d += 5;
	host->name = mystrdup(d);
	if (host->name[0] && host->name[0] != '/') {
	    host->type = TYPEINET;
	    host->port = split_at_right(host->name, ':');
	} else {
	    host->type = TYPEUNIX;
	    host->port = 0;
	}
    }
    if (msg_verbose > 1)
	msg_info("%s: host=%s, port=%s, type=%s", myname, host->name,
		 host->port ? host->port : "",
		 host->type == TYPEUNIX ? "unix" :
		 host->type == TYPEINET ? "inet" :
		 "uri");
    return host;
}

/* dict_pgsql_close - close PGSQL data base */

static void dict_pgsql_close(DICT *dict)
{
    DICT_PGSQL *dict_pgsql = (DICT_PGSQL *) dict;

    plpgsql_dealloc(dict_pgsql->pldb);
    cfg_parser_free(dict_pgsql->parser);
    myfree(dict_pgsql->username);
    myfree(dict_pgsql->password);
    myfree(dict_pgsql->dbname);
    myfree(dict_pgsql->encoding);
    myfree(dict_pgsql->query);
    myfree(dict_pgsql->result_format);
    if (dict_pgsql->hosts)
	argv_free(dict_pgsql->hosts);
    if (dict_pgsql->ctx)
	db_common_free_ctx(dict_pgsql->ctx);
    if (dict->fold_buf)
	vstring_free(dict->fold_buf);
    dict_free(dict);
}

/* plpgsql_dealloc - free memory associated with PLPGSQL close databases */

static void plpgsql_dealloc(PLPGSQL *PLDB)
{
    int     i;

    for (i = 0; i < PLDB->len_hosts; i++) {
	event_cancel_timer(dict_pgsql_event, (void *) (PLDB->db_hosts[i]));
	if (PLDB->db_hosts[i]->db)
	    PQfinish(PLDB->db_hosts[i]->db);
	myfree(PLDB->db_hosts[i]->hostname);
	myfree(PLDB->db_hosts[i]->name);
	myfree((void *) PLDB->db_hosts[i]);
    }
    myfree((void *) PLDB->db_hosts);
    myfree((void *) (PLDB));
}

#endif
