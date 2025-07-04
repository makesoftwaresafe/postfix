/*++
/* NAME
/*	dict_cidr 3
/* SUMMARY
/*	Dictionary interface for CIDR data
/* SYNOPSIS
/*	#include <dict_cidr.h>
/*
/*	DICT	*dict_cidr_open(name, open_flags, dict_flags)
/*	const char *name;
/*	int	open_flags;
/*	int	dict_flags;
/* DESCRIPTION
/*	dict_cidr_open() opens the named file and stores
/*	the key/value pairs where the key must be either a
/*	"naked" IP address or a netblock in CIDR notation.
/* SEE ALSO
/*	dict(3) generic dictionary manager
/*	cidr_table(5) CIDR table configuration
/* AUTHOR(S)
/*	Jozsef Kadlecsik
/*	kadlec@blackhole.kfki.hu
/*	KFKI Research Institute for Particle and Nuclear Physics
/*	POB. 49
/*	1525 Budapest, Hungary
/*
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*
/*	Wietse Venema
/*	Google, Inc.
/*	111 8th Avenue
/*	New York, NY 10011, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Utility library. */

#include <mymalloc.h>
#include <msg.h>
#include <vstream.h>
#include <vstring.h>
#include <stringops.h>
#include <readlline.h>
#include <dict.h>
#include <myaddrinfo.h>
#include <cidr_match.h>
#include <dict_cidr.h>
#include <warn_stat.h>
#include <mvect.h>

/* Application-specific. */

 /*
  * Each rule in a CIDR table is parsed and stored in a linked list.
  */
typedef struct DICT_CIDR_ENTRY {
    CIDR_MATCH cidr_info;		/* must be first */
    char   *value;			/* lookup result */
    int     lineno;
} DICT_CIDR_ENTRY;

typedef struct {
    DICT    dict;			/* generic members */
    DICT_CIDR_ENTRY *head;		/* first entry */
} DICT_CIDR;

/* dict_cidr_lookup - CIDR table lookup */

static const char *dict_cidr_lookup(DICT *dict, const char *key)
{
    DICT_CIDR *dict_cidr = (DICT_CIDR *) dict;
    DICT_CIDR_ENTRY *entry;

    if (msg_verbose)
	msg_info("dict_cidr_lookup: %s: %s", dict->name, key);

    dict->error = 0;

    if ((entry = (DICT_CIDR_ENTRY *)
	 cidr_match_execute(&(dict_cidr->head->cidr_info), key)) != 0)
	return (entry->value);
    return (0);
}

/* dict_cidr_close - close the CIDR table */

static void dict_cidr_close(DICT *dict)
{
    DICT_CIDR *dict_cidr = (DICT_CIDR *) dict;
    DICT_CIDR_ENTRY *entry;
    DICT_CIDR_ENTRY *next;

    for (entry = dict_cidr->head; entry; entry = next) {
	next = (DICT_CIDR_ENTRY *) entry->cidr_info.next;
	myfree(entry->value);
	myfree((void *) entry);
    }
    dict_free(dict);
}

/* dict_cidr_parse_rule - parse CIDR table rule into network, mask and value */

