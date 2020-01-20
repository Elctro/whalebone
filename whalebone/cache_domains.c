#include <string.h>

#include "log.h"
#include "cache_domains.h"

unsigned char cache_domain_get_flags(unsigned long long flagsl, int n)
{
	unsigned char *temp = (unsigned char *)&flagsl;
	return temp[n];

	//return (flags >> (8 * n)) & 0xff; 
}

int cache_domain_compare(const void * a, const void * b)
{
	const unsigned long long ai = *(const unsigned long long*)a;
	const unsigned long long bi = *(const unsigned long long*)b;

	if (ai < bi)
	{
		return -1;
	}
	else if (ai > bi)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

cache_domain* cache_domain_init(int count)
{
	cache_domain *item = (cache_domain *)calloc(1, sizeof(cache_domain));
	if (item == NULL)
	{
		return NULL;
	}

	item->capacity = count;
	item->index = 0;
	item->searchers = 0;

	item->base = (unsigned long long *)malloc(item->capacity * sizeof(unsigned long long));
	item->accuracy = (short *)calloc(1, item->capacity * sizeof(short));
	item->flags = (unsigned long long *)malloc(item->capacity * sizeof(unsigned long long));

	if (item->base == NULL || item->accuracy == NULL || item->flags == NULL)
	{
		return NULL;
	}

	return item;
}

cache_domain* cache_domain_init_ex(unsigned long long *domains, short *accuracy, unsigned long long *flagsl, int count)
{
	cache_domain *item = (cache_domain *)calloc(1, sizeof(cache_domain));
	if (item == NULL)
	{
		return NULL;
	}

	item->capacity = count;
	item->index = count;
	item->searchers = 0;
	item->base = (unsigned long long *)domains;
	item->accuracy = (short *)accuracy;
	item->flags = (unsigned long long *)flagsl;
	if (item->base == NULL || item->accuracy == NULL || item->flags == NULL)
	{
		return NULL;
	}

	return item;
}

cache_domain* cache_domain_init_ex2(unsigned long long *domains, int count)
{
	cache_domain *item = (cache_domain *)calloc(1, sizeof(cache_domain));
	if (item == NULL)
	{
		return NULL;
	}

	item->capacity = count;
	item->index = count;
	item->searchers = 0;
	item->base = (unsigned long long *)domains;
	item->accuracy = NULL;
	item->flags = NULL;
	if (item->base == NULL)
	{
		return NULL;
	}

	return item;
}



void cache_domain_destroy(cache_domain *cache)
{
	if (cache == NULL)
	{
		return;
	}

	//printf(" free domain start\n");
	while (cache->searchers > 0)
	{
		usleep(50000);
	}

	if (cache->base)
	{
		//printf(" free domain base\n");
		free(cache->base);
		cache->base = NULL;
	}
	if (cache->accuracy)
	{
		//printf(" free domain accuracy\n");
		free(cache->accuracy);
		cache->accuracy = NULL;
	}
	if (cache->flags)
	{
		//printf(" free domain flags\n");
		free(cache->flags);
		cache->flags = NULL;
	}

	//  printf(" free cache domains\n");
	//  if (cache != NULL)
	//  {
	//	  free(cache);  
	//    cache = NULL;
	//  }
	//printf(" cache domains freed\n"); 
}

int cache_domain_add(cache_domain* cache, unsigned long long value, short accuracy, unsigned long long flagsl)
{
	if (cache == NULL)
		return -1;

	if (cache->index >= cache->capacity)
		return -1;

	cache->base[cache->index] = value;
	cache->accuracy[cache->index] = accuracy;
	cache->flags[cache->index] = flagsl;
	cache->index++;

	return 0;
}

int cache_domain_update(cache_domain* cache, unsigned long long value, short accuracy, unsigned long long flagsl)
{
	if (cache->index > cache->capacity)
		return -1;

	int position = cache->index;

	while (--position >= 0)
	{
		if (cache->base[position] == value)
		{
			cache->accuracy[position] = accuracy;
			cache->flags[position] = flagsl;
			break;
		}
	}

	return 0;
}

/// does not sort the other fields of the list
/// TODO: fix qsort to work with a whole struct
void cache_domain_sort(cache_domain* cache)
{
	qsort(cache->base, (size_t)cache->index, sizeof(unsigned long long), cache_domain_compare);
}

int cache_domain_contains(MDB_env *env, unsigned long long value, lmdbdomain *item)
{
	MDB_dbi dbi;
	MDB_txn *txn = NULL;
	MDB_cursor *cursor = NULL;
	MDB_val key_r, data_r;

	int rc = 0;
	if ((rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0)
	{
		return 0;
	}
	if ((rc = mdb_dbi_open(txn, "domain", MDB_DUPSORT, &dbi)) != 0)
	{
		return 0;
	}
	if ((rc = mdb_cursor_open(txn, dbi, &cursor)) != 0)
	{
		return 0;	
	}

	debugLog("\"method\":\"cache_domain_contains\",\"message\":\"get %ull\"", value);
	key_r.mv_size = sizeof(unsigned long long);
	key_r.mv_data = &value;
	data_r.mv_size = 0;
	data_r.mv_data = NULL;
	while ((rc = mdb_cursor_get(cursor, &key_r, &data_r, MDB_SET_KEY)) == 0)
	{
		memcpy(item, data_r.mv_data, data_r.mv_size);
		// debugLog("\"method\":\"cache_domain_contains\",\"size\":\"%x %x %x %X %x\"", ((char *)data_r.mv_data)[0]
		// , ((char *)data_r.mv_data)[1]
		// , ((char *)data_r.mv_data)[2]
		// , ((char *)data_r.mv_data)[3]
		// , ((char *)data_r.mv_data)[4]);		
		// debugLog("\"method\":\"cache_domain_contains\",\"accu\":\"%d\"", item->accuracy);
		// debugLog("\"method\":\"cache_domain_contains\",\"threatTypes\":\"%x\"", item->threatTypes);
		// debugLog("\"method\":\"cache_domain_contains\",\"TT_C_AND_C\":\"%d\"", item->threatTypes & TT_C_AND_C == item->threatTypes);
		// debugLog("\"method\":\"cache_domain_contains\",\"TT_MALWARE\":\"%d\"", item->threatTypes & TT_MALWARE == item->threatTypes);

		mdb_cursor_close(cursor);
		mdb_txn_abort(txn);
		mdb_dbi_close(env, dbi);

		return 1;
	}
	//CHECK(rc == MDB_NOTFOUND, "mdb_cursor_get");
	
	mdb_cursor_close(cursor);
	mdb_txn_abort(txn);
	mdb_dbi_close(env, dbi);

	return 0;

	/*
	if (cache == NULL)
	{
		return 0;
	}

	cache->searchers++;
	int lowerbound = 0;
	int upperbound = cache->index;
	int position;

	position = (lowerbound + upperbound) / 2;

	while ((cache->base[position] != value) && (lowerbound <= upperbound))
	{
		if (cache->base[position] > value)
		{
			upperbound = position - 1;
		}
		else
		{
			lowerbound = position + 1;
		}
		position = (lowerbound + upperbound) / 2;
	}

	if (lowerbound <= upperbound)
	{
		if (iscustom == 0)
		{
			citem->crc = cache->base[position];
			citem->accuracy = cache->accuracy[position];
			citem->flags = cache->flags[position];
		}
	}

	cache->searchers--;
	return (lowerbound <= upperbound);
	*/
}