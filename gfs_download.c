// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>

/* ── Config ─────────────────────────────────────────────────────────────── */

#define DEFAULT_DATASET_DIR   "./dataset"
#define DEFAULT_WORK_DIR      "/tmp/gfs_work"
#define DEFAULT_KEEP_DAYS     2
#define NOAA_BASE  "https://nomads.ncep.noaa.gov/pub/data/nccf/com/gfs/prod"

#define DS_HOURS    65
#define DS_LEVELS   47
#define DS_VARS      3
#define DS_LAT     361
#define DS_LON     720
#define DS_ELEMS   ((size_t)DS_HOURS * DS_LEVELS * DS_VARS * DS_LAT * DS_LON)
#define DS_SIZE    (DS_ELEMS * sizeof(float))

#define VAR_HGT  0
#define VAR_U    1
#define VAR_V    2

static const int ALL_LEVELS[DS_LEVELS] = {
    1000,975,950,925,900,875,850,825,800,775,750,725,700,675,650,625,
    600,575,550,525,500,475,450,425,400,375,350,325,300,275,250,225,
    200,175,150,125,100,70,50,30,20,10,7,5,3,2,1
};

#define DS_IDX(h,l,v,lat,lon) \
    (((size_t)(h)*DS_LEVELS*DS_VARS*DS_LAT*DS_LON) + \
     ((size_t)(l)*DS_VARS*DS_LAT*DS_LON) + \
     ((size_t)(v)*DS_LAT*DS_LON) + \
     ((size_t)(lat)*DS_LON) + (size_t)(lon))

/* ── Logging ─────────────────────────────────────────────────────────────── */

static void logmsg(const char *fmt, ...)
{
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    char ts[20];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    fprintf(stderr, "[%s] ", ts);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* ── curl subprocess ─────────────────────────────────────────────────────── */

static int curl_download(const char *url, const char *outfile)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -fsL --retry 2 --retry-delay 5 "
        "--connect-timeout 30 --max-time 300 "
        "-o '%s' '%s' 2>/dev/null", outfile, url);
    if (system(cmd) != 0) { remove(outfile); return -1; }
    struct stat st;
    if (stat(outfile, &st) != 0 || st.st_size < 10000) { remove(outfile); return -1; }
    return 0;
}

static int curl_head_exists(const char *url)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -fsLI --connect-timeout 15 --max-time 20 '%s' >/dev/null 2>&1", url);
    return (system(cmd) == 0);
}

/* ── GRIB2 Bit Reader ────────────────────────────────────────────────────── */

static uint32_t read_bits(const uint8_t *data, size_t data_len,
                           size_t bit_pos, int nbits)
{
    if (nbits == 0) return 0;
    size_t byte_off = bit_pos >> 3;
    int    bit_off  = (int)(bit_pos & 7);
    int    need     = nbits + bit_off;
    int    nbytes   = (need + 7) / 8;
    uint32_t raw = 0;
    for (int b = 0; b < nbytes; b++) {
        raw = (raw << 8) | (byte_off + b < data_len ? data[byte_off + b] : 0);
    }
    raw >>= (nbytes * 8 - need);
    return raw & ((1u << nbits) - 1u);
}

/* ── GRIB2 Big-Endian Readers ────────────────────────────────────────────── */

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0]<<8)|p[1];
}
static uint64_t be64(const uint8_t *p) {
    uint64_t v=0; for(int i=0;i<8;i++) v=(v<<8)|p[i]; return v;
}
static int wmo16(const uint8_t *p) {   /* WMO sign-magnitude 16-bit */
    uint16_t r = be16(p);
    return (r >> 15) ? -(int)(r & 0x7FFF) : (int)(r & 0x7FFF);
}
static float be32f(const uint8_t *p) { /* IEEE 754 big-endian float */
    uint32_t u = be32(p); float f; memcpy(&f, &u, 4); return f;
}

/* ── GRIB2 Simple Packing (Template 5.0) ────────────────────────────────── */

