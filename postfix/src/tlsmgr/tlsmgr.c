/*++
/* NAME
/*	tlsmgr 8
/* SUMMARY
/*	Postfix TLS session cache and PRNG manager
/* SYNOPSIS
/*	\fBtlsmgr\fR [generic Postfix daemon options]
/* DESCRIPTION
/*	The \fBtlsmgr\fR(8) manages the Postfix TLS session caches.
/*	It stores and retrieves cache entries on request by
/*	\fBsmtpd\fR(8) and \fBsmtp\fR(8) processes, and periodically
/*	removes entries that have expired.
/*
/*	The \fBtlsmgr\fR(8) also manages the PRNG (pseudo random number
/*	generator) pool. It answers queries by the \fBsmtpd\fR(8)
/*	and \fBsmtp\fR(8)
/*	processes to seed their internal PRNG pools.
/*
/*	The \fBtlsmgr\fR(8)'s PRNG pool is initially seeded from
/*	an external source (EGD, /dev/urandom, or regular file).
/*	It is updated at configurable pseudo-random intervals with
/*	data from the external source. It is updated periodically
/*	with data from TLS session cache entries and with the time
/*	of day, and is updated with the time of day whenever a
/*	process requests \fBtlsmgr\fR(8) service.
/*
/*	The \fBtlsmgr\fR(8) saves the PRNG state to an exchange file
/*	periodically and when the process terminates, and reads
/*	the exchange file when initializing its PRNG.
/* SECURITY
/* .ad
/* .fi
/*	The \fBtlsmgr\fR(8) is not security-sensitive. The code that maintains
/*	the external and internal PRNG pools does not "trust" the
/*	data that it manipulates, and the code that maintains the
/*	TLS session cache does not touch the contents of the cached
/*	entries, except for seeding its internal PRNG pool.
/*
/*	The \fBtlsmgr\fR(8) can be run chrooted and with reduced privileges.
/*	At process startup it connects to the entropy source and
/*	exchange file, and creates or truncates the optional TLS
/*	session cache files.
/* DIAGNOSTICS
/*	Problems and transactions are logged to the syslog daemon.
/* BUGS
/*	There is no automatic means to limit the number of entries in the
/*	TLS session caches and/or the size of the TLS cache files.
/* CONFIGURATION PARAMETERS
/* .ad
/* .fi
/*	Changes to \fBmain.cf\fR are not picked up automatically,
/*	because \fBtlsmgr\fR(8) is a persistent processes.  Use the
/*	command "\fBpostfix reload\fR" after a configuration change.
/*
/*	The text below provides only a parameter summary. See
/*	\fBpostconf\fR(5) for more details including examples.
/* TLS SESSION CACHE
/* .ad
/* .fi
/* .IP "\fBsmtpd_tls_session_cache_database (empty)\fR"
/*	Name of the file containing the optional Postfix SMTP server
/*	TLS session cache.
/* .IP "\fBsmtpd_tls_session_cache_timeout (3600s)\fR"
/*	The expiration time of Postfix SMTP server TLS session cache
/*	information.
/* .IP "\fBsmtp_tls_session_cache_database (empty)\fR"
/*	Name of the file containing the optional Postfix SMTP client
/*	TLS session cache.
/* .IP "\fBsmtp_tls_session_cache_timeout (3600s)\fR"
/*	The expiration time of Postfix SMTP client TLS session cache
/*	information.
/* PSEUDO RANDOM NUMBER GENERATOR
/* .ad
/* .fi
/* .IP "\fBtls_random_source (see 'postconf -d' output)\fR"
/*	The external entropy source for the in-memory \fBtlsmgr\fR(8) pseudo
/*	random number generator (PRNG) pool.
/* .IP "\fBtls_random_bytes (32)\fR"
/*	The number of bytes that \fBtlsmgr\fR(8) reads from $tls_random_source
/*	when (re)seeding the in-memory pseudo random number generator (PRNG)
/*	pool.
/* .IP "\fBtls_random_exchange_name (${config_directory}/prng_exch)\fR"
/*	Name of the pseudo random number generator (PRNG) state file
/*	that is maintained by \fBtlsmgr\fR(8).
/* .IP "\fBtls_random_prng_update_period (3600s)\fR"
/*	The time between attempts by \fBtlsmgr\fR(8) to save the state of
/*	the pseudo random number generator (PRNG) to the file specified
/*	with $tls_random_exchange_name.
/* .IP "\fBtls_random_reseed_period (3600s)\fR"
/*	The maximal time between attempts by \fBtlsmgr\fR(8) to re-seed the
/*	in-memory pseudo random number generator (PRNG) pool from external
/*	sources.
/* MISCELLANEOUS CONTROLS
/* .ad
/* .fi
/* .IP "\fBconfig_directory (see 'postconf -d' output)\fR"
/*	The default location of the Postfix main.cf and master.cf
/*	configuration files.
/* .IP "\fBdaemon_timeout (18000s)\fR"
/*	How much time a Postfix daemon process may take to handle a
/*	request before it is terminated by a built-in watchdog timer.
/* .IP "\fBprocess_id (read-only)\fR"
/*	The process ID of a Postfix command or daemon process.
/* .IP "\fBprocess_name (read-only)\fR"
/*	The process name of a Postfix command or daemon process.
/* .IP "\fBsyslog_facility (mail)\fR"
/*	The syslog facility of Postfix logging.
/* .IP "\fBsyslog_name (postfix)\fR"
/*	The mail system name that is prepended to the process name in syslog
/*	records, so that "smtpd" becomes, for example, "postfix/smtpd".
/* SEE ALSO
/*	smtp(8), Postfix SMTP client
/*	smtpd(8), Postfix SMTP server
/*	postconf(5), configuration parameters
/*	master(5), generic daemon options
/*	master(8), process manager
/*	syslogd(8), system logging
/* README FILES
/* .ad
/* .fi
/*	Use "\fBpostconf readme_directory\fR" or
/*	"\fBpostconf html_directory\fR" to locate this information.
/* .na
/* .nf
/*	TLS_README, Postfix TLS configuration and operation
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Lutz Jaenicke
/*	BTU Cottbus
/*	Allgemeine Elektrotechnik
/*	Universitaetsplatz 3-4
/*	D-03044 Cottbus, Germany
/*
/*	Adapted by:
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>			/* gettimeofday, not POSIX */
#include <limits.h>

