
// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#include "models.h"

#include <math.h>

double air_density(double alt)
{
    double temp, pressure;

    if (alt > 25000.0) {
        temp     = -131.21 + 0.00299  * alt;
        pressure =   2.488 * pow((temp + 273.1) / 216.6, -11.388);
    } else if (alt > 11000.0) {
        temp     = -56.46;
        pressure =  22.65 * exp(1.73 - 0.000157 * alt);
    } else {
        temp     =  15.04 - 0.00649   * alt;
        pressure = 101.29 * pow((temp + 273.1) / 288.08, 5.256);
    }

    return pressure / (0.2869 * (temp + 273.1));
}

Delta model_constant_ascent(double t, double lat, double lng, double alt,
                             void *ctx)
{
    (void)t; (void)lat; (void)lng; (void)alt;
    ConstAscentCtx *c = (ConstAscentCtx *)ctx;
    Delta d = { 0.0, 0.0, c->ascent_rate };
    return d;
}

void make_drag_ctx(DragDescentCtx *ctx, double sea_level_descent_rate)
{
    
    ctx->drag_coeff = sea_level_descent_rate * 1.1045;
}

Delta model_drag_descent(double t, double lat, double lng, double alt,
                         void *ctx)
{
    (void)t; (void)lat; (void)lng;
    DragDescentCtx *c = (DragDescentCtx *)ctx;
    double dalt = -(c->drag_coeff) / sqrt(air_density(alt));
    Delta d = { 0.0, 0.0, dalt };
    return d;
}

#define PI_180  (M_PI / 180.0)
#define _180_PI (180.0 / M_PI)
#define EARTH_R  6371009.0   

Delta model_wind_velocity(double t, double lat, double lng, double alt,
                          void *ctx)
{
    WindCtx *c = (WindCtx *)ctx;

    double t_rel  = t - (double)c->dataset_epoch;
    double hour   = t_rel / 3600.0;

    
    double lng360 = fmod(lng + 360.0, 360.0);

    WindUV w = get_wind_cached(c->ds, c->warn, &c->cache,
                               hour, lat, lng360, alt);

    double R    = EARTH_R + alt;
    double dlat = _180_PI * w.v / R;
    double dlng = _180_PI * w.u / (R * cos(lat * PI_180));

    Delta d = { dlat, dlng, 0.0 };
    return d;
}

Delta model_linear(double t, double lat, double lng, double alt, void *ctx)
{
    LinearModelCtx *c = (LinearModelCtx *)ctx;
    Delta sum = { 0.0, 0.0, 0.0 };

    for (int i = 0; i < c->n; i++) {
        Delta d = c->fns[i](t, lat, lng, alt, c->ctxs[i]);
        sum.dlat += d.dlat;
        sum.dlng += d.dlng;
        sum.dalt += d.dalt;
    }
    return sum;
}

Delta model_reverse_wind_velocity(double t, double lat, double lng, double alt,
                                   void *ctx)
{
    Delta d = model_wind_velocity(t, lat, lng, alt, ctx);
    d.dlat = -d.dlat;
    d.dlng = -d.dlng;
    return d;
}

int term_burst(double t, double lat, double lng, double alt, void *ctx)
{
    (void)t; (void)lat; (void)lng;
    BurstCtx *c = (BurstCtx *)ctx;
    return alt >= c->burst_altitude;
}

int term_sea_level(double t, double lat, double lng, double alt, void *ctx)
{
    (void)t; (void)lat; (void)lng; (void)ctx;
    return alt <= 0.0;
}

int term_time(double t, double lat, double lng, double alt, void *ctx)
{
    (void)lat; (void)lng; (void)alt;
    TimeCtx *c = (TimeCtx *)ctx;
    return t > c->max_time;
}

int term_min_time(double t, double lat, double lng, double alt, void *ctx)
{
    (void)lat; (void)lng; (void)alt;
    MinTimeCtx *c = (MinTimeCtx *)ctx;
    return t < c->min_time;
}

int term_elevation(double t, double lat, double lng, double alt, void *ctx)
{
    (void)t;
    ElevationCtx *c = (ElevationCtx *)ctx;
    double lng360 = fmod(lng + 360.0, 360.0);
    double ground = (double)elevation_get(c->el, lat, lng360);
    return (alt <= ground) || (alt <= 0.0);
}

int term_any(double t, double lat, double lng, double alt, void *ctx)
{
    AnyTermCtx *a = (AnyTermCtx *)ctx;
    for (int i = 0; i < a->n; i++)
        if (a->fns[i](t, lat, lng, alt, a->ctxs[i])) return 1;
    return 0;
}
