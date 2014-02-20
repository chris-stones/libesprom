
/***************************************************************************************
 * AUTHOR: Chris Stones ( chris.stones _AT_ zoho.com / chris.stones _AT_ gmail.com )
 * LICENCE: GPL-v3.
 *
 * Library for random file access on embedded Linux systems ( requires O_DIRECT ).
 * Each buffer 'ef_buffer_t' is 4k in size.
 * Buffers can be shared between multiple files 'ef_file' as-long as files are not
 * 	accesses concurrently.
 **************************************************************************************/

#define _GNU_SOURCE

#include "embedded_file.h"

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <string.h>

#define EF_ALIGNMENT 512
#define EF_BLOCKSIZE 4096 //MUST BE A MULTIPLE OF EF_ALIGNMENT

//#define DEBUG_PRINTF(...) do { printf(__VA_ARGS__); } while(0)
//#include <stdio.h>

#define DEBUG_PRINTF(...) do {} while(0)

struct ef_buffer {

	void * buffer;
	size_t file_uid;

	off_t   file_offset;
	ssize_t data_length;
};

struct ef_file {

	int fd;
	size_t uid;
	off_t  file_offset;
	size_t file_size;
};


int ef_buffer_create (ef_buffer_t * buffer) {

	if(buffer) {
		*buffer = calloc(1, sizeof(struct ef_buffer));
		if(*buffer) {

			if( posix_memalign(&((*buffer)->buffer),EF_ALIGNMENT,EF_BLOCKSIZE) == 0)
				return 0;
			free(*buffer);
		}
		*buffer = NULL;
	}
	return -1;
}

int ef_buffer_destroy(ef_buffer_t   buffer) {

	if(buffer)
		free(buffer->buffer);
	free(buffer);
	return 0;
}

static size_t file_guid = 0;

int ef_file_open (ef_file_t *file, const char * path, int flags, mode_t mode) {

	if(file) {
		*file = calloc(1, sizeof(struct ef_file) );
		if(*file) {

			struct stat _stat;
			if(stat(path, &_stat) == 0)
				(*file)->file_size = _stat.st_size;

			flags |= O_DIRECT;
			if(((*file)->fd = open( path, flags, mode )) != -1) {
				(*file)->uid = __sync_fetch_and_add( &file_guid, 1 );

				DEBUG_PRINTF("ef: opened %s (fd: %d) ( size %zu )\n",path, (*file)->fd, (*file)->file_size);

				return 0;
			}

			free(*file);
		}
		*file = NULL;
	}
	return -1;
}

int ef_file_close(ef_file_t  file) {

	if(file) {
		if( file->fd != -1)
			close(file->fd);
		free(file);
	}
	return 0;
}

off_t ef_file_seek( ef_file_t file, off_t offset, int whence ) {

	if( !file || (file->fd == -1))
		return -1;

	switch( whence ) {
	case SEEK_SET:
		file->file_offset  = offset;
		break;
	case SEEK_CUR:
		file->file_offset += offset;
		break;
	case SEEK_END:
		file->file_offset  = offset + file->file_size;
		break;
	}

	if( file->file_offset < 0)
		file->file_offset = 0;
	if( file->file_offset > file->file_size)
		file->file_offset = file->file_size;

	DEBUG_PRINTF("ef: seek fd:%d to %zu\n", file->fd, file->file_offset);

	return file->file_offset;
}

static int _should_discard_io_buffer(ef_file_t file, ef_buffer_t io_buffer) {

	if(io_buffer->data_length <= 0) {
		DEBUG_PRINTF("df: discarding io buffer, length <=0\n");
		return 1; // no-data.
	}

	if(file->uid != io_buffer->file_uid) {
		DEBUG_PRINTF("df: discarding io buffer, wrong uid\n");
		return 1; // wrong file buffered.
	}

	if( file->file_offset >= ( io_buffer->file_offset + io_buffer->data_length ) ) {
		DEBUG_PRINTF("df: discarding io buffer, need to seek forwards\n");
		DEBUG_PRINTF("\t %zu >= %zu + %zu\n",file->file_offset,io_buffer->file_offset,io_buffer->data_length);
		return 1; // wrong part of file buffered.
	}

	if( file->file_offset < io_buffer->file_offset ) {
		DEBUG_PRINTF("df: discarding io buffer, need to seek backwards\n");
		return 1; // wrong part of file buffered.
	}

	return 0;
}

static size_t _refill_io_buffer(ef_file_t file, ef_buffer_t io_buffer) {

	off_t aligned_file_offset = file->file_offset - ( file->file_offset % EF_ALIGNMENT );

	DEBUG_PRINTF("ef: refill buffer fd:%d\n", file->fd);
	DEBUG_PRINTF("\t offset: %d", file->file_offset);
	DEBUG_PRINTF("\t aligned offset: %d\n", aligned_file_offset);

	if( lseek( file->fd, aligned_file_offset, SEEK_SET ) != aligned_file_offset )
		return -1;

	io_buffer->file_uid = file->uid;
	io_buffer->file_offset = aligned_file_offset;

	DEBUG_PRINTF("\t read: dst_buf %p size %d\n", io_buffer->buffer, EF_BLOCKSIZE );
	io_buffer->data_length = read( file->fd, io_buffer->buffer, EF_BLOCKSIZE );
	DEBUG_PRINTF("\t\t actual read = %d\n", io_buffer->data_length);

	return io_buffer->data_length;
}

static size_t _buffered_read( ef_file_t file, ef_buffer_t io_buffer,uint8_t * dst_buffer, size_t count ) {

	size_t total = 0;
	while(count > 0) {

		if(_should_discard_io_buffer( file, io_buffer )) {
			ssize_t read = _refill_io_buffer( file, io_buffer );
			if( read == 0 ) {
				DEBUG_PRINTF("refill = EOF\n");
				return total; // EOF
			}
			if( read == -1) {
				DEBUG_PRINTF("refill = ERROR\n");
				return -1; // ERROR
			}
		}

		{
			off_t  io_offset = file->file_offset - io_buffer->file_offset;
			size_t io_size = io_buffer->data_length - io_offset;
			size_t actual_sz = io_size < count ? io_size : count;

			DEBUG_PRINTF("ef: fd:%d memcpy(%p,%p,%d)\n", file->fd, dst_buffer, ((char*)io_buffer->buffer) + io_offset, actual_sz);
			memcpy(dst_buffer, ((char*)io_buffer->buffer) + io_offset, actual_sz );
			file->file_offset += actual_sz;
			count             -= actual_sz;
			total             += actual_sz;
			dst_buffer        += actual_sz;
		}
	}
	return total;
}

ssize_t ef_file_read( ef_file_t file, ef_buffer_t io_buffer,void * dst_buffer, size_t count) {

	if( !file || (file->fd == -1))
		return -1;

	if( !io_buffer )
		return -1;

	return _buffered_read( file, io_buffer, (uint8_t*)dst_buffer, count );
}

