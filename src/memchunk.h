#pragma once

#define ALLOC_CHUNK_SIZE 4096 * 2

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

void free_chunks(struct mem_chunk * head);
struct mem_chunk * alloc_chunks(size_t bytes);
int mem_chunk_getbuffer(mem_chunk_ctx_t * ctx, void ** buffer, size_t * bufferlen );
int mem_chunk_seek( mem_chunk_ctx_t * ctx, long offset, int whence);

