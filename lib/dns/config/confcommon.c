/*
 * Copyright (C) 1999  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#include <config.h>

#include <sys/types.h>	/* XXXRTH */

#include <limits.h>
#include <syslog.h>	/* XXXRTH */
#include <ctype.h>
#include <string.h>

#include <isc/assertions.h>
#include <isc/buffer.h>

/* XXX this next include is needed by <dns/rdataclass.h>  */
#include <dns/result.h>

#include <dns/rdataclass.h>
#include <dns/rdatatype.h>

#include <dns/confcommon.h>



/***
 *** TYPES
 ***/
 
#define ordering_nametable_size \
	(sizeof (ordering_nametable) / sizeof (struct dns_c_pvt_ont))
static struct dns_c_pvt_ont {
	dns_c_ordering_t val;
	const char *strval;
} ordering_nametable[] = {
	{ dns_c_ordering_fixed,		"fixed" },
	{ dns_c_ordering_random,	"random" },
	{ dns_c_ordering_cyclic,	"cyclic" }
};


#define log_severity_nametable_size \
	(sizeof (log_severity_nametable) / sizeof (struct dns_c_pvt_lsnt))
static struct dns_c_pvt_lsnt {
	dns_c_logseverity_t val;
	const char *strval;
} log_severity_nametable[] = {
	{ dns_c_log_critical,		"critical" },
	{ dns_c_log_error,		"error" },
	{ dns_c_log_info,		"info" },
	{ dns_c_log_notice,		"notice" },
	{ dns_c_log_warn,		"warning" },
	{ dns_c_log_debug,		"debug" },
	{ dns_c_log_dynamic,		"dynamic" }
};

	
static struct dsn_c_pvt_sfnt {
	int val;
	const char *strval;
} syslog_facil_nametable[] = {
	{ LOG_KERN,			"kern" },
	{ LOG_USER,			"user" },
	{ LOG_MAIL,			"mail" },
	{ LOG_DAEMON,			"daemon" },
	{ LOG_AUTH,			"auth" },
	{ LOG_SYSLOG,			"syslog" },
	{ LOG_LPR,			"lpr" },
#ifdef LOG_NEWS
	{ LOG_NEWS,			"news" },
#endif
#ifdef LOG_UUCP
	{ LOG_UUCP,			"uucp" },
#endif
#ifdef LOG_CRON
	{ LOG_CRON,			"cron" },
#endif
#ifdef LOG_AUTHPRIV
	{ LOG_AUTHPRIV,			"authpriv" },
#endif
#ifdef LOG_FTP
	{ LOG_FTP,			"ftp" },
#endif
	{ LOG_LOCAL0,			"local0"}, 
	{ LOG_LOCAL1,			"local1"}, 
	{ LOG_LOCAL2,			"local2"}, 
	{ LOG_LOCAL3,			"local3"}, 
	{ LOG_LOCAL4,			"local4"}, 
	{ LOG_LOCAL5,			"local5"}, 
	{ LOG_LOCAL6,			"local6"}, 
	{ LOG_LOCAL7,			"local7"}, 
	{ 0,				NULL }
};


#define category_nametable_size \
	(sizeof (category_nametable) / sizeof(struct dns_c_pvt_cntable))
static struct dns_c_pvt_cntable {
	dns_c_category_t val;
	const char *strval;
} category_nametable[] = {
	{ dns_c_cat_default,		"default" },
	{ dns_c_cat_config,		"config" },
	{ dns_c_cat_parser,		"parser" },
	{ dns_c_cat_queries,		"queries" },
	{ dns_c_cat_lameservers,	"lame-servers" },
	{ dns_c_cat_statistics,		"statistics" },
	{ dns_c_cat_panic,		"panic" },
	{ dns_c_cat_update,		"update" },
	{ dns_c_cat_ncache,		"ncache" },
	{ dns_c_cat_xferin,		"xfer-in" },
	{ dns_c_cat_xferout,		"xfer-out" },
	{ dns_c_cat_db,			"db" },
	{ dns_c_cat_eventlib,		"eventlib" },
	{ dns_c_cat_packet,		"packet" },
	{ dns_c_cat_notify,		"notify" },
	{ dns_c_cat_cname,		"cname" },
	{ dns_c_cat_security,		"security" },
	{ dns_c_cat_os,			"os" },
	{ dns_c_cat_insist,		"insist" },
	{ dns_c_cat_maint,		"maintenance" },
	{ dns_c_cat_load,		"load" },
	{ dns_c_cat_respchecks,		"response-checks" },
	{ dns_c_cat_control,		"control" }
};



