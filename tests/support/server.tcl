# Latte Redis 服务端启动/停止与存活检测（参考 Redis 官方 tests/support/server.tcl）

set ::latte_server_path [file normalize ./src/main]

# 带超时的 socket 读取，避免测试卡死；返回读到的内容，超时或出错返回空或抛错
proc read_socket_with_timeout {fd maxbytes timeout_ms} {
    fconfigure $fd -blocking 0
    set reply ""
    set deadline [expr {[clock milliseconds] + $timeout_ms}]
    while {[clock milliseconds] < $deadline} {
        append reply [read $fd $maxbytes]
        if {[eof $fd] || [string length $reply] >= $maxbytes} { break }
        if {[string length $reply] > 0} { break }
        after 50
        update
    }
    fconfigure $fd -blocking 1
    return $reply
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

# 启动 Latte Redis 服务端（通用方法）
# overrides: 可选，如 {port 6379} 指定端口，不传则自动分配
# 返回 dict: pid, host, port；并将 pid 加入 ::pids 便于 cleanup 时统一杀进程
proc start_latte_redis_server {{overrides {}}} {
    set host $::host
    if {[dict exists $overrides port]} {
        set port [dict get $overrides port]
    } else {
        set port [find_available_port $::baseport 100]
    }
    set cmd [list $::latte_server_path --port $port]
    set res [exec {*}$cmd &]
    set pid [lindex $res 0]
    lappend ::pids $pid

    set retrynum 100
    if {![latte_server_is_up $host $port $retrynum]} {
        stop_instance $pid
        set ::pids [lsearch -all -inline -not -exact $::pids $pid]
        error "start_latte_redis_server: server did not become ready on ${host}:${port} (pid $pid)"
    }

    dict set srv pid $pid
    dict set srv host $host
    dict set srv port $port
    return $srv
}

# 停止由 start_latte_redis_server 返回的 srv，并从 ::pids 中移除
proc kill_latte_server {srv} {
    set pid [dict get $srv pid]
    stop_instance $pid
    set ::pids [lsearch -all -inline -not -exact $::pids $pid]
}
