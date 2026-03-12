#ifndef __REDIS_METRIC_H
#define __REDIS_METRIC_H

#include "dict/dict.h"
#include "dict/dict_plugins.h"

/* 默认样本数量 */
#define METRIC_SAMPLES 16

/**
 * 指标条目结构体
 */
typedef struct metric_entry_t {
    long long last_sample_base;   /* 上次采样的基准值（时间戳等） */
    long long last_sample_value;  /* 上次采样的数值 */
    int idx;                      /* 当前写入位置索引（环形缓冲区） */
    long long samples[];          /* 样本数组（可变长度） */
} metric_entry_t;

/**
 * 创建指标条目（可变长数组 samples[]）
 * 输入: samples - 样本数量
 * 返回: 指标条目指针，失败返回NULL
 */
metric_entry_t* metric_entry_new(int samples);

/**
 * 删除指标条目
 * 输入: entry - 指标条目指针
 * 返回: 无
 */
void metric_entry_delete(metric_entry_t* entry);

/**
 * 指标管理器结构体
 */
typedef struct metric_t {
   dict_t* metrics;  /* 指标字典（char* key, metric_entry_t* val） */
   size_t samples;   /* 每个指标的样本数量 */
} metric_t;

/**
 * 创建指标管理器
 * 输入: samples - 样本数量
 * 返回: 指标管理器指针，失败返回NULL
 */
metric_t* metric_new(size_t samples);

/**
 * 追踪瞬时指标（计算两次采样的平均速率，写入环形缓冲区）
 * 输入: metric - 指标管理器, name - 指标名, current_value - 当前值
 *       current_base - 当前基准值, factor - 计算因子
 * 返回: 无
 */
void metric_track_instantaneous(metric_t* metric, const char* name,
    long long current_value, long long current_base, long long factor);

/**
 * 获取平均瞬时值（对 samples 求平均）
 * 输入: metric - 指标管理器, name - 指标名
 * 返回: 平均值，不存在返回-1
 */
long long metric_get_instantaneous(metric_t* metric, const char* name);

/**
 * 释放指标管理器
 * 输入: metric - 指标管理器指针
 * 返回: 无
 */
void metric_delete(metric_t* metric);

// latte_iterator_t* metric_get_info_iterator(metric_t* metric);

#endif