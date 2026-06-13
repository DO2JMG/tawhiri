
// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#include "httpd.h"
#include "models.h"
#include "solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *dst, const char *src, size_t dstlen)
{
    size_t i = 0;
    while (*src && i < dstlen - 1) {
        if (*src == '%' && src[1] && src[2]) {
            int hi = hex_val(src[1]), lo = hex_val(src[2]);
            if (hi >= 0 && lo >= 0) {
                dst[i++] = (char)(hi * 16 + lo);
                src += 3;
                continue;
            }
        }
        if (*src == '+') { dst[i++] = ' '; src++; continue; }
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

#define MAX_PARAMS 32

typedef struct { char key[64]; char val[256]; } Param;

static int parse_query(const char *qs, Param params[], int max)
{
    int n = 0;
    char buf[4096];
    strncpy(buf, qs, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(buf, "&", &saveptr);
    while (tok && n < max) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            url_decode(params[n].key, tok,    sizeof(params[n].key));
            url_decode(params[n].val, eq + 1, sizeof(params[n].val));
            n++;
        }
        tok = strtok_r(NULL, "&", &saveptr);
    }
    return n;
}

static const char *param_get(const Param *params, int n, const char *key)
{
    for (int i = 0; i < n; i++)
        if (strcmp(params[i].key, key) == 0)
            return params[i].val;
    return NULL;
}

static int param_double(const Param *p, int n, const char *key,
                        double *out, double min, double max)
{
    const char *v = param_get(p, n, key);
    if (!v) return 0;
    char *end;
    *out = strtod(v, &end);
    if (end == v) return 0;
    if (*out < min || *out > max) return 0;
    return 1;
}

static time_t parse_iso8601(const char *s)
{
    int Y, M, D, h, m, sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6)
        return -1;

    
    int a  = (14 - M) / 12;
    int yr = Y + 4800 - a;
    int mo = M + 12*a - 3;
    long jdn = D + (153*mo+2)/5 + 365L*yr + yr/4 - yr/100 + yr/400 - 32045;
    long unix_epoch_jdn = 2440588L;
    return (time_t)((jdn - unix_epoch_jdn) * 86400L + h*3600L + m*60L + sec);
}

static void send_str(int fd, const char *s)
{
    size_t len = strlen(s);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, s + sent, len - sent);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static void send_fmt(int fd, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send_str(fd, buf);
}

static void http_error(int fd, int code, const char *msg)
{
    const char *status = (code == 400) ? "Bad Request"
                       : (code == 404) ? "Not Found"
                       : (code == 405) ? "Method Not Allowed"
                       :                 "Internal Server Error";
    char body[512];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", msg);
    send_fmt(fd,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        code, status, strlen(body), body);
}

static void fmt_iso8601(double t, char *buf, size_t len)
{
    time_t tt = (time_t)t;
    struct tm *utc = gmtime(&tt);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", utc);
}

