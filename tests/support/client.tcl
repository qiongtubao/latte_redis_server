# Latte Redis 测试客户端（参考 Redis 官方 tests/support/redis.tcl）
# 两种用法:
#   (1) redis-cli 风格（阻塞，推荐）: set r [redis_cli $host $port]; $r ping; $r set k v; $r get k; $r close
#   (2) 带超时: set r [latte_client $host $port]; $r ping; $r close

namespace eval latte_client {
    variable id 0
    variable fd
    variable addr
    variable timeout_ms
    array set fd {}
    array set addr {}
    array set timeout_ms {}
    set default_timeout_ms 5000
    variable blocking_id 0
    array set blocking_fd {}
}

# ---------- redis-cli 风格：阻塞 socket，统一发命令+解析返回 ----------
# 阻塞读一行（到 \n）
proc ::latte_client::read_resp_line_blocking {fd} {
    set line [gets $fd]
    if {$line eq "" && [eof $fd]} {
        after 200
        update
        set line [gets $fd]
    }
    if {$line eq "" && [eof $fd]} { return -code error "connection closed" }
    return [string trim $line "\r\n"]
}

# 阻塞读一条完整 RESP 回复，返回解析出的字符串（+/-/: 取内容，$ 取 bulk 或 {}，*1 取首元素）
proc ::latte_client::read_one_resp_reply_blocking {fd} {
    set line [::latte_client::read_resp_line_blocking $fd]
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
        return [::latte_client::read_one_resp_reply_blocking $fd]
    }
    return $line
}

# 发送一条 RESP 命令并读回一条回复，返回解析后的字符串（PONG/OK/值/null 等）
proc ::latte_client::send_resp_command_and_read_reply {fd cmd} {
    puts -nonewline $fd $cmd
    flush $fd
    return [::latte_client::read_one_resp_reply_blocking $fd]
}

# redis-cli 风格分发：close 专用实现，_read_one 读一条回复（兼容 SET 双回复等），其余命令按 RESP 发送并统一解析返回
proc ::latte_client::dispatch_blocking {id method args} {
    variable blocking_fd
    if {![info exists blocking_fd($id)]} {
        return -code error "connection closed"
    }
    set fd $blocking_fd($id)
    if {[string tolower $method] eq "close"} {
        catch {close $fd}
        unset blocking_fd($id)
        catch {interp alias {} ::latte_client::blocking_handle$id {}}
        return
    }
    if {$method eq "_read_one"} {
        return [::latte_client::read_one_resp_reply_blocking $fd]
    }
    set cmd [::latte_client::build_resp_command $method {*}$args]
    return [::latte_client::send_resp_command_and_read_reply $fd $cmd]
}

# 连接并返回 redis-cli 风格 client：$r close | $r ping | $r set k v | $r get k，返回值统一为解析后的字符串
proc redis_cli {host port} {
    set fd [socket $host $port]
    fconfigure $fd -blocking 1 -encoding binary -translation binary
    set id [incr ::latte_client::blocking_id]
    set ::latte_client::blocking_fd($id) $fd
    interp alias {} ::latte_client::blocking_handle$id {} ::latte_client::dispatch_blocking $id
    return ::latte_client::blocking_handle$id
}

# 从 start_latte_redis_server 返回的 dict 创建 redis_cli 连接
proc redis_cli_from_srv {srv} {
    return [redis_cli [dict get $srv host] [dict get $srv port]]
}

# 带超时：先等通道可读（或超时），再读一行；用全局变量保证 fileevent 回调能写入
proc ::latte_client::read_line_with_timeout {chan timeout_ms} {
    set ::latte_client_read_done ""
    after $timeout_ms [list set ::latte_client_read_done timeout]
    fileevent $chan readable [list set ::latte_client_read_done ready]
    vwait ::latte_client_read_done
    fileevent $chan readable {}
    catch {after cancel [list set ::latte_client_read_done timeout]}
    if {$::latte_client_read_done eq "timeout"} {
        return -code error "timeout reading reply line (${timeout_ms}ms)"
    }
    # 可读后临时阻塞读满一行（避免 -blocking 0 时 gets 读不到整行）
    fconfigure $chan -blocking 1
    set line [gets $chan]
    fconfigure $chan -blocking 0
    if {$line eq "" && [eof $chan]} {
        return -code error "connection closed"
    }
    return [string trim $line "\r\n"]
}

