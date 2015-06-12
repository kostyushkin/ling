// Copyright (c) 2013-2014 Cloudozer LLP. All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// 
// * Redistributions in any form must be accompanied by information on how to
// obtain complete source code for the LING software and any accompanying
// software that uses the LING software. The source code must either be included
// in the distribution or be available for no more than the cost of distribution
// plus a nominal fee, and must be freely redistributable under reasonable
// conditions.  For an executable file, complete source code means the source
// code for all modules it contains. It does not include source code for modules
// or files that typically accompany the major components of the operating
// system on which the executable file runs.
// 
// THIS SOFTWARE IS PROVIDED BY CLOUDOZER LLP ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE
// DISCLAIMED. IN NO EVENT SHALL CLOUDOZER LLP BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdint.h>

#include "limits.h"
#include "term.h"
#include "heap.h"
#include "mm.h"
#include "atoms.h"
#include "getput.h"

//
// embed_buck_t and embed_bin_t both have a '_raw' fields that are resolved to
// terms at runtime. It is possible to resolve them during compile time but this
// greatly increases the build time.
//

typedef struct embed_buck_t embed_buck_t;
struct embed_buck_t {
	uint8_t *bucket_raw;	//see above
	term_t bucket;
	int start_index;
	int end_index;
};

typedef struct embed_bin_t embed_bin_t;
struct embed_bin_t {
	uint8_t *name_raw;		//see above
	term_t name;
	uint8_t *starts;
	uint8_t *ends;
};

embed_buck_t *embed_bucks;
int nr_embed_bucks;
embed_bin_t *embed_bins;
int nr_embed_bins;

// defined in embed.fs.o generated by railing
extern const unsigned char _binary_embed_fs_start[];

static embed_buck_t *find_bucket(term_t bucket);

int embed_init(void)
{
	// atoms and mm must be initialised by now
	uint8_t * p = (uint8_t *)_binary_embed_fs_start;

	nr_embed_bucks = GET_UINT_32(p); p += 4;
	int buck_pages = (sizeof(embed_buck_t) * nr_embed_bucks - 1) / PAGE_SIZE + 1;
	embed_bucks = (embed_buck_t *)mm_alloc_pages(buck_pages);

	nr_embed_bins = GET_UINT_32(p); p += 4;
	int bin_pages = (sizeof(embed_bin_t) * nr_embed_bins - 1) / PAGE_SIZE + 1;
	embed_bins = (embed_bin_t *)mm_alloc_pages(bin_pages);

	embed_buck_t *buck = embed_bucks;
	embed_bin_t * bin = embed_bins;
	int index = 0;

	for (int i = 0; i < nr_embed_bucks; ++i, ++buck)
	{
		buck->bucket = tag_atom(atoms_set(p)); p += *p + 1;
		buck->start_index = index;
		index += GET_UINT_32(p); p += 4;
		buck->end_index = index;

		for (int j = buck->start_index; j < buck->end_index; ++j, ++bin)
		{
			bin->name = tag_atom(atoms_set(p)); p += *p + 1;
			int data_size = GET_UINT_32(p); p += 4;
			bin->starts = p; p += data_size;
			bin->ends = p;
		}
	}

	return 0;
}

term_t embed_all_buckets(heap_t *hp)
{
	term_t buckets = nil;
	embed_buck_t *ptr = embed_bucks;
	embed_buck_t *end = ptr +nr_embed_bucks;
	while (ptr < end)
	{
		buckets = heap_cons(hp, ptr->bucket, buckets);
		ptr++;
	}
	return buckets;
}

term_t embed_list_bucket(term_t bucket, heap_t *hp)
{
	embed_buck_t *eb = find_bucket(bucket);
	if (eb == 0)
		return noval;

	term_t names = nil;
	for (int i = eb->start_index; i < eb->end_index; i++)
		names = heap_cons(hp, embed_bins[i].name, names);
	return names;
}

uint8_t *embed_lookup_simple(term_t name, int *size)
{
	*size = 0;

	//NB: traverse the list in reverse order to provide for proper shadowing of
	//    the files with the same name.
	
	embed_bin_t *ptr = embed_bins +nr_embed_bins -1;
	while (ptr >= embed_bins)
	{
		if (ptr->name == name)
		{
			*size = ptr->ends - ptr->starts;
			return ptr->starts;
		}
		ptr--;
	}
	return 0;
}

uint8_t *embed_lookup(term_t bucket, term_t name, int *size)
{
	*size = 0;

	embed_buck_t *eb = find_bucket(bucket);
	if (eb == 0)
		return 0;
	
	embed_bin_t *ptr = embed_bins +eb->start_index;
	embed_bin_t *end = embed_bins +eb->end_index;
	while (ptr < end)
	{
		if (ptr->name == name)
		{
			*size = ptr->ends - ptr->starts;
			return ptr->starts;
		}
		ptr++;
	}
	return 0;
}

static embed_buck_t *find_bucket(term_t bucket)
{
	// the list of buckets contains a dozen items - use linear search
	embed_buck_t *ptr = embed_bucks;
	embed_buck_t *end = ptr +nr_embed_bucks;
	while (ptr < end)
	{
		if (ptr->bucket == bucket)
			return ptr;
		ptr++;
	}
	return 0;
}