static void run_prediction(int fd, const ServerConfig *cfg,
                           const Param *params, int np)
{
    
    double lat, lng, alt, asc, burst, desc, dt_req;

    if (!param_double(params, np, "launch_latitude",   &lat,   -90,    90))
        { http_error(fd, 400, "Missing or invalid launch_latitude");  return; }
    if (!param_double(params, np, "launch_longitude",  &lng,  -180,   360))
        { http_error(fd, 400, "Missing or invalid launch_longitude"); return; }
    if (!param_double(params, np, "launch_altitude",   &alt,    0,  40000))
        { http_error(fd, 400, "Missing or invalid launch_altitude");  return; }
    if (!param_double(params, np, "ascent_rate",       &asc,   0.1,    50))
        { http_error(fd, 400, "Missing or invalid ascent_rate");      return; }
    if (!param_double(params, np, "burst_altitude",    &burst, 1000, 50000))
        { http_error(fd, 400, "Missing or invalid burst_altitude");   return; }
    if (!param_double(params, np, "descent_rate",      &desc,  0.1,    50))
        { http_error(fd, 400, "Missing or invalid descent_rate");     return; }

    dt_req = cfg->dt;
    param_double(params, np, "dt", &dt_req, 1, 300);

    
    const char *profile_str = param_get(params, np, "profile");
    int is_reverse = (profile_str && strcmp(profile_str, "reverse_profile") == 0);

    
    double t0 = (double)cfg->wind_ds->ds_time;
    const char *ldt = param_get(params, np, "launch_datetime");
    if (ldt && *ldt) {
        time_t ts = parse_iso8601(ldt);
        if (ts < 0) { http_error(fd, 400, "Invalid launch_datetime"); return; }
        t0 = (double)ts;
    }

    
    double lng_norm = fmod(lng + 360.0, 360.0);

    
    WarningCounts warn = { 0 };
    ConstAscentCtx asc_ctx  = { asc };
    WindCtx        wind_ctx = { cfg->wind_ds, &warn, cfg->wind_ds->ds_time };

    LinearModelCtx up_model = {
        .n    = 2,
        .fns  = { model_constant_ascent, model_wind_velocity },
        .ctxs = { &asc_ctx, &wind_ctx }
    };

    ElevationCtx elev_ctx = { cfg->has_elev ? cfg->elev_ds : NULL };
    TerminatorFn land_term = cfg->has_elev ? term_elevation : term_sea_level;
    void        *land_ctx  = cfg->has_elev ? (void *)&elev_ctx : NULL;

    FlightStage stages[2];
    int n_stages;
    const char *stage_names[2];

    BurstCtx       burst_ctx = { burst };
    DragDescentCtx drag_ctx;

    if (!is_reverse) {
        make_drag_ctx(&drag_ctx, desc);
        LinearModelCtx down_model = {
            .n    = 2,
            .fns  = { model_drag_descent, model_wind_velocity },
            .ctxs = { &drag_ctx, &wind_ctx }
        };
        static LinearModelCtx down_static;
        down_static = down_model;

        stages[0] = (FlightStage){ model_linear, &up_model,    term_burst, &burst_ctx, 0 };
        stages[1] = (FlightStage){ model_linear, &down_static, land_term,  land_ctx,   0 };
        n_stages       = 2;
        stage_names[0] = "ascent";
        stage_names[1] = "descent";
    } else {
        static LinearModelCtx rev_model;
        rev_model = (LinearModelCtx){
            .n    = 2,
            .fns  = { model_constant_ascent, model_wind_velocity },
            .ctxs = { &asc_ctx, &wind_ctx }
        };
        MinTimeCtx min_time_ctx = { (double)cfg->wind_ds->ds_time };
        static TerminatorFn rev_fns[2];
        static void *rev_ctxs[2];
        static MinTimeCtx min_ctx_s;
        min_ctx_s = min_time_ctx;
        rev_fns[0]  = land_term; rev_ctxs[0] = land_ctx;
        rev_fns[1]  = term_min_time; rev_ctxs[1] = &min_ctx_s;
        static AnyTermCtx any_ctx;
        any_ctx = (AnyTermCtx){ 2, rev_fns, rev_ctxs };
        stages[0] = (FlightStage){ model_linear, &rev_model, term_any, &any_ctx, 1 };
        n_stages       = 1;
        stage_names[0] = "reverse_ascent";
    }

    SolverResult *result = solve(t0, lat, lng_norm, alt, stages, n_stages, dt_req);
    if (!result) { http_error(fd, 500, "Solver allocation failure"); return; }

    
    char *json = NULL;
    size_t jlen = 0;
    FILE *jf = open_memstream(&json, &jlen);
    if (!jf) { solver_result_free(result); http_error(fd, 500, "open_memstream failed"); return; }

    
    char ts_now[40];
    {
        struct timespec tp;
        clock_gettime(CLOCK_REALTIME, &tp);
        time_t now = tp.tv_sec;
        struct tm *utc = gmtime(&now);
        char base[32];
        strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", utc);
        snprintf(ts_now, sizeof(ts_now), "%s.%06dZ", base, (int)(tp.tv_nsec/1000));
    }
    char ds_dt[32], launch_dt[32];
    fmt_iso8601((double)cfg->wind_ds->ds_time, ds_dt, sizeof(ds_dt));
    fmt_iso8601(t0, launch_dt, sizeof(launch_dt));

    fprintf(jf, "{\n");
    fprintf(jf, "  \"metadata\":{\n");
    fprintf(jf, "    \"start_datetime\":\"%s\",\n", ts_now);
    fprintf(jf, "    \"complete_datetime\":\"%s\"\n", ts_now);
    fprintf(jf, "  },\n");
    fprintf(jf, "  \"prediction\":[\n");

    for (int s = 0; s < result->n_stages; s++) {
        Stage *st = &result->stages[s];
        fprintf(jf, "    {\"stage\":\"%s\",\"trajectory\":[\n", stage_names[s]);
        for (size_t i = 0; i < st->n; i++) {
            Point *p = &st->pts[i];
            char ts[32];
            fmt_iso8601(p->t, ts, sizeof(ts));
            double lo = p->lng > 180.0 ? p->lng - 360.0 : p->lng;
            fprintf(jf,
                "      {\"altitude\":%.6g,\"datetime\":\"%s\","
                "\"latitude\":%.6g,\"longitude\":%.6g}%s\n",
                p->alt, ts, p->lat, lo,
                (i == st->n - 1) ? "" : ",");
        }
        fprintf(jf, "    ]}%s\n", (s == result->n_stages - 1) ? "" : ",");
    }

    fprintf(jf, "  ],\n");
    fprintf(jf, "  \"request\":{\n");
    fprintf(jf, "    \"ascent_rate\":%.6g,\n",      asc);
    fprintf(jf, "    \"burst_altitude\":%.6g,\n",   burst);
    fprintf(jf, "    \"dataset\":\"%s\",\n",         ds_dt);
    fprintf(jf, "    \"descent_rate\":%.6g,\n",     desc);
    fprintf(jf, "    \"format\":\"json\",\n");
    fprintf(jf, "    \"launch_altitude\":%.6g,\n",  alt);
    fprintf(jf, "    \"launch_datetime\":\"%s\",\n", launch_dt);
    fprintf(jf, "    \"launch_latitude\":%.6g,\n",  lat);
    fprintf(jf, "    \"launch_longitude\":%.6g,\n", lng);
    fprintf(jf, "    \"profile\":\"%s\",\n",
            is_reverse ? "reverse_profile" : "standard_profile");
    fprintf(jf, "    \"version\":1\n");
    fprintf(jf, "  },\n");
    fprintf(jf, "  \"warnings\":{");
    if (warn.altitude_too_high > 0)
        fprintf(jf,
            "\n    \"altitude_too_high\":{"
            "\"count\":%d,"
            "\"description\":\"Altitude exceeded max forecast wind data\"}\n  ",
            warn.altitude_too_high);
    fprintf(jf, "}\n}\n");
    fclose(jf);

    solver_result_free(result);

    
    send_fmt(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        jlen);
    send_str(fd, json);
    free(json);
}

