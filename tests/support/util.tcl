


set ::last_port_attempted 0
proc find_available_port {start count} {
    set port [expr $::last_port_attempted + 1]
    for {set attempts 0} {$attempts < $count} {incr attempts} {
        if {$port < $start || $port >= $start+$count} {
            set port $start
        }
        set fd1 -1
        if {[catch {set fd1 [socket -server 127.0.0.1 $port]}] ||
            [catch {set fd2 [socket -server 127.0.0.1 [expr $port+10000]]}]} {
            if {$fd1 != -1} {
                close $fd1
            }
        } else {
            close $fd1
            close $fd2
            set ::last_port_attempted $port
            return $port
        }
        incr port
    }
    error "Can't find a non busy port in the $start-[expr {$start+$count-1}] range."
}


# Test if TERM looks like to support colors
proc color_term {} {
    expr {[info exists ::env(TERM)] && [string match *xterm* $::env(TERM)]}
}

# 彩色的输出
proc colorstr {color str} {
    if {[color_term]} {
        set b 0
        if {[string range $color 0 4] eq {bold-}} {
            set b 1
            set color [string range $color 5 end]
        }
        switch $color {
            red {set colorcode {31}}
            green {set colorcode {32}}
            yellow {set colorcode {33}}
            blue {set colorcode {34}}
            magenta {set colorcode {35}}
            cyan {set colorcode {36}}
            white {set colorcode {37}}
            default {set colorcode {37}}
        }
        if {$colorcode ne {}} {
            return "\033\[$b;${colorcode};49m$str\033\[0m"
        }
    } else {
        return $str
    }
}