# 带超时：先等可读再读满 n 字节
proc ::latte_client::read_n_with_timeout {chan n timeout_ms} {
    set got ""
    set deadline [expr {[clock milliseconds] + $timeout_ms}]
    while {[string length $got] < $n} {
        if {[clock milliseconds] >= $deadline} {
            return -code error "timeout reading bulk (${timeout_ms}ms)"
        }
        set ::latte_client_read_done ""
        after 500 [list set ::latte_client_read_done timeout]
        fileevent $chan readable [list set ::latte_client_read_done ready]
        vwait ::latte_client_read_done
        fileevent $chan readable {}
        catch {after cancel [list set ::latte_client_read_done timeout]}
        if {$::latte_client_read_done eq "timeout"} { continue }
        fconfigure $chan -blocking 1
        append got [read $chan [expr {$n - [string length $got]}]]
        fconfigure $chan -blocking 0
        if {[eof $chan]} { break }
    }
    if {[string length $got] != $n} {
        return -code error "timeout reading bulk (${timeout_ms}ms)"
    }
    return $got
}

# 创建客户端连接，返回句柄（用于 $handle method args ...）
# timeout_ms: 可选，读回复超时毫秒数，默认 5000
proc latte_client {host port {timeout_ms {}}} {
    if {$timeout_ms eq ""} { set timeout_ms $latte_client::default_timeout_ms }
    set fd [socket $host $port]
    # 非阻塞便于 fileevent 触发，读时用 vwait+fileevent 带超时
    fconfigure $fd -translation binary -encoding binary -blocking 0
    set id [incr latte_client::id]
    set latte_client::fd($id) $fd
    set latte_client::addr($id) [list $host $port]
    set latte_client::timeout_ms($id) $timeout_ms
    interp alias {} ::latte_client::handle$id {} ::latte_client::dispatch $id
    return ::latte_client::handle$id
}

# 从 start_latte_redis_server 返回的 dict 创建客户端
proc latte_client_from_srv {srv} {
    set host [dict get $srv host]
    set port [dict get $srv port]
    return [latte_client $host $port]
}

# 按 RESP 协议拼装一条命令（数组 + 若干 bulk string），不写死任何长度或内容
# cmd: 命令名，args: 参数列表。无参如 PING 传 {}；多参如 set k v 可传多个独立参数或 {k v}。
# 使用 {*}$args 调用时此处会收到多参数；若只收到一个含空格的字符串（如 "k v"），则按空格拆成多个参数。
proc ::latte_client::build_resp_command {cmd args} {
    set args [concat $args]
    if {[llength $args] == 1 && [string first " " [lindex $args 0]] >= 0} {
        set args [split [lindex $args 0] " "]
    }
    if {$args eq "" || ([llength $args] == 1 && [lindex $args 0] eq "")} {
        set args {}
    }
    set n [expr {1 + [llength $args]}]
    set out "*${n}\r\n"
    set len [string length $cmd]
    append out "\$${len}\r\n${cmd}\r\n"
    foreach a $args {
        set len [string length $a]
        append out "\$${len}\r\n${a}\r\n"
    }
    return $out
}

# 发送已拼装好的 RESP 命令到 channel，并读回复（由 call 使用）
proc ::latte_client::send_and_read {chan cmd_payload timeout_ms} {
    puts -nonewline $chan $cmd_payload
    flush $chan
    return [::latte_client::read_reply $chan $timeout_ms]
}

# 句柄分发：$r method arg1 arg2 ... -> 拼装 RESP 后发送并返回解析后的回复
# 支持 $r set key value、$r get key 等，协议由 build_resp_command 动态生成
proc ::latte_client::dispatch {id method args} {
    variable fd
    variable addr
    variable timeout_ms
    if {![info exists fd($id)]} {
        return -code error "connection closed"
    }
    set chan $fd($id)
    if {[string tolower $method] eq "close"} {
        catch {close $chan}
        unset fd($id)
        unset addr($id)
        catch {unset timeout_ms($id)}
        catch {interp alias {} ::latte_client::handle$id {}}
        return
    }
    set cmd [::latte_client::build_resp_command $method {*}$args]
    set to [set latte_client::timeout_ms($id)]
    return [::latte_client::send_and_read $chan $cmd $to]
}

# 解析 RESP 单条回复（+, -, :, $, *），带超时避免阻塞
proc ::latte_client::read_reply {chan timeout_ms} {
    set line [::latte_client::read_line_with_timeout $chan $timeout_ms]
    puts $line
    if {$line eq ""} {
        if {[eof $chan]} { return -code error "connection closed" }
        return -code error "I/O error reading reply"
    }
    set type [string index $line 0]
    set payload [string range $line 1 end]
    switch -exact -- $type {
        "+" - "(" { return $payload }
        "-" { return -code error $payload }
        ":" { return $payload }
        "$" {
            set len [string trim $payload]
            if {$len == "-1"} { return {} }
            set buf [::latte_client::read_n_with_timeout $chan [expr {$len + 2}] $timeout_ms]
            return [string range $buf 0 end-2]
        }
        "*" {
            set n [string trim $payload]
            if {$n == "-1"} { return {} }
            set result {}
            for {set i 0} {$i < $n} {incr i} {
                lappend result [::latte_client::read_reply $chan $timeout_ms]
            }
            return $result
        }
        default { return -code error "Bad protocol: reply type '$type'" }
    }
}
