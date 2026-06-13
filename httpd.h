// Tawhiri - Server and downloader
//
// Copyright (C) Jean-Michael (DO2JMG) <info@wettersonde.net>
//
// Released under GNU GPL v3 or later

#ifndef HTTPD_H
#define HTTPD_H

#include "dataset.h"
#include "elevation.h"
#include "interpolate.h"

typedef struct {
    int                   port;       
    const char           *bind_addr;  
    const Dataset        *wind_ds;    
    const ElevationDataset *elev_ds;  
    int                   has_elev;
    double                dt;         
} ServerConfig;

int httpd_run(const ServerConfig *cfg);

#endif 