/***
 *** DATA
 ***/

isc_boolean_t debug_mem_print;
FILE *debug_mem_print_stream;



/***
 *** FUNCTIONS
 ***/

#if 0					/* XXXJAB delete this code */
static void default_cfgerror(isc_result_t result, const char *fmt,
			     va_list args);
#endif



void
dns_c_printinunits(isc_log_t *lctx, FILE *fp, isc_uint32_t val)
{
	isc_uint32_t one_gig = (1024 * 1024 * 1024);
	isc_uint32_t one_meg = (1024 * 1024);
	isc_uint32_t one_k = 1024;

	(void) lctx;
	
	if (val == DNS_C_SIZE_SPEC_DEFAULT)
		fprintf(fp, "default");
	else if ((val % one_gig) == 0)
		fprintf(fp, "%luG", (unsigned long) val / one_gig);
	else if ((val % one_meg) == 0)
		fprintf(fp, "%luM", (unsigned long) val / one_meg);
	else if ((val % one_k) == 0)
		fprintf(fp, "%luK", (unsigned long) val / one_k);
	else if (val == DNS_C_SIZE_SPEC_UNLIM)
		fprintf(fp, "unlimited");
	else
		fprintf(fp, "%lu", (unsigned long) val);
}


void
dns_c_dataclass_tostream(isc_log_t *lctx, FILE *fp, dns_rdataclass_t rclass)
{
	char buffer[64];
	isc_buffer_t sourceb;

	(void) lctx;
	
	isc_buffer_init(&sourceb, buffer, sizeof buffer,
			ISC_BUFFERTYPE_GENERIC);
	
	if (dns_rdataclass_totext(rclass, &sourceb) == DNS_R_SUCCESS) {
		INSIST(sourceb.used + 1 < sizeof buffer);
		buffer[sourceb.used] = '\0';
		fputs(buffer, fp);
	} else {
		fprintf(fp, "UNKNOWN-CLASS(%d)",(int) rclass);
	}
}


void
dns_c_datatype_tostream(isc_log_t *lctx, FILE *fp, dns_rdatatype_t rtype)
{
	char buffer[64];
	isc_buffer_t sourceb;

	(void) lctx;
	
	isc_buffer_init(&sourceb, buffer, sizeof buffer,
			ISC_BUFFERTYPE_GENERIC);

	if (dns_rdatatype_totext(rtype, &sourceb) == DNS_R_SUCCESS) {
		INSIST(sourceb.used + 1 < sizeof buffer);
		buffer[sourceb.used] = '\0';
		fputs(buffer, fp);
	} else {
		fprintf(fp, "UNKNOWN-RDATATYPE(%d)",(int) rtype);
	}
}


void
dns_c_printtabs(isc_log_t *lctx, FILE *fp, int count)
{

	(void) lctx;

	while (count > 0) {
		fputc('\t', fp);
		count--;
	}
}



isc_result_t
dns_c_string2ordering(isc_log_t *lctx, char *name, dns_c_ordering_t *ordering)
{
	unsigned int i;
	isc_result_t rval = ISC_R_FAILURE;

	(void) lctx;
	
	for (i = 0 ; i < ordering_nametable_size ; i++) {
		if (strcmp(ordering_nametable[i].strval, name) == 0) {
			*ordering = ordering_nametable[i].val;
			rval = ISC_R_SUCCESS;
			break;
		}
	}
	
	return (rval);
}


