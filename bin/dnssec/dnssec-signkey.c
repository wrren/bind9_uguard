/*
 * Copyright (C) 1999, 2000  Internet Software Consortium.
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

#include <stdlib.h>

#include <isc/string.h>
#include <isc/commandline.h>
#include <isc/mem.h>
#include <isc/util.h>

#include <dns/db.h>
#include <dns/dnssec.h>
#include <dns/log.h>
#include <dns/rdata.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatastruct.h>
#include <dns/result.h>
#include <dns/secalg.h>

#define PROGRAM "dnssec-signkey"

#define BUFSIZE 2048

typedef struct keynode keynode_t;
struct keynode {
	dst_key_t *key;
	isc_boolean_t verified;
	ISC_LINK(keynode_t) link;
};
typedef ISC_LIST(keynode_t) keylist_t;

static isc_stdtime_t now;
static int verbose;

static isc_mem_t *mctx = NULL;
static keylist_t keylist;

static void
fatal(char *format, ...) {
	va_list args;

	fprintf(stderr, "%s: ", PROGRAM);
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(1);
}

static inline void
check_result(isc_result_t result, char *message) {
	if (result != ISC_R_SUCCESS) {
		fprintf(stderr, "%s: %s: %s\n", PROGRAM, message,
			isc_result_totext(result));
		exit(1);
	}
}

/* Not thread-safe! */
static char *
nametostr(dns_name_t *name) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;
	static char data[1025];

	isc_buffer_init(&b, data, sizeof(data));
	result = dns_name_totext(name, ISC_FALSE, &b);
	check_result(result, "dns_name_totext()");
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	return (char *) r.base;
}

/* Not thread-safe! */
static char *
algtostr(const dns_secalg_t alg) {
	isc_buffer_t b;
		        isc_region_t r;
	isc_result_t result;
	static char data[10];

	isc_buffer_init(&b, data, sizeof(data));
	result = dns_secalg_totext(alg, &b);
	check_result(result, "dns_secalg_totext()");
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	return (char *) r.base;
}


static void
usage(void) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s [options] keyset keys\n", PROGRAM);

	fprintf(stderr, "\n");

	fprintf(stderr, "Options: (default value in parenthesis) \n");
	fprintf(stderr, "\t-v level:\n");
	fprintf(stderr, "\t\tverbose level (0)\n");

	fprintf(stderr, "\n");

	fprintf(stderr, "keyset:\n");
	fprintf(stderr, "\tfile name of key set to be signed\n");
	fprintf(stderr, "keys:\n");
	fprintf(stderr, "\tkeyfile (Kname+alg+id)\n");
	exit(0);
}

static void
loadkeys(dns_name_t *name, dns_rdataset_t *rdataset) {
	dst_key_t *key;
	dns_rdata_t rdata;
	keynode_t *keynode;
	isc_result_t result;

	ISC_LIST_INIT(keylist);
	result = dns_rdataset_first(rdataset);
	check_result(result, "dns_rdataset_first");
	for (; result == ISC_R_SUCCESS; result = dns_rdataset_next(rdataset)) {
		dns_rdataset_current(rdataset, &rdata);
		key = NULL;
		result = dns_dnssec_keyfromrdata(name, &rdata, mctx, &key);
		if (result != ISC_R_SUCCESS)
			continue;
		if (!dst_key_iszonekey(key))
			continue;
		keynode = isc_mem_get(mctx, sizeof (keynode_t));
		if (keynode == NULL)
			fatal("out of memory");
		keynode->key = key;
		keynode->verified = ISC_FALSE;
		ISC_LINK_INIT(keynode, link);
		ISC_LIST_APPEND(keylist, keynode, link);
	}
	if (result != ISC_R_NOMORE)
		fatal("failure traversing key list");
}

static dst_key_t *
findkey(dns_rdata_sig_t *sig) {
	keynode_t *keynode;
	for (keynode = ISC_LIST_HEAD(keylist);
	     keynode != NULL;
	     keynode = ISC_LIST_NEXT(keynode, link))
	{
		if (dst_key_id(keynode->key) == sig->keyid &&
		    dst_key_alg(keynode->key) == sig->algorithm) {
			keynode->verified = ISC_TRUE;
			return (keynode->key);
		}
	}
	fatal("signature generated by non-zone or missing key");
	return (NULL);
}

