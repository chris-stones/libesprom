
/***************************************************************************************
 * AUTHOR: Chris Stones ( chris.stones _AT_ zoho.com / chris.stones _AT_ gmail.com )
 * LICENCE: GPL-v3.
 *
 * Library for random file access on embedded Linux systems ( requires O_DIRECT ).
 * Each buffer 'ef_buffer_t' is 4k in size.
 * Buffers can be shared between multiple files 'ef_file' as-long as files are not
 * 	accesses concurrently ( do your own locking! ).
 **************************************************************************************/

// TODO:
//
//		 3) implement buffered_write.

#define _GNU_SOURCE

#include "embedded_file.h"

#include <stdlib.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define EF_ALIGNMENT 512
#define EF_BLOCKSIZE 4096 //MUST BE A MULTIPLE OF EF_ALIGNMENT

//#define DEBUG_PRINTF(...) do { printf(__VA_ARGS__); } while(0)
//#include <stdio.h>

#define DEBUG_PRINTF(...) do {} while(0)

typedef enum {

	EF_BUFFER_FLAG_DIRTY = 0x01,

} ef_buffer_flags_t;

struct ef_buffer {

	void * buffer;
	size_t file_uid;

	off_t   file_offset;
	ssize_t data_length;

	int flags;

	size_t refcount;
};

struct ef_file {

	int fd;
	size_t uid;
	off_t  file_offset;
	size_t file_size;
	int flags;

	ef_buffer_t buffer;
};


int ef_buffer_create (ef_buffer_t * buffer) {

	if(buffer) {
		*buffer = calloc(1, sizeof(struct ef_buffer));
		if(*buffer) {

			if( posix_memalign(&((*buffer)->buffer),EF_ALIGNMENT,EF_BLOCKSIZE) == 0) {

				(*buffer)->refcount = 1;
				return 0;
			}
			free(*buffer);
		}
		*buffer = NULL;
	}
	return -1;
}

int ef_buffer_destroy(ef_buffer_t   buffer) {

	if(buffer && (buffer->refcount > 0)) {

		buffer->refcount--;
		if(buffer->refcount == 0) {
			free(buffer->buffer);
			free(buffer);
		}
	}

	return 0;
}

static size_t file_guid = 0;

int ef_file_open (ef_file_t *file, ef_buffer_t shared_buffer, const char * path, int flags, mode_t mode) {

	if(file) {

		ef_buffer_t buff;

		// Catch attempts to open for write-only,
		//	and change it to read-write
		//  we need read access to implement unaligned writes on top of O_DIRECT.
		if( (flags & O_WRONLY) == O_WRONLY ) {
			flags &= (~O_WRONLY);
			flags |= ( O_RDWR  );
		}

		if(!(buff = shared_buffer))
			if( ef_buffer_create( &buff ) != 0 )
				return -1; // no shared buffer provided, and no memory for a new one.

		*file = calloc(1, sizeof(struct ef_file) );
		if(*file) {

			struct stat _stat;
			if(stat(path, &_stat) == 0)
				(*file)->file_size = _stat.st_size;

			// set O_DIRECT if user forgot to... otherwise this is all pointless!
			flags |= O_DIRECT;

			(*file)->flags = flags;

			if(((*file)->fd = open( path, flags, mode )) != -1) {

				(*file)->uid = __sync_fetch_and_add( &file_guid, 1 );

				if(buff == shared_buffer)
					buff->refcount++; // sharing buffer, bump reference.

				(*file)->buffer = buff;

				return 0;
			}

			free(*file);
		}
		*file = NULL;
	}
	return -1;
}

int ef_file_close(ef_file_t  file) {

	int err = 0;
	if(file) {

		ef_buffer_destroy( file->buffer );

		if( file->fd != -1) {
			// writes that extend the file will round its size up to a multiple of EF_BLOCKSIZE.
			//	Trim the fat here.
			err = ftruncate(file->fd, file->file_size );
			close(file->fd);
		}
		free(file);
	}
	return err;
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

	return file->file_offset;
}

static int _need_to_refill_buffer(ef_file_t file, ef_buffer_t io_buffer) {

	if(io_buffer->data_length <= 0)
		return 1; // no-data.

	if(file->uid != io_buffer->file_uid)
		return 1; // wrong file buffered.

	if( file->file_offset >= ( io_buffer->file_offset + io_buffer->data_length ) )
		return 1; // wrong part of file buffered.

	if( file->file_offset < io_buffer->file_offset )
		return 1; // wrong part of file buffered.

	return 0;
}

static size_t _refill_io_buffer(ef_file_t file, ef_buffer_t io_buffer) {

	off_t aligned_file_offset;

	aligned_file_offset = file->file_offset - ( file->file_offset % EF_ALIGNMENT );

	if( lseek( file->fd, aligned_file_offset, SEEK_SET ) != aligned_file_offset )
		return -1;

	io_buffer->file_uid = file->uid;
	io_buffer->file_offset = aligned_file_offset;

	io_buffer->data_length = read( file->fd, io_buffer->buffer, EF_BLOCKSIZE );

	return io_buffer->data_length;
}

