# tawhiri-c

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
