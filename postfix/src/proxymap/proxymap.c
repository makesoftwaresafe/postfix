/*++
/* NAME
/*	proxymap 8
/* SUMMARY
/*	Postfix lookup table proxy server
/* SYNOPSIS
/*	\fBproxymap\fR [generic Postfix daemon options]
/* DESCRIPTION
/*	The \fBproxymap\fR(8) server provides read-only or read-write
/*	table lookup service to Postfix processes. These services are
/*	implemented with distinct service names: \fBproxymap\fR and
/*	\fBproxywrite\fR, respectively. The purpose of these services is:
/* .IP \(bu
/*	To overcome chroot restrictions. For example, a chrooted SMTP
/*	server needs access to the system passwd file in order to
/*	reject mail for non-existent local addresses, but it is not
/*	practical to maintain a copy of the passwd file in the chroot
/*	jail.  The solution:
/* .sp
/* .nf
/*	local_recipient_maps =
/*	    proxy:unix:passwd.byname $alias_maps
/* .fi
/* .IP \(bu
/*	To consolidate the number of open lookup tables by sharing
/*	one open table among multiple processes. For example, making
/*	mysql connections from every Postfix daemon process results
/*	in "too many connections" errors. The solution:
/* .sp
/* .nf
/*	virtual_alias_maps =
/*	    proxy:mysql:/etc/postfix/virtual_alias.cf
/* .fi
/* .sp
/*	The total number of connections is limited by the number of
/*	proxymap server processes.
/* .IP \(bu
/*	To provide single-updater functionality for lookup tables
/*	that do not reliably support multiple writers (i.e. all
/*	file-based tables that are not based on \fBlmdb\fR).
/* .PP
/*	The \fBproxymap\fR(8) server implements the following requests:
/* .IP "\fBopen\fR \fImaptype:mapname instance-flags\fR"
/*	Open the table with type \fImaptype\fR and name \fImapname\fR,
/*	with initial dictionary flags \fIinstance-flags\fR. The reply
/*	contains the actual dictionary flags (for example, to distinguish
/*	a fixed-string table from a regular-expression table).
/* .IP "\fBlookup\fR \fImaptype:mapname instance-flags request-flags key\fR"
/*	Look up the data stored under the requested key using the
/*	dictionary flags in \fIrequest-flags\fR.
/*	The reply contains the request completion status code, the
/*	resulting dictionary flags, and the lookup result value.
/*	The \fImaptype:mapname\fR and \fIinstance-flags\fR are the same
/*	as with the \fBopen\fR request.
/* .IP "\fBupdate\fR \fImaptype:mapname instance-flags request-flags key value\fR"
/*	Update the data stored under the requested key using the
/*	dictionary flags in \fIrequest-flags\fR.
/*	The reply contains the request completion status code and the
/*	resulting dictionary flags.
/*	The \fImaptype:mapname\fR and \fIinstance-flags\fR are the same
/*	as with the \fBopen\fR request.
/* .sp
/*	To implement single-updater maps, specify a process limit
/*	of 1 in the master.cf file entry for the \fBproxywrite\fR
/*	service.
/* .sp
/*	This request is supported in Postfix 2.5 and later.
/* .IP "\fBdelete\fR \fImaptype:mapname instance-flags request-flags key\fR"
/*	Delete the data stored under the requested key, using the
/*	dictionary flags in \fIrequest-flags\fR.
/*	The reply contains the request completion status code and the
/*	resulting dictionary flags.
/*	The \fImaptype:mapname\fR and \fIinstance-flags\fR are the same
/*	as with the \fBopen\fR request.
/* .sp
/*	This request is supported in Postfix 2.5 and later.
/* .IP "\fBsequence\fR \fImaptype:mapname instance-flags request-flags function\fR"
/*	Iterate over the specified database, using the dictionary flags
/*	in \fIrequest-flags\fR. The \fIfunction\fR is either
/*	DICT_SEQ_FUN_FIRST or DICT_SEQ_FUN_NEXT.
/*	The reply contains the request completion status code, the
/*	resulting dictionary flags, and a lookup key and result value
/*	if found.
/*	The \fImaptype:mapname\fR and \fIinstance-flags\fR are the same
/*	as with the \fBopen\fR request.
/* .sp
/*	This request is supported in Postfix 2.9 and later.
/* .IP "Not implemented: close"
/*	There is no \fBclose\fR request, nor are tables implicitly closed
/*	when a client disconnects. The purpose is to share tables among
/*	multiple client processes. Due to the absence of an explicit or
/*	implicit \fBclose\fR, updates are forced to be synchronous.
/* .PP
/*	The request completion status is one of OK, RETRY, NOKEY
/*	(lookup failed because the key was not found), BAD (malformed
/*	request) or DENY (the table is not approved for proxy read
/*	or update access).
/* SERVER PROCESS MANAGEMENT
/* .ad
/* .fi
/*	\fBproxymap\fR(8) servers run under control by the Postfix
/*	\fBmaster\fR(8)
/*	server.  Each server can handle multiple simultaneous connections.
/*	When all servers are busy while a client connects, the \fBmaster\fR(8)
/*	creates a new \fBproxymap\fR(8) server process, provided that the
/*	process limit is not exceeded.
/*	Each server terminates after serving at least \fB$max_use\fR clients
/*	or after \fB$max_idle\fR seconds of idle time.
/* SECURITY
/* .ad
/* .fi
/*	The \fBproxymap\fR(8) server opens only tables that are
/*	approved via the \fBproxy_read_maps\fR or \fBproxy_write_maps\fR
/*	configuration parameters, does not talk to
/*	users, and can run at fixed low privilege, chrooted or not.
/*	However, running the proxymap server chrooted severely limits
/*	usability, because it can open only chrooted tables.
/*
/*	The \fBproxymap\fR(8) server is not a trusted daemon process, and must
/*	not be used to look up sensitive information such as UNIX user or
/*	group IDs, mailbox file/directory names or external commands.
/*
/*	In Postfix version 2.2 and later, the proxymap client recognizes
/*	requests to access a table for security-sensitive purposes,
/*	and opens the table directly. This allows the same main.cf
/*	setting to be used by sensitive and non-sensitive processes.
/*
/*	Postfix-writable data files should be stored under a dedicated
/*	directory that is writable only by the Postfix mail system,
/*	such as the Postfix-owned \fBdata_directory\fR.
/*
/*	In particular, Postfix-writable files should never exist
/*	in root-owned directories. That would open up a particular
/*	type of security hole where ownership of a file or directory
/*	does not match the provider of its content.
/* DIAGNOSTICS
/*	Problems and transactions are logged to \fBsyslogd\fR(8)
/*	or \fBpostlogd\fR(8).
/* BUGS
/*	The \fBproxymap\fR(8) server provides service to multiple clients,
/*	and must therefore not be used for tables that have high-latency
/*	lookups.
/*
/*	The \fBproxymap\fR(8) read-write service does not explicitly
/*	close lookup tables (even if it did, this could not be relied on,
/*	because the process may be terminated between table updates).
/*	The read-write service should therefore not be used with tables that
/*	leave persistent storage in an inconsistent state between
/*	updates (for example, CDB). Tables that support "sync on
/*	update" should be safe (for example, Berkeley DB) as should
/*	tables that are implemented by a real DBMS.
/* CONFIGURATION PARAMETERS
/* .ad
/* .fi
/*	On busy mail systems a long time may pass before
/*	\fBproxymap\fR(8) relevant
/*	changes to \fBmain.cf\fR are picked up. Use the command
/*	"\fBpostfix reload\fR" to speed up a change.
/*
/*	The text below provides only a parameter summary. See
/*	\fBpostconf\fR(5) for more details including examples.
/* .IP "\fBconfig_directory (see 'postconf -d' output)\fR"
/*	The default location of the Postfix main.cf and master.cf
/*	configuration files.
/* .IP "\fBdata_directory (see 'postconf -d' output)\fR"
/*	The directory with Postfix-writable data files (for example:
/*	caches, pseudo-random numbers).
/* .IP "\fBdaemon_timeout (18000s)\fR"
/*	How much time a Postfix daemon process may take to handle a
/*	request before it is terminated by a built-in watchdog timer.
/* .IP "\fBipc_timeout (3600s)\fR"
/*	The time limit for sending or receiving information over an internal
/*	communication channel.
/* .IP "\fBmax_idle (100s)\fR"
/*	The maximum amount of time that an idle Postfix daemon process waits
/*	for an incoming connection before terminating voluntarily.
/* .IP "\fBmax_use (100)\fR"
/*	The maximal number of incoming connections that a Postfix daemon
/*	process will service before terminating voluntarily.
/* .IP "\fBprocess_id (read-only)\fR"
/*	The process ID of a Postfix command or daemon process.
/* .IP "\fBprocess_name (read-only)\fR"
/*	The process name of a Postfix command or daemon process.
/* .IP "\fBproxy_read_maps (see 'postconf -d' output)\fR"
/*	The lookup tables that the \fBproxymap\fR(8) server is allowed to
/*	access for the read-only service.
/* .PP
/*	Available in Postfix 2.5 and later:
/* .IP "\fBdata_directory (see 'postconf -d' output)\fR"
/*	The directory with Postfix-writable data files (for example:
/*	caches, pseudo-random numbers).
/* .IP "\fBproxy_write_maps (see 'postconf -d' output)\fR"
/*	The lookup tables that the \fBproxymap\fR(8) server is allowed to
/*	access for the read-write service.
/* .PP
/*	Available in Postfix 3.3 and later:
/* .IP "\fBservice_name (read-only)\fR"
/*	The master.cf service name of a Postfix daemon process.
/* SEE ALSO
/*	postconf(5), configuration parameters
/*	master(5), generic daemon options
/* README FILES
/* .ad
/* .fi
/*	Use "\fBpostconf readme_directory\fR" or
/*	"\fBpostconf html_directory\fR" to locate this information.
/* .na
/* .nf
/*	DATABASE_README, Postfix lookup table overview
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* HISTORY
/* .ad
/* .fi
/*	The proxymap service was introduced with Postfix 2.0.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*
/*	Wietse Venema
/*	Google, Inc.
/*	111 8th Avenue
/*	New York, NY 10011, USA
/*
/*	Wietse Venema
/*	porcupine.org
/*--*/