static int _ef_file_flush(ef_file_t file, ef_buffer_t io_buffer) {

	if( io_buffer->flags & EF_BUFFER_FLAG_DIRTY) {

		if( file->uid != io_buffer->file_uid )
			return -1; // buffer is dirty with a different files data!

		if( lseek( file->fd, io_buffer->file_offset, SEEK_SET ) != io_buffer->file_offset )
			return -1;

		if( write( file->fd, io_buffer->buffer, EF_BLOCKSIZE ) != EF_BLOCKSIZE)
			return -1;

		io_buffer->flags &= (~EF_BUFFER_FLAG_DIRTY);
	}

	return 0;
}

static size_t _buffered_read( ef_file_t file, ef_buffer_t io_buffer,uint8_t * dst_buffer, size_t count ) {

	size_t total = 0;
	while(count > 0) {

		if(_need_to_refill_buffer( file, io_buffer )) {

			ssize_t read;

			if( _ef_file_flush( file, io_buffer ) != 0)
				return -1;

			read = _refill_io_buffer( file, io_buffer );

			if( read == 0 )
				return total; // EOF
			if( read == -1)
				return -1; // ERROR
		}

		{
			off_t  io_offset = file->file_offset - io_buffer->file_offset;
			size_t io_size = io_buffer->data_length - io_offset;
			size_t actual_sz = io_size < count ? io_size : count;

			if(dst_buffer) {
				memcpy(dst_buffer, ((char*)io_buffer->buffer) + io_offset, actual_sz );
				dst_buffer        += actual_sz;
			}

			file->file_offset += actual_sz;
			count             -= actual_sz;
			total             += actual_sz;
		}
	}
	return total;
}

static ssize_t _buffered_write(ef_file_t file, ef_buffer_t io_buffer, const void * src_buffer, size_t count) {

	size_t total = 0;
	while(count > 0) {

		if(_need_to_refill_buffer( file, io_buffer )) {

			ssize_t read;

			if( _ef_file_flush( file, io_buffer ) != 0)
				return -1;

			read = _refill_io_buffer( file, io_buffer );

			if( read == -1)
				return -1; // ERROR

			// zero unread bytes due to EOF.
			if( read < EF_BLOCKSIZE )
				memset(((char*)(io_buffer->buffer)) + read, 0, EF_BLOCKSIZE - read );
		}

		{
			off_t  io_offset = file->file_offset - io_buffer->file_offset;
			size_t io_size = io_buffer->data_length - io_offset;
			size_t actual_sz = io_size < count ? io_size : count;

			if( actual_sz )
				io_buffer->flags |= EF_BUFFER_FLAG_DIRTY;

			if(src_buffer) {
				memcpy(((char*)io_buffer->buffer) + io_offset, src_buffer, actual_sz );
				src_buffer        += actual_sz;
			}

			file->file_offset += actual_sz;
			count             -= actual_sz;
			total             += actual_sz;

			// write may have made the file larger.
			if(file->file_offset > file->file_size)
				file->file_size = file->file_offset;
		}
	}
	return total;
}

ssize_t ef_file_read( ef_file_t file,void * dst_buffer, size_t count) {

	if( !file || (file->fd == -1))
		return -1;

	return _buffered_read( file, file->buffer, (uint8_t*)dst_buffer, count );
}

int ef_file_flush(ef_file_t file) {

	if(!file || (file->fd == -1))
		return -1;

	return _ef_file_flush( file, file->buffer );
}

ssize_t ef_file_write(ef_file_t file, const void * src_buffer, size_t count) {

	if( !file || (file->fd == -1))
		return -1;

	return _buffered_write( file, file->buffer, src_buffer, count);
}

int ef_copy(const char * from, const char * to, int mode) {

	void * buffer = NULL;
	struct stat _stat;
	int src   = -1;
	int dst   = -1;
	int blocksize = 4096;

	if( stat( from, &_stat ) != 0)
		goto bad;

	if( _stat.st_size < blocksize) {
		blocksize  = _stat.st_size + (EF_ALIGNMENT-1);
		blocksize -= blocksize % EF_ALIGNMENT;
	}

	if( posix_memalign(&buffer,EF_ALIGNMENT,blocksize) != 0)
		goto bad;

	if((src = open( from, O_RDONLY )) == -1)
		goto bad;

	if((dst = open(   to, O_WRONLY | O_CREAT, mode)) == -1)
		goto bad;

	for(;;) {
		size_t rbytes = read ( src, buffer, blocksize );
		size_t wbytes = 0;

		if( rbytes > 0 ) {
			rbytes += (EF_ALIGNMENT-1);
			rbytes -= rbytes % EF_ALIGNMENT;
			wbytes = write( dst, buffer, rbytes);

			if( rbytes != wbytes )
				goto bad; // disk full ???
		}

		if( rbytes == 0)
			break; // EOF;

		if( rbytes < 0)
			goto bad; // ERROR
	}

	free(buffer);
	close(src);
	ftruncate(dst, _stat.st_size);
	close(dst);
	return 0;

bad:
	if(src != -1) close(src);
	if(dst != -1) close(dst);
	free(buffer);
	return -1;
}
