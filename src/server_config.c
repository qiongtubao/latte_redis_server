
#include "server.h"

configRule port_rule = {
    .update = longLongUpdate,
    .writeConfigSds = longLongWriteConfigSds,
    .releaseValue = longLongReleaseValue,
    .load = longLongLoadConfig
};

/** latte config module **/
config* createServerConfig() {
    config* c = createConfig();
    sds port = sdsnewlen("port", 4);
    registerConfig(c, port, &port_rule);
    return c;
}