/* System library. */

#include <sys_defs.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Utility library. */

#include <msg.h>
#include <mymalloc.h>
#include <vstring.h>
#include <htable.h>
#include <stringops.h>
#include <dict.h>
#include <dict_pipe.h>
#include <dict_union.h>

/* Global library. */

#include <mail_conf.h>
#include <mail_params.h>
#include <mail_version.h>
#include <mail_proto.h>
#include <dict_proxy.h>

/* Server skeleton. */

#include <mail_server.h>

/* Application-specific. */

 /*
  * XXX All but the last are needed here so that $name expansion dependencies
  * aren't too broken. The fix is to gather all parameter default settings in
  * one place.
  */
char   *var_alias_maps;
char   *var_local_rcpt_maps;
char   *var_virt_alias_maps;
char   *var_virt_alias_doms;
char   *var_virt_mailbox_maps;
char   *var_virt_mailbox_doms;
char   *var_relay_rcpt_maps;
char   *var_canonical_maps;
char   *var_send_canon_maps;
char   *var_rcpt_canon_maps;
char   *var_relocated_maps;
char   *var_transport_maps;
char   *var_verify_map;
char   *var_smtpd_snd_auth_maps;
char   *var_psc_cache_map;
char   *var_proxy_read_maps;
char   *var_proxy_write_maps;

 /*
  * The pre-approved, pre-parsed list of maps.
  */
