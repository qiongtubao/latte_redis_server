#include "metric.h"
#include "zmalloc/zmalloc.h"
#include "dict/dict.h"
#include "sds/sds.h"
#include <string.h>

#define UNUSED(x) (void)(x)

metric_entry_t* metric_entry_new(int samples) {
    if (samples <= 0) {
        samples = METRIC_SAMPLES;
    }
    metric_entry_t* entry = (metric_entry_t*)zmalloc(sizeof(metric_entry_t)
     + sizeof(long long) * samples);
    entry->last_sample_base = 0;
    entry->last_sample_value = 0;
    memset(entry->samples, 0, sizeof(long long) * samples);
    entry->idx = 0;
    return entry;
}

void metric_entry_delete(metric_entry_t* entry) {
    zfree(entry);
}

void dict_delete_metric_entry(dict_t* dict, void* entry) {
    UNUSED(dict);
    metric_entry_delete(entry);
}

dict_func_t metric_dict_type =  {
    .hashFunction = dict_char_hash,
    .keyCompare = dict_char_key_compare,
    .keyDestructor = dict_sds_destructor,
    .valDestructor = dict_delete_metric_entry,
};

metric_t* metric_new(size_t samples) {
    metric_t* metric = (metric_t*)zmalloc(sizeof(metric_t));
    metric->metrics = dict_new(&metric_dict_type);
    metric->samples = samples;
    return metric;
}

void metric_track_instantaneous(metric_t* metric, const char* name, 
    long long current_value, long long current_base, long long factor) {
    // dict_add(metric->metrics, sds_new(name), metric_entry_new(0, value));
    dict_entry_t* entry = dict_find(metric->metrics, name);
    metric_entry_t* metric_entry;
    if (entry) {
        metric_entry = dict_get_entry_val(entry);
        if (metric_entry->last_sample_base > 0) {
            long long base = current_base - metric_entry->last_sample_base;
            long long value = current_value - metric_entry->last_sample_value;
            long long avg = base > 0? (value * factor) / base : 0;
            metric_entry->samples[metric_entry->idx] = avg;
            metric_entry->idx = (metric_entry->idx + 1) % metric->samples;
        } 
    } else {
        metric_entry = metric_entry_new(metric->samples);
        dict_add(metric->metrics, sds_new(name), metric_entry);
    }
    metric_entry->last_sample_base = current_base;
    metric_entry->last_sample_value = current_value;

}

long long metric_get_instantaneous(metric_t* metric, const char* name) {
    dict_entry_t* entry = dict_find(metric->metrics, name);
    metric_entry_t* metric_entry = dict_get_entry_val(entry);
    if (metric_entry == NULL) {
        return -1;
    }
    int j;
    long long sum = 0;

    for (j = 0; j < metric->samples; j++) {
        sum += metric_entry->samples[j];
    }
    return sum / metric->samples;
}

void metric_delete(metric_t* metric) {
    dict_delete(metric->metrics);
    zfree(metric);
}

