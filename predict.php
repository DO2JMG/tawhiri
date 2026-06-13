<?php
/**
 * predict.php – HTTP-Wrapper für tawhiri-c
 * Kompatibel mit der Original-Tawhiri-API von prediction.wettersonde.net
 *
 * GET /?
 *   launch_latitude=51.9343
 *   &launch_longitude=15.218
 *   &launch_altitude=300
 *   &launch_datetime=2026-06-10T23:58:25Z   (ISO-8601 UTC, optional)
 *   &ascent_rate=5
 *   &burst_altitude=32000
 *   &descent_rate=8
 *   &dataset=2026061012                      (optional, sonst neuestes)
 *   &profile=standard_profile               (optional)
 *   &dt=60                                   (optional, Zeitschritt Sekunden)
 */

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');

define('TAWHIRI_BIN',     __DIR__ . '/tawhiri-c');
define('DATASET_DIR',     __DIR__ . '/dataset');
define('MAX_RUNTIME_SEC', 30);

function json_error(int $code, string $msg): never {
    http_response_code($code);
    echo json_encode(['error' => $msg]);
    exit;
}

if (!is_executable(TAWHIRI_BIN)) {
    json_error(500, 'tawhiri-c binary not found or not executable');
}

$required = ['launch_latitude', 'launch_longitude', 'launch_altitude',
             'ascent_rate', 'burst_altitude', 'descent_rate'];
foreach ($required as $p) {
    if (!isset($_GET[$p])) {
        json_error(400, "Missing parameter: $p");
    }
}

function float_param(string $name, float $min, float $max): float {
    $v = filter_input(INPUT_GET, $name, FILTER_VALIDATE_FLOAT);
    if ($v === false || $v === null) {
        json_error(400, "Invalid parameter: $name");
    }
    if ($v < $min || $v > $max) {
        json_error(400, "Parameter $name out of range [$min, $max]");
    }
    return $v;
}

$lat   = float_param('launch_latitude',   -90,    90);
$lng   = float_param('launch_longitude', -180,   360);
$alt   = float_param('launch_altitude',     0, 10000);
$asc   = float_param('ascent_rate',        0.1,   50);
$burst = float_param('burst_altitude',    1000, 50000);
$desc  = float_param('descent_rate',       0.1,   50);
$dt    = isset($_GET['dt']) ? float_param('dt', 1, 300) : 60;

$allowed_profiles = ['standard_profile', 'reverse_profile'];
$profile = $_GET['profile'] ?? 'standard_profile';
if (!in_array($profile, $allowed_profiles, true)) {
    json_error(400, 'Invalid profile. Use: ' . implode(' | ', $allowed_profiles));
}

/* Dataset bestimmen */
if (isset($_GET['dataset'])) {
    $ds = $_GET['dataset'];
    if (!preg_match('/^\d{10}$/', $ds)) {
        json_error(400, 'Invalid dataset format. Use YYYYMMDDHH');
    }
    if (!file_exists(DATASET_DIR . '/' . $ds)) {
        json_error(404, "Dataset $ds not found");
    }
} else {
    $datasets = glob(DATASET_DIR . '/[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]');
    if (empty($datasets)) {
        json_error(500, 'No datasets found in ' . DATASET_DIR);
    }
    sort($datasets);
    $ds = basename(end($datasets));
}

/* launch_datetime: ISO-8601 UTC → Unix-Timestamp */
$time_arg = '';
if (!empty($_GET['launch_datetime'])) {
    $dt_str = trim($_GET['launch_datetime']);
    try {
        $dto = new DateTimeImmutable(
            str_replace('Z', '+00:00', $dt_str),
            new DateTimeZone('UTC')
        );
        $ts = $dto->getTimestamp();
        $time_arg = '--time ' . escapeshellarg((string)$ts);
    } catch (Exception $e) {
        json_error(400, 'Invalid launch_datetime. Use ISO-8601 UTC, e.g. 2026-06-10T12:00:00Z');
    }
}

/* Kommando zusammenbauen */
$parts = [
    escapeshellarg(TAWHIRI_BIN),
    '-d',        escapeshellarg($ds),
    '--dir',     escapeshellarg(DATASET_DIR),
    '--lat',     escapeshellarg((string)$lat),
    '--lng',     escapeshellarg((string)$lng),
    '--alt',     escapeshellarg((string)$alt),
    '--asc',     escapeshellarg((string)$asc),
    '--burst',   escapeshellarg((string)$burst),
    '--desc',    escapeshellarg((string)$desc),
    '--dt',      escapeshellarg((string)$dt),
    '--profile', escapeshellarg($profile),
];
if ($time_arg) $parts[] = $time_arg;
$parts[] = '2>/dev/null';
$cmd = implode(' ', $parts);

/* Ausführen mit Timeout */
$proc = proc_open($cmd, [
    0 => ['pipe', 'r'],
    1 => ['pipe', 'w'],
    2 => ['pipe', 'w'],
], $pipes);

if (!is_resource($proc)) {
    json_error(500, 'Failed to start tawhiri-c');
}

fclose($pipes[0]);
stream_set_blocking($pipes[1], false);
stream_set_blocking($pipes[2], false);

$output = '';
$stderr = '';
$deadline = time() + MAX_RUNTIME_SEC;

while (true) {
    $read = [$pipes[1], $pipes[2]];
    $w = $e = null;
    $remaining = $deadline - time();
    if ($remaining <= 0) {
        proc_terminate($proc);
        json_error(504, 'tawhiri-c timed out after ' . MAX_RUNTIME_SEC . 's');
    }
    if (stream_select($read, $w, $e, $remaining) === false) break;
    foreach ($read as $s) {
        $data = fread($s, 8192);
        if ($data !== false && $data !== '') {
            if ($s === $pipes[1]) $output .= $data;
            else                  $stderr .= $data;
        }
    }
    if (feof($pipes[1]) && feof($pipes[2])) break;
}

fclose($pipes[1]);
fclose($pipes[2]);
$exit_code = proc_close($proc);

if ($exit_code !== 0 || empty($output)) {
    json_error(500, 'tawhiri-c failed: ' . (trim($stderr) ?: 'no output'));
}

$result = json_decode($output);
if ($result === null) {
    json_error(500, 'Invalid JSON from tawhiri-c: ' . substr($output, 0, 200));
}

echo json_encode($result, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE);
