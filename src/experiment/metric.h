#ifndef __REDIS_METRIC_H
#define __REDIS_METRIC_H

#include "dict/dict.h"
#include "dict/dict_plugins.h"

#define METRIC_SAMPLES 16

typedef struct metric_entry_t {
    long long last_sample_base;
    long long last_sample_value;
    int idx;
    long long samples[];
} metric_entry_t;

metric_entry_t* metric_entry_new(int samples);
void metric_entry_delete(metric_entry_t* entry);

typedef struct metric_t {
   dict_t* metrics;
   size_t samples;
} metric_t;

metric_t* metric_new(size_t samples);
void metric_track_instantaneous(metric_t* metric, const char* name, 
    long long current_value, long long current_base, long long factor);
long long metric_get_instantaneous(metric_t* metric, const char* name);
void metric_delete(metric_t* metric);

// latte_iterator_t* metric_get_info_iterator(metric_t* metric);

#endif