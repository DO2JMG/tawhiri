
// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <dirent.h>

#include "dataset.h"
#include "interpolate.h"
#include "models.h"
#include "solver.h"
#include "elevation.h"
#include "httpd.h"

static const char *arg_str(int argc, char **argv, const char *key,
                            const char *def)
{
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], key) == 0)
            return argv[i + 1];
    return def;
}

static double arg_dbl(int argc, char **argv, const char *key,
                       double def, int *found)
{
    const char *s = arg_str(argc, argv, key, NULL);
    if (found) *found = (s != NULL);
    return s ? atof(s) : def;
}

static int arg_flag(int argc, char **argv, const char *key)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], key) == 0) return 1;
    return 0;
}

static double descent_dt_default(double dt)
{
    return dt;
}

static void format_iso8601(double t, char *buf, size_t len)
{
    time_t tt = (time_t)t;
    struct tm *utc = gmtime(&tt);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", utc);
}

static void print_csv_header(void)
{
    puts("stage,datetime,lat,lng,alt");
}

static void print_csv(const char *stage, double t, double lat, double lng, double alt)
{
    char ts[32];
    format_iso8601(t, ts, sizeof(ts));
    printf("%s,%s,%.6f,%.6f,%.2f\n", stage, ts, lat, lng, alt);
}

#include "httpd.h"