#ifndef UCHAR_MAX
#define UCHAR_MAX 0xff
#endif

/* OpenSSL library. */

#ifdef USE_TLS
#include <openssl/rand.h>		/* For the PRNG */
#endif

/* Utility library. */

#include <msg.h>
#include <events.h>
#include <stringops.h>
#include <mymalloc.h>
#include <iostuff.h>
#include <vstream.h>
#include <vstring.h>
#include <vstring_vstream.h>
#include <attr.h>

/* Global library. */

#include <mail_conf.h>
#include <mail_params.h>
#include <tls_mgr.h>
#include <mail_proto.h>

/* Master process interface. */

#include <master_proto.h>
#include <mail_server.h>

/* TLS library. */

#ifdef USE_TLS
#include <tls_prng.h>
#include <tls_scache.h>

/* Application-specific. */

 /*
  * Tunables.
  */
char   *var_tls_rand_source;
int     var_tls_rand_bytes;
int     var_tls_reseed_period;
int     var_tls_prng_exch_period;

 /*
  * Bound the time that we are willing to wait for an I/O operation. This
  * produces better error messages than waiting until the watchdog timer
  * kills the process.
  */
#define TLS_MGR_TIMEOUT	10

 /*
  * State for updating the PRNG exchange file.
  */
static TLS_PRNG_SRC *rand_exch;

 /*
  * State for seeding the internal PRNG from external source.
  */
static TLS_PRNG_SRC *rand_source_dev;
static TLS_PRNG_SRC *rand_source_egd;
static TLS_PRNG_SRC *rand_source_file;

 /*
  * The external entropy source type is encoded in the source name. The
  * obvious alternative is to have separate configuration parameters per
  * source type, so that one process can query multiple external sources.
  */
#define DEV_PREF "dev:"
#define DEV_PREF_LEN (sizeof((DEV_PREF)) - 1)
#define DEV_PATH(dev) ((dev) + EGD_PREF_LEN)

