# PING 命令测试：start_latte_redis_server + 通用客户端 latte_client，验证 PING 返回 PONG

test "PING returns PONG" {
    set srv [start_latte_redis_server]
    set r [latte_client_from_srv $srv]

    set reply [$r ping]
    $r close
    kill_latte_server $srv

    assert {$reply eq "PONG"}
}
