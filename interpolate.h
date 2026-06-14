// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#ifndef INTERPOLATE_H
#define INTERPOLATE_H

#include "dataset.h"

typedef struct {
    int altitude_too_high;
} WarningCounts;

typedef struct {
    double u;   
    double v;   
} WindUV;

typedef struct {
    int  valid;
    long altidx;
} WindInterpCache;

WindUV get_wind(const Dataset *ds, WarningCounts *warn,
                double hour, double lat, double lng, double alt);

WindUV get_wind_cached(const Dataset *ds, WarningCounts *warn,
                       WindInterpCache *cache,
                       double hour, double lat, double lng, double alt);

#endif 
