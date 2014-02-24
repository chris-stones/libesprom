
/***
 * AUTHOR:  Christopher Stones
 * EMAIL:   chris.stones _AT_ zoho.com / chris.stones _AT_ gmail.com
 * LICENSE: GPL-v3
 */

#include "libesprom.h"
#include "embedded_file.h"

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <byteswap.h>
#include <stdint.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

#include "memchunk.h"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define RH_BIG_ENDIAN
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define RH_LITTLE_ENDIAN
#endif

#if defined(RH_LITTLE_ENDIAN)
	#define BE_TO_CPU_16_INPLACE(x) do{ (x) = bswap_16((x)); }while(0)
	#define BE_TO_CPU_32_INPLACE(x) do{ (x) = bswap_32((x)); }while(0)
#elif defined(RH_BIG_ENDIAN)
	#define BE_TO_CPU_16_INPLACE(x) do{                      }while(0)
	#define BE_TO_CPU_32_INPLACE(x) do{                      }while(0)
#else
	#error cannot determine endianness!
#endif



struct sample_header_struct {

	size_t   start;
	size_t   end;
};
typedef struct sample_header_struct sample_header_t;

struct esprom_struct {

	mem_chunk_ctx_t mem_chunk_ctx;

	sample_header_t * sample_headers;

	short samples;
};

typedef struct esprom_struct prom_context_t;

// EXPORTED SYMBOL
int esprom_alloc( const char * const fn, esprom_handle * ph ) {

//	FILE * file = NULL;

	ef_file_t   ef_file = NULL;

	size_t highest_address = 0;
	size_t lowest_address = -1;

	if(!ph || !fn)
		goto bad;

	*ph = NULL;

	if( ef_file_open(&ef_file, NULL, fn, O_RDONLY, 0))
		goto bad;

	if( ef_file_seek(ef_file, 14, SEEK_SET) != 14 )
		goto bad;

	if((*ph = calloc(1, sizeof(prom_context_t) )) == NULL)
		goto bad;

	if(ef_file_read(ef_file, &((*ph)->samples) ,2) != 2)
		goto bad;

	BE_TO_CPU_16_INPLACE((*ph)->samples);

	(*ph)->sample_headers = (sample_header_t *)calloc( (*ph)->samples, sizeof(sample_header_t));
	if(!((*ph)->sample_headers))
		goto bad;

	{
		// First pass - determine amount of memory we need to allocate.
		int i;
		for(i=0;i< (*ph)->samples; i++) {

			int data[2];

			if( ef_file_seek(ef_file, 18 + 10 * i, SEEK_SET) != (18 + 10 * i) )
				goto bad;

			if(ef_file_read(ef_file, data ,sizeof data) != sizeof data)
				goto bad;

			BE_TO_CPU_32_INPLACE(data[0]);
			BE_TO_CPU_32_INPLACE(data[1]);

			if( data[1] > highest_address)
				highest_address = data[1];
			if( data[0] < lowest_address)
				lowest_address = data[0];
		}

		// allocate the memory!
		(*ph)->mem_chunk_ctx.size = 1+(highest_address - lowest_address);
		if(!(*ph)->mem_chunk_ctx.size)
			goto bad;
		(*ph)->mem_chunk_ctx.thiz =
		(*ph)->mem_chunk_ctx.base = alloc_chunks((*ph)->mem_chunk_ctx.size);
		if(!(*ph)->mem_chunk_ctx.base)
			goto bad;

		// second pass - load prom into memory!
		for(i=0;i< (*ph)->samples; i++) {

			int data[2];

			if( ef_file_seek(ef_file, 18 + 10 * i, SEEK_SET) != (18 + 10 * i) )
				goto bad;

			if(ef_file_read(ef_file, data ,sizeof data) != sizeof data)
				goto bad;

			BE_TO_CPU_32_INPLACE(data[0]);
			BE_TO_CPU_32_INPLACE(data[1]);

			if( ef_file_seek(ef_file, data[0], SEEK_SET) != data[0] )
				goto bad;

			{
				size_t remaining = 1 + (data[1] - data[0]);
				void * buffer;
				size_t bufferlen = 0;

				(*ph)->sample_headers[i].start = (*ph)->mem_chunk_ctx.cur_pos;
				(*ph)->sample_headers[i].end   = (*ph)->mem_chunk_ctx.cur_pos + remaining - 1;

				while( remaining ) {

					size_t readsize = remaining;

					mem_chunk_getbuffer( &(*ph)->mem_chunk_ctx, &buffer, &bufferlen );

					if(bufferlen < readsize)
						readsize = bufferlen;

					if(ef_file_read(ef_file, buffer , readsize) != readsize)
						goto bad;

					if( mem_chunk_seek(&(*ph)->mem_chunk_ctx, readsize, SEEK_CUR) != 0 )
						goto bad;

					remaining -= readsize;
				}
			}
		}
	}

	ef_file_close(ef_file);

	return 0;

bad:

	if(ph) {
		if(*ph) {
			free( (*ph)->sample_headers );
			free_chunks( (*ph)->mem_chunk_ctx.thiz );
			free(*ph);
		}
		if(ef_file)
			ef_file_close(ef_file);
	}

	return -1;
}