#define EGD_PREF "egd:"
#define EGD_PREF_LEN (sizeof((EGD_PREF)) - 1)
#define EGD_PATH(egd) ((egd) + EGD_PREF_LEN)

 /*
  * State for TLS client and server session caches.
  */
static TLS_SCACHE *clnt_scache_info;	/* cache handle */
static int clnt_scache_active;		/* scan in progress */

static TLS_SCACHE *srvr_scache_info;	/* cache handle */
static int srvr_scache_active;		/* scan in progress */

#define WHICH_CACHE_INFO(type) \
    (type == TLS_MGR_SCACHE_CLIENT ? clnt_scache_info : \
	type == TLS_MGR_SCACHE_SERVER ? srvr_scache_info : 0)

 /*
  * SLMs.
  */
#define STR(x)		vstring_str(x)
#define LEN(x)		VSTRING_LEN(x)
#define STREQ(x, y)	(strcmp((x), (y)) == 0)

/* tlsmgr_prng_exch_event - update PRNG exchange file */

static void tlsmgr_prng_exch_event(int unused_event, char *dummy)
{
    const char *myname = "tlsmgr_prng_exch_event";
    unsigned char randbyte;
    int     next_period;
    struct stat st;

    if (msg_verbose)
	msg_info("%s: update PRNG exchange file", myname);

    /*
     * Sanity check. If the PRNG exchange file was removed, there is no point
     * updating it further. Restart the process and update the new file.
     */
    if (fstat(rand_exch->fd, &st) < 0)
	msg_fatal("cannot fstat() the PRNG exchange file: %m");
    if (st.st_nlink == 0) {
	msg_warn("PRNG exchange file was removed -- exiting to reopen");
	sleep(1);
	exit(0);
    }
    tls_prng_exch_update(rand_exch);

    /*
     * Make prediction difficult for outsiders and calculate the time for the
     * next execution randomly.
     */
    RAND_bytes(&randbyte, 1);
    next_period = (var_tls_prng_exch_period * randbyte) / UCHAR_MAX;
    event_request_timer(tlsmgr_prng_exch_event, dummy, next_period);
}

/* tlsmgr_reseed_event - re-seed the internal PRNG pool */

static void tlsmgr_reseed_event(int unused_event, char *dummy)
{
    int     next_period;
    unsigned char randbyte;
    int     must_exit = 0;

    /*
     * Reseed the internal PRNG from external source. Errors are recoverable.
     * We simply restart and reconnect without making a fuss. This is OK
     * because we do require that exchange file updates succeed. The exchange
     * file is the only entropy source that really matters in the long term.
     * 
     * If the administrator specifies an external randomness source that we
     * could not open upon start-up, restart to see if we can open it now
     * (and log a nagging warning if we can't).
     */
    if (*var_tls_rand_source) {

	/*
	 * Source is a random device.
	 */
	if (rand_source_dev) {
	    if (tls_prng_dev_read(rand_source_dev, var_tls_rand_bytes) <= 0) {
		msg_info("cannot read from entropy device %s: %m -- "
			 "exiting to reopen", DEV_PATH(var_tls_rand_source));
		must_exit = 1;
	    }
	}

	/*
	 * Source is an EGD compatible socket.
	 */
	else if (rand_source_egd) {
	    if (tls_prng_egd_read(rand_source_egd, var_tls_rand_bytes) <= 0) {
		msg_info("lost connection to EGD server %s -- "
		     "exiting to reconnect", EGD_PATH(var_tls_rand_source));
		must_exit = 1;
	    }
	}

	/*
	 * Source is a regular file. Read the content once and close the
	 * file.
	 */
	else if (rand_source_file) {
	    if (tls_prng_file_read(rand_source_file, var_tls_rand_bytes) <= 0)
		msg_warn("cannot read from entropy file %s: %m",
			 var_tls_rand_source);
	    tls_prng_file_close(rand_source_file);
	    rand_source_file = 0;
	    var_tls_rand_source[0] = 0;
	}

	/*
	 * Could not open the external source upon start-up. See if we can
	 * open it this time. Save PRNG state before we exit.
	 */
	else {
	    msg_info("exiting to reopen external entropy source %s",
		     var_tls_rand_source);
	    must_exit = 1;
	}
    }

    /*
     * Save PRNG state in case we must exit.
     */
    if (must_exit) {
	if (rand_exch)
	    tls_prng_exch_update(rand_exch);
	sleep(1);
	exit(0);
    }

    /*
     * Make prediction difficult for outsiders and calculate the time for the
     * next execution randomly.
     */
    RAND_bytes(&randbyte, 1);
    next_period = (var_tls_reseed_period * randbyte) / UCHAR_MAX;
    event_request_timer(tlsmgr_reseed_event, dummy, next_period);
}