int
main(int argc, char *argv[]) {
	int i, ch;
	char tdomain[1025];
	dns_fixedname_t fdomain;
	dns_name_t *domain;
	char *output = NULL;
	char *endp;
	unsigned char *data;
	dns_db_t *db;
	dns_dbnode_t *node;
	dns_dbversion_t *version;
	dst_key_t *key = NULL;
	dns_rdata_t *rdata, sigrdata;
	dns_rdatalist_t sigrdatalist;
	dns_rdataset_t rdataset, sigrdataset, newsigrdataset;
	dns_rdata_sig_t sig;
	isc_result_t result;
	isc_buffer_t b;
	isc_region_t r;
	isc_log_t *log = NULL;
	isc_logconfig_t *logconfig;
	keynode_t *keynode;

	dns_result_register();

	result = isc_mem_create(0, 0, &mctx);
	check_result(result, "isc_mem_create()");

	while ((ch = isc_commandline_parse(argc, argv, "v:")) != -1)
	{
		switch (ch) {
		case 'v':
			endp = NULL;
			verbose = strtol(isc_commandline_argument, &endp, 0);
			if (*endp != '\0')
				fatal("verbose level must be numeric");
			break;

		default:
			usage();

		}
	}

	argc -= isc_commandline_index;
	argv += isc_commandline_index;

	if (argc < 2)
		usage();

	isc_stdtime_get(&now);

	if (verbose > 0) {
		RUNTIME_CHECK(isc_log_create(mctx, &log, &logconfig)
			      == ISC_R_SUCCESS);
		isc_log_setcontext(log);
		dns_log_init(log);
		dns_log_setcontext(log);
		RUNTIME_CHECK(isc_log_usechannel(logconfig, "default_stderr",
						 NULL, NULL) == ISC_R_SUCCESS);
	}

	if (strlen(argv[0]) < 8 ||
	    strcmp(argv[0] + strlen(argv[0]) - 7, ".keyset") != 0)
		fatal("keyset file must end in .keyset");

	dns_fixedname_init(&fdomain);
	domain = dns_fixedname_name(&fdomain);
	isc_buffer_init(&b, argv[0], strlen(argv[0]) - 7);
	isc_buffer_add(&b, strlen(argv[0]) - 7);
	result = dns_name_fromtext(domain, &b, dns_rootname, ISC_FALSE, NULL);
	if (result != ISC_R_SUCCESS)
		fatal("'%s' does not contain a valid domain name", argv[0]);
	isc_buffer_init(&b, tdomain, sizeof(tdomain) - 1);
	result = dns_name_totext(domain, ISC_FALSE, &b);
	check_result(result, "dns_name_totext()");
	isc_buffer_usedregion(&b, &r);
	tdomain[r.length] = 0;

	output = isc_mem_allocate(mctx,
				  strlen(tdomain) + strlen("signedkey") + 1);
	if (output == NULL)
		fatal("out of memory");
	strcpy(output, tdomain);
	strcat(output, "signedkey");

	db = NULL;
	result = dns_db_create(mctx, "rbt", domain, ISC_FALSE,
			       dns_rdataclass_in, 0, NULL, &db);
	check_result(result, "dns_db_create()");

	result = dns_db_load(db, argv[0]);
	if (result != ISC_R_SUCCESS)
		fatal("failed to load database from '%s': %s", argv[0],
		      isc_result_totext(result));

	version = NULL;
	dns_db_newversion(db, &version);

	node = NULL;
	result = dns_db_findnode(db, domain, ISC_FALSE, &node);
	if (result != ISC_R_SUCCESS)
		fatal("failed to find database node '%s': %s",
		      nametostr(domain), isc_result_totext(result));

	dns_rdataset_init(&rdataset);
	dns_rdataset_init(&sigrdataset);
	result = dns_db_findrdataset(db, node, version, dns_rdatatype_key, 0,
				     0, &rdataset, &sigrdataset);
	if (result != ISC_R_SUCCESS)
		fatal("failed to find rdataset '%s KEY': %s",
		      nametostr(domain), isc_result_totext(result));

	loadkeys(domain, &rdataset);

	if (!dns_rdataset_isassociated(&sigrdataset))
		fatal("no SIG KEY set present");

	result = dns_rdataset_first(&sigrdataset);
	check_result(result, "dns_rdataset_first()");
	do {
		dns_rdataset_current(&sigrdataset, &sigrdata);
		result = dns_rdata_tostruct(&sigrdata, &sig, mctx);
		check_result(result, "dns_rdata_tostruct()");
		key = findkey(&sig);
		result = dns_dnssec_verify(domain, &rdataset, key,
					   ISC_TRUE, mctx, &sigrdata);
		if (result != ISC_R_SUCCESS)
			fatal("signature by key '%s/%s/%d' did not verify: %s",
			      dst_key_name(key), algtostr(dst_key_alg(key)),
			      dst_key_id(key), isc_result_totext(result));
		dns_rdata_freestruct(&sig);
		result = dns_rdataset_next(&sigrdataset);
	} while (result == ISC_R_SUCCESS);

	for (keynode = ISC_LIST_HEAD(keylist);
	     keynode != NULL;
	     keynode = ISC_LIST_NEXT(keynode, link))
		if (!keynode->verified)
			fatal("Not all zone keys self signed the key set");

	result = dns_rdataset_first(&sigrdataset);
	check_result(result, "dns_rdataset_first()");
	dns_rdataset_current(&sigrdataset, &sigrdata);
	result = dns_rdata_tostruct(&sigrdata, &sig, mctx);
	check_result(result, "dns_rdata_tostruct()");

	dns_rdataset_disassociate(&sigrdataset);

	argc -= 1;
	argv += 1;

	dns_rdatalist_init(&sigrdatalist);
	sigrdatalist.rdclass = rdataset.rdclass;
	sigrdatalist.type = dns_rdatatype_sig;
	sigrdatalist.covers = dns_rdatatype_key;
	sigrdatalist.ttl = rdataset.ttl;

	for (i = 0; i < argc; i++) {
		isc_uint16_t id;
		int alg;
		char *namestr = NULL;

		isc_buffer_init(&b, argv[i], strlen(argv[i]));
		isc_buffer_add(&b, strlen(argv[i]));
		result = dst_key_parsefilename(&b, mctx, &namestr, &id, &alg,
					       NULL);
		if (result != ISC_R_SUCCESS)
			usage();

		key = NULL;
		result = dst_key_fromfile(namestr, id, alg, DST_TYPE_PRIVATE,
					  mctx, &key);
		if (result != ISC_R_SUCCESS)
			fatal("failed to read key %s/%s/%d from disk: %s",
			      dst_key_name(key), algtostr(dst_key_alg(key)),
			      dst_key_id(key), isc_result_totext(result));
		isc_mem_put(mctx, namestr, strlen(namestr) + 1);

		rdata = isc_mem_get(mctx, sizeof(dns_rdata_t));
		if (rdata == NULL)
			fatal("out of memory");
		data = isc_mem_get(mctx, BUFSIZE);
		if (data == NULL)
			fatal("out of memory");
		isc_buffer_init(&b, data, BUFSIZE);
		result = dns_dnssec_sign(domain, &rdataset, key,
					 &sig.timesigned, &sig.timeexpire,
					 mctx, &b, rdata);
		if (result != ISC_R_SUCCESS)
			fatal("key '%s/%s/%d' failed to sign data: %s",
			      dst_key_name(key), algtostr(dst_key_alg(key)),
			      dst_key_id(key), isc_result_totext(result));
		ISC_LIST_APPEND(sigrdatalist.rdata, rdata, link);
		dst_key_free(&key);
	}

	dns_rdataset_init(&newsigrdataset);
	result = dns_rdatalist_tordataset(&sigrdatalist, &newsigrdataset);
	check_result (result, "dns_rdatalist_tordataset()");

	dns_db_addrdataset(db, node, version, 0, &newsigrdataset, 0, NULL);
	check_result (result, "dns_db_addrdataset()");

	dns_db_detachnode(db, &node);
	dns_db_closeversion(db, &version, ISC_TRUE);
	result = dns_db_dump(db, version, output);
	if (result != ISC_R_SUCCESS)
		fatal("failed to write database to '%s': %s",
		      output, isc_result_totext(result));

	dns_rdataset_disassociate(&rdataset);
	dns_rdataset_disassociate(&newsigrdataset);

	dns_rdata_freestruct(&sig);

        while (!ISC_LIST_EMPTY(sigrdatalist.rdata)) {
                rdata = ISC_LIST_HEAD(sigrdatalist.rdata);
                ISC_LIST_UNLINK(sigrdatalist.rdata, rdata, link);
                isc_mem_put(mctx, rdata->data, BUFSIZE);
                isc_mem_put(mctx, rdata, sizeof *rdata);
        }

	dns_db_detach(&db);

	while (!ISC_LIST_EMPTY(keylist)) {
		keynode = ISC_LIST_HEAD(keylist);
		ISC_LIST_UNLINK(keylist, keynode, link);
		dst_key_free(&keynode->key);
		isc_mem_put(mctx, keynode, sizeof(keynode_t));
	}

	if (log != NULL)
		isc_log_destroy(&log);

        isc_mem_free(mctx, output);
	isc_mem_destroy(&mctx);
	return (0);
}
