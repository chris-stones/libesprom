
/***************************************************************************************
 * AUTHOR: Chris Stones ( chris.stones _AT_ zoho.com / chris.stones _AT_ gmail.com )
 * LICENCE: GPL-v3.
 *
 * Library for random file access on embedded Linux systems ( requires O_DIRECT ).
 * Each buffer 'ef_buffer_t' is 4k in size.
 * Buffers can be shared between multiple files 'ef_file' as-long as files are not
 * 	accesses concurrently ( do your own locking! ).
 **************************************************************************************/

#pragma once

#include <fcntl.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ef_buffer;
typedef struct ef_buffer * ef_buffer_t;

struct ef_file;
typedef struct ef_file * ef_file_t;

int ef_buffer_create (ef_buffer_t * buffer);
int ef_buffer_destroy(ef_buffer_t   buffer);

int ef_file_open (ef_file_t *file, ef_buffer_t shared_buffer, const char * path, int flags, mode_t mode);
int ef_file_close(ef_file_t  file);

off_t   ef_file_seek( ef_file_t file, off_t offset, int whence );
ssize_t ef_file_read( ef_file_t file, void * dst_buffer, size_t count);

int ef_file_flush(ef_file_t file);
ssize_t ef_file_write(ef_file_t file, const void * src_buffer, size_t count);

int ef_copy(const char * from, const char * to, int mode);

#ifdef __cplusplus
} // extern "C" {
#endif