// EXPORTED SYMBOL
void esprom_free(esprom_handle ph) {

	if(ph) {
		free( ph->sample_headers );
		free_chunks( ph->mem_chunk_ctx.base );
		free(ph);
	}
}

struct esprom_sample_struct {

	esprom_handle prom;
	mem_chunk_ctx_t mem_chunk_ctx;
	size_t start;
	size_t end;

};
typedef struct esprom_sample_struct sample_t;


// EXPORTED SYMBOL
int esprom_sample_alloc( esprom_handle prom, int sample_id, esprom_sample_handle * sample ) {

	if(!prom || !sample)
		return -1;

	if(sample_id >= prom->samples)
		return -1;

	if((*sample = calloc(1, sizeof(sample_t))) == NULL)
		goto bad;

	(*sample)->prom = prom;

	// COPY THE PROM'S memory chunk context.
	(*sample)->mem_chunk_ctx = prom->mem_chunk_ctx;

	// SEEK to this samples start address.
	mem_chunk_seek(&(*sample)->mem_chunk_ctx, prom->sample_headers[sample_id].start ,SEEK_SET);

	// OPTIMISATION: re-base this context for faster rewind's
	(*sample)->mem_chunk_ctx.base    = (*sample)->mem_chunk_ctx.thiz;
	(*sample)->mem_chunk_ctx.cur_pos = (*sample)->mem_chunk_ctx.thiz_offset;

	// re-calculate size - actual sample size + previous samples at the start of this chunk.
	(*sample)->mem_chunk_ctx.size    = 1 + (*sample)->mem_chunk_ctx.cur_pos
			+ (prom->sample_headers[sample_id].end - prom->sample_headers[sample_id].start);

	// re-calculate sample start / end.
	(*sample)->start = (*sample)->mem_chunk_ctx.cur_pos;
	(*sample)->end   = (*sample)->start
			+ (prom->sample_headers[sample_id].end - prom->sample_headers[sample_id].start);

	return 0;

bad:

	free(*sample);

	return -1;
}

static int esprom_sample_seek( esprom_sample_handle sample, long offset, int whence ) {

	if(!sample)
		return -1;

	// adjust for previous samples data at head of memory chunk.
	switch(whence) {
	case SEEK_SET:
		offset += sample->start;
		break;
	case SEEK_CUR:
		break;
	case SEEK_END:
		break;
	}

	return mem_chunk_seek(&sample->mem_chunk_ctx, offset, whence);
}

// EXPORTED SYMBOL
int esprom_sample_rewind( esprom_sample_handle sample ) {

	return esprom_sample_seek(sample, 0, SEEK_SET);
}

// EXPORTED SYMBOL
int esprom_sample_getbuffer(esprom_sample_handle sample, void ** buffer, size_t * bufferlen ) {

	int err;

	*buffer = NULL;
	*bufferlen = 0;

	if(!sample)
		return -1;

	if((err = mem_chunk_getbuffer( &sample->mem_chunk_ctx, buffer, bufferlen)) == 0)
	{
		size_t size = 1 + (sample->end - sample->mem_chunk_ctx.cur_pos);
		if( *bufferlen > size)
			*bufferlen = size;

		err = esprom_sample_seek( sample, *bufferlen, SEEK_CUR );
	}

	return err;
}

// EXPORTED SYMBOL
void esprom_sample_free( esprom_sample_handle sample ) {

	if(sample)
		free(sample);
}


