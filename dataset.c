// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#include "dataset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define DEFAULT_DIRECTORY "./dataset"

int dataset_open(Dataset *ds, const char *ds_time_str, const char *directory)
{
    memset(ds, 0, sizeof(*ds));

    
    const char *dir = directory ? directory : DEFAULT_DIRECTORY;
    snprintf(ds->path, sizeof(ds->path), "%s/%s", dir, ds_time_str);

    
    if (strlen(ds_time_str) < 10) {
        fprintf(stderr, "dataset_open: time string too short '%s'\n", ds_time_str);
        return -1;
    }
    char buf[5];
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    
    memcpy(buf, ds_time_str+0, 4); buf[4] = '\0'; tm.tm_year = atoi(buf) - 1900;
    
    memcpy(buf, ds_time_str+4, 2); buf[2] = '\0'; tm.tm_mon  = atoi(buf) - 1;
    
    memcpy(buf, ds_time_str+6, 2); buf[2] = '\0'; tm.tm_mday = atoi(buf);
    
    memcpy(buf, ds_time_str+8, 2); buf[2] = '\0'; tm.tm_hour = atoi(buf);
    tm.tm_isdst = 0;
    
    
    {
        
        int y = atoi(ds_time_str+0) - 1900;  
        int m = atoi(buf+0);                  
        (void)m;
        
        int year  = tm.tm_year + 1900;
        int month = tm.tm_mon + 1;
        int day   = tm.tm_mday;
        int hour  = tm.tm_hour;
        (void)y;

        
        
        int a = (14 - month) / 12;
        int yr = year + 4800 - a;
        int mo = month + 12*a - 3;
        long jdn = day + (153*mo+2)/5 + 365L*yr + yr/4 - yr/100 + yr/400 - 32045;
        long unix_epoch_jdn = 2440588L;  
        long days = jdn - unix_epoch_jdn;
        ds->ds_time = (time_t)(days * 86400L + hour * 3600L);
    }

    
    int fd = open(ds->path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "dataset_open: open('%s'): %s\n",
                ds->path, strerror(errno));
        return -1;
    }

    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "dataset_open: fstat: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if ((size_t)st.st_size != DS_SIZE) {
        fprintf(stderr, "dataset_open: expected %zu bytes, got %lld\n",
                DS_SIZE, (long long)st.st_size);
        close(fd);
        return -1;
    }

    
    void *ptr = mmap(NULL, DS_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);   

    if (ptr == MAP_FAILED) {
        fprintf(stderr, "dataset_open: mmap: %s\n", strerror(errno));
        return -1;
    }

    madvise(ptr, DS_SIZE, MADV_RANDOM);
    madvise(ptr, DS_SIZE, MADV_WILLNEED);

    ds->mmap_ptr = ptr;
    ds->mmap_len = DS_SIZE;
    ds->data     = (float *)ptr;

    return 0;
}

void dataset_close(Dataset *ds)
{
    if (ds->mmap_ptr && ds->mmap_ptr != MAP_FAILED) {
        munmap(ds->mmap_ptr, ds->mmap_len);
        ds->mmap_ptr = NULL;
        ds->data     = NULL;
    }
}
