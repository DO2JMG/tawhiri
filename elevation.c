
// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#include "elevation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <math.h>
#include <errno.h>

int elevation_open(ElevationDataset *el, const char *path)
{
    memset(el, 0, sizeof(*el));

    const char *p = path ? path : EL_DEFAULT_PATH;
    snprintf(el->path, sizeof(el->path), "%s", p);

    int fd = open(el->path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "elevation_open: open('%s'): %s\n",
                el->path, strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "elevation_open: fstat: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if ((size_t)st.st_size != EL_SIZE) {
        fprintf(stderr,
                "elevation_open: expected %zu bytes, got %lld\n"
                "  (wrong file? expected shape [%d][%d][%d][%d] int16)\n",
                EL_SIZE, (long long)st.st_size,
                EL_BLOCK_ROWS, EL_BLOCK_COLS, EL_TILE_ROWS, EL_TILE_COLS);
        close(fd);
        return -1;
    }

    void *ptr = mmap(NULL, EL_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) {
        fprintf(stderr, "elevation_open: mmap: %s\n", strerror(errno));
        return -1;
    }

    madvise(ptr, EL_SIZE, MADV_RANDOM);   

    el->mmap_ptr = ptr;
    el->mmap_len = EL_SIZE;
    el->data     = (int16_t *)ptr;

    return 0;
}

void elevation_close(ElevationDataset *el)
{
    if (el->mmap_ptr && el->mmap_ptr != MAP_FAILED) {
        munmap(el->mmap_ptr, el->mmap_len);
        el->mmap_ptr = NULL;
        el->data     = NULL;
    }
}

int16_t elevation_get(const ElevationDataset *el, double lat, double lng)
{
    if (!el->data) return 0;   

    
    lng = fmod(lng + 360.0, 360.0);

    if (lat < -90.0) lat = -90.0;
    if (lat >  90.0) lat =  90.0;

    
    double i_f = 90.0 - lat;
    double j_f = fmod(lng + 180.0, 360.0);

    
    long gi = (long)round(i_f * EL_LAT_RES);
    long gj = (long)round(j_f * EL_LNG_RES);

    
    long total_rows = (long)EL_BLOCK_ROWS * (EL_TILE_ROWS - 1);
    long total_cols = (long)EL_BLOCK_COLS * (EL_TILE_COLS - 1);

    
    if (gi < 0) gi = 0;
    if (gi >= total_rows) gi = total_rows - 1;
    if (gj < 0) gj = 0;
    if (gj >= total_cols) gj = total_cols - 1;

    
    long block_r = gi / (EL_TILE_ROWS - 1);
    long row     = gi % (EL_TILE_ROWS - 1);
    long block_c = gj / (EL_TILE_COLS - 1);
    long col     = gj % (EL_TILE_COLS - 1);

    
    if (block_r >= EL_BLOCK_ROWS) { block_r = EL_BLOCK_ROWS - 1; row = EL_TILE_ROWS - 2; }
    if (block_c >= EL_BLOCK_COLS) { block_c = EL_BLOCK_COLS - 1; col = EL_TILE_COLS - 2; }

    
    size_t idx = ((size_t)block_r * EL_BLOCK_COLS * EL_TILE_ROWS * EL_TILE_COLS)
               + ((size_t)block_c * EL_TILE_ROWS  * EL_TILE_COLS)
               + ((size_t)row     * EL_TILE_COLS)
               +  (size_t)col;

    return el->data[idx];
}
