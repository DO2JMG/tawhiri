# tawhiri-c

C port of [Tawhiri](https://github.com/projecthorus/tawhiri) – a high-altitude
balloon (radiosonde) trajectory predictor.

Mirrors the core Python/Cython modules:

| Original (Python/Cython) | This port (C)       | Role                              |
|--------------------------|---------------------|-----------------------------------|
| `dataset.py`             | `dataset.c/h`       | Memory-map NOAA wind dataset      |
| `interpolate.pyx`        | `interpolate.c/h`   | 4-D trilinear wind interpolation  |
| `models.py`              | `models.c/h`        | Physics models + terminators      |
| `solver.pyx`             | `solver.c/h`        | RK4 numerical integration         |
| `api.py`                 | `main.c`            | CLI entry point                   |

---

## Build

```bash
make
```

Requires a C11 compiler and `libm`. Tested with GCC ≥ 9 and Clang ≥ 11 on Linux.

---

## Wind dataset

Tawhiri uses pre-processed NOAA GFS datasets stored as flat binary files of
`float32` values with shape `[65][47][3][361][720]` (≈ 10.5 GB each).

- Download tool: see the original Tawhiri repo (`tawhiri-oldui-gribdownloader`)
- Default location: `/srv/tawhiri-datasets/YYYYMMDDHH`

---

## Usage

```
tawhiri-c -d YYYYMMDDHH --lat L --lng G --alt A \
          --time T --asc R --burst B --desc D   \
          [--dir PATH] [--dt SECS] [--csv]
```

| Option       | Description                                    | Example       |
|--------------|------------------------------------------------|---------------|
| `-d`         | Dataset timestamp                              | `2023012400`  |
| `--lat`      | Launch latitude (decimal degrees)              | `51.5`        |
| `--lng`      | Launch longitude (decimal degrees, −180..360)  | `−0.1`        |
| `--alt`      | Launch altitude (m AMSL)                       | `100`         |
| `--time`     | Launch time (Unix timestamp)                   | `1674518400`  |
| `--asc`      | Ascent rate (m/s)                              | `5.0`         |
| `--burst`    | Burst altitude (m)                             | `30000`       |
| `--desc`     | Sea-level parachute descent rate (m/s)         | `6.0`         |
| `--dir`      | Dataset directory (optional)                   | `/data/gfs`   |
| `--dt`       | Timestep in seconds (default 60)               | `30`          |
| `--csv`      | Output CSV instead of JSON                     |               |

### Example

```bash
# JSON output
./tawhiri-c -d 2023012400 \
    --lat 51.5 --lng -0.1 --alt 100 \
    --time 1674518400 \
    --asc 5.0 --burst 30000 --desc 6.0

# CSV output
./tawhiri-c -d 2023012400 \
    --lat 51.5 --lng -0.1 --alt 100 \
    --time 1674518400 \
    --asc 5.0 --burst 30000 --desc 6.0 \
    --csv
```

### JSON output format

```json
[
  {"stage":1,"time":1674518400.000,"lat":51.500000,"lng":-0.100000,"alt":100.00},
  ...
  {"stage":2,"time":1674534210.000,"lat":52.341000,"lng":1.230000,"alt":0.00}
]
```

---

## Architecture

```
main.c
  └─ solver.c       (RK4 loop, binary-search termination refinement)
       └─ models.c  (constant ascent, drag descent, wind velocity, terminators)
            └─ interpolate.c  (trilinear 4-D interpolation)
                 └─ dataset.c (mmap of binary NOAA wind file)
```

### RK4 integration

Each timestep `dt` (default 60 s):

```
k1 = f(t,        y)
k2 = f(t + dt/2, y + dt/2 · k1)
k3 = f(t + dt/2, y + dt/2 · k2)
k4 = f(t + dt,   y + dt   · k3)

y_next = y + dt/6 · (k1 + 2·k2 + 2·k3 + k4)
```

When the termination criterion fires, the exact crossing point is found
by binary search (tolerance 0.01 in the interpolation parameter).

### Atmosphere model (NASA)

```
alt > 25 000 m : T = −131.21 + 0.00299·alt   P = 2.488·((T+273.1)/216.6)^−11.388
11 000–25 000 m: T = −56.46                   P = 22.65·exp(1.73 − 0.000157·alt)
0–11 000 m     : T = 15.04 − 0.00649·alt      P = 101.29·((T+273.1)/288.08)^5.256
ρ = P / (0.2869·(T+273.1))
```

---

## Limitations / known differences from original Tawhiri

- **Elevation dataset** (ruaumoko) not yet integrated; descent terminates at
  altitude = 0 m (sea level) instead of ground level.
- **Float profile** and **reverse profile** are not exposed via CLI (the model
  infrastructure supports them; extend `main.c`).
- No HTTP API (the original `api.py` layer is not ported).