const char *
dns_c_ordering2string(isc_log_t *lctx, dns_c_ordering_t ordering,
		      isc_boolean_t printable)
{
	unsigned int i;
	const char *rval = NULL;

	(void) lctx;
	
	for (i = 0 ; i < ordering_nametable_size ; i++) {
		if (ordering_nametable[i].val == ordering) {
			rval = ordering_nametable[i].strval;
			break;
		}
	}

	return (rval == NULL && printable ? "UNKNOWN_ORDERING" : rval);
}


const char *
dns_c_logseverity2string(isc_log_t *lctx, dns_c_logseverity_t severity,
			 isc_boolean_t printable)
{
	unsigned int i;
	const char *rval = NULL;

	(void) lctx;
	
	for (i = 0 ; i < log_severity_nametable_size ; i++) {
		if (log_severity_nametable[i].val == severity) {
			rval = log_severity_nametable[i].strval;
			break;
		}
	}

	return (rval == NULL && printable ? "UNKNOWN_SEVERITY" : rval);
}


isc_result_t
dns_c_string2logseverity(isc_log_t *lctx, const char *string,
			 dns_c_logseverity_t *result)
{
	unsigned int i;
	isc_result_t rval = ISC_R_FAILURE;

	(void) lctx;

	REQUIRE(result != NULL);
	
	for (i = 0 ; i < log_severity_nametable_size ; i++) {
		if (strcmp(log_severity_nametable[i].strval, string) == 0) {
			*result = log_severity_nametable[i].val;
			rval = ISC_R_SUCCESS;
			break;
		}
	}

	return rval;
}


const char *
dns_c_category2string(isc_log_t *lctx, dns_c_category_t cat,
		      isc_boolean_t printable)
{
	unsigned int i;
	const char *rval = NULL;

	(void) lctx;
	
	for (i = 0 ; i < category_nametable_size ; i++) {
		if (category_nametable[i].val == cat) {
			rval = category_nametable[i].strval;
			break;
		}
	}

	return (rval == NULL && printable ? "UNKNOWN_CATEGORY" : rval);
}


isc_result_t
dns_c_string2category(isc_log_t *lctx, const char *string,
		      dns_c_category_t *category)
{
	unsigned int i;
	isc_result_t rval = ISC_R_FAILURE;

	(void) lctx;
	
	REQUIRE (category != NULL);
	
	for (i = 0 ; i < category_nametable_size ; i++) {
		if (strcmp(category_nametable[i].strval, string) == 0) {
			*category = category_nametable[i].val;
			rval = ISC_R_SUCCESS;
			break;
		}
	}

	return (rval);
}



const char *
dns_c_facility2string(isc_log_t *lctx, int facility, isc_boolean_t printable)
{
	int i;
	const char *rval = NULL;

	(void) lctx;
	
	for (i = 0 ; syslog_facil_nametable[i].strval != NULL ; i++) {
		if (syslog_facil_nametable[i].val == facility) {
			rval = syslog_facil_nametable[i].strval;
			break;
		}
	}
	
	return (rval == NULL && printable ? "UNKNOWN_FACILITY" : rval);
}


isc_result_t
dns_c_string2facility(isc_log_t *lctx, const char *string, int *result)
{
	int i;
	isc_result_t rval = ISC_R_FAILURE;

	(void) lctx;
	
	for (i = 0 ; syslog_facil_nametable[i].strval != NULL ; i++) {
		if (strcmp(syslog_facil_nametable[i].strval, string) == 0) {
			*result = syslog_facil_nametable[i].val;
			rval = ISC_R_SUCCESS;
			break;
		}
	}

	return rval;
}


const char *
dns_c_transformat2string(isc_log_t *lctx, dns_transfer_format_t tformat,
			 isc_boolean_t printable)
{
	const char *rval = NULL;

	(void) lctx;
	
	switch (tformat) {
	case dns_one_answer:
		rval = "one-answer";
		break;

	case dns_many_answers:
		rval = "many-answers";
		break;
	}

	return (rval == NULL && printable ? "UNKNOWN_TRANSFER_FORMAT" : rval);
}




const char *
dns_c_transport2string(isc_log_t *lctx, dns_c_trans_t transport,
		       isc_boolean_t printable)
{
	const char *rval = NULL;

	(void) lctx;
	
	switch (transport) {
	case dns_trans_primary:
		rval = "master";
		break;
		
	case dns_trans_secondary:
		rval = "slave";
		break;

	case dns_trans_response:
		rval = "response";
		break;
	}

	return (rval == NULL && printable ? "UNKNOWN_TRANSPORT" : rval);
}


