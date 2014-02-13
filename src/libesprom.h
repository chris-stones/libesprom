
#pragma once

#include<stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct esprom_struct;
typedef struct esprom_struct * esprom_handle;

struct esprom_sample_struct;
typedef struct esprom_sample_struct * esprom_sample_handle;

// Create / destroy a sound prom.
int  esprom_alloc( const char * const fn, esprom_handle * ph );
void esprom_free (esprom_handle ph);

// Create / destroy a sample on a prom.
int esprom_sample_alloc( esprom_handle prom, int sample_id, esprom_sample_handle * sample );
void esprom_sample_free( esprom_sample_handle sample );

// Seek to the beginning of a sample.
int esprom_sample_rewind( esprom_sample_handle sample );

// Get a filled buffer. you should release it with _releasebuffer when it is no-longer needed.
int esprom_sample_getbuffer(esprom_sample_handle sample, void ** buffer, size_t * bufferlen );

// free a buffer.
int esprom_sample_releasebuffer(esprom_sample_handle sample);




#ifdef __cplusplus
} // extern "C" {
#endif


