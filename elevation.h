// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#ifndef ELEVATION_H
#define ELEVATION_H

#include <stddef.h>
#include <stdint.h>

#define EL_BLOCK_ROWS   4
#define EL_BLOCK_COLS   6
#define EL_TILE_ROWS    10801
#define EL_TILE_COLS    14401

#define EL_ELEMS  ((size_t)EL_BLOCK_ROWS * EL_BLOCK_COLS * EL_TILE_ROWS * EL_TILE_COLS)
#define EL_SIZE   (EL_ELEMS * sizeof(int16_t))   

#define EL_LAT_RES  ((double)(EL_TILE_ROWS - 1) / 45.0)   
#define EL_LNG_RES  ((double)(EL_TILE_COLS - 1) / 60.0)   

#define EL_DEFAULT_PATH "./dataset/ruaumoko-dataset"

typedef struct {
    int16_t *data;      
    void    *mmap_ptr;
    size_t   mmap_len;
    char     path[512];
} ElevationDataset;

int  elevation_open (ElevationDataset *el, const char *path);
void elevation_close(ElevationDataset *el);

int16_t elevation_get(const ElevationDataset *el, double lat, double lng);

#endif 
