# tawhiri

[Tawhiri](https://github.com/projecthorus/tawhiri) programmed in C – a high-altitude
balloon (radiosonde) trajectory predictor.


## Build

```
git clone https://github.com/DO2JMG/tawhiri
cd tawhiri
make
```

## Create folder

```
mkdir dataset
```

## Start server

```
./tawhiri --server --port 8080 --dir ./dataset
```

Use `--dir PATH` to choose the directory that contains the dataset files.
If `-d YYYYMMDDHH` is omitted, the server automatically uses the newest dataset
in that directory and switches to a newer one without restarting when it appears.

To pin a fixed dataset, use:

```
./tawhiri --server --port 8080 --dir ./dataset -d 2026061112
```

## Run downloader 

```
./tawhiri-downloader 2026061112 --dir ./dataset
```

Use `--dir PATH` to choose where the downloader writes datasets.
The old environment variable `DATASET_DIR` is still supported.


## Create service (optional)

```
cat > /etc/systemd/system/tawhiri.service << 'EOF'
[Unit]
Description=Tawhiri Balloon Trajectory Server
After=network.target

[Service]
Type=simple
WorkingDirectory=/root/Tawhiri
ExecStart=/root/Tawhiri/tawhiri --server --port 65432 --dir /root/Tawhiri/dataset
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

systemctl enable tawhiri
systemctl start tawhiri
```

## Create Cronjobs (optional)

Download
```
30 4,10,16,22 * * * /root/Tawhiri/tawhiri-downloader --dir /root/Tawhiri/dataset >> /var/log/tawhiri-gfs.log 2>&1
```
Remove old datasets
```
30 4,10,16,22 * * * /root/Tawhiri/tawhiri-downloader --dir /root/Tawhiri/dataset --keep-days 1 >> /var/log/tawhiri-gfs.log 2>&1
```


