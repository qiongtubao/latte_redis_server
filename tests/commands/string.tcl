# SET/GET 命令单测：启动时加载 modules 生成的 latte.so
# 使用 server.tcl 中的 read_one_resp_reply_blocking（阻塞读），不依赖 fileevent，避免超时

proc read_one_resp_reply {fd timeout_ms} {
    set chunk [read_socket_with_timeout $fd 512 $timeout_ms 1]
    if {$chunk eq ""} { return -code error "timeout or empty" }
    set first [string index $chunk 0]
    if {$first eq "+" || $first eq "-" || $first eq ":"} {
        return [string trim [string range $chunk 1 end] "\r\n"]
    }
    if {$first eq "$"} {
        set idx [string first "\r\n" $chunk]
        set line [string trim [string range $chunk 1 $idx-1]]
        if {$line eq "-1"} { return {} }
        set len [expr {int($line)}]
        set got [string range $chunk $idx+2 end]
        set need [expr {$len + 2 - [string length $got]}]
        if {$need > 0} {
            fconfigure $fd -blocking 1
            append got [read $fd $need]
            fconfigure $fd -blocking 0
        }
        return [string range $got 0 [expr {$len - 1}]]
    }
    if {$first eq "*"} {
        set idx [string first "\r\n" $chunk]
        set n [expr {int([string trim [string range $chunk 1 $idx-1]])}]
        if {$n <= 0} { return {} }
        set rest [string range $chunk $idx+2 end]
        # 只支持单元素数组，返回该元素
        if {$n == 1} { return [read_one_resp_reply_from_buf $fd $rest $timeout_ms] }
        return -code error "multi-element array not supported"
    }
    # 服务端可能只发 bulk 内容不带 $len\r\n 前缀，当作裸字符串
    if {[string first "\r\n" $chunk] >= 0} {
        return [string trim [string range $chunk 0 [string first "\r\n" $chunk]-1]]
    }
    return $chunk
}

# 从已有 buffer 开始解析一条 RESP（用于 * 后的元素）
proc read_one_resp_reply_from_buf {fd buf timeout_ms} {
    if {[string length $buf] == 0} {
        set buf [read_socket_with_timeout $fd 512 $timeout_ms 1]
    }
    set first [string index $buf 0]
    if {$first eq "+" || $first eq "-" || $first eq ":"} {
        return [string trim [string range $buf 1 end] "\r\n"]
    }
    if {$first eq "$"} {
        set idx [string first "\r\n" $buf]
        set line [string trim [string range $buf 1 $idx-1]]
        if {$line eq "-1"} { return {} }
        set len [expr {int($line)}]
        set got [string range $buf $idx+2 end]
        set need [expr {$len + 2 - [string length $got]}]
        if {$need > 0} {
            fconfigure $fd -blocking 1
            append got [read $fd $need]
            fconfigure $fd -blocking 0
        }
        return [string range $got 0 [expr {$len - 1}]]
    }
    return -code error "unsupported reply type in array element"
}

test "SET and GET with module loaded" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    if {[catch { set r1 [$r set mykey myvalue] } err]} {
        $r close
        kill_latte_server $srv
        return -code error "string test: failed to read SET reply (server may have closed connection): $err"
    }
    if {$r1 eq "myvalue"} {
        set r2 [$r _read_one]
        assert {$r2 eq "OK"}
    } else {
        assert {$r1 eq "OK"}
    }

    set reply [$r get mykey]
    assert {$reply eq "myvalue"}

    set reply [$r get nonexistent]
    assert {$reply eq {}}

    $r close
    kill_latte_server $srv
}