static float *unpack_simple(const uint8_t *data, size_t data_len,
                              size_t npts, float R, int E, int D, int bits)
{
    if (bits < 0 || bits > 31) return NULL;
    float *out = malloc(npts * sizeof(float));
    if (!out) return NULL;
    double scale = pow(2.0, E) / pow(10.0, D);
    if (bits == 0) {
        for (size_t i = 0; i < npts; i++) out[i] = R;
        return out;
    }
    size_t bp = 0;
    uint32_t mask = (1u << bits) - 1u;
    for (size_t i = 0; i < npts; i++) {
        size_t byte_off = bp >> 3;
        int    bit_off  = (int)(bp & 7);
        int    need     = bits + bit_off;
        int    nb       = (need + 7) / 8;
        uint32_t raw = 0;
        for (int b = 0; b < nb; b++)
            raw = (raw << 8) | (byte_off+b < data_len ? data[byte_off+b] : 0);
        raw >>= (nb * 8 - need);
        out[i] = (float)(R + (double)(raw & mask) * scale);
        bp += bits;
    }
    return out;
}

/* ── GRIB2 Complex Packing (Template 5.2 and 5.3) ───────────────────────── */
/*
 * WMO FM92 GRIB2 Section 5 Template 2/3 decode:
 *
 * Section 5 layout (offsets from section start byte 0):
 *   0-3:  section length
 *   4:    section number (5)
 *   5-8:  number of packed values
 *   9-10: data representation template (2 or 3)
 *   11-14: reference value R (IEEE 754 float32 big-endian)
 *   15-16: binary scale factor E (WMO sign-magnitude)
 *   17-18: decimal scale factor D (WMO sign-magnitude)
 *   19:   bits per value (for group reference values)
 *   20:   type of original field (0=float, 1=int)
 *   21:   group splitting method (0=row-by-row, 1=general)
 *   22:   missing value management used
 *   23-26: primary missing value substitute
 *   27-30: secondary missing value substitute
 *   31-34: number of groups Ng
 *   35:   reference for group widths
 *   36:   bits for group widths
 *   37-40: reference for group lengths
 *   41:   length increment for group lengths
 *   42-45: true length of last group
 *   46:   bits for scaled group lengths
 *   -- Template 5.3 only --
 *   47:   order of spatial differencing (1 or 2)
 *   48:   extra octets (number of bytes for spatial diff values)
 *
 * Data section (Section 7) layout:
 *   -- Template 5.3 only: spatial differencing initial values --
 *   [Ng * bits] group reference values (simple packed, MSB first)
 *   [Ng * grw_bits] group widths (simple packed)
 *   [Ng * grl_bits] scaled group lengths (simple packed) for first Ng-1 groups
 *   For each group g (0..Ng-1):
 *     [grp_lens[g] * grp_widths[g]] packed data values
 *
 * Final value = (R + (grp_ref[g] + X) * 2^E) / 10^D
 */