const char *
dns_c_nameseverity2string(isc_log_t *lctx, dns_c_severity_t severity,
			  isc_boolean_t printable)
{
	const char *rval = NULL;

	(void) lctx;
	
	switch (severity) {
	case dns_c_severity_ignore:
		rval = "ignore";
		break;

	case dns_c_severity_warn:
		rval = "warn";
		break;

	case dns_c_severity_fail:
		rval = "fail";
		break;
	}

	return (rval == NULL && printable ? "UNKNOWN_NAME_SEVERITY" : rval);
}


const char *
dns_c_forward2string(isc_log_t *lctx, dns_c_forw_t forw,
		     isc_boolean_t printable)
{
	const char *rval = NULL;

	(void) lctx;
	
	switch (forw) {
	case dns_c_forw_only:
		rval = "only";
		break;

	case dns_c_forw_first:
		rval = "first";
		break;

	case dns_c_forw_noanswer:
		rval = "if-no-answer";
		break;

	case dns_c_forw_nodomain:
		rval = "if-no-domain";
		break;
	}

	return (rval == NULL && printable ? "UNKNOWN_FORWARDING" : rval);
}



int
dns_c_isanyaddr(isc_log_t *lctx, isc_sockaddr_t *inaddr)
{
	int result = 0;

	(void) lctx;
	
	if (inaddr->type.sa.sa_family == AF_INET) {
		if (inaddr->type.sin.sin_addr.s_addr == htonl(INADDR_ANY)) {
			result = 1;
		}
	} else {
		if (memcmp(&inaddr->type.sin6.sin6_addr,
			   &in6addr_any, sizeof in6addr_any) == 0) {
			result = 1;
		}
	}

	return (result);
}
	

	
void
dns_c_print_ipaddr(isc_log_t *lctx, FILE *fp, isc_sockaddr_t *inaddr)
{
	const char *p;
	char tmpaddrstr[64];
	int family = inaddr->type.sa.sa_family;
	void *addr;

	if (dns_c_isanyaddr(lctx, inaddr)) {
		if (family == AF_INET) {
			fprintf(fp, "*");
		} else {
			fprintf(fp, "0::0");
		}
	} else {
		addr = (family == AF_INET ?
			(void *)&inaddr->type.sin.sin_addr :
			(void *)&inaddr->type.sin6.sin6_addr);
		
		p = inet_ntop(family, addr, tmpaddrstr, sizeof tmpaddrstr);
		if (p == NULL) {
			fprintf(fp, "BAD-IP-ADDRESS");
		} else {
			fprintf(fp, "%s", tmpaddrstr);
		}
	}
}


isc_boolean_t
dns_c_need_quote(isc_log_t *lctx, const char *string)
{
	isc_boolean_t rval = ISC_FALSE;

	(void) lctx;
	
	while (string != NULL && *string != '\0') {
		if (!(isalnum(*string & 0xff) || *string == '_')) {
			rval = ISC_TRUE;
			break;
		}
		string++;
	}

	return rval;
}


		
	
#if 0					/* XXXJAB remove this code */

/* ************************************************** */
/* ******** CONFIG MODULE ERROR PRINTING ************ */
/* ************************************************** */


static dns_cfg_err_handler_t cfgerrorptr = default_cfgerror;

dns_cfg_err_handler_t
dns_c_seterrorhandler(dns_cfg_err_handler_t newhandler)
{
	dns_cfg_err_handler_t oldhandler = cfgerrorptr;

	cfgerrorptr = newhandler;

	return oldhandler;
}


void
dns_c_error(isc_result_t result, const char *fmt, ...)
{
    va_list args ;

    va_start (args, fmt) ;
    cfgerrorptr(result, fmt, args);
    va_end (ap) ;
}



