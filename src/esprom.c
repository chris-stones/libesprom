
/***
 * AUTHOR:  Christopher Stones
 * EMAIL:   chris.stones _AT_ zoho.com / chris.stones _AT_ gmail.com
 * LICENSE: GPL-v3
 */

#include "libesprom.h"

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <byteswap.h>
#include <stdint.h>
#include <stdarg.h>
#include <linux/limits.h>

#define ALLOC_CHUNK_SIZE 4096 * 2

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define RH_BIG_ENDIAN
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define RH_LITTLE_ENDIAN
#endif

#if defined(RH_LITTLE_ENDIAN)
	#define RH_IS_LITTLE_ENDIAN 1
	#define RH_IS_BIG_ENDIAN 0
	#define CPU_TO_BE_32(x) (bswap_32((x)))
	#define BE_TO_CPU_32(x) (bswap_32((x)))
	#define CPU_TO_LE_32(x) ((x))
    #define LE_TO_CPU_32(x) ((x))
	#define CPU_TO_BE_16(x) (bswap_16((x)))
	#define BE_TO_CPU_16(x) (bswap_16((x)))
	#define CPU_TO_LE_16(x) ((x))
    #define LE_TO_CPU_16(x) ((x))
	#define BE_TO_CPU_16_INPLACE(x) do{ (x) = bswap_16((x)); }while(0)
	#define BE_TO_CPU_32_INPLACE(x) do{ (x) = bswap_32((x)); }while(0)
	#define LE_TO_CPU_16_INPLACE(x) do{                      }while(0)
	#define LE_TO_CPU_32_INPLACE(x) do{                      }while(0)
	#define COPY_TWO_CPU16_TO_BE16(out, in)\
	do {\
		((uint16_t*)out)[0] = CPU_TO_BE_16(((uint16_t*)in)[0]);\
		((uint16_t*)out)[1] = CPU_TO_BE_16(((uint16_t*)in)[1]);\
	}while(0)
#elif defined(RH_BIG_ENDIAN)
	#define RH_IS_LITTLE_ENDIAN 0
	#define RH_IS_BIG_ENDIAN 1
	#define CPU_TO_BE_32(x) ((x))
    #define BE_TO_CPU_32(x) ((x))
	#define CPU_TO_LE_32(x) (bswap_32((x)))
	#define LE_TO_CPU_32(x) (bswap_32((x)))
	#define CPU_TO_BE_16(x) ((x))
    #define BE_TO_CPU_16(x) ((x))
	#define CPU_TO_LE_16(x) (bswap_16((x)))
	#define LE_TO_CPU_16(x) (bswap_16((x)))
	#define LE_TO_CPU_16_INPLACE(x) do{ (x) = bswap_16((x)); }while(0)
	#define LE_TO_CPU_32_INPLACE(x) do{ (x) = bswap_32((x)); }while(0)
	#define BE_TO_CPU_16_INPLACE(x) do{                      }while(0)
	#define BE_TO_CPU_32_INPLACE(x) do{                      }while(0)
	#define COPY_TWO_CPU16_TO_BE16(out, in)\
		(*((uint32_t*)(out))) = (*((uint32_t*)(in)))
#else
	#error cannot determine endianness!
#endif

struct mem_chunk;

struct mem_chunk_header {
	struct mem_chunk * next;
};

#define ALLOC_DATA_SIZE (ALLOC_CHUNK_SIZE - sizeof(struct mem_chunk_header))

struct mem_chunk {
	struct mem_chunk_header header;
	uint8_t data[ALLOC_DATA_SIZE];
};

struct mem_chunk_ctx {

	struct mem_chunk * base;
	struct mem_chunk * thiz;
	size_t cur_pos;
	size_t thiz_offset;
	size_t size;
};
typedef struct mem_chunk_ctx mem_chunk_ctx_t;

void free_chunks(struct mem_chunk * head) {

	while(head) {
		struct mem_chunk * p = head;
		head = head->header.next;
		free(p);
	}
}

static struct mem_chunk * alloc_chunk(struct mem_chunk * parent) {

	struct mem_chunk * chunk = malloc(sizeof(struct mem_chunk));

	if( chunk ) {
		chunk->header.next = NULL;
		if( parent )
			parent->header.next = chunk;
	}

	return chunk;
}

static struct mem_chunk * _alloc_num_chunks(size_t chunks) {

	struct mem_chunk * head = NULL;
	struct mem_chunk * thiz = NULL;

	while(chunks--) {
		if((thiz = alloc_chunk(thiz))==NULL)
			goto cleanup;
		if(!head)
			head = thiz;
	}

	return head;

cleanup:

	free_chunks(head);
	return NULL;
}

static struct mem_chunk * alloc_chunks(size_t bytes) {

	return _alloc_num_chunks( (bytes + (ALLOC_DATA_SIZE-1)) / ALLOC_DATA_SIZE );
}

static int mem_chunk_getbuffer(mem_chunk_ctx_t * ctx, void ** buffer, size_t * bufferlen ) {

	if(!ctx || ! buffer || !bufferlen)
		return 0;

	*buffer = ctx->thiz->data + ctx->thiz_offset;
	*bufferlen = sizeof( ctx->thiz->data ) - ctx->thiz_offset;

	return 0;
}

static int mem_chunk_seek( mem_chunk_ctx_t * ctx, long offset, int whence) {

	// determine absolute address
	size_t abs;
	switch(whence)
	{
	case SEEK_SET:
		abs = offset;
		break;
	case SEEK_CUR:
		abs = ctx->cur_pos + offset;
		break;
	case SEEK_END:
		abs = ctx->size + offset;
		break;
	}

	/*** is a relative seek possible ??? ***/
	if( (ctx->cur_pos - ctx->thiz_offset) <= abs ) {

		/*
		 * yes! - rewind to the beginning of this block
		 * and make the absolute address relative.
		 */
		ctx->cur_pos -= ctx->thiz_offset;
		ctx->thiz_offset = 0;
		abs -= ctx->cur_pos;
	}
	else {

		/*
		 * no! - rewind to the beginning of the file.
		 */
		ctx->cur_pos = 0;
		ctx->thiz_offset = 0;
		ctx->thiz = ctx->base;
	}

	// now seek forward to target address.
	for(;;) {
		if( abs < ALLOC_DATA_SIZE ) {
			ctx->thiz_offset  = abs;
			ctx->cur_pos += abs;
			return 0;
		}
		else {

			if( !ctx->thiz->header.next )
				return -1; // attempted to seek past end of file!

			abs -= ALLOC_DATA_SIZE;
			ctx->cur_pos += ALLOC_DATA_SIZE;
			ctx->thiz = ctx->thiz->header.next;
		}
	}

	return -1; // never hit
}

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

	FILE * file = NULL;
	size_t highest_address = 0;
	size_t lowest_address = -1;

	if(!ph || !fn)
		goto bad;

	*ph = NULL;

	if(!(file = fopen(fn, "rb")))
		goto bad;

	if( fseek(file, 14, SEEK_SET) != 0 )
		goto bad;

	if((*ph = calloc(1, sizeof(prom_context_t) )) == NULL)
		goto bad;

	if(fread( &((*ph)->samples) ,2,1,file) != 1)
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

			if( fseek(file, 18 + 10 * i, SEEK_SET) != 0 )
				goto bad;

			if(fread( data, sizeof data, 1, file ) != 1)
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

			if( fseek(file, 18 + 10 * i, SEEK_SET) != 0 )
				goto bad;

			if(fread( data, sizeof data, 1, file ) != 1)
				goto bad;

			BE_TO_CPU_32_INPLACE(data[0]);
			BE_TO_CPU_32_INPLACE(data[1]);

			if( fseek(file, data[0], SEEK_SET) != 0)
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

					if(fread(buffer, readsize, 1, file) != 1 )
						goto bad;

					if( mem_chunk_seek(&(*ph)->mem_chunk_ctx, readsize, SEEK_CUR) != 0 )
						goto bad;

					remaining -= readsize;
				}
			}
		}
	}

	fclose(file);

	return 0;

bad:

	if(ph) {
		if(*ph) {
			free( (*ph)->sample_headers );
			free_chunks( (*ph)->mem_chunk_ctx.thiz );
			free(*ph);
		}
		if(file)
			fclose(file);
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

// EXPORTED SYMBOL
int esprom_sample_seek( esprom_sample_handle sample, long offset, int whence ) {

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
int esprom_sample_getbuffer(esprom_sample_handle sample, void ** buffer, size_t * bufferlen ) {

	int err;

	if(!sample)
		return -1;

	if((err = mem_chunk_getbuffer( &sample->mem_chunk_ctx, buffer, bufferlen)) == 0)
	{
		size_t size = 1 + (sample->end - sample->mem_chunk_ctx.cur_pos);
		if( *bufferlen > size)
			*bufferlen = size;
	}

	return err;
}

// EXPORTED SYMBOL
size_t esprom_sample_size(esprom_sample_handle sample) {

	if(!sample)
		return -1;

	return 1 + (sample->end - sample->start);
}

// EXPORTED SYMBOL
void esprom_sample_free( esprom_sample_handle sample ) {

	if(sample)
		free(sample);
}