static float *unpack_complex(const uint8_t *sec5, size_t sec5_len,
                              const uint8_t *sec7, size_t sec7_len,
                              size_t npts, int tmpl)
{
    if (sec5_len < 47) return NULL;

    float    R        = be32f(sec5 + 11);
    int      E        = wmo16(sec5 + 15);
    int      D        = wmo16(sec5 + 17);
    int      bits     = sec5[19];       /* bits per group reference value */
    int      mv_mgmt  = sec5[22];
    uint32_t Ng       = be32(sec5 + 31);
    int      grw_ref  = sec5[35];
    int      grw_bits = sec5[36];
    uint32_t grl_ref  = be32(sec5 + 37);
    int      grl_inc  = sec5[41];
    uint32_t grl_last = be32(sec5 + 42);
    int      grl_bits = sec5[46];

    if (Ng == 0 || Ng > 500000) return NULL;

    double bscale = pow(2.0, E);
    double dscale = 1.0 / pow(10.0, D);

    /* Template 3: spatial differencing parameters */
    int      spd_order = 0;
    int      spd_bytes = 0;  /* nbitsd/8 */
    if (tmpl == 3 && sec5_len >= 49) {
        spd_order = sec5[47];
        spd_bytes = sec5[48];   /* number of octets for each extra value */
    }
    int nbitsd = spd_bytes * 8;

    /* Allocate integer working array */
    int64_t *ifld = calloc(npts, sizeof(int64_t));
    if (!ifld) return NULL;

    /* Spatial differencing header (Section 7 start) */
    size_t iofst = 0;   /* bit offset in sec7 */
    int64_t ival1 = 0, ival2 = 0, minsd = 0;
    if (tmpl == 3 && nbitsd > 0) {
        /* ival1: nbitsd bits unsigned */
        ival1 = (int64_t)read_bits(sec7, sec7_len, iofst, nbitsd);
        iofst += nbitsd;
        if (spd_order == 2) {
            ival2 = (int64_t)read_bits(sec7, sec7_len, iofst, nbitsd);
            iofst += nbitsd;
        }
        /* minsd: 1 sign bit + (nbitsd-1) magnitude bits */
        int isign = (int)read_bits(sec7, sec7_len, iofst, 1);
        iofst += 1;
        minsd = (int64_t)read_bits(sec7, sec7_len, iofst, nbitsd - 1);
        iofst += nbitsd - 1;
        if (isign) minsd = -minsd;
    }

    /* Step 1: group reference values (bits each), pad to byte */
    uint32_t *grp_ref = malloc(Ng * sizeof(uint32_t));
    if (!grp_ref) { free(ifld); return NULL; }
    if (bits > 0) {
        for (uint32_t g = 0; g < Ng; g++)
            grp_ref[g] = read_bits(sec7, sec7_len, iofst + (size_t)g * bits, bits);
        iofst += (size_t)Ng * bits;
        if (iofst & 7) iofst = (iofst | 7) + 1;   /* pad to byte */
    } else {
        for (uint32_t g = 0; g < Ng; g++) grp_ref[g] = 0;
    }

    /* Step 2: group widths (grw_bits each), pad to byte */
    uint32_t *grp_width = malloc(Ng * sizeof(uint32_t));
    if (!grp_width) { free(grp_ref); free(ifld); return NULL; }
    if (grw_bits > 0) {
        for (uint32_t g = 0; g < Ng; g++)
            grp_width[g] = read_bits(sec7, sec7_len, iofst + (size_t)g * grw_bits, grw_bits)
                           + grw_ref;
        iofst += (size_t)Ng * grw_bits;
        if (iofst & 7) iofst = (iofst | 7) + 1;
    } else {
        for (uint32_t g = 0; g < Ng; g++) grp_width[g] = grw_ref;
    }

    /* Step 3: group lengths (grl_bits each for ALL Ng), pad to byte */
    uint32_t *grp_len = malloc(Ng * sizeof(uint32_t));
    if (!grp_len) { free(grp_width); free(grp_ref); free(ifld); return NULL; }
    if (grl_bits > 0) {
        for (uint32_t g = 0; g < Ng; g++)
            grp_len[g] = read_bits(sec7, sec7_len, iofst + (size_t)g * grl_bits, grl_bits)
                         * grl_inc + grl_ref;
        iofst += (size_t)Ng * grl_bits;
        if (iofst & 7) iofst = (iofst | 7) + 1;
    } else {
        for (uint32_t g = 0; g < Ng; g++) grp_len[g] = grl_ref;
    }
    grp_len[Ng - 1] = grl_last;   /* last group uses true length from sec5 */

    /* Step 4: decode group values into ifld[] as integers */
    size_t out_idx = 0;
    for (uint32_t g = 0; g < Ng && out_idx < npts; g++) {
        int      w   = grp_width[g];
        int64_t  ref = (int64_t)grp_ref[g];
        uint32_t len = grp_len[g];
        uint32_t max_val = (w > 0 && w < 32) ? ((1u << w) - 1u) : 0xFFFFFFFFu;

        for (uint32_t k = 0; k < len && out_idx < npts; k++, out_idx++) {
            if (w == 0) {
                ifld[out_idx] = ref;
            } else {
                uint32_t X = read_bits(sec7, sec7_len, iofst, w);
                iofst += w;
                /* Missing value check */
                if (mv_mgmt > 0 && X == max_val) {
                    ifld[out_idx] = 0;   /* will be overwritten below */
                    continue;
                }
                if (mv_mgmt == 2 && X == max_val - 1) {
                    ifld[out_idx] = 0;
                    continue;
                }
                ifld[out_idx] = ref + (int64_t)X;
            }
        }
    }

    /* Template 3: undo spatial differencing (in integer domain) */
    if (tmpl == 3 && spd_order > 0) {
        if (spd_order == 1) {
            ifld[0] = ival1;
            for (size_t n = 1; n < npts; n++)
                ifld[n] = ifld[n] + minsd + ifld[n-1];
        } else {   /* order 2 */
            ifld[0] = ival1;
            ifld[1] = ival2;
            for (size_t n = 2; n < npts; n++)
                ifld[n] = ifld[n] + minsd + 2*ifld[n-1] - ifld[n-2];
        }
    }

    /* Final scaling: fld[n] = (ifld[n] * bscale + R) * dscale */
    float *out = malloc(npts * sizeof(float));
    if (!out) { free(ifld); free(grp_ref); free(grp_width); free(grp_len); return NULL; }
    for (size_t n = 0; n < npts; n++)
        out[n] = (float)(((double)ifld[n] * bscale + (double)R) * dscale);

    free(ifld); free(grp_ref); free(grp_width); free(grp_len);
    return out;
}