static HTABLE *proxy_auth_maps;

 /*
  * Shared and static to reduce memory allocation overhead.
  */
static VSTRING *request;
static VSTRING *request_map;
static VSTRING *request_key;
static VSTRING *request_value;
static VSTRING *map_type_name_flags;

 /*
  * Are we a proxy writer or not?
  */
static int proxy_writer;

 /*
  * Silly little macros.
  */
#define STR(x)			vstring_str(x)
#define VSTREQ(x,y)		(strcmp(STR(x),y) == 0)

/* get_nested_dict_name - return nested dictionary name pointer, or null */

static char *get_nested_dict_name(char *type_name)
{
    const struct {
	const char *type_col;
	ssize_t type_col_len;
    }      *prefix, prefixes[] = {
	DICT_TYPE_UNION ":", (sizeof(DICT_TYPE_UNION ":") - 1),
	DICT_TYPE_PIPE ":", (sizeof(DICT_TYPE_PIPE ":") - 1),
    };

#define COUNT_OF(x) (sizeof(x)/sizeof((x)[0]))

    for (prefix = prefixes; prefix < prefixes + COUNT_OF(prefixes); prefix++) {
	if (strncmp(type_name, prefix->type_col, prefix->type_col_len) == 0)
	    return (type_name + prefix->type_col_len);
    }
    return (0);
}

/* proxy_map_find - look up or open table */