static void handle_request(int fd, const ServerConfig *cfg)
{
    
    char req[4096] = {0};
    ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    
    char method[8], path[2048];
    if (sscanf(req, "%7s %2047s", method, path) != 2) return;

    
    if (strcmp(method, "GET") != 0) {
        http_error(fd, 405, "Method Not Allowed");
        return;
    }

    
    if (strcmp(path, "/health") == 0) {
        const char *body = "{\"status\":\"ok\"}";
        send_fmt(fd,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(body), body);
        return;
    }

    
    char *qs = strchr(path, '?');
    if (!qs || *(qs + 1) == '\0') {
        http_error(fd, 400,
            "Missing parameters. Required: launch_latitude, launch_longitude, "
            "launch_altitude, ascent_rate, burst_altitude, descent_rate");
        return;
    }
    qs++; 

    Param params[MAX_PARAMS];
    int np = parse_query(qs, params, MAX_PARAMS);

    run_prediction(fd, cfg, params, np);
}

#include <pthread.h>

#define POOL_THREADS   16      
#define QUEUE_SIZE     64      

typedef struct {
    int fds[QUEUE_SIZE];
    int head, tail, count;
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    int             stop;
    const ServerConfig *cfg;
} WorkQueue;

static void wq_push(WorkQueue *q, int fd)
{
    pthread_mutex_lock(&q->mu);
    while (q->count == QUEUE_SIZE && !q->stop)
        pthread_cond_wait(&q->not_full, &q->mu);
    if (!q->stop) {
        q->fds[q->tail] = fd;
        q->tail = (q->tail + 1) % QUEUE_SIZE;
        q->count++;
        pthread_cond_signal(&q->not_empty);
    } else {
        close(fd);   
    }
    pthread_mutex_unlock(&q->mu);
}

