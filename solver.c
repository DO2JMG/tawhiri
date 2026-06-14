
// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#include "solver.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct { double lat, lng, alt; } Vec;

static inline Vec vecadd(Vec a, double k, Vec b)
{
    Vec r;
    r.lat  = a.lat + k * b.lat;
    r.lng  = a.lng + k * b.lng;
    r.alt  = a.alt + k * b.alt;
    r.lng  = fmod(r.lng + 360.0 * 10.0, 360.0);  
    return r;
}

static inline double lerp1(double a, double b, double l)
{
    return (1.0 - l) * a + l * b;
}

static inline Vec veclerp(Vec a, Vec b, double l)
{
    Vec r;
    r.lat = lerp1(a.lat, b.lat, l);
    r.alt = lerp1(a.alt, b.alt, l);

    
    double al = a.lng, bl = b.lng;
    if (al > bl) { double tmp = al; al = bl; bl = tmp;
                   double tl  = l;  l  = 1.0 - tl; }
    if (bl - al < 180.0)
        r.lng = lerp1(al, bl, l);
    else
        r.lng = fmod(lerp1(al + 360.0, bl, l), 360.0);

    return r;
}

static inline Vec f(const FlightStage *stage,
                    double t, Vec y)
{
    Delta d = stage->model(t, y.lat, y.lng, y.alt, stage->model_ctx);
    Vec r = { d.dlat, d.dlng, d.dalt };
    return r;
}

static inline int tc(const FlightStage *stage,
                     double t, Vec y)
{
    return stage->terminator(t, y.lat, y.lng, y.alt, stage->term_ctx);
}

static int stage_push(Stage *s, double t, double lat, double lng, double alt)
{
    if (s->n >= s->capacity) {
        size_t newcap = s->capacity ? s->capacity * 2 : 1024;
        Point *p = realloc(s->pts, newcap * sizeof(Point));
        if (!p) return -1;
        s->pts      = p;
        s->capacity = newcap;
    }
    s->pts[s->n++] = (Point){ t, lat, lng, alt };
    return 0;
}

static Stage rk4_stage(double t, Vec y,
                        const FlightStage *stage,
                        double dt,
                        double tol)      
{
    Stage result;
    memset(&result, 0, sizeof(result));

    
    if (stage->reverse)
        dt = -fabs(dt);
    else
        dt = fabs(dt);

    stage_push(&result, t, y.lat, y.lng, y.alt);

    Vec k1, k2, k3, k4, y2;
    double t2;

    for (;;) {
        k1 = f(stage, t,            y);
        k2 = f(stage, t + dt / 2.0, vecadd(y, dt / 2.0, k1));
        k3 = f(stage, t + dt / 2.0, vecadd(y, dt / 2.0, k2));
        k4 = f(stage, t + dt,       vecadd(y, dt,        k3));

        
        y2 = y;
        y2 = vecadd(y2, dt / 6.0, k1);
        y2 = vecadd(y2, dt / 3.0, k2);
        y2 = vecadd(y2, dt / 3.0, k3);
        y2 = vecadd(y2, dt / 6.0, k4);
        t2 = t + dt;

        if (tc(stage, t2, y2))
            break;

        t = t2;
        y = y2;
        if (stage_push(&result, t, y.lat, y.lng, y.alt) < 0) {
            fprintf(stderr, "solver: out of memory\n");
            return result;
        }
    }

    
    double left = 0.0, right = 1.0;
    double t3 = t2;
    Vec    y3 = y2;

    while (right - left > tol) {
        double mid = (left + right) / 2.0;
        t3 = lerp1(t, t2, mid);
        y3 = veclerp(y, y2, mid);

        if (tc(stage, t3, y3))
            right = mid;
        else
            left  = mid;
    }

    stage_push(&result, t3, y3.lat, y3.lng, y3.alt);
    return result;
}

SolverResult *solve(double t0, double lat0, double lng0, double alt0,
                    const FlightStage *stages, int n_stages,
                    double dt)
{
    SolverResult *res = malloc(sizeof(SolverResult));
    if (!res) return NULL;

    res->stages   = calloc((size_t)n_stages, sizeof(Stage));
    res->n_stages = n_stages;
    if (!res->stages) { free(res); return NULL; }

    double t   = t0;
    Vec    pos = { lat0, lng0, alt0 };

    for (int i = 0; i < n_stages; i++) {
        double stage_dt = stages[i].dt > 0.0 ? stages[i].dt : dt;
        Stage s = rk4_stage(t, pos, &stages[i], stage_dt, 0.01);
        res->stages[i] = s;

        
        if (s.n > 0) {
            Point last = s.pts[s.n - 1];
            t          = last.t;
            pos.lat    = last.lat;
            pos.lng    = last.lng;
            pos.alt    = last.alt;
        }
    }

    return res;
}

void solver_result_free(SolverResult *r)
{
    if (!r) return;
    for (int i = 0; i < r->n_stages; i++)
        free(r->stages[i].pts);
    free(r->stages);
    free(r);
}