static DICT_CIDR_ENTRY *dict_cidr_parse_rule(DICT *dict, char *p, int lineno,
					          int nesting, VSTRING *why)
{
    DICT_CIDR_ENTRY *rule;
    char   *pattern;
    char   *value;
    CIDR_MATCH cidr_info;
    MAI_HOSTADDR_STR hostaddr;
    int     match = 1;

    /*
     * IF must be followed by a pattern.
     */
    if (strncasecmp(p, "IF", 2) == 0 && !ISALNUM(p[2])) {
	p += 2;
	for (;;) {
	    if (*p == '!')
		match = !match;
	    else if (!ISSPACE(*p))
		break;
	    p++;
	}
	if (*p == 0) {
	    vstring_sprintf(why, "no address pattern");
	    return (0);
	}
	trimblanks(p, 0)[0] = 0;		/* Trim trailing blanks */
	if (cidr_match_parse_if(&cidr_info, p, match, why) != 0)
	    return (0);
	value = "";
    }

    /*
     * ENDIF must not be followed by other text.
     */
    else if (strncasecmp(p, "ENDIF", 5) == 0 && !ISALNUM(p[5])) {
	p += 5;
	while (*p && ISSPACE(*p))		/* Skip whitespace */
	    p++;
	if (*p != 0) {
	    vstring_sprintf(why, "garbage after ENDIF");
	    return (0);
	}
	if (nesting == 0) {
	    vstring_sprintf(why, "ENDIF without IF");
	    return (0);
	}
	cidr_match_endif(&cidr_info);
	value = "";
    }

    /*
     * An address pattern.
     */
    else {

	/*
	 * Process negation operators.
	 */
	for (;;) {
	    if (*p == '!')
		match = !match;
	    else if (!ISSPACE(*p))
		break;
	    p++;
	}

	/*
	 * Split the rule into key and value. We already eliminated leading
	 * whitespace, comments, empty lines or lines with whitespace only.
	 * This means a null key can't happen but we will handle this anyway.
	 */
	pattern = p;
	while (*p && !ISSPACE(*p))		/* Skip over key */
	    p++;
	if (*p)					/* Terminate key */
	    *p++ = 0;
	while (*p && ISSPACE(*p))		/* Skip whitespace */
	    p++;
	value = p;
	trimblanks(value, 0)[0] = 0;		/* Trim trailing blanks */
	if (*pattern == 0) {
	    vstring_sprintf(why, "no address pattern");
	    return (0);
	}

	/*
	 * Parse the pattern, destroying it in the process.
	 */
	if (cidr_match_parse(&cidr_info, pattern, match, why) != 0)
	    return (0);

	if (*value == 0) {
	    vstring_sprintf(why, "no lookup result");
	    return (0);
	}
    }

    /*
     * Optionally replace the value file the contents of a file.
     */
    if (dict->flags & DICT_FLAG_SRC_RHS_IS_FILE) {
	VSTRING *base64_buf;
	char   *err;

	if ((base64_buf = dict_file_to_b64(dict, value)) == 0) {
	    err = dict_file_get_error(dict);
	    vstring_strcpy(why, err);
	    myfree(err);
	    return (0);
	}
	value = vstring_str(base64_buf);
    }

    /*
     * Bundle up the result.
     */
    rule = (DICT_CIDR_ENTRY *) mymalloc(sizeof(DICT_CIDR_ENTRY));
    rule->cidr_info = cidr_info;
    rule->value = mystrdup(value);
    rule->lineno = lineno;

    if (msg_verbose) {
	if (inet_ntop(cidr_info.addr_family, cidr_info.net_bytes,
		      hostaddr.buf, sizeof(hostaddr.buf)) == 0)
	    msg_fatal("inet_ntop: %m");
	msg_info("dict_cidr_open: add %s/%d %s",
		 hostaddr.buf, cidr_info.mask_shift, rule->value);
    }
    return (rule);
}

/* dict_cidr_open - parse CIDR table */

