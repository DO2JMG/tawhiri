#ifndef DATASET_H
#define DATASET_H

#include <stddef.h>
#include <time.h>

#define DS_HOURS   65
#define DS_LEVELS  47
#define DS_VARS     3
#define DS_LATS   361
#define DS_LONS   720

#define DS_ELEMS  ((size_t)DS_HOURS * DS_LEVELS * DS_VARS * DS_LATS * DS_LONS)
#define DS_SIZE   (DS_ELEMS * sizeof(float))   

#define VAR_HEIGHT 0
#define VAR_U      1
#define VAR_V      2

typedef struct {
    float   *data;       
    void    *mmap_ptr;   
    size_t   mmap_len;
    time_t   ds_time;    
    char     path[512];
} Dataset;

int  dataset_open (Dataset *ds, const char *ds_time_str,
                   const char *directory);

void dataset_close(Dataset *ds);

static inline float dataset_get(const Dataset *ds,
                                 int hour, int level, int var,
                                 int lat,  int lon)
{
    size_t idx = ((size_t)hour  * DS_LEVELS * DS_VARS * DS_LATS * DS_LONS)
               + ((size_t)level * DS_VARS   * DS_LATS * DS_LONS)
               + ((size_t)var   * DS_LATS   * DS_LONS)
               + ((size_t)lat   * DS_LONS)
               +  (size_t)lon;
    return ds->data[idx];
}

#endif 