static int wq_pop(WorkQueue *q)
{
    pthread_mutex_lock(&q->mu);
    while (q->count == 0 && !q->stop)
        pthread_cond_wait(&q->not_empty, &q->mu);
    if (q->stop && q->count == 0) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
    int fd = q->fds[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mu);
    return fd;
}

static void *worker_thread(void *arg)
{
    WorkQueue *q = (WorkQueue *)arg;
    for (;;) {
        int fd = wq_pop(q);
        if (fd < 0) break;   
        handle_request(fd, q->cfg);
        close(fd);
    }
    return NULL;
}

static volatile int running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

int httpd_run(const ServerConfig *cfg)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)cfg->port);
    addr.sin_addr.s_addr = inet_addr(cfg->bind_addr ? cfg->bind_addr : "0.0.0.0");

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv); return -1;
    }
    if (listen(srv, 128) < 0) {
        perror("listen"); close(srv); return -1;
    }

    
    WorkQueue q;
    memset(&q, 0, sizeof(q));
    q.cfg = cfg;
    pthread_mutex_init(&q.mu, NULL);
    pthread_cond_init(&q.not_empty, NULL);
    pthread_cond_init(&q.not_full,  NULL);

    pthread_t threads[POOL_THREADS];
    for (int i = 0; i < POOL_THREADS; i++)
        pthread_create(&threads[i], NULL, worker_thread, &q);

    fprintf(stderr,
        "tawhiri-c HTTP server listening on %s:%d  "
        "(%d worker threads, queue depth %d)\n",
        cfg->bind_addr ? cfg->bind_addr : "0.0.0.0",
        cfg->port, POOL_THREADS, QUEUE_SIZE);
    fprintf(stderr,
        "Example: http://localhost:%d/?launch_latitude=51.5"
        "&launch_longitude=8.1&launch_altitude=100"
        "&ascent_rate=5&burst_altitude=30000&descent_rate=6\n",
        cfg->port);

    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        if (select(srv + 1, &rfds, NULL, NULL, &tv) <= 0) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(srv, (struct sockaddr *)&client_addr, &client_len);
        if (client < 0) continue;

        int one = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        struct timeval rtv = { .tv_sec = 10, .tv_usec = 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &rtv, sizeof(rtv));

        wq_push(&q, client);   
    }

    
    pthread_mutex_lock(&q.mu);
    q.stop = 1;
    pthread_cond_broadcast(&q.not_empty);
    pthread_mutex_unlock(&q.mu);

    for (int i = 0; i < POOL_THREADS; i++)
        pthread_join(threads[i], NULL);

    pthread_mutex_destroy(&q.mu);
    pthread_cond_destroy(&q.not_empty);
    pthread_cond_destroy(&q.not_full);

    close(srv);
    fprintf(stderr, "\ntawhiri-c server stopped.\n");
    return 0;
}
