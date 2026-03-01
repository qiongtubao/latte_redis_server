# PING 命令测试：redis-cli 风格 client

test "PING returns PONG" {
    set srv [start_latte_redis_server]
    after 600
    update
    set r [redis_cli_from_srv $srv]
    after 400
    update
    set reply [$r ping]
    $r close
    kill_latte_server $srv
    puts $reply
    assert {[string match "*PONG*" $reply]}
}
