# Latte Redis 服务端启动/停止与存活检测（参考 Redis 官方 tests/support/server.tcl）

set ::latte_server_path [file normalize ./src/main]

# 带超时的 socket 读取，避免测试卡死；返回读到的内容，超时或出错返回空或抛错
# 若 need_line 为 1，则至少读到一行（含 \r\n）或 maxbytes 或 eof 才返回
# 用 fileevent 等待 fd 可读；每次用独立变量名避免嵌套调用互相覆盖
proc read_socket_with_timeout {fd maxbytes timeout_ms {need_line 0}} {
    fconfigure $fd -blocking 0
    set reply ""
    set deadline [expr {[clock milliseconds] + $timeout_ms}]
    set varname "::read_socket_done_[clock milliseconds]_[pid]"
    while {[clock milliseconds] < $deadline} {
        append reply [read $fd $maxbytes]
        if {[eof $fd] || [string length $reply] >= $maxbytes} { break }
        if {[string length $reply] > 0} {
            if {!$need_line || [string first "\r\n" $reply] >= 0} { break }
        }
        set $varname ""
        fileevent $fd readable [list set $varname ready]
        set timeout_id [after 500 [list set $varname timeout]]
        vwait $varname
        fileevent $fd readable {}
        catch {after cancel $timeout_id}
        if {[set $varname] ne "ready"} { continue }
    }
    if {[info exists $varname]} { unset $varname }
    fconfigure $fd -blocking 1
    return $reply
}

# 阻塞读一行（到 \n），fd 需已配置 -blocking 1；不依赖 fileevent，避免超时
proc read_resp_line {fd} {
    set line [gets $fd]
    if {$line eq "" && [eof $fd]} {
        after 300
        update
        set line [gets $fd]
    }
    if {$line eq "" && [eof $fd]} { return -code error "connection closed" }
    return [string trim $line "\r\n"]
}

# 阻塞读一条完整 RESP 回复（供 string/ping 等测试用，不依赖超时）
# 返回：简单串取内容、bulk 取内容或 {}、错误串取整行、*1 递归取首元素
proc read_one_resp_reply_blocking {fd} {
    set line [read_resp_line $fd]
    if {$line eq ""} { return -code error "empty line" }
    set first [string index $line 0]
    if {$first eq "+" || $first eq "-" || $first eq ":"} {
        return [string range $line 1 end]
    }
    if {$first eq "$"} {
        set rest [string range $line 1 end]
        if {$rest eq "-1"} { return {} }
        set len [expr {int($rest)}]
        set got [read $fd [expr {$len + 2}]]
        return [string range $got 0 end-2]
    }
    if {$first eq "*"} {
        set n [expr {int([string range $line 1 end])}]
        if {$n <= 0} { return {} }
        if {$n != 1} { return -code error "multi-element array not supported" }
        return [read_one_resp_reply_blocking $fd]
    }
    return $line
}

# 通用：向已连接的 fd 发送一条 RESP 命令并读回一条 RESP 回复，返回解析出的字符串（PONG/OK/值/null 等）
# 供 PING、SET、GET 等测试共用
proc send_resp_command_and_read_reply {fd cmd} {
    puts -nonewline $fd $cmd
    flush $fd
    return [read_one_resp_reply_blocking $fd]
}

# 向 host:port 发送 RESP PING，返回 1 表示成功收到 PONG，否则 0
proc ping_latte_server {host port} {
    set retval 0
    if {[catch {
        set fd [socket $host $port]
        fconfigure $fd -blocking 1 -encoding binary -translation binary
        puts -nonewline $fd "*1\r\n\$4\r\nPING\r\n"
        flush $fd
        set reply [read_socket_with_timeout $fd 1024 3000]
        close $fd
        if {[string match "*PONG*" $reply]} {
            set retval 1
        }
    }]} {
        set retval 0
    }
    return $retval
}

# 轮询检测服务是否就绪，每 50ms 试一次，共 retrynum 次；返回 1 表示已就绪
proc latte_server_is_up {host port retrynum} {
    after 10
    while {[incr retrynum -1] >= 0} {
        if {[ping_latte_server $host $port]} { return 1 }
        after 50
    }
    return 0
}