/* tlsmgr_clnt_cache_run_event - start SMTP client TLS session cache scan */

static void tlsmgr_clnt_cache_run_event(int unused_event, char *dummy)
{
    const char *myname = "tlsmgr_clnt_cache_run_event";

    /*
     * This routine runs when it is time for another TLS session cache scan.
     * Make sure this routine gets called again in the future.
     * 
     * Don't start a new scan when the timer goes off while cache cleanup is
     * still in progress.
     */
    if (var_smtp_tls_loglevel >= 3)
	msg_info("%s: start TLS client session cache cleanup", myname);

    if (clnt_scache_active == 0)
	clnt_scache_active =
	    tls_scache_sequence(clnt_scache_info, DICT_SEQ_FUN_FIRST,
				TLS_SCACHE_SEQUENCE_NOTHING);

    event_request_timer(tlsmgr_clnt_cache_run_event, dummy,
			var_smtp_tls_scache_timeout);
}

/* tlsmgr_srvr_cache_run_event - start SMTP server TLS session cache scan */

static void tlsmgr_srvr_cache_run_event(int unused_event, char *dummy)
{
    const char *myname = "tlsmgr_srvr_cache_run_event";

    /*
     * This routine runs when it is time for another TLS session cache scan.
     * Make sure this routine gets called again in the future.
     * 
     * Don't start a new scan when the timer goes off while cache cleanup is
     * still in progress.
     */
    if (var_smtpd_tls_loglevel >= 3)
	msg_info("%s: start TLS server session cache cleanup", myname);

    if (srvr_scache_active == 0)
	srvr_scache_active =
	    tls_scache_sequence(srvr_scache_info, DICT_SEQ_FUN_FIRST,
				TLS_SCACHE_SEQUENCE_NOTHING);

    event_request_timer(tlsmgr_srvr_cache_run_event, dummy,
			var_smtpd_tls_scache_timeout);
}

/* tlsmgr_loop - TLS manager main loop */

static int tlsmgr_loop(char *unused_name, char **unused_argv)
{
    struct timeval tv;

    /*
     * Update the PRNG pool with the time of day. We do it here after every
     * event (including internal timer events and external client request
     * events), instead of doing it in individual event call-back routines.
     */
    GETTIMEOFDAY(&tv);
    RAND_seed(&tv, sizeof(struct timeval));

    /*
     * This routine runs as part of the event handling loop, after the event
     * manager has delivered a timer or I/O event, or after it has waited for
     * a specified amount of time. The result value of tlsmgr_loop()
     * specifies how long the event manager should wait for the next event.
     * 
     * We use this loop to interleave TLS session cache cleanup with other
     * activity. Interleaved processing is needed when we use a client-server
     * protocol for entropy and session state exchange with smtp(8) and
     * smtpd(8) processes.
     */
#define DONT_WAIT	0
#define WAIT_FOR_EVENT	(-1)

    if (clnt_scache_active)
	clnt_scache_active =
	    tls_scache_sequence(clnt_scache_info, DICT_SEQ_FUN_NEXT,
				TLS_SCACHE_SEQUENCE_NOTHING);
    if (srvr_scache_active)
	srvr_scache_active =
	    tls_scache_sequence(srvr_scache_info, DICT_SEQ_FUN_NEXT,
				TLS_SCACHE_SEQUENCE_NOTHING);

    if (clnt_scache_active || srvr_scache_active)
	return (DONT_WAIT);
    return (WAIT_FOR_EVENT);
}

