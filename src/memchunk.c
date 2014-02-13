
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "memchunk.h"

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

struct mem_chunk * alloc_chunks(size_t bytes) {

	return _alloc_num_chunks( (bytes + (ALLOC_DATA_SIZE-1)) / ALLOC_DATA_SIZE );
}

int mem_chunk_getbuffer(mem_chunk_ctx_t * ctx, void ** buffer, size_t * bufferlen ) {

	if(!ctx || ! buffer || !bufferlen)
		return 0;

	*buffer = ctx->thiz->data + ctx->thiz_offset;
	*bufferlen = sizeof( ctx->thiz->data ) - ctx->thiz_offset;

	return 0;
}

int mem_chunk_seek( mem_chunk_ctx_t * ctx, long offset, int whence) {

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

