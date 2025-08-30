#include "testassert.h"
#include "testhelp.h"
#include "metric.h"
#include "log/log.h"

int test_metric(void) {
    metric_t* metric = metric_new(10);
    metric_track_instantaneous(metric, "test", 100, 100, 10);
    long long value = metric_get_instantaneous(metric, "test");
    LATTE_LIB_LOG(LOG_DEBUG, "value: %lld", value);
    metric_delete(metric);
    return 1;
}

int test_api(void) {
    log_module_init();
    assert(log_add_stdout(LATTE_LIB, LOG_DEBUG) == 1);
    {
        
        // test_cond("io net write", 
        //     test_server(false, false, false, false) == 1);
        test_cond("async io net write", 
            test_metric() == 1);
        
    } test_report()

    return 1;
}

int main() {
    test_api();
    return 0;
}



