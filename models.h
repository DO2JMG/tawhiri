// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#ifndef MODELS_H
#define MODELS_H

#include "dataset.h"
#include "interpolate.h"
#include "elevation.h"

typedef struct { double dlat, dlng, dalt; } Delta;

typedef Delta (*ModelFn)     (double t, double lat, double lng, double alt,
                               void *ctx);
typedef int   (*TerminatorFn)(double t, double lat, double lng, double alt,
                               void *ctx);

typedef struct { double ascent_rate; } ConstAscentCtx;

typedef struct { double drag_coeff; } DragDescentCtx;

typedef struct {
    const Dataset  *ds;
    WarningCounts  *warn;
    time_t          dataset_epoch;
    WindInterpCache cache;
} WindCtx;

#define MAX_SUBMODELS 4
typedef struct {
    int      n;
    ModelFn  fns[MAX_SUBMODELS];
    void    *ctxs[MAX_SUBMODELS];
} LinearModelCtx;

typedef struct { double burst_altitude; } BurstCtx;

typedef struct { double max_time; } TimeCtx;

typedef struct { double min_time; } MinTimeCtx;

typedef struct { const ElevationDataset *el; } ElevationCtx;

Delta model_constant_ascent(double t, double lat, double lng, double alt,
                             void *ctx);

Delta model_drag_descent    (double t, double lat, double lng, double alt,
                             void *ctx);

Delta model_wind_velocity   (double t, double lat, double lng, double alt,
                             void *ctx);

Delta model_linear          (double t, double lat, double lng, double alt,
                             void *ctx);

Delta model_reverse_wind_velocity(double t, double lat, double lng, double alt,
                                   void *ctx);

int term_burst     (double t, double lat, double lng, double alt, void *ctx);
int term_sea_level (double t, double lat, double lng, double alt, void *ctx);
int term_time      (double t, double lat, double lng, double alt, void *ctx);
int term_min_time  (double t, double lat, double lng, double alt, void *ctx);
int term_elevation (double t, double lat, double lng, double alt, void *ctx);

typedef struct {
    int           n;
    TerminatorFn *fns;
    void        **ctxs;
} AnyTermCtx;

int term_any(double t, double lat, double lng, double alt, void *ctx);

void   make_drag_ctx   (DragDescentCtx *ctx, double sea_level_descent_rate);
double air_density     (double alt);

#endif 