int main(int argc, char **argv)
{
    
    if (arg_flag(argc, argv, "--server")) {
        const char *dir      = arg_str(argc, argv, "--dir",  NULL);
        const char *ds_str   = arg_str(argc, argv, "-d",     NULL);
        const char *bind     = arg_str(argc, argv, "--bind", "0.0.0.0");
        int found;
        int port = (int)arg_dbl(argc, argv, "--port", 8080, &found);
        double dt = arg_dbl(argc, argv, "--dt", 60.0, NULL);
        double descent_dt = arg_dbl(argc, argv, "--descent-dt",
                                    descent_dt_default(dt), NULL);

        
        int auto_latest = (ds_str == NULL);

        ElevationDataset el;
        int has_el = 0;
        if (elevation_open(&el, NULL) == 0) {
            has_el = 1;
            fprintf(stderr, "Elevation dataset loaded.\n");
        }

        ServerConfig cfg = {
            .port         = port,
            .bind_addr    = bind,
            .dataset_dir  = dir ? dir : "./dataset",
            .dataset_name = ds_str,
            .auto_latest  = auto_latest,
            .elev_ds      = has_el ? &el : NULL,
            .has_elev  = has_el,
            .dt        = dt,
            .descent_dt = descent_dt,
        };

        int ret = httpd_run(&cfg);
        if (has_el) elevation_close(&el);
        return ret;
    }

    if (argc < 2 || arg_flag(argc, argv, "--help") || arg_flag(argc, argv, "-h")) {
        fprintf(stderr,
            "CLI mode:\n"
            "  tawhiri-c -d YYYYMMDDHH --lat L --lng G --alt A\n"
            "            --asc R --burst B --desc D\n"
            "            [--dir PATH] [--time T] [--dt SECS] [--descent-dt SECS] [--csv]\n"
            "            [--profile standard_profile|reverse_profile]\n"
            "\n"
            "Server mode:\n"
            "  tawhiri-c --server [--port 8080] [--bind 0.0.0.0]\n"
            "            [-d YYYYMMDDHH] [--dir PATH] [--dt SECS] [--descent-dt SECS]\n"
            "            (without -d, the newest dataset in --dir is used and auto-reloaded)\n"
            "\n"
            "  GET /?launch_latitude=51.5&launch_longitude=8.1\n"
            "       &launch_altitude=100&ascent_rate=5\n"
            "       &burst_altitude=30000&descent_rate=6\n"
            "       [&launch_datetime=2026-06-10T12:00:00Z]\n"
            "       [&profile=standard_profile]\n");
        return 1;
    }

    
    const char *ds_str   = arg_str(argc, argv, "-d",      NULL);
    const char *dir      = arg_str(argc, argv, "--dir",   NULL);
    int found;
    double lat0          = arg_dbl(argc, argv, "--lat",   0, &found);
    if (!found) { fprintf(stderr, "Missing --lat\n"); return 1; }
    double lng0          = arg_dbl(argc, argv, "--lng",   0, &found);
    if (!found) { fprintf(stderr, "Missing --lng\n"); return 1; }
    double alt0          = arg_dbl(argc, argv, "--alt",   0, &found);
    if (!found) { fprintf(stderr, "Missing --alt\n"); return 1; }
    double t0            = arg_dbl(argc, argv, "--time",  0, &found);
    int has_time         = found;
    double ascent_rate   = arg_dbl(argc, argv, "--asc",   5, &found);
    if (!found) { fprintf(stderr, "Missing --asc\n"); return 1; }
    double burst_alt     = arg_dbl(argc, argv, "--burst", 30000, &found);
    int    has_burst     = found;
    double descent_rate  = arg_dbl(argc, argv, "--desc",  6, &found);
    int    has_desc      = found;
    double dt            = arg_dbl(argc, argv, "--dt",    60.0, NULL);
    double descent_dt    = arg_dbl(argc, argv, "--descent-dt",
                                   descent_dt_default(dt), NULL);
    int    csv_mode      = arg_flag(argc, argv, "--csv");
    const char *profile  = arg_str(argc, argv, "--profile", "standard_profile");
    const char *elev_path = arg_str(argc, argv, "--elev", NULL);  

    
    int is_standard = (strcmp(profile, "standard_profile") == 0);
    int is_reverse  = (strcmp(profile, "reverse_profile")  == 0);
    if (!is_standard && !is_reverse) {
        fprintf(stderr, "Unknown --profile '%s' (standard_profile | reverse_profile)\n", profile);
        return 1;
    }
    if (is_standard && !has_burst) { fprintf(stderr, "Missing --burst\n"); return 1; }
    if (is_standard && !has_desc)  { fprintf(stderr, "Missing --desc\n");  return 1; }

    if (!ds_str) { fprintf(stderr, "Missing -d YYYYMMDDHH\n"); return 1; }

    
    Dataset ds;
    if (dataset_open(&ds, ds_str, dir) < 0)
        return 1;

    
    if (!has_time)
        t0 = (double)ds.ds_time;

    
    double lng_norm = fmod(lng0 + 360.0, 360.0);

    
    ElevationDataset el;
    int has_elevation = 0;
    if (elevation_open(&el, elev_path) == 0) {
        has_elevation = 1;
    } else if (elev_path) {
        
        dataset_close(&ds);
        return 1;
    } else {
        
        fprintf(stderr, "Note: elevation dataset not found at '%s', "
                        "using sea level (alt=0) as landing criterion\n",
                EL_DEFAULT_PATH);
    }

    
    ElevationCtx elev_ctx = { has_elevation ? &el : NULL };

    
    TerminatorFn land_term = has_elevation ? term_elevation : term_sea_level;
    void        *land_ctx  = has_elevation ? (void *)&elev_ctx : NULL;

    WarningCounts warn = { 0 };

    
    ConstAscentCtx asc_ctx  = { ascent_rate };
    WindCtx        wind_ctx = { &ds, &warn, ds.ds_time, { 0, 0 } };

    
    FlightStage    stages[2];
    int            n_stages;
    const char    *stage_names[2];

    BurstCtx       burst_ctx = { burst_alt };
    DragDescentCtx drag_ctx;
    LinearModelCtx down_model;
    LinearModelCtx rev_model;
    MinTimeCtx     min_time_ctx;
    TerminatorFn   rev_terms[2];
    void          *rev_term_ctxs[2];
    AnyTermCtx     any_ctx;

    
    LinearModelCtx up_model = {
        .n    = 2,
        .fns  = { model_constant_ascent, model_wind_velocity },
        .ctxs = { &asc_ctx, &wind_ctx }
    };

    if (is_standard) {
        make_drag_ctx(&drag_ctx, descent_rate);

        down_model = (LinearModelCtx){
            .n    = 2,
            .fns  = { model_drag_descent, model_wind_velocity },
            .ctxs = { &drag_ctx, &wind_ctx }
        };

        stages[0] = (FlightStage){ model_linear, &up_model,          term_burst,  &burst_ctx, 0, dt };
        stages[1] = (FlightStage){ model_linear, &down_model,         land_term,   land_ctx,   0, descent_dt };
        n_stages       = 2;
        stage_names[0] = "ascent";
        stage_names[1] = "descent";

    } else {
        
        rev_model = (LinearModelCtx){
            .n    = 2,
            .fns  = { model_constant_ascent, model_wind_velocity },
            .ctxs = { &asc_ctx, &wind_ctx }   
        };

        
        min_time_ctx = (MinTimeCtx){ (double)ds.ds_time };

        
        rev_terms[0]    = land_term;   rev_term_ctxs[0] = land_ctx;
        rev_terms[1]    = term_min_time;  rev_term_ctxs[1] = &min_time_ctx;

        any_ctx = (AnyTermCtx){ 2, rev_terms, rev_term_ctxs };

        stages[0] = (FlightStage){ model_linear, &rev_model, term_any, &any_ctx, 1, dt };
        n_stages       = 1;
        stage_names[0] = "reverse_ascent";
    }

    
    SolverResult *result = solve(t0, lat0, lng_norm, alt0,
                                 stages, n_stages, dt);
    if (!result) {
        fprintf(stderr, "solver: allocation failure\n");
        dataset_close(&ds);
        return 1;
    }

    
    
    char ts_start[40], ts_complete[40];
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    {
        
        time_t now = tp.tv_sec;
        struct tm *utc = gmtime(&now);
        char base[32];
        strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", utc);
        int usec = (int)(tp.tv_nsec / 1000);
        snprintf(ts_start,    sizeof(ts_start),    "%s.%06dZ", base, usec);
        snprintf(ts_complete, sizeof(ts_complete), "%s.%06dZ", base, usec);
    }

    
    char ds_dt[32];
    format_iso8601((double)ds.ds_time, ds_dt, sizeof(ds_dt));

    
    char launch_dt[32];
    format_iso8601(t0, launch_dt, sizeof(launch_dt));

    if (csv_mode) {
        print_csv_header();
        for (int s = 0; s < result->n_stages; s++) {
            Stage *st = &result->stages[s];
            for (size_t i = 0; i < st->n; i++) {
                Point *p = &st->pts[i];
                double lng_out = p->lng > 180.0 ? p->lng - 360.0 : p->lng;
                print_csv(stage_names[s], p->t, p->lat, lng_out, p->alt);
            }
        }
    } else {
        
        printf("{\n");

        
        printf("  \"metadata\":{\n");
        printf("    \"start_datetime\":\"%s\",\n",    ts_start);
        printf("    \"complete_datetime\":\"%s\"\n",  ts_complete);
        printf("  },\n");

        
        printf("  \"prediction\":[\n");
        for (int s = 0; s < result->n_stages; s++) {
            Stage *st = &result->stages[s];
            printf("    {\"stage\":\"%s\",\"trajectory\":[\n", stage_names[s]);
            for (size_t i = 0; i < st->n; i++) {
                Point *p = &st->pts[i];
                char ts[32];
                format_iso8601(p->t, ts, sizeof(ts));
                double lng_out = p->lng > 180.0 ? p->lng - 360.0 : p->lng;
                int last_pt = (i == st->n - 1);
                printf("      {\"altitude\":%.6g,\"datetime\":\"%s\","
                       "\"latitude\":%.6g,\"longitude\":%.6g}%s\n",
                       p->alt, ts, p->lat, lng_out,
                       last_pt ? "" : ",");
            }
            int last_stage = (s == result->n_stages - 1);
            printf("    ]}%s\n", last_stage ? "" : ",");
        }
        printf("  ],\n");

        
        printf("  \"request\":{\n");
        printf("    \"ascent_rate\":%.6g,\n",     ascent_rate);
        printf("    \"burst_altitude\":%.6g,\n",  burst_alt);
        printf("    \"dataset\":\"%s\",\n",        ds_dt);
        printf("    \"descent_rate\":%.6g,\n",    descent_rate);
        printf("    \"descent_dt\":%.6g,\n",      descent_dt);
        printf("    \"format\":\"json\",\n");
        printf("    \"launch_altitude\":%.6g,\n", alt0);
        printf("    \"launch_datetime\":\"%s\",\n", launch_dt);
        printf("    \"launch_latitude\":%.6g,\n", lat0);
        printf("    \"launch_longitude\":%.6g,\n", lng0);
        printf("    \"profile\":\"%s\",\n", profile);
        printf("    \"version\":1\n");
        printf("  },\n");

        
        printf("  \"warnings\":{");
        if (warn.altitude_too_high > 0)
            printf("\n    \"altitude_too_high\":{"
                   "\"count\":%d,"
                   "\"description\":\"The altitude went too high, above the max forecast wind. "
                   "Wind data will be unreliable\"}\n  ",
                   warn.altitude_too_high);
        printf("}\n}\n");
    }

    solver_result_free(result);
    dataset_close(&ds);
    if (has_elevation) elevation_close(&el);
    return 0;
}