/* ── GRIB2 File Processor ───────────────────────────────────────────────── */

static int process_grib2_file(const char *path, int hour_idx, int out_fd)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    size_t flen = ftell(f);
    rewind(f);
    uint8_t *fdata = malloc(flen);
    if (!fdata) { fclose(f); return -1; }
    if (fread(fdata, 1, flen, f) != flen) { free(fdata); fclose(f); return -1; }
    fclose(f);

    int found = 0;
    size_t pos = 0;

    while (pos < flen) {
        /* Find GRIB magic */
        while (pos + 4 <= flen &&
               !(fdata[pos]=='G' && fdata[pos+1]=='R' &&
                 fdata[pos+2]=='I' && fdata[pos+3]=='B'))
            pos++;
        if (pos + 16 > flen) break;
        if (fdata[pos+7] != 2) { pos++; continue; }

        uint64_t total_len = be64(fdata + pos + 8);
        if (pos + total_len > flen) break;
        size_t msg_end = pos + total_len;

        int disc=-1, cat=-1, num=-1, ltype=-1, level=-1;
        int ni=0, nj=0, j_pos=0;
        const uint8_t *sec5=NULL, *sec7=NULL;
        size_t sec5_len=0, sec7_len=0;

        disc = fdata[pos + 6];  /* Discipline is in the GRIB Indicator (Section 0) */

        size_t p = pos + 16;
        while (p < msg_end - 4) {
            if (memcmp(fdata+p, "7777", 4) == 0) break;
            if (p + 4 > msg_end) break;
            uint32_t slen = be32(fdata+p);
            if (slen < 5 || p + slen > msg_end) break;
            int snum = fdata[p+4];

            switch (snum) {
            case 3:
                ni    = (int)be32(fdata+p+30);
                nj    = (int)be32(fdata+p+34);
                j_pos = (fdata[p+71] >> 1) & 1;
                break;
            case 4:
                cat   = fdata[p+9];
                num   = fdata[p+10];
                ltype = fdata[p+22];
                level = (int)(be32(fdata+p+24) / 100);
                break;
            case 5:
                sec5     = fdata + p;
                sec5_len = slen;
                break;
            case 7:
                sec7     = fdata + p + 5;
                sec7_len = slen - 5;
                break;
            }
            p += slen;
        }

        pos = msg_end;

        /* Filter: only atmospheric pressure-level fields */
        if (disc != 0 || ltype != 100) continue;
        if (!sec5 || !sec7)            continue;

        /* Variable */
        int var_idx = -1;
        if      (cat==3 && num==5) var_idx = VAR_HGT;
        else if (cat==2 && num==2) var_idx = VAR_U;
        else if (cat==2 && num==3) var_idx = VAR_V;
        else continue;

        /* Level */
        int lev_idx = -1;
        for (int l = 0; l < DS_LEVELS; l++)
            if (ALL_LEVELS[l] == level) { lev_idx = l; break; }
        if (lev_idx < 0) continue;

        /* Decode */
        int tmpl = (int)be16(sec5 + 9);
        size_t npts = (size_t)ni * nj;
        float *grid = NULL;

        if (tmpl == 0) {
            float  R    = be32f(sec5 + 11);
            int    E    = wmo16(sec5 + 15);
            int    D    = wmo16(sec5 + 17);
            int    bits = sec5[19];
            grid = unpack_simple(sec7, sec7_len, npts, R, E, D, bits);
        } else if (tmpl == 2 || tmpl == 3) {
            grid = unpack_complex(sec5, sec5_len, sec7, sec7_len, npts, tmpl);
        } else {
            continue;  /* JPEG2000 / PNG / AEC not needed for GFS 0.5deg */
        }

        if (!grid) continue;

        /*
         * GFS: lat 90→-90 (j_pos=0), Tawhiri needs lat -90→+90 → flip
         * GFS: lon 0→359.5, Tawhiri: same
         */
        float row[DS_LON];
        for (int lat = 0; lat < DS_LAT && lat < nj; lat++) {
            int gfs_lat = (j_pos == 0) ? (nj - 1 - lat) : lat;
            for (int lon = 0; lon < DS_LON && lon < ni; lon++)
                row[lon] = grid[gfs_lat * ni + lon];
            off_t off = (off_t)DS_IDX(hour_idx, lev_idx, var_idx, lat, 0) * sizeof(float);
            if (pwrite(out_fd, row, DS_LON * sizeof(float), off)
                    != (ssize_t)(DS_LON * sizeof(float))) {
                free(grid); free(fdata); return -1;
            }
        }
        free(grid);
        found++;
    }

    free(fdata);
    return found;
}

