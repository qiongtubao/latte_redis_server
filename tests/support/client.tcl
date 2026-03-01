# Latte Redis 测试客户端（参考 Redis 官方 tests/support/redis.tcl）
# 用法:
#   set r [latte_client 127.0.0.1 6379]
#   $r ping                    -> 返回 PONG
#   $r set foo bar             -> 发送 SET foo bar，返回解析后的回复
#   $r get foo
#   $r close
#
#   set srv [start_latte_redis_server]
#   set r [latte_client_from_srv $srv]
#   $r ping
#   $r close
#   kill_latte_server $srv

namespace eval latte_client {
    variable id 0
    variable fd
    variable addr
    array set fd {}
    array set addr {}
}

# 创建客户端连接，返回句柄（用于 $handle method args ...）
proc latte_client {host port} {
    set fd [socket $host $port]
    fconfigure $fd -translation binary -encoding binary -blocking 1
    set id [incr latte_client::id]
    set latte_client::fd($id) $fd
    set latte_client::addr($id) [list $host $port]
    interp alias {} ::latte_client::handle$id {} ::latte_client::dispatch $id
    return ::latte_client::handle$id
}

# 从 start_latte_redis_server 返回的 dict 创建客户端
proc latte_client_from_srv {srv} {
    set host [dict get $srv host]
    set port [dict get $srv port]
    return [latte_client $host $port]
}

# 句柄分发：$r method arg1 arg2 ... -> 发 RESP 命令并返回解析后的回复
proc ::latte_client::dispatch {id method args} {
    variable fd
    variable addr
    if {![info exists fd($id)]} {
        return -code error "connection closed"
    }
    set chan $fd($id)
    if {[string tolower $method] eq "close"} {
        catch {close $chan}
        unset fd($id)
        unset addr($id)
        catch {interp alias {} ::latte_client::handle$id {}}
        return
    }
    # 构建 RESP: *n\r\n $len\r\n METHOD\r\n $len\r\n arg\r\n ...
    set cmd "*[expr {[llength $args] + 1}]\r\n"
    append cmd "\$[string length $method]\r\n$method\r\n"
    foreach a $args {
        append cmd "\$[string length $a]\r\n$a\r\n"
    }
    puts -nonewline $chan $cmd
    flush $chan
    return [::latte_client::read_reply $chan]
}

# 解析 RESP 单条回复（+, -, :, $, *）
proc ::latte_client::read_reply {chan} {
    set line [gets $chan]
    if {$line eq ""} {
        if {[eof $chan]} { return -code error "connection closed" }
        return -code error "I/O error reading reply"
    }
    set line [string trim $line "\r\n"]
    set type [string index $line 0]
    set payload [string range $line 1 end]
    switch -exact -- $type {
        "+" - "(" { return $payload }
        "-" { return -code error $payload }
        ":" { return $payload }
        "$" {
            set len [string trim $payload]
            if {$len == "-1"} { return {} }
            set buf [read $chan $len]
            read $chan 2
            return $buf
        }
        "*" {
            set n [string trim $payload]
            if {$n == "-1"} { return {} }
            set result {}
            for {set i 0} {$i < $n} {incr i} {
                lappend result [::latte_client::read_reply $chan]
            }
            return $result
        }
        default { return -code error "Bad protocol: reply type '$type'" }
    }
}