static DICT *proxy_map_find(const char *map_type_name, int inst_flags,
			            int *statp)
{
    static HTABLE *new_flags;
    HTABLE_INFO *ht;
    DICT   *dict;

#define PROXY_COLON	DICT_TYPE_PROXY ":"
#define PROXY_COLON_LEN	(sizeof(PROXY_COLON) - 1)
#define READ_OPEN_FLAGS	O_RDONLY
#define WRITE_OPEN_FLAGS (O_RDWR | O_CREAT)

    /*
     * Canonicalize the map name. If the map is not on the approved list,
     * deny the request.
     */
#define PROXY_MAP_FIND_ERROR_RETURN(x)  { *statp = (x); return (0); }
#define PROXY_MAP_PARAM_NAME(proxy_writer)  \
	((proxy_writer) == 0 ? VAR_PROXY_READ_MAPS : VAR_PROXY_WRITE_MAPS)

    while (strncmp(map_type_name, PROXY_COLON, PROXY_COLON_LEN) == 0)
	map_type_name += PROXY_COLON_LEN;
    /* XXX The following breaks with maps that have ':' in their name. */
    if (strchr(map_type_name, ':') == 0)
	PROXY_MAP_FIND_ERROR_RETURN(PROXY_STAT_BAD);
    if (htable_locate(proxy_auth_maps, map_type_name) == 0) {
	msg_warn("request for unapproved table: \"%s\"", map_type_name);
	msg_warn("to approve this table for %s access, list %s:%s in %s:%s",
		 proxy_writer == 0 ? "read-only" : "read-write",
		 DICT_TYPE_PROXY, map_type_name, MAIN_CONF_FILE,
		 PROXY_MAP_PARAM_NAME(proxy_writer));
	PROXY_MAP_FIND_ERROR_RETURN(PROXY_STAT_DENY);
    }

    /*
     * Open one instance of a map for each combination of name+flags.
     */
    dict = dict_open(map_type_name, proxy_writer ?
		     WRITE_OPEN_FLAGS : READ_OPEN_FLAGS,
		     inst_flags);
    if (dict == 0)
	msg_panic("proxy_map_find: dict_open null result");

    /*
     * Remember the mapping from dict->reg_name to the dict->flags of a
     * newly-initialized instance. Always return an instance with those new
     * dict->flags, to avoid crosstalk between different clients.
     */
    if (new_flags == 0)
	new_flags = htable_create(100);
    if ((ht = htable_locate(new_flags, dict->reg_name)) == 0) {
	(void) htable_enter(new_flags, dict->reg_name,
			    CAST_INT_TO_VOID_PTR(dict->flags));
    } else {
	dict->flags = CAST_ANY_PTR_TO_INT(ht->value);
    }
    dict->error = 0;
    return (dict);
}

/* proxymap_sequence_service - remote sequence service */

static void proxymap_sequence_service(VSTREAM *client_stream)
{
    int     inst_flags;
    int     request_flags;
    DICT   *dict;
    int     request_func;
    const char *reply_key;
    const char *reply_value;
    int     dict_status;
    int     reply_status;

    /*
     * Process the request.
     */
    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
		  RECV_ATTR_STR(MAIL_ATTR_TABLE, request_map),
		  RECV_ATTR_INT(MAIL_ATTR_INST_FLAGS, &inst_flags),
		  RECV_ATTR_INT(MAIL_ATTR_FLAGS, &request_flags),
		  RECV_ATTR_INT(MAIL_ATTR_FUNC, &request_func),
		  ATTR_TYPE_END) != 4
	|| (request_func != DICT_SEQ_FUN_FIRST
	    && request_func != DICT_SEQ_FUN_NEXT)) {
	reply_status = PROXY_STAT_BAD;
	reply_key = reply_value = "";
    } else if ((dict = proxy_map_find(STR(request_map), inst_flags,
				      &reply_status)) == 0) {
	reply_key = reply_value = "";
    } else {
	dict->flags = request_flags;
	dict_status = dict_seq(dict, request_func, &reply_key, &reply_value);
	if (dict_status == 0) {
	    reply_status = PROXY_STAT_OK;
	} else if (dict->error == 0) {
	    reply_status = PROXY_STAT_NOKEY;
	    reply_key = reply_value = "";
	} else {
	    reply_status = (dict->error == DICT_ERR_RETRY ?
			    PROXY_STAT_RETRY : PROXY_STAT_CONFIG);
	    reply_key = reply_value = "";
	}
    }

    /*
     * Respond to the client.
     */
    attr_print(client_stream, ATTR_FLAG_NONE,
	       SEND_ATTR_INT(MAIL_ATTR_STATUS, reply_status),
	       SEND_ATTR_INT(MAIL_ATTR_FLAGS, dict->flags),
	       SEND_ATTR_STR(MAIL_ATTR_KEY, reply_key),
	       SEND_ATTR_STR(MAIL_ATTR_VALUE, reply_value),
	       ATTR_TYPE_END);
}

/* proxymap_lookup_service - remote lookup service */