static void
default_cfgerror(isc_result_t result, const char *fmt, va_list args)
{
	vfprintf(stderr, fmt, args);
	if (result != ISC_R_SUCCESS) {
		fprintf(stderr, " (%s)", isc_result_totext(result));
	}
	fputc('\n', stderr);
}

#endif

/* ************************************************** */
/* ********** MEMORY DEBUGGING ROUTINES. ************ */
/* ************************************************** */

#if defined (DEBUG_MEM_STUFF)

#if defined(isc_mem_strdup)
#undef isc_mem_get
#undef isc_mem_put
#undef isc_mem_strdup
#undef isc_mem_free
#undef isc_mem_destroy
#endif

/*
 * This is a horribly inefficient memory tracking system, but it's not meant 
 * to be turned on very often (just in development).
 */

struct dns_c_pvt_mement 
{
	const char *file;
	int line;
	isc_mem_t *mem;
	size_t amt;
	void *ptr;
};

#define MEMTABLESIZE 10000
static struct dns_c_pvt_mement table[MEMTABLESIZE];
static isc_boolean_t trackingOff;

static void
addent(const char *file, int line, isc_mem_t *mem, size_t amount, void *ptr)
{
	int i;
	
	for (i = 0 ; i < MEMTABLESIZE ; i++) {
		if (table[i].file == NULL) {
			table[i].file = file;
			table[i].line = line;
			table[i].mem = mem;
			table[i].amt = amount;
			table[i].ptr = ptr;
			return;
		}
	}

	trackingOff = ISC_TRUE;
	fprintf(stderr, "Insufficient space to track memory allocations");
}


static void
rment(const char *file, int line, isc_mem_t *mem, void *p, size_t sz)
{
	int i;

	if (trackingOff) {
		return;
	}
	
	for (i = 0 ; i < MEMTABLESIZE ; i++) {
		if (table[i].ptr == p) {
			INSIST(sz == table[i].amt);
			
			table[i].file = NULL;
			table[i].line = 0;
			table[i].mem = NULL;
			table[i].amt = 0;
			table[i].ptr = NULL;
			return;
		}
	}

	fprintf(stderr, "Can't find memory in alloc table: %s %d %p %p %d\n",
		file, line, mem, p, sz);
	abort();
}


static void
remainders(isc_mem_t *mem)
{
	int i ;
	FILE *stream;
	
	if (trackingOff) {
		fprintf(stream,
			"No remainders tracked due to table overflow\n");
		return;
	}
	
	if (debug_mem_print) {
		stream = (debug_mem_print_stream == NULL ?
			  stderr : debug_mem_print_stream);

		for (i = 0 ; i < MEMTABLESIZE ; i++) {
			if (table[i].mem == mem) {
				fprintf(stream, "remainder: %s:%d %p ",
					table[i].file,
					table[i].line,
					table[i].ptr);
				if ((int)table[i].amt == -1) {
					fprintf(stream, "\"%s\"",
						(char *)table[i].ptr);
				} else {
					fprintf(stream, "%d", table[i].amt);
				}
				
				fputc('\n', stream);
			}
		}
		fflush(stream);
	}
}


void *
dns_c_memget_wrapper(const char *file, int line, isc_mem_t *mem, size_t amount)
{
	void *m = __isc_mem_get(mem, amount);
	
	addent(file, line, mem, amount, m);
	
	return m;
}

void
dns_c_memput_wrapper(const char *file, int line, isc_mem_t *mem, void *p,
		   size_t sz)
{
	rment(file, line, mem, p, sz);
	
	__isc_mem_put(mem, p, sz);
}

char *
dns_c_memstrdup_wrapper(const char *file, int line, isc_mem_t *mem,
			const char *s)
{
	char *d = isc_mem_strdup(mem, s);

	addent(file, line, mem, -1, d);

	return (d);
}

void
dns_c_memfree_wrapper(const char *file, int line, isc_mem_t *mem, char *s)
{
	rment(file, line, mem, s, -1);
	
	isc_mem_free(mem, s);
}

void
dns_c_memdestroy_wrapper(const char *file, int line, isc_mem_t **mem)
{
	(void) file;
	(void) line;
	
	remainders(*mem);
	
	isc_mem_destroy(mem);
}

#endif
