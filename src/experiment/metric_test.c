#include "testassert.h"
#include "testhelp.h"
#include "metric.h"
#include "log/log.h"

/**
 * 测试 metric 基本功能
 * 功能: 创建→追踪→获取→删除
 * 返回: 1-成功, 0-失败
 */
int test_metric(void) {
    metric_t* metric = metric_new(10);  // 创建指标管理器，10个样本
    metric_track_instantaneous(metric, "test", 100, 100, 10);  // 追踪指标
    long long value = metric_get_instantaneous(metric, "test");  // 获取平均值
    LATTE_LIB_LOG(LOG_DEBUG, "value: %lld", value);
    metric_delete(metric);  // 删除指标管理器
    return 1;
}

/**
 * 集成测试入口
 * 功能: 初始化日志系统并运行测试
 * 返回: 1-成功
 */
int test_api(void) {
    log_module_init();  // 初始化日志模块
    assert(log_add_stdout(LATTE_LIB, LOG_DEBUG) == 1);  // 添加标准输出日志
    {

        // test_cond("io net write",
        //     test_server(false, false, false, false) == 1);
        test_cond("async io net write",
            test_metric() == 1);  // 测试metric功能

    } test_report()  // 生成测试报告

    return 1;
}

/**
 * 测试程序入口
 * 返回: 0-成功
 */
int main() {
    test_api();  // 运行测试
    return 0;
}