static void proxymap_lookup_service(VSTREAM *client_stream)
{
    int     inst_flags;
    int     request_flags;
    DICT   *dict;
    const char *reply_value;
    int     reply_status;

    /*
     * Process the request.
     */
    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
		  RECV_ATTR_STR(MAIL_ATTR_TABLE, request_map),
		  RECV_ATTR_INT(MAIL_ATTR_INST_FLAGS, &inst_flags),
		  RECV_ATTR_INT(MAIL_ATTR_FLAGS, &request_flags),
		  RECV_ATTR_STR(MAIL_ATTR_KEY, request_key),
		  ATTR_TYPE_END) != 4) {
	reply_status = PROXY_STAT_BAD;
	reply_value = "";
    } else if ((dict = proxy_map_find(STR(request_map), inst_flags,
				      &reply_status)) == 0) {
	reply_value = "";
    } else if (dict->flags = request_flags,
	       (reply_value = dict_get(dict, STR(request_key))) != 0) {
	reply_status = PROXY_STAT_OK;
    } else if (dict->error == 0) {
	reply_status = PROXY_STAT_NOKEY;
	reply_value = "";
    } else {
	reply_status = (dict->error == DICT_ERR_RETRY ?
			PROXY_STAT_RETRY : PROXY_STAT_CONFIG);
	reply_value = "";
    }

    /*
     * Respond to the client.
     */
    attr_print(client_stream, ATTR_FLAG_NONE,
	       SEND_ATTR_INT(MAIL_ATTR_STATUS, reply_status),
	       SEND_ATTR_INT(MAIL_ATTR_FLAGS, dict->flags),
	       SEND_ATTR_STR(MAIL_ATTR_VALUE, reply_value),
	       ATTR_TYPE_END);
}

/* proxymap_update_service - remote update service */

static void proxymap_update_service(VSTREAM *client_stream)
{
    int     inst_flags;
    int     request_flags;
    DICT   *dict;
    int     dict_status;
    int     reply_status;

    /*
     * Process the request.
     * 
     * XXX We don't close maps, so we must turn on synchronous update to ensure
     * that the on-disk data is in a consistent state between updates.
     * 
     * XXX We ignore duplicates, because the proxymap server would abort
     * otherwise.
     */
    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
		  RECV_ATTR_STR(MAIL_ATTR_TABLE, request_map),
		  RECV_ATTR_INT(MAIL_ATTR_INST_FLAGS, &inst_flags),
		  RECV_ATTR_INT(MAIL_ATTR_FLAGS, &request_flags),
		  RECV_ATTR_STR(MAIL_ATTR_KEY, request_key),
		  RECV_ATTR_STR(MAIL_ATTR_VALUE, request_value),
		  ATTR_TYPE_END) != 5) {
	reply_status = PROXY_STAT_BAD;
    } else if (proxy_writer == 0) {
	msg_warn("refusing %s update request on non-%s service",
		 STR(request_map), MAIL_SERVICE_PROXYWRITE);
	reply_status = PROXY_STAT_DENY;
    } else if ((dict = proxy_map_find(STR(request_map), inst_flags,
				      &reply_status)) == 0) {
	 /* void */ ;
    } else {
	/* Sync the table now. Don't abort on duplicate update. */
	dict->flags = (request_flags
		       | DICT_FLAG_SYNC_UPDATE | DICT_FLAG_DUP_REPLACE);
	dict_status = dict_put(dict, STR(request_key), STR(request_value));
	if (dict_status == 0) {
	    reply_status = PROXY_STAT_OK;
	} else if (dict->error == 0) {
	    reply_status = PROXY_STAT_NOKEY;
	} else {
	    reply_status = (dict->error == DICT_ERR_RETRY ?
			    PROXY_STAT_RETRY : PROXY_STAT_CONFIG);
	}
    }

    /*
     * Respond to the client.
     */
    attr_print(client_stream, ATTR_FLAG_NONE,
	       SEND_ATTR_INT(MAIL_ATTR_STATUS, reply_status),
	       SEND_ATTR_INT(MAIL_ATTR_FLAGS, dict->flags),
	       ATTR_TYPE_END);
}

/* proxymap_delete_service - remote delete service */