# 启动 Latte Redis 服务端（统一方法）
# 参数：
#   - overrides: 可选，如 {port 6379} 指定端口，不传则自动分配
#   - module_paths: 可选，模块路径列表，空列表表示不加载模块，默认 {}
#   - use_ping: 可选，是否使用 ping 检测服务器就绪，默认 true
#   - wait_after_ping: 可选，ping 检测后等待时间（毫秒），默认 400
#   - wait_ms: 可选，不使用 ping 时直接等待的时间（毫秒），默认 1500
# 返回 dict: pid, host, port, stdout_file, stderr_file, log_file, workdir, config_file(如果加载了模块)
proc start_latte_redis_server {{overrides {}} {module_paths {}} {use_ping 1} {wait_after_ping 400} {wait_ms 1500}} {
    set host $::host
    if {[dict exists $overrides port]} {
        set port [dict get $overrides port]
    } else {
        set port [find_available_port $::baseport 100]
    }
    
    # 创建服务器实例的工作目录
    set cfgdir "tests/tmp"
    if {![file exists $cfgdir]} { file mkdir $cfgdir }
    set workdir [file join $cfgdir "server_[pid]_[clock milliseconds]"]
    file mkdir $workdir
    
    # 设置配置文件路径（如果需要加载模块）
    set config_file ""
    if {[llength $module_paths] > 0} {
        set config_file [file join $workdir "modules.conf"]
        set fd [open $config_file w]
        foreach path $module_paths {
            puts $fd "load-module $path"
        }
        close $fd
    }
    
    # 设置输出文件路径
    set stdout_file [file join $workdir "stdout.log"]
    set stderr_file [file join $workdir "stderr.log"]
    set log_file [file join $workdir "server.log"]
    
    # 构建命令，添加日志文件参数和 DEBUG 日志级别
    set cmd [list $::latte_server_path]
    if {$config_file ne ""} {
        lappend cmd $config_file
    }
    lappend cmd --port $port --log-file $log_file --log-level debug
    
    # 启动服务器并重定向输出
    set res [exec {*}$cmd > $stdout_file 2> $stderr_file &]
    set pid [lindex $res 0]
    lappend ::pids $pid

    # 根据 use_ping 参数决定如何检测服务器就绪
    if {$use_ping} {
        set retrynum 100
        if {![latte_server_is_up $host $port $retrynum]} {
            stop_instance $pid
            set ::pids [lsearch -all -inline -not -exact $::pids $pid]
            if {$config_file ne ""} {
                catch {file delete $config_file}
            }
            error "start_latte_redis_server: server did not become ready on ${host}:${port} (pid $pid)"
        }
        # 等待 ping 连接关闭后 server 稳定，再交给调用方建连，减少 connection closed
        after $wait_after_ping
        update
    } else {
        # 不使用 ping，直接等待指定时间
        after $wait_ms
        update
    }

    dict set srv pid $pid
    dict set srv host $host
    dict set srv port $port
    dict set srv stdout_file $stdout_file
    dict set srv stderr_file $stderr_file
    dict set srv log_file $log_file
    dict set srv workdir $workdir
    if {$config_file ne ""} {
        dict set srv config_file $config_file
    }
    return $srv
}

# 停止由 start_latte_redis_server 返回的 srv，并从 ::pids 中移除
proc kill_latte_server {srv} {
    set pid [dict get $srv pid]
    stop_instance $pid
    set ::pids [lsearch -all -inline -not -exact $::pids $pid]
    if {[dict exists $srv config_file]} {
        catch {file delete [dict get $srv config_file]}
    }
    # 注意：不删除 workdir，保留日志文件供调试
}

# 启动时加载 modules 目录下生成的 .so（通过临时配置文件）
# module_paths: 可选，.so 路径列表，默认 [./src/modules/latte.so]
# 返回与 start_latte_redis_server 相同的 dict，多一个 config_file 供 kill 时删除
# 注意：此函数为兼容性包装器，实际调用统一的 start_latte_redis_server
proc start_latte_redis_server_with_modules {{module_paths {}}} {
    if {[llength $module_paths] == 0} {
        set module_paths [list [file normalize ./src/modules/latte.so]]
    }
    return [start_latte_redis_server {} $module_paths 1 400 1500]
}

# 启动带 module 的 server，仅固定等待不 ping，保证测试时只有一条连接（避免 ping 连接干扰导致 connection closed）
# module_paths: 可选，.so 路径列表，默认 [./src/modules/latte.so]
# wait_ms: 可选，等待时间（毫秒），默认 1500
# 注意：此函数为兼容性包装器，实际调用统一的 start_latte_redis_server
proc start_latte_redis_server_with_modules_no_ping {{module_paths {}} {wait_ms 1500}} {
    if {[llength $module_paths] == 0} {
        set module_paths [list [file normalize ./src/modules/latte.so]]
    }
    return [start_latte_redis_server {} $module_paths 0 400 $wait_ms]
}
