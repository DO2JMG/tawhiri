

#include "interpolate.h"

#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <string.h>

typedef struct {
    long   index;
    double lerp;
} Lerp1;

typedef struct {
    long   hour, lat, lng;
    double lerp;  
} Lerp3;

static int pick(double left, double step, long n, double value,
                const char *name, Lerp1 out[2])
{
    double a = (value - left) / step;
    long   b = (long)a;

    if (b < 0 || b >= n - 1) {
        fprintf(stderr, "interpolate: %s=%g out of range\n", name, value);
        errno = ERANGE;
        return -1;
    }

    double l = a - (double)b;
    out[0].index = b;       out[0].lerp = 1.0 - l;
    out[1].index = b + 1;   out[1].lerp = l;
    return 0;
}

static int pick3(double hour, double lat, double lng, Lerp3 out[8])
{
    Lerp1 lhour[2], llat[2], llng[2];

    
    if (pick(0.0,   3.0, DS_HOURS,   hour, "hour", lhour) < 0) return -1;
    
    if (pick(-90.0, 0.5, DS_LATS,    lat,  "lat",  llat)  < 0) return -1;
    
    if (pick(0.0,   0.5, DS_LONS+1,  lng,  "lng",  llng)  < 0) return -1;

    
    if (llng[1].index == DS_LONS) llng[1].index = 0;

    int i = 0;
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++)
            for (int c = 0; c < 2; c++) {
                out[i].hour = lhour[a].index;
                out[i].lat  = llat[b].index;
                out[i].lng  = llng[c].index;
                out[i].lerp = lhour[a].lerp * llat[b].lerp * llng[c].lerp;
                i++;
            }
    return 0;
}

static double interp3(const Dataset *ds, const Lerp3 corners[8],
                      int variable, int level)
{
    double r = 0.0;
    for (int i = 0; i < 8; i++) {
        float v = dataset_get(ds,
                              (int)corners[i].hour,
                              level,
                              variable,
                              (int)corners[i].lat,
                              (int)corners[i].lng);
        r += (double)v * corners[i].lerp;
    }
    return r;
}

static long search_level(const Dataset *ds, const Lerp3 corners[8],
                         double alt)
{
    long lower = 0, upper = DS_LEVELS - 2;  

    while (lower < upper) {
        long mid  = (lower + upper + 1) / 2;
        double h  = interp3(ds, corners, VAR_HEIGHT, mid);
        if (alt <= h)
            upper = mid - 1;
        else
            lower = mid;
    }
    return lower;
}

static double interp4(const Dataset *ds, const Lerp3 corners[8],
                      Lerp1 alt_lerp, int variable)
{
    double lo = interp3(ds, corners, variable, (int)alt_lerp.index);
    double hi = interp3(ds, corners, variable, (int)alt_lerp.index + 1);
    return lo * alt_lerp.lerp + hi * (1.0 - alt_lerp.lerp);
}

WindUV get_wind(const Dataset *ds, WarningCounts *warn,
                double hour, double lat, double lng, double alt)
{
    WindUV result = {0.0, 0.0};

    Lerp3 corners[8];
    if (pick3(hour, lat, lng, corners) < 0)
        return result;   

    
    long altidx = search_level(ds, corners, alt);

    double lower_h = interp3(ds, corners, VAR_HEIGHT, (int)altidx);
    double upper_h = interp3(ds, corners, VAR_HEIGHT, (int)altidx + 1);

    double lerp_alt;
    if (lower_h != upper_h)
        lerp_alt = (upper_h - alt) / (upper_h - lower_h);
    else
        lerp_alt = 0.5;

    if (lerp_alt < 0.0 && warn)
        warn->altitude_too_high++;

    Lerp1 alt_lerp = { altidx, lerp_alt };

    result.u = interp4(ds, corners, alt_lerp, VAR_U);
    result.v = interp4(ds, corners, alt_lerp, VAR_V);

    return result;
}