static void proxymap_delete_service(VSTREAM *client_stream)
{
    int     inst_flags;
    int     request_flags;
    DICT   *dict;
    int     dict_status;
    int     reply_status;

    /*
     * Process the request.
     * 
     * XXX We don't close maps, so we must turn on synchronous update to ensure
     * that the on-disk data is in a consistent state between updates.
     */
    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
		  RECV_ATTR_STR(MAIL_ATTR_TABLE, request_map),
		  RECV_ATTR_INT(MAIL_ATTR_INST_FLAGS, &inst_flags),
		  RECV_ATTR_INT(MAIL_ATTR_FLAGS, &request_flags),
		  RECV_ATTR_STR(MAIL_ATTR_KEY, request_key),
		  ATTR_TYPE_END) != 4) {
	reply_status = PROXY_STAT_BAD;
    } else if (proxy_writer == 0) {
	msg_warn("refusing %s delete request on non-%s service",
		 STR(request_map), MAIL_SERVICE_PROXYWRITE);
	reply_status = PROXY_STAT_DENY;
    } else if ((dict = proxy_map_find(STR(request_map), inst_flags,
				      &reply_status)) == 0) {
	 /* void */ ;
    } else {
	/* Sync the table now. There is no close() request. */
	dict->flags = (request_flags
		       | DICT_FLAG_SYNC_UPDATE);
	dict_status = dict_del(dict, STR(request_key));
	if (dict_status == 0) {
	    reply_status = PROXY_STAT_OK;
	} else if (dict->error == 0) {
	    reply_status = PROXY_STAT_NOKEY;
	} else {
	    reply_status = (dict->error == DICT_ERR_RETRY ?
			    PROXY_STAT_RETRY : PROXY_STAT_CONFIG);
	}
    }

    /*
     * Respond to the client.
     */
    attr_print(client_stream, ATTR_FLAG_NONE,
	       SEND_ATTR_INT(MAIL_ATTR_STATUS, reply_status),
	       SEND_ATTR_INT(MAIL_ATTR_FLAGS, dict->flags),
	       ATTR_TYPE_END);
}

/* proxymap_open_service - open remote lookup table */

static void proxymap_open_service(VSTREAM *client_stream)
{
    int     inst_flags;
    DICT   *dict;
    int     reply_status;
    int     reply_flags;

    /*
     * Process the request.
     */
    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
		  RECV_ATTR_STR(MAIL_ATTR_TABLE, request_map),
		  RECV_ATTR_INT(MAIL_ATTR_INST_FLAGS, &inst_flags),
		  ATTR_TYPE_END) != 2) {
	reply_status = PROXY_STAT_BAD;
	reply_flags = 0;
    } else if ((dict = proxy_map_find(STR(request_map), inst_flags,
				      &reply_status)) == 0) {
	reply_flags = 0;
    } else {
	reply_status = PROXY_STAT_OK;
	reply_flags = dict->flags;
    }

    /*
     * Respond to the client.
     */
    attr_print(client_stream, ATTR_FLAG_NONE,
	       SEND_ATTR_INT(MAIL_ATTR_STATUS, reply_status),
	       SEND_ATTR_INT(MAIL_ATTR_FLAGS, reply_flags),
	       ATTR_TYPE_END);
}

/* proxymap_service - perform service for client */

static void proxymap_service(VSTREAM *client_stream, char *unused_service,
			             char **argv)
{

    /*
     * Sanity check. This service takes no command-line arguments.
     */
    if (argv[0])
	msg_fatal("unexpected command-line argument: %s", argv[0]);

    /*
     * Deadline enforcement.
     */
    if (vstream_fstat(client_stream, VSTREAM_FLAG_DEADLINE) == 0)
	vstream_control(client_stream,
			CA_VSTREAM_CTL_TIMEOUT(1),
			CA_VSTREAM_CTL_END);

    /*
     * This routine runs whenever a client connects to the socket dedicated
     * to the proxymap service. All connection-management stuff is handled by
     * the common code in multi_server.c.
     */
    vstream_control(client_stream,
		    CA_VSTREAM_CTL_START_DEADLINE,
		    CA_VSTREAM_CTL_END);
    if (attr_scan(client_stream,
		  ATTR_FLAG_MORE | ATTR_FLAG_STRICT,
		  RECV_ATTR_STR(MAIL_ATTR_REQ, request),
		  ATTR_TYPE_END) == 1) {
	if (VSTREQ(request, PROXY_REQ_LOOKUP)) {
	    proxymap_lookup_service(client_stream);
	} else if (VSTREQ(request, PROXY_REQ_UPDATE)) {
	    proxymap_update_service(client_stream);
	} else if (VSTREQ(request, PROXY_REQ_DELETE)) {
	    proxymap_delete_service(client_stream);
	} else if (VSTREQ(request, PROXY_REQ_SEQUENCE)) {
	    proxymap_sequence_service(client_stream);
	} else if (VSTREQ(request, PROXY_REQ_OPEN)) {
	    proxymap_open_service(client_stream);
	} else {
	    msg_warn("unrecognized request: \"%s\", ignored", STR(request));
	    attr_print(client_stream, ATTR_FLAG_NONE,
		       SEND_ATTR_INT(MAIL_ATTR_STATUS, PROXY_STAT_BAD),
		       ATTR_TYPE_END);
	}
    }
    vstream_control(client_stream,
		    CA_VSTREAM_CTL_START_DEADLINE,
		    CA_VSTREAM_CTL_END);
    vstream_fflush(client_stream);
}

/* dict_proxy_open - intercept remote map request from inside library */

