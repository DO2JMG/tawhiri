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

WindUV get_wind(const Dataset *ds, WarningCounts *warn,
                double hour, double lat, double lng, double alt);

#endif 