/* tlsmgr_request_receive - receive request */

static int tlsmgr_request_receive(VSTREAM *client_stream, VSTRING *request)
{
    int     count;

    /*
     * Kluge: choose the protocol depending on the request size.
     */
    if (read_wait(vstream_fileno(client_stream), var_ipc_timeout) < 0) {
	msg_warn("timeout while waiting for data from %s",
		 VSTREAM_PATH(client_stream));
	return (-1);
    }
    if ((count = peekfd(vstream_fileno(client_stream))) < 0) {
	msg_warn("cannot examine read buffer of %s: %m",
		 VSTREAM_PATH(client_stream));
	return (-1);
    }

    /*
     * Short request: master trigger. Use the string+null protocol.
     */
    if (count <= 2) {
	if (vstring_get_null(request, client_stream) == VSTREAM_EOF) {
	    msg_warn("end-of-input while reading request from %s: %m",
		     VSTREAM_PATH(client_stream));
	    return (-1);
	}
    }

    /*
     * Long request: real tlsmgr client. Use the attribute list protocol.
     */
    else {
	if (attr_scan(client_stream,
		      ATTR_FLAG_MORE | ATTR_FLAG_STRICT,
		      ATTR_TYPE_STR, TLS_MGR_ATTR_REQ, request,
		      ATTR_TYPE_END) != 1) {
	    return (-1);
	}
    }
    return (0);
}

/* tlsmgr_service - respond to external request */

