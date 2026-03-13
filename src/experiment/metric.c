#include "metric.h"
#include "zmalloc/zmalloc.h"
#include "dict/dict.h"
#include "sds/sds.h"
#include <string.h>

#define UNUSED(x) (void)(x)

/**
 * 创建指标条目（可变长数组 samples[]）
 * 输入: samples - 样本数量
 * 返回: 指标条目指针，失败返回NULL
 */
metric_entry_t* metric_entry_new(int samples) {
    if (samples <= 0) {
        samples = METRIC_SAMPLES;  // 使用默认样本数量
    }
    /* 分配结构体 + 样本数组的内存 */
    metric_entry_t* entry = (metric_entry_t*)zmalloc(sizeof(metric_entry_t)
     + sizeof(long long) * samples);
    entry->last_sample_base = 0;
    entry->last_sample_value = 0;
    memset(entry->samples, 0, sizeof(long long) * samples);  // 初始化样本数组
    entry->idx = 0;
    return entry;
}

/**
 * 释放指标条目
 * 输入: entry - 指标条目指针
 * 返回: 无
 */
void metric_entry_delete(metric_entry_t* entry) {
    zfree(entry);
}

/**
 * 字典值析构回调
 * 输入: dict - 字典指针, entry - 条目指针
 * 返回: 无
 */
void dict_delete_metric_entry(dict_t* dict, void* entry) {
    UNUSED(dict);
    metric_entry_delete(entry);
}

/* 指标字典类型（char* key, metric_entry_t* val） */
dict_func_t metric_dict_type =  {
    .hashFunction = dict_char_hash,
    .keyCompare = dict_char_key_compare,
    .keyDestructor = dict_sds_destructor,
    .valDestructor = dict_delete_metric_entry,
};

/**
 * 创建指标管理器
 * 输入: samples - 样本数量
 * 返回: 指标管理器指针，失败返回NULL
 */
metric_t* metric_new(size_t samples) {
    metric_t* metric = (metric_t*)zmalloc(sizeof(metric_t));
    metric->metrics = dict_new(&metric_dict_type);  // 创建指标字典
    metric->samples = samples;
    return metric;
}

/**
 * 追踪瞬时指标（计算两次采样的平均速率，写入环形缓冲区）
 * 输入: metric - 指标管理器, name - 指标名, current_value - 当前值
 *       current_base - 当前基准值, factor - 计算因子
 * 返回: 无
 */
void metric_track_instantaneous(metric_t* metric, const char* name,
    long long current_value, long long current_base, long long factor) {
    // dict_add(metric->metrics, sds_new(name), metric_entry_new(0, value));
    dict_entry_t* entry = dict_find(metric->metrics, name);  // 查找指标是否存在
    metric_entry_t* metric_entry;
    if (entry) {
        /* 指标已存在，计算速率并写入环形缓冲区 */
        metric_entry = dict_get_entry_val(entry);
        if (metric_entry->last_sample_base > 0) {
            long long base = current_base - metric_entry->last_sample_base;      // 时间差
            long long value = current_value - metric_entry->last_sample_value;   // 数值差
            long long avg = base > 0? (value * factor) / base : 0;              // 计算平均速率
            metric_entry->samples[metric_entry->idx] = avg;                      // 写入样本
            metric_entry->idx = (metric_entry->idx + 1) % metric->samples;       // 环形缓冲区索引更新
        }
    } else {
        /* 指标不存在，创建新指标条目 */
        metric_entry = metric_entry_new(metric->samples);
        dict_add(metric->metrics, sds_new(name), metric_entry);  // 添加到字典
    }
    /* 更新最新采样值 */
    metric_entry->last_sample_base = current_base;
    metric_entry->last_sample_value = current_value;

}

/**
 * 获取平均瞬时值（对 samples 求平均）
 * 输入: metric - 指标管理器, name - 指标名
 * 返回: 平均值，不存在返回-1
 */
long long metric_get_instantaneous(metric_t* metric, const char* name) {
    dict_entry_t* entry = dict_find(metric->metrics, name);  // 查找指标
    metric_entry_t* metric_entry = dict_get_entry_val(entry);
    if (metric_entry == NULL) {
        return -1;  // 指标不存在
    }
    int j;
    long long sum = 0;

    /* 计算所有样本的平均值 */
    for (j = 0; j < metric->samples; j++) {
        sum += metric_entry->samples[j];
    }
    return sum / metric->samples;  // 返回平均值
}

/**
 * 释放指标管理器
 * 输入: metric - 指标管理器指针
 * 返回: 无
 */
void metric_delete(metric_t* metric) {
    dict_delete(metric->metrics);  // 删除指标字典（会自动调用析构函数）
    zfree(metric);  // 释放管理器内存
}

