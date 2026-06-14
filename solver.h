// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#ifndef SOLVER_H
#define SOLVER_H

#include "models.h"

typedef struct {
    double t;
    double lat;
    double lng;
    double alt;
} Point;

typedef struct {
    Point  *pts;
    size_t  n;
    size_t  capacity;
} Stage;

typedef struct {
    ModelFn      model;
    void        *model_ctx;
    TerminatorFn terminator;
    void        *term_ctx;
    int          reverse;    
    double       dt;         
} FlightStage;

typedef struct {
    Stage  *stages;
    int     n_stages;
} SolverResult;

SolverResult *solve(double t0, double lat0, double lng0, double alt0,
                    const FlightStage *stages, int n_stages,
                    double dt);

void solver_result_free(SolverResult *r);

#endif 