static void tlsmgr_service(VSTREAM *client_stream, char *unused_service,
			           char **argv)
{
    static VSTRING *request = 0;
    static VSTRING *cache_id = 0;
    static VSTRING *buffer = 0;
    int     cache_type;
    int     len;
    static char wakeup[] = {		/* master wakeup request */
	TRIGGER_REQ_WAKEUP,
	0,
    };
    TLS_SCACHE *cache;
    int     status = TLS_MGR_STAT_FAIL;

    /*
     * Sanity check. This service takes no command-line arguments.
     */
    if (argv[0])
	msg_fatal("unexpected command-line argument: %s", argv[0]);

    /*
     * Initialize. We're select threaded, so we can use static buffers.
     */
    if (request == 0) {
	request = vstring_alloc(10);
	cache_id = vstring_alloc(10);
	buffer = vstring_alloc(10);
    }

    /*
     * This routine runs whenever a client connects to the socket dedicated
     * to the tlsmgr service (including wake up events sent by the master).
     * All connection-management stuff is handled by the common code in
     * multi_server.c.
     */
    if (tlsmgr_request_receive(client_stream, request) == 0) {

	/*
	 * Load session from cache.
	 */
	if (STREQ(STR(request), TLS_MGR_REQ_LOOKUP)) {
	    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
			ATTR_TYPE_INT, TLS_MGR_ATTR_CACHE_TYPE, &cache_type,
			  ATTR_TYPE_STR, TLS_MGR_ATTR_CACHE_ID, cache_id,
			  ATTR_TYPE_END) == 2) {
		if ((cache = WHICH_CACHE_INFO(cache_type)) == 0) {
		    msg_warn("bogus cache type \"%d\" in \"%s\" request",
			     cache_type, TLS_MGR_REQ_LOOKUP);
		    VSTRING_RESET(buffer);
		} else {
		    status = tls_scache_lookup(cache, STR(cache_id), buffer) ?
			TLS_MGR_STAT_OK : TLS_MGR_STAT_ERR;
		}
	    }
	    attr_print(client_stream, ATTR_FLAG_NONE,
		       ATTR_TYPE_INT, MAIL_ATTR_STATUS, status,
		       ATTR_TYPE_DATA, TLS_MGR_ATTR_SESSION,
		       LEN(buffer), STR(buffer),
		       ATTR_TYPE_END);
	}

	/*
	 * Save session to cache.
	 */
	else if (STREQ(STR(request), TLS_MGR_REQ_UPDATE)) {
	    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
			ATTR_TYPE_INT, TLS_MGR_ATTR_CACHE_TYPE, &cache_type,
			  ATTR_TYPE_STR, TLS_MGR_ATTR_CACHE_ID, cache_id,
			  ATTR_TYPE_DATA, TLS_MGR_ATTR_SESSION, buffer,
			  ATTR_TYPE_END) == 3) {
		if ((cache = WHICH_CACHE_INFO(cache_type)) == 0) {
		    msg_warn("bogus cache type \"%d\" in \"%s\" request",
			     cache_type, TLS_MGR_REQ_UPDATE);
		} else {
		    status =
			tls_scache_update(cache, STR(cache_id),
					  STR(buffer), LEN(buffer)) ?
			TLS_MGR_STAT_OK : TLS_MGR_STAT_ERR;
		}
	    }
	    attr_print(client_stream, ATTR_FLAG_NONE,
		       ATTR_TYPE_INT, MAIL_ATTR_STATUS, status,
		       ATTR_TYPE_END);
	}

	/*
	 * Delete session from cache.
	 */
	else if (STREQ(STR(request), TLS_MGR_REQ_DELETE)) {
	    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
			ATTR_TYPE_INT, TLS_MGR_ATTR_CACHE_TYPE, &cache_type,
			  ATTR_TYPE_STR, TLS_MGR_ATTR_CACHE_ID, cache_id,
			  ATTR_TYPE_END) == 2) {
		if ((cache = WHICH_CACHE_INFO(cache_type)) == 0) {
		    msg_warn("bogus cache type \"%d\" in \"%s\" request",
			     cache_type, TLS_MGR_REQ_DELETE);
		} else {
		    status = tls_scache_delete(cache, STR(cache_id)) ?
			TLS_MGR_STAT_OK : TLS_MGR_STAT_ERR;
		}
	    }
	    attr_print(client_stream, ATTR_FLAG_NONE,
		       ATTR_TYPE_INT, MAIL_ATTR_STATUS, status,
		       ATTR_TYPE_END);
	}

	/*
	 * Entropy request.
	 */
	else if (STREQ(STR(request), TLS_MGR_REQ_SEED)) {
	    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
			  ATTR_TYPE_INT, TLS_MGR_ATTR_SIZE, &len,
			  ATTR_TYPE_END) == 1) {
		VSTRING_RESET(buffer);
		if (len <= 0 || len > 255) {
		    msg_warn("bogus seed length \"%d\" in \"%s\" request",
			     len, TLS_MGR_REQ_SEED);
		} else {
		    VSTRING_SPACE(buffer, len);
		    RAND_bytes((unsigned char *) STR(buffer), len);
		    VSTRING_AT_OFFSET(buffer, len);	/* XXX not part of the
							 * official interface */
		    status = TLS_MGR_STAT_OK;
		}
	    }
	    attr_print(client_stream, ATTR_FLAG_NONE,
		       ATTR_TYPE_INT, MAIL_ATTR_STATUS, status,
		       ATTR_TYPE_DATA, TLS_MGR_ATTR_SEED,
		       LEN(buffer), STR(buffer),
		       ATTR_TYPE_END);
	}

	/*
	 * Caching policy request.
	 */
	else if (STREQ(STR(request), TLS_MGR_REQ_POLICY)) {
	    int     cache_types = 0;

	    if (attr_scan(client_stream, ATTR_FLAG_STRICT,
			  ATTR_TYPE_END) == 0) {
		if (clnt_scache_info)
		    cache_types |= TLS_MGR_SCACHE_CLIENT;
		if (srvr_scache_info)
		    cache_types |= TLS_MGR_SCACHE_SERVER;
		status = TLS_MGR_STAT_OK;
	    }
	    attr_print(client_stream, ATTR_FLAG_NONE,
		       ATTR_TYPE_INT, MAIL_ATTR_STATUS, status,
		       ATTR_TYPE_INT, TLS_MGR_ATTR_POLICY, cache_types,
		       ATTR_TYPE_END);
	}

	/*
	 * Master trigger. Normally, these triggers arrive only after some
	 * other process requested the tlsmgr's service. The purpose is to
	 * restart the tlsmgr after it aborted due to a fatal run-time error,
	 * so that it can continue its housekeeping even while nothing is
	 * using TLS.
	 * 
	 * XXX Which begs the question, if TLS isn't used often, do we need a
	 * tlsmgr background process? It could terminate when the session
	 * caches are empty.
	 */
	else if (STREQ(STR(request), wakeup)) {
	    if (msg_verbose)
		msg_info("received master trigger");
	    multi_server_disconnect(client_stream);
	    return;				/* NOT: vstream_fflush */
	}
    }

    /*
     * Protocol error.
     */
    else {
	attr_print(client_stream, ATTR_FLAG_NONE,
		   ATTR_TYPE_INT, MAIL_ATTR_STATUS, TLS_MGR_STAT_FAIL,
		   ATTR_TYPE_END);
    }
    vstream_fflush(client_stream);
}