DICT   *dict_cidr_open(const char *mapname, int open_flags, int dict_flags)
{
    const char myname[] = "dict_cidr_open";
    DICT_CIDR *dict_cidr;
    VSTREAM *map_fp = 0;
    struct stat st;
    VSTRING *line_buffer = 0;
    VSTRING *why = 0;
    DICT_CIDR_ENTRY *rule;
    DICT_CIDR_ENTRY *last_rule = 0;
    int     last_line = 0;
    int     lineno;
    int     nesting = 0;
    DICT_CIDR_ENTRY **rule_stack = 0;
    MVECT   mvect;

    /*
     * Let the optimizer worry about eliminating redundant code.
     */
#define DICT_CIDR_OPEN_RETURN(d) do { \
	DICT *__d = (d); \
	if (map_fp != 0 && vstream_fclose(map_fp)) \
	    msg_fatal("cidr map %s: read error: %m", mapname); \
	if (line_buffer != 0) \
	    vstring_free(line_buffer); \
	if (why != 0) \
	    vstring_free(why); \
	return (__d); \
    } while (0)

    /*
     * Sanity checks.
     */
    if (open_flags != O_RDONLY)
	DICT_CIDR_OPEN_RETURN(dict_surrogate(DICT_TYPE_CIDR, mapname,
					     open_flags, dict_flags,
				  "%s:%s map requires O_RDONLY access mode",
					     DICT_TYPE_CIDR, mapname));

    /*
     * Open the configuration file.
     */
    if ((map_fp = dict_stream_open(DICT_TYPE_CIDR, mapname, O_RDONLY,
				   dict_flags, &st, &why)) == 0)
	DICT_CIDR_OPEN_RETURN(dict_surrogate(DICT_TYPE_CIDR, mapname,
					     open_flags, dict_flags,
					     "%s", vstring_str(why)));
    line_buffer = vstring_alloc(100);
    why = vstring_alloc(100);

    /*
     * XXX Eliminate unnecessary queries by setting a flag that says "this
     * map matches network addresses only".
     */
    dict_cidr = (DICT_CIDR *) dict_alloc(DICT_TYPE_CIDR, mapname,
					 sizeof(*dict_cidr));
    dict_cidr->dict.lookup = dict_cidr_lookup;
    dict_cidr->dict.close = dict_cidr_close;
    dict_cidr->dict.flags = dict_flags | DICT_FLAG_PATTERN;
    dict_cidr->head = 0;

    dict_cidr->dict.owner.uid = st.st_uid;
    dict_cidr->dict.owner.status = (st.st_uid != 0);

    while (readllines(line_buffer, map_fp, &last_line, &lineno)) {
	rule = dict_cidr_parse_rule(&dict_cidr->dict,
				    vstring_str(line_buffer), lineno,
				    nesting, why);
	if (rule == 0) {
	    msg_warn("cidr map %s, line %d: %s: skipping this rule",
		     mapname, lineno, vstring_str(why));
	    continue;
	}
	if (rule->cidr_info.op == CIDR_MATCH_OP_IF) {
	    if (rule_stack == 0)
		rule_stack = (DICT_CIDR_ENTRY **) mvect_alloc(&mvect,
					   sizeof(*rule_stack), nesting + 1,
						(MVECT_FN) 0, (MVECT_FN) 0);
	    else
		rule_stack =
		    (DICT_CIDR_ENTRY **) mvect_realloc(&mvect, nesting + 1);
	    rule_stack[nesting] = rule;
	    nesting++;
	} else if (rule->cidr_info.op == CIDR_MATCH_OP_ENDIF) {
	    DICT_CIDR_ENTRY *if_rule;

	    if (nesting-- <= 0)
		/* Already handled in dict_cidr_parse_rule(). */
		msg_panic("%s: ENDIF without IF", myname);
	    if_rule = rule_stack[nesting];
	    if (if_rule->cidr_info.op != CIDR_MATCH_OP_IF)
		msg_panic("%s: unexpected rule stack element type %d",
			  myname, if_rule->cidr_info.op);
	    if_rule->cidr_info.block_end = &(rule->cidr_info);
	}
	if (last_rule == 0)
	    dict_cidr->head = rule;
	else
	    last_rule->cidr_info.next = &(rule->cidr_info);
	last_rule = rule;
    }

    while (nesting-- > 0)
	msg_warn("cidr map %s, line %d: IF has no matching ENDIF",
		 mapname, rule_stack[nesting]->lineno);

    if (rule_stack)
	(void) mvect_free(&mvect);

    dict_file_purge_buffers(&dict_cidr->dict);
    DICT_CIDR_OPEN_RETURN(&dict_cidr->dict);
}
