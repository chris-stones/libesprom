
#ifndef ESPROM_H_
#define ESPROM_H_

#include<stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct esprom_struct;
typedef struct esprom_struct * esprom_handle;

struct esprom_sample_struct;
typedef struct esprom_sample_struct * esprom_sample_handle;

int  esprom_alloc( const char * const fn, esprom_handle * ph );
void esprom_free (esprom_handle ph);

int esprom_sample_alloc( esprom_handle prom, int sample_id, esprom_sample_handle * sample );
int esprom_sample_seek( esprom_sample_handle sample, long offset, int whence );
size_t esprom_sample_size(esprom_sample_handle sample);
int esprom_sample_getbuffer(esprom_sample_handle sample, void ** buffer, size_t * bufferlen );
void esprom_sample_free( esprom_sample_handle sample );


#ifdef __cplusplus
} // extern "C" {
#endif

#endif /* ESPROM_H_ */

