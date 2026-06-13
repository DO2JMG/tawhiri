# tawhiri

C port of [Tawhiri](https://github.com/projecthorus/tawhiri) – a high-altitude
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
./tawhiri --server --port 8080
```

## Run downloader 

```
./tawhiri-downloader 2026061112
```


## Create service (optional)

```
cat > /etc/systemd/system/tawhiri.service << 'EOF'
[Unit]
Description=Tawhiri Balloon Trajectory Server
After=network.target

[Service]
Type=simple
WorkingDirectory=/root/Tawhiri
ExecStart=/root/Tawhiri/tawhiri --server --port 65432
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
30 4,10,16,22 * * * /root/Tawhiri/tawhiri-downloader >> /var/log/tawhiri-gfs.log 2>&1 && systemctl restart tawhiri
```
Remove old datasets
```
30 4,10,16,22 * * * KEEP_DAYS=1 /root/Tawhiri/tawhiri-downloader >> /var/log/tawhiri-gfs.log 2>&1 && systemctl restart tawhiri
```

