#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>      
#include <unistd.h>

#include "cache_customlist.h"
#include "crc64.h"
#include "log.h"

int cache_customlist_contains(MDB_env *env, char *domain, const char *identity, lmdbcustomlist *item)
{
	char *ptr = domain;
	ptr += strlen(domain);
	int result = 0;
	int found = 0;
	while (ptr-- != (char *)domain)
	{
		if (ptr[0] == '.')
		{
			if (++found > 1)
			{
				if ((result = cache_custom_exploded_contains(env, ptr + 1, identity, item)) == 1)
				{
					return result;
				}
			}
		}
		else
		{
			if (ptr == (char *)domain)
			{
				if ((result = cache_custom_exploded_contains(env, ptr, identity, item)) == 1)
				{
					return result;
				}
			}
		}
	}

	return result;
}

int cache_custom_exploded_contains(MDB_env *env, char *domain, const char *identity, lmdbcustomlist *item)
{
	MDB_dbi dbi;
	MDB_txn *txn = NULL;
	MDB_cursor *cursor = NULL;
	MDB_val key_r, data_r;

	char merger[4096] = {};
	strcat(merger, domain);
	strcat(merger, identity);
	if (strlen(merger) == 0)
	{
		return 0;
	}
	unsigned long long value = crc64(0, merger, strlen(merger));

	int rc = 0;
	if ((rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0)
	{
		debugLog("\"method\":\"cache_customlist_contains\",\"mdb_txn_begin\":\"%s\"", mdb_strerror(rc));
		return 0;
	}
	if ((rc = mdb_dbi_open(txn, "custom_list", MDB_DUPSORT, &dbi)) != 0)
	{
		debugLog("\"method\":\"cache_customlist_contains\",\"mdb_dbi_open\":\"%s\"", mdb_strerror(rc));
		return 0;
	}
	if ((rc = mdb_cursor_open(txn, dbi, &cursor)) != 0)
	{
		debugLog("\"method\":\"cache_customlist_contains\",\"mdb_cursor_open\":\"%s\"", mdb_strerror(rc));
		return 0;	
	}

	debugLog("\"method\":\"cache_customlist_contains\",\"message\":\"get %s\"", merger);
	key_r.mv_size = sizeof(unsigned long long);
	key_r.mv_data = &value;
	data_r.mv_size = 0;
	data_r.mv_data = NULL;
	while ((rc = mdb_cursor_get(cursor, &key_r, &data_r, MDB_SET_KEY)) == 0)
	{
		memcpy(item, data_r.mv_data, data_r.mv_size);
		//debugLog("\"method\":\"cache_customlist_contains\",\"customlisttypes\":\"%d\"", item->customlisttypes);

		mdb_cursor_close(cursor);
		mdb_txn_abort(txn);
		mdb_dbi_close(env, dbi);

		return 1;
	}
	
	
	mdb_cursor_close(cursor);
	mdb_txn_abort(txn);
	mdb_dbi_close(env, dbi);

	return 0;
}