/* tlsmgr_pre_init - pre-jail initialization */

static void tlsmgr_pre_init(char *unused_name, char **unused_argv)
{
    char   *path;
    struct timeval tv;

    /*
     * If nothing else works then at least this will get us a few bits of
     * entropy.
     */
    GETTIMEOFDAY(&tv);
    tv.tv_sec ^= getpid();
    RAND_seed(&tv, sizeof(struct timeval));


    /*
     * Open the external entropy source. We will not be able to open it again
     * after we are sent to chroot jail, so we keep it open. Errors are not
     * fatal. The exchange file (see below) is the only entropy source that
     * really matters in the long run.
     * 
     * Security note: we open the entropy source while privileged, but we don't
     * access the source until after we release privileges. This way, none of
     * the OpenSSL code gets to execute while we are privileged.
     */
    if (*var_tls_rand_source) {

	/*
	 * Source is a random device.
	 */
	if (!strncmp(var_tls_rand_source, DEV_PREF, DEV_PREF_LEN)) {
	    path = DEV_PATH(var_tls_rand_source);
	    rand_source_dev = tls_prng_dev_open(path, TLS_MGR_TIMEOUT);
	    if (rand_source_dev == 0)
		msg_warn("cannot open entropy device %s: %m", path);
	}

	/*
	 * Source is an EGD compatible socket.
	 */
	else if (!strncmp(var_tls_rand_source, EGD_PREF, EGD_PREF_LEN)) {
	    path = EGD_PATH(var_tls_rand_source);
	    rand_source_egd = tls_prng_egd_open(path, TLS_MGR_TIMEOUT);
	    if (rand_source_egd == 0)
		msg_warn("cannot connect to EGD server %s: %m", path);
	}

	/*
	 * Source is regular file. We read this only once.
	 */
	else {
	    rand_source_file =
		tls_prng_file_open(var_tls_rand_source, TLS_MGR_TIMEOUT);
	}
    } else {
	msg_warn("no entropy source specified with parameter %s",
		 VAR_TLS_RAND_SOURCE);
	msg_warn("encryption keys etc. may be predictable");
    }

    /*
     * Open the PRNG exchange file while privileged. Start the exchange file
     * read/update pseudo thread after dropping privileges.
     */
    if (*var_tls_rand_exch_name) {
	rand_exch = tls_prng_exch_open(var_tls_rand_exch_name);
	if (rand_exch == 0)
	    msg_fatal("cannot open PRNG exchange file %s: %m",
		      var_tls_rand_exch_name);
    }

    /*
     * Open the session cache files and discard old information while
     * privileged. Start the cache maintenance pseudo threads after dropping
     * privileges.
     */
    if (*var_smtp_tls_scache_db)
	clnt_scache_info =
	    tls_scache_open(var_smtp_tls_scache_db, "client",
			    var_smtp_tls_loglevel,
			    var_smtp_tls_scache_timeout);
    if (*var_smtpd_tls_scache_db)
	srvr_scache_info =
	    tls_scache_open(var_smtpd_tls_scache_db, "server",
			    var_smtpd_tls_loglevel,
			    var_smtpd_tls_scache_timeout);
}

/* tlsmgr_post_init - post-jail initialization */