/* ── Find latest GFS run ─────────────────────────────────────────────────── */

static int find_latest_run(char run_out[11])
{
    time_t now = time(NULL);
    for (int offset_h = 4; offset_h <= 16; offset_h++) {
        time_t t = now - (time_t)offset_h * 3600;
        struct tm *tm = gmtime(&t);
        int cycle = (tm->tm_hour / 6) * 6;
        char date[9], cyc[4];
        strftime(date, sizeof(date), "%Y%m%d", tm);
        snprintf(cyc, sizeof(cyc), "%02d", cycle);

        /* Verify f000 AND f192 both exist – ensures run is complete */
        char url000[512], url192[512];
        snprintf(url000, sizeof(url000),
            "%s/gfs.%s/%s/atmos/gfs.t%sz.pgrb2.0p50.f000",
            NOAA_BASE, date, cyc, cyc);
        snprintf(url192, sizeof(url192),
            "%s/gfs.%s/%s/atmos/gfs.t%sz.pgrb2.0p50.f192",
            NOAA_BASE, date, cyc, cyc);

        if (curl_head_exists(url000) && curl_head_exists(url192)) {
            snprintf(run_out, 11, "%s%s", date, cyc);
            return 1;
        }
    }
    return 0;
}

/* ── Filesystem helpers ──────────────────────────────────────────────────── */

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp+1; *p; p++) {
        if (*p == '/') { *p='\0'; mkdir(tmp,0755); *p='/'; }
    }
    mkdir(tmp, 0755);
}