DICT   *dict_proxy_open(const char *map, int open_flags, int dict_flags)
{
    if (msg_verbose)
	msg_info("dict_proxy_open(%s, 0%o, 0%o) called from internal routine",
		 map, open_flags, dict_flags);
    while (strncmp(map, PROXY_COLON, PROXY_COLON_LEN) == 0)
	map += PROXY_COLON_LEN;
    return (dict_open(map, open_flags, dict_flags));
}

/* authorize_proxied_maps - recursively authorize maps */

static void authorize_proxied_maps(char *bp)
{
    const char *sep = CHARS_COMMA_SP;
    const char *parens = CHARS_BRACE;
    char   *type_name;

    while ((type_name = mystrtokq(&bp, sep, parens)) != 0) {
	char   *nested_info;

	/* Maybe { maptype:mapname attr=value... } */
	if (*type_name == parens[0]) {
	    char   *err;

	    /* Warn about blatant syntax error. */
	    if ((err = extpar(&type_name, parens, EXTPAR_FLAG_NONE)) != 0) {
		msg_warn("bad %s parameter value: %s",
			 PROXY_MAP_PARAM_NAME(proxy_writer), err);
		myfree(err);
		continue;
	    }
	    /* Don't try to second-guess the semantics of { }. */
	    if ((type_name = mystrtokq(&type_name, sep, parens)) == 0)
		continue;
	}
	/* Recurse into nested map (pipemap, unionmap). */
	if ((nested_info = get_nested_dict_name(type_name)) != 0) {
	    char   *err;

	    if (*nested_info != parens[0])
		continue;
	    /* Warn about blatant syntax error. */
	    if ((err = extpar(&nested_info, parens, EXTPAR_FLAG_NONE)) != 0) {
		msg_warn("bad %s parameter value: %s",
			 PROXY_MAP_PARAM_NAME(proxy_writer), err);
		myfree(err);
		continue;
	    }
	    authorize_proxied_maps(nested_info);
	    continue;
	}
	if (strncmp(type_name, PROXY_COLON, PROXY_COLON_LEN))
	    continue;
	do {
	    type_name += PROXY_COLON_LEN;
	} while (!strncmp(type_name, PROXY_COLON, PROXY_COLON_LEN));
	if (strchr(type_name, ':') != 0
	    && htable_locate(proxy_auth_maps, type_name) == 0) {
	    (void) htable_enter(proxy_auth_maps, type_name, (void *) 0);
	    if (msg_verbose)
		msg_info("allowlisting %s from %s", type_name,
			 PROXY_MAP_PARAM_NAME(proxy_writer));
	}
    }
}

/* post_jail_init - initialization after privilege drop */

static void post_jail_init(char *service_name, char **unused_argv)
{
    char   *saved_filter;

    /*
     * Are we proxy writer?
     */
    if (strcmp(service_name, MAIL_SERVICE_PROXYWRITE) == 0)
	proxy_writer = 1;
    else if (strcmp(service_name, MAIL_SERVICE_PROXYMAP) != 0)
	msg_fatal("invalid service name: \"%s\" - "
		  "service name must be \"%s\" or \"%s\"",
		  service_name, MAIL_SERVICE_PROXYWRITE,
		  MAIL_SERVICE_PROXYMAP);

    /*
     * Pre-allocate buffers.
     */
    request = vstring_alloc(10);
    request_map = vstring_alloc(10);
    request_key = vstring_alloc(10);
    request_value = vstring_alloc(10);
    map_type_name_flags = vstring_alloc(10);

    /*
     * Prepare the pre-approved list of proxied tables.
     */
    saved_filter = mystrdup(proxy_writer ? var_proxy_write_maps :
			    var_proxy_read_maps);
    proxy_auth_maps = htable_create(13);
    authorize_proxied_maps(saved_filter);
    myfree(saved_filter);

    /*
     * Never, ever, get killed by a master signal, as that could corrupt a
     * persistent database when we're in the middle of an update.
     */
    if (proxy_writer != 0)
	setsid();
}

/* pre_accept - see if tables have changed */

static void pre_accept(char *unused_name, char **unused_argv)
{
    const char *table;

    if (proxy_writer == 0 && (table = dict_changed_name()) != 0) {
	msg_info("table %s has changed -- restarting", table);
	exit(0);
    }
}

/* post_accept - announce our protocol name */

static void post_accept(VSTREAM *stream, char *unused_name, char **unused_argv,
			        HTABLE *unused_attr)
{

    /*
     * Announce the protocol.
     */
    attr_print(stream, ATTR_FLAG_NONE,
	       SEND_ATTR_STR(MAIL_ATTR_PROTO, MAIL_ATTR_PROTO_PROXYMAP),
	       ATTR_TYPE_END);
    (void) vstream_fflush(stream);
}