static void tlsmgr_post_init(char *unused_name, char **unused_argv)
{
#define NULL_EVENT	(0)
#define NULL_CONTEXT	((char *) 0)

    /*
     * This routine runs after the skeleton code has entered the chroot jail,
     * but before any client requests are serviced. Prevent automatic process
     * suicide after a limited number of client requests or after a limited
     * amount of idle time.
     */
    var_use_limit = 0;
    var_idle_limit = 0;

    /*
     * Start the internal PRNG re-seeding pseudo thread first.
     */
    if (*var_tls_rand_source) {
	if (var_tls_reseed_period > INT_MAX / UCHAR_MAX)
	    var_tls_reseed_period = INT_MAX / UCHAR_MAX;
	tlsmgr_reseed_event(NULL_EVENT, NULL_CONTEXT);
    }

    /*
     * Start the exchange file read/update pseudo thread.
     */
    if (*var_tls_rand_exch_name) {
	if (var_tls_prng_exch_period > INT_MAX / UCHAR_MAX)
	    var_tls_prng_exch_period = INT_MAX / UCHAR_MAX;
	tlsmgr_prng_exch_event(NULL_EVENT, NULL_CONTEXT);
    }

    /*
     * Start the cache maintenance pseudo threads last. Strictly speaking
     * there is nothing to clean up after we truncate the database to zero
     * length, but early cleanup makes verbose logging more informative (we
     * get positive confirmation that the cleanup threads are running).
     */
    if (*var_smtp_tls_scache_db)
	tlsmgr_clnt_cache_run_event(NULL_EVENT, NULL_CONTEXT);
    if (*var_smtpd_tls_scache_db)
	tlsmgr_srvr_cache_run_event(NULL_EVENT, NULL_CONTEXT);
}

/* tlsmgr_before_exit - save PRNG state before exit */

static void tlsmgr_before_exit(char *unused_service_name, char **unused_argv)
{

    /*
     * Save state before we exit after "postfix reload".
     */
    if (rand_exch)
	tls_prng_exch_update(rand_exch);
}

/* main - the main program */

int     main(int argc, char **argv)
{
    static CONFIG_STR_TABLE str_table[] = {
	VAR_TLS_RAND_SOURCE, DEF_TLS_RAND_SOURCE, &var_tls_rand_source, 0, 0,
	0,
    };
    static CONFIG_TIME_TABLE time_table[] = {
	VAR_TLS_RESEED_PERIOD, DEF_TLS_RESEED_PERIOD, &var_tls_reseed_period, 1, 0,
	VAR_TLS_PRNG_UPD_PERIOD, DEF_TLS_PRNG_UPD_PERIOD, &var_tls_prng_exch_period, 1, 0,
	0,
    };
    static CONFIG_INT_TABLE int_table[] = {
	VAR_TLS_RAND_BYTES, DEF_TLS_RAND_BYTES, &var_tls_rand_bytes, 1, 0,
	0,
    };

    /*
     * Use the multi service skeleton, and require that no-one else is
     * monitoring our service port while this process runs.
     */
    multi_server_main(argc, argv, tlsmgr_service,
		      MAIL_SERVER_TIME_TABLE, time_table,
		      MAIL_SERVER_INT_TABLE, int_table,
		      MAIL_SERVER_STR_TABLE, str_table,
		      MAIL_SERVER_PRE_INIT, tlsmgr_pre_init,
		      MAIL_SERVER_POST_INIT, tlsmgr_post_init,
		      MAIL_SERVER_EXIT, tlsmgr_before_exit,
		      MAIL_SERVER_LOOP, tlsmgr_loop,
		      MAIL_SERVER_SOLITARY,
		      0);
}

#else

/* tlsmgr_service - respond to external trigger(s), non-TLS version */

static void tlsmgr_service(VSTREAM *unused_stream, char *unused_service,
			           char **unused_argv)
{
    msg_info("TLS support is not compiled in -- exiting");
}

/* main - the main program, non-TLS version */

int     main(int argc, char **argv)
{

    /*
     * 200411 We can't simply use msg_fatal() here, because the logging
     * hasn't been initialized. The text would disappear because stderr is
     * redirected to /dev/null.
     * 
     * We invoke multi_server_main() to complete program initialization
     * (including logging) and then invoke the tlsmgr_service() routine to
     * log the message that says why this program will not run.
     */
    multi_server_main(argc, argv, tlsmgr_service,
		      0);
}

#endif