static void cleanup_old_datasets(const char *dir, int keep_days)
{
    time_t cutoff = time(NULL) - (time_t)keep_days * 86400;
    struct tm *tm = gmtime(&cutoff);
    char cutoff_str[11];
    strftime(cutoff_str, sizeof(cutoff_str), "%Y%m%d%H", tm);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strlen(ent->d_name) != 10) continue;
        int ok = 1;
        for (int i = 0; i < 10; i++)
            if (ent->d_name[i]<'0'||ent->d_name[i]>'9') { ok=0; break; }
        if (!ok) continue;
        if (strcmp(ent->d_name, cutoff_str) < 0) {
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
            logmsg("Removing old dataset: %s", ent->d_name);
            remove(full);
        }
    }
    closedir(d);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *dataset_dir = getenv("DATASET_DIR") ?: DEFAULT_DATASET_DIR;
    const char *work_dir    = getenv("WORK_DIR")    ?: DEFAULT_WORK_DIR;
    int keep_days = DEFAULT_KEEP_DAYS;
    const char *kd = getenv("KEEP_DAYS");
    if (kd) keep_days = atoi(kd);

    int check_only = 0, force = 0;
    char run[11] = {0};

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--check") == 0) check_only = 1;
        else if (strcmp(argv[i], "--force") == 0) force = 1;
        else if (strlen(argv[i]) == 10) strncpy(run, argv[i], 10);
        else { fprintf(stderr, "Usage: %s [YYYYMMDDHH] [--check] [--force]\n", argv[0]); return 1; }
    }

    if (!run[0]) {
        logmsg("Searching for latest available GFS run...");
        if (!find_latest_run(run)) {
            fprintf(stderr, "No available GFS run found.\n"); return 1;
        }
    }
    logmsg("GFS run: %s", run);

    char date[9], cycle[4];
    memcpy(date, run, 8); date[8] = '\0';
    memcpy(cycle, run+8, 2); cycle[2] = '\0';

    char outfile[512], tmpfile[520];
    snprintf(outfile, sizeof(outfile), "%s/%s", dataset_dir, run);
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", outfile);

    struct stat st;
    if (!force && stat(outfile, &st) == 0 && (size_t)st.st_size == DS_SIZE) {
        logmsg("Dataset %s already complete. Use --force to re-download.", run);
        return 0;
    }
    if (force) { remove(outfile); }

    if (check_only) { logmsg("Dataset %s not found or incomplete.", run); return 1; }

    mkdir_p(dataset_dir);

    logmsg("Creating output file (%.1f GB)...", DS_SIZE / 1e9);
    int fd = open(tmpfile, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { fprintf(stderr, "open: %s\n", strerror(errno)); return 1; }
    if (ftruncate(fd, (off_t)DS_SIZE) < 0) {
        fprintf(stderr, "ftruncate: %s\n", strerror(errno));
        close(fd); remove(tmpfile); return 1;
    }

    char grib_dir[512];
    snprintf(grib_dir, sizeof(grib_dir), "%s/%s", work_dir, run);
    mkdir_p(grib_dir);

    logmsg("Loading and processing %d time steps x 2 files...", DS_HOURS);

    int errors = 0;
    time_t t_start = time(NULL);

    for (int hi = 0; hi < DS_HOURS; hi++) {
        int hour = hi * 3;
        char fff[4];
        snprintf(fff, sizeof(fff), "%03d", hour);

        for (int ft = 0; ft < 2; ft++) {
            const char *ftype = ft ? "pgrb2b" : "pgrb2";
            char grib_path[640];
            snprintf(grib_path, sizeof(grib_path), "%s/%s.f%s", grib_dir, ftype, fff);

            if (stat(grib_path, &st) == 0 && st.st_size > 100000) goto process;

            {
                char url[512];
                snprintf(url, sizeof(url),
                    "%s/gfs.%s/%s/atmos/gfs.t%sz.%s.0p50.f%s",
                    NOAA_BASE, date, cycle, cycle, ftype, fff);
                char tmp_path[660];
                snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", grib_path);
                int ok = 0;
                for (int retry = 0; retry < 3 && !ok; retry++) {
                    if (retry > 0) sleep(10 * retry);
                    if (curl_download(url, tmp_path) == 0) {
                        rename(tmp_path, grib_path);
                        ok = 1;
                    }
                    remove(tmp_path);
                }
                if (!ok) {
                    logmsg("WARNING: Download failed: h=%d %s", hour, ftype);
                    errors++;
                    /* If first hour fails, the run is likely not ready yet */
                    if (hi == 0 && ft == 0) {
                        fprintf(stderr, "Run %s not yet complete. Try later or specify an older run.\n", run);
                        close(fd); remove(tmpfile); rmdir(grib_dir); return 1;
                    }
                    continue;
                }
            }

            process: {
                int n = process_grib2_file(grib_path, hi, fd);
                if (n < 0) { logmsg("WARNING: Processing error: %s", grib_path); errors++; }
                remove(grib_path);
            }
        }

        if ((hi+1) % 5 == 0 || hi == DS_HOURS-1) {
            time_t elapsed = time(NULL) - t_start;
            int rem = (int)((double)elapsed/(hi+1)*(DS_HOURS-hi-1));
            logmsg("[%2d/%d] Hour %3dh  --  ~%d min remaining",
                   hi+1, DS_HOURS, hour, rem/60);
        }
    }

    if (errors > 10) {
        fprintf(stderr, "Too many errors (%d), aborting.\n", errors);
        close(fd); remove(tmpfile); return 1;
    }

    close(fd);
    rename(tmpfile, outfile);
    logmsg("Dataset %s complete: %s", run, outfile);
    rmdir(grib_dir);
    cleanup_old_datasets(dataset_dir, keep_days);
    return 0;
}