MAIL_VERSION_STAMP_DECLARE;

/* main - pass control to the multi-threaded skeleton */

int     main(int argc, char **argv)
{
    static const CONFIG_STR_TABLE str_table[] = {
	VAR_ALIAS_MAPS, DEF_ALIAS_MAPS, &var_alias_maps, 0, 0,
	VAR_LOCAL_RCPT_MAPS, DEF_LOCAL_RCPT_MAPS, &var_local_rcpt_maps, 0, 0,
	VAR_VIRT_ALIAS_MAPS, DEF_VIRT_ALIAS_MAPS, &var_virt_alias_maps, 0, 0,
	VAR_VIRT_ALIAS_DOMS, DEF_VIRT_ALIAS_DOMS, &var_virt_alias_doms, 0, 0,
	VAR_VIRT_MAILBOX_MAPS, DEF_VIRT_MAILBOX_MAPS, &var_virt_mailbox_maps, 0, 0,
	VAR_VIRT_MAILBOX_DOMS, DEF_VIRT_MAILBOX_DOMS, &var_virt_mailbox_doms, 0, 0,
	VAR_RELAY_RCPT_MAPS, DEF_RELAY_RCPT_MAPS, &var_relay_rcpt_maps, 0, 0,
	VAR_CANONICAL_MAPS, DEF_CANONICAL_MAPS, &var_canonical_maps, 0, 0,
	VAR_SEND_CANON_MAPS, DEF_SEND_CANON_MAPS, &var_send_canon_maps, 0, 0,
	VAR_RCPT_CANON_MAPS, DEF_RCPT_CANON_MAPS, &var_rcpt_canon_maps, 0, 0,
	VAR_RELOCATED_MAPS, DEF_RELOCATED_MAPS, &var_relocated_maps, 0, 0,
	VAR_TRANSPORT_MAPS, DEF_TRANSPORT_MAPS, &var_transport_maps, 0, 0,
	VAR_VERIFY_MAP, DEF_VERIFY_MAP, &var_verify_map, 0, 0,
	VAR_SMTPD_SND_AUTH_MAPS, DEF_SMTPD_SND_AUTH_MAPS, &var_smtpd_snd_auth_maps, 0, 0,
	VAR_PSC_CACHE_MAP, DEF_PSC_CACHE_MAP, &var_psc_cache_map, 0, 0,
	/* The following two must be last for $mapname to work as expected. */
	VAR_PROXY_READ_MAPS, DEF_PROXY_READ_MAPS, &var_proxy_read_maps, 0, 0,
	VAR_PROXY_WRITE_MAPS, DEF_PROXY_WRITE_MAPS, &var_proxy_write_maps, 0, 0,
	0,
    };

    /*
     * Fingerprint executables and core dumps.
     */
    MAIL_VERSION_STAMP_ALLOCATE;

    /*
     * XXX When invoked with the master.cf service name "proxywrite", the
     * proxymap daemon will allow update requests. To update a table that is
     * not multi-writer safe (for example, some versions of Berkeley DB), the
     * "proxywrite" service should run as a single updater (i.e. a process
     * limit of 1, which could be enforced below by requesting
     * CA_MAIL_SERVER_SOLITARY).
     * 
     * In the default master.cf file, the "proxywrite" service has a process
     * limit of 1. Assuming that updates will be rare, this process limit
     * will suffice. Latency-sensitive services such as postscreen must not
     * use the proxywrite service (in fact, postscreen has a latency check
     * built-in).
     * 
     * Optimizing for multi-writer operation would suffer from all kinds of
     * complexity that would make it hard to use:
     * 
     * - The master daemon specifies the "proxywrite" service name with the -n
     * command-line option. This information is not known here, before the
     * multi_server_main() call. The multi_server_main() function could
     * reveal process limit information to its call-back functions, and leave
     * single-updater enforcement to its call-back functions.
     * 
     * - If we really want multi-writer update support, the "proxywrite" service
     * would have to parse the $proxy_write_maps value, and permit
     * multi-writer operation only if all tables are multi-writer safe. That
     * would require a new dict(3) method, to query each lookup table
     * implementation if it is multi-writer safe, without instantiating a
     * lookup table client.
     */
    multi_server_main(argc, argv, proxymap_service,
		      CA_MAIL_SERVER_STR_TABLE(str_table),
		      CA_MAIL_SERVER_POST_INIT(post_jail_init),
		      CA_MAIL_SERVER_PRE_ACCEPT(pre_accept),
		      CA_MAIL_SERVER_POST_ACCEPT(post_accept),
    /* XXX CA_MAIL_SERVER_SOLITARY if proxywrite */
		      0);
}
