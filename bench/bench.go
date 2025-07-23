package main

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"log"
	"math"
	mrand "math/rand"
	"net"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gomodule/redigo/redis"
	"github.com/rcrowley/go-metrics"
	"github.com/shirou/gopsutil/v3/process"
	"go.uber.org/ratelimit"
)

var delayHist metrics.Histogram
var cpuHist metrics.Histogram
var memHist metrics.Histogram
var diskreadHist metrics.Histogram
var diskwriteHist metrics.Histogram
var redisPid int
var diskName string
var rl ratelimit.Limiter

var reConnectOnError bool
var logOnError bool
var connCoolDown int
var connSockTimeout int
var connConnTimeout int
var keyTTL int

var valueMode string

// 创建redis 链接
func getRedisConn(hostPort, passwd string) (redis.Conn, error) {
	conn, err := redis.Dial("tcp", hostPort,
		redis.DialReadTimeout(time.Duration(connSockTimeout)*time.Millisecond),
		redis.DialWriteTimeout(time.Duration(connSockTimeout)*time.Millisecond),
		redis.DialConnectTimeout(time.Duration(connConnTimeout)*time.Millisecond))
	if err != nil {
		fmt.Printf("[conn] connect %v failed: %v\n", hostPort, err)
		return nil, err
	} else {
		fmt.Printf("[conn] connect %v ok\n", hostPort)
	}

	if passwd != "" && passwd != "nopass" {
		conn.Do("AUTH", passwd)
	}

	return conn, nil
}

// 初始化参数
func init() {
	flag.IntVar(&connCoolDown, "conn-cool-down", 10, "connection reconnect cool down ms.")
	flag.IntVar(&connSockTimeout, "conn-sock-timeout", 60000, "connection socket timeout ms.")
	flag.IntVar(&connConnTimeout, "conn-conn-timeout", 60000, "connection connect timeout ms.")
	flag.IntVar(&keyTTL, "key-ttl", 60, "key ttl for expire/hsetexpire command.")
	flag.BoolVar(&logOnError, "log-on-error", true, "log on error?")
	flag.BoolVar(&reConnectOnError, "reconnect-on-error", true, "reconnect on error?")
	flag.StringVar(&valueMode, "value-mode", "random", "how to generate value? random/zero/digit/ror")

	delayHist = metrics.NewHistogram(metrics.NewUniformSample(1028))
	cpuHist = metrics.NewHistogram(metrics.NewUniformSample(1028))
	memHist = metrics.NewHistogram(metrics.NewUniformSample(1028))
	diskreadHist = metrics.NewHistogram(metrics.NewUniformSample(1028))
	diskwriteHist = metrics.NewHistogram(metrics.NewUniformSample(1028))
	metrics.Register("latency", delayHist)
	metrics.Register("cpu", cpuHist)
	metrics.Register("mem", memHist)
	metrics.Register("diskread", diskreadHist)
	metrics.Register("diskwrite", diskwriteHist)

}

func usage() {
	// fmt.Println("bench <redis-host-port> <paswd> <107|nop|get|mget|set|setex|hmset|hmget|hgetall|hget|hset|hsetexpire|del|expire|zadd|zrangebyscore|sadd|smembers|rpushall|rpush|rpop|lrange|lindex|setbit|getbit|bitop|mgetmiss|json.get|json.set|json.get1|json.set1|pfadd|pfcount>-[multi/tiny/small/medium/big/huge/ignorenil] <count> <prefix> <nthd> [qps] [diskname]")
	// fmt.Println("  note: rpushall pushes 32(eles)*64(byte per ele) into list(usually used to init list), while rpush pushes one ele (64 byte) into list")
	// fmt.Println("        mget gets 64 random keys")
	fmt.Println("bench <redis-host-port> <paswd> <hmset>-<subkeysize>-<valuesize> <count> <prefix> <nthd> [qps]")
}

func randomString(l int) string {
	buff := make([]byte, int(math.Ceil(float64(l)/2)))
	rand.Read(buff)
	str := hex.EncodeToString(buff)
	return str[:l] // strip 1 extra character we get from odd length results
}

func uniqString(prefix string, l int) string {
	chars := "0123456789ABCDEF"
	buff := make([]byte, l+len(prefix))
	for i := 0; i < l; i++ {
		buff[i] = chars[i%16]
	}
	return prefix + string(buff)
}

func digitString(l int) string {
	chars := "0123456789"
	buff := make([]byte, l)
	for i := range buff {
		buff[i] = chars[i%10]
	}
	return string(buff)
}

func zeroString(l int) string {
	buff := make([]byte, l)
	for i := range buff {
		buff[i] = '0'
	}
	return string(buff)
}

func rorString(l int) string {
	buff := make([]byte, l)
	randbytes := make([]byte, int(math.Ceil(float64(l)/6)))
	rand.Read(randbytes)
	randStr := hex.EncodeToString(randbytes)

	var i int
	for i = 0; i < len(randStr) && i < l; i++ {
		buff[i] = randStr[i]
	}
	for ; i < l; i++ {
		buff[i] = '0'
	}

	return string(buff)
}

func generateValue(l int) string {
	if valueMode == "random" {
		return randomString(l)
	} else if valueMode == "zero" {
		return zeroString(l)
	} else if valueMode == "digit" {
		return digitString(l)
	} else if valueMode == "ror" {
		return rorString(l)
	} else {
		panic("value mode unpexcted")
	}
}

func hmsetCommand(conn redis.Conn, key, cmd_parse string) (e error) {
	splits := strings.Split(cmd_parse, "-")
	//参数0  key个数
	count, err := strconv.Atoi(splits[0])
	if err != nil {
		return err
	}
	val_size, err := strconv.Atoi(splits[1])
	if err != nil {
		return err
	}
	subkey_prefix := ""
	if len(splits) > 2 {
		subkey_prefix = splits[1]
	}
	args := []interface{}{key}

	for i := 0; i < count; i++ {
		args = append(args, subkey_prefix+fmt.Sprintf("%05d", i), generateValue(val_size))
	}
	_, err = conn.Do("hmset", args...)
	if err != nil {
		return err
	}
	return
}

func pingCommand(conn redis.Conn, key, cmd_parse string) (e error) {
	_, err := conn.Do("PING")
	if err != nil {
		return err
	}
	return
}

func hgetallCommand(conn redis.Conn, key, cmd_parse string) (e error) {
	_, err := conn.Do("HGETALL", key)
	if err != nil {
		return err
	}
	return
}

func hgetCommand(conn redis.Conn, key, cmd_parse string) (e error) {
	splits := strings.Split(cmd_parse, "-")
	subkey_prefix := ""
	if len(splits) > 0 {
		subkey_prefix = splits[0]
	}

	_, err := conn.Do("HGET", key, subkey_prefix+"00000")
	if err != nil {
		return err
	}
	return
}

func setCommand(conn redis.Conn, key, cmd_parse string) (e error) {
	splits := strings.Split(cmd_parse, "-")
	var err error
	value_size := 1000
	if len(splits) > 0 {
		value_size, err = strconv.Atoi(splits[0])
		if err != nil {
			return err
		}
	}
	_, err = conn.Do("SET", key, generateValue(value_size))
	if err != nil {
		return err
	}
	return
}

func getCommand(conn redis.Conn, key, cmd_parse string) (e error) {
	v, err := redis.String(conn.Do("GET", key))
	if err != nil {
		return err
	}
	if v == "" {
		return fmt.Errorf("empty reply for key: %v", key)
	}
	return
}

func mgetCommand(conn redis.Conn, cmd_parse, key_prefix string, random_max int) (e error) {
	args := []interface{}{}
	splits := strings.Split(cmd_parse, "-")
	if len(splits) == 0 {
		return fmt.Errorf("not find mget key count ")
	}
	key_size, err := strconv.Atoi(splits[0])
	if err != nil {
		return err
	}
	for i := 0; i < key_size; i++ {
		args = append(args, fmt.Sprintf("%v:%v", key_prefix, mrand.Intn(random_max)))
	}
	_, err = redis.Strings(conn.Do("MGET", args...))
	if err != nil {
		return err
	}
	return
}

var benchcommands = map[string]func(conn redis.Conn, cmd_parse, key_prefix string, random_max int) error{
	"mget": mgetCommand,
}
var commands = map[string]func(conn redis.Conn, key, cmd_parse string) error{
	"ping":    pingCommand,
	"set":     setCommand,
	"get":     getCommand,
	"hmset":   hmsetCommand,
	"hgetall": hgetallCommand,
	"hget":    hgetCommand,
}

func parseCommand(cmdstr string) (cmd string, cmd_parse string, err error) {
	strs := strings.SplitN(cmdstr, "-", 2)
	if len(strs) == 0 {
		err = errors.New("command mode format error")
		return
	}
	cmd = strs[0]
	_, exists := commands[cmd]
	_, bench_exists := benchcommands[cmd]
	if !exists && !bench_exists {
		err = errors.New("command not supported")
		return
	}
	if len(strs) == 2 {
		cmd_parse = strs[1]
	}
	return
}

// Get preferred outbound ip of this machine
func GetOutboundIP() net.IP {
	conn, err := net.Dial("udp", "8.8.8.8:80")
	if err != nil {
		log.Fatal(err)
	}
	defer conn.Close()

	localAddr := conn.LocalAddr().(*net.UDPAddr)

	return localAddr.IP
}

func getRedisPid(hostPort, passwd string) (int, error) {
	local_ipa := string(GetOutboundIP())
	var err error

	host := strings.Split(hostPort, ":")[0]
	if host != "127.0.0.1" && host != "localhost" && host != local_ipa {
		fmt.Printf("Redis running in remote, process stats will not be collected.")
		return 0, nil
	}

	conn, err := getRedisConn(hostPort, passwd)
	defer conn.Close()

	infostr, err := redis.String(conn.Do("INFO", "SERVER"))
	if err != nil {
		return 0, err
	}

	r := regexp.MustCompile(`(?m).*process_id:(\d+).*`)
	matches := r.FindStringSubmatch(infostr)

	redisPid, _ := strconv.Atoi(strings.TrimSpace(matches[1]))
	return redisPid, nil
}

var metricStarted bool

func getDiskStats(name string) (read, write int) {
	cmdline := fmt.Sprintf("iostat -y 1 1 | grep %s", name)
	output, _ := exec.Command("bash", "-c", cmdline).Output()
	fields := strings.Fields(string(output))
	if len(fields) < 3 {
		return 0, 0
	}
	readf, _ := strconv.ParseFloat(fields[2], 64)
	writef, _ := strconv.ParseFloat(fields[3], 64)
	read = int(readf)
	write = int(writef)
	return read, write
}

func updateMetrics() {
	if redisPid == 0 {
		return
	}

	p, e := process.NewProcess(int32(redisPid))
	if e != nil {
		fmt.Printf("pid :%v\n", redisPid)
		panic(e)
	}
	for {
		cpuPer, _ := p.Percent(time.Second)
		meminfo, _ := p.MemoryInfo()
		read, write := getDiskStats(diskName)
		cpuHist.Update(int64(cpuPer))
		memHist.Update(int64(meminfo.RSS))
		diskreadHist.Update(int64(read))
		diskwriteHist.Update(int64(write))
	}
}

func startMetric() {
	if !metricStarted {
		metricStarted = true
		go metrics.Log(metrics.DefaultRegistry, time.Second, log.New(os.Stdout, "metrics: ", log.Lmicroseconds))
		go updateMetrics()
	}
}

func dobench(hostPort, passwd string, nkey, nthd int, cmd string, cmd_args string, prefix string, loops int) {
	var wg sync.WaitGroup
	wg.Add(nthd)

	startMetric()
	// var syncCounter int64

	nkeyPerThd := nkey / nthd
	for i := 0; i < nthd; i++ {
		go func(thd int) {
			var con redis.Conn
			var e error
			for {
				con, e = getRedisConn(hostPort, passwd)
				if e != nil {
					time.Sleep(time.Duration(connCoolDown) * time.Millisecond)
				} else {
					break
				}
			}
			randArr := []int{}
			for j := 0; j < nkeyPerThd; j++ {
				randArr = append(randArr, j)
			}

			mrand.Seed(time.Now().UnixNano())
			mrand.Shuffle(len(randArr), func(i, j int) { randArr[i], randArr[j] = randArr[j], randArr[i] })

			for loop := 0; loop < loops; loop++ {
				for j := 0; j < nkeyPerThd; j++ {
					errorHappen := false

					if con == nil {
						time.Sleep(time.Duration(connCoolDown) * time.Millisecond)
						con, e = getRedisConn(hostPort, passwd)
						if con == nil {
							continue
						}
					}

					if rl != nil {
						rl.Take()
					}
					start := time.Now()
					var err error
					// if multi {
					// 	_, err = redis.
					// }
					if handler, exists := benchcommands[cmd]; exists {
						err = handler(con, cmd_args, prefix, nkey)
						if err != nil {
							if err != redis.ErrNil {
								errorHappen = true
							}
						}
					} else if handler, exists := commands[cmd]; exists {
						key := fmt.Sprintf("%v:%v", prefix, thd*nkeyPerThd+randArr[j])
						err = handler(con, key, cmd_args)
						if err != nil {
							if err != redis.ErrNil {
								errorHappen = true
							}
						}
					} else {
						panic("cmd not supported")
					}

					if errorHappen {
						if reConnectOnError {
							if con != nil {
								con.Close()
								con = nil
							}
						}
						if logOnError {
							fmt.Printf("client-%d:%v\n", thd, err)
						}
					}

					delayHist.Update(time.Since(start).Microseconds())
				}
			}
			wg.Done()
		}(i)

	}
	wg.Wait()
}

func main() {
	//解析命令
	flag.Parse()

	fmt.Printf("valueMode=%v\n", valueMode)
	fmt.Printf("flag.Args()=%v\n", flag.Args())

	if len(flag.Args()) < 6 {
		usage()
		return
	}
	// 参数1 地址
	redisHostPort := flag.Arg(0)
	// 参数2 密码
	passwd := flag.Arg(1)
	// 参数3 命令相关
	cmdstr := flag.Arg(2)
	cmd, cmd_args, err := parseCommand(cmdstr)

	if err != nil {
		fmt.Printf("err:%v \n", err)
		usage()
		return
	}
	loops := 1
	// 参数4 次数  如果是小于0   表示无限循环
	count, _ := strconv.Atoi(flag.Arg(3))
	if count < 0 {
		loops = 2147483647
		count = -count
	}

	// 参数5 key前缀
	key_prefix := flag.Arg(4)
	// 参数6 线程数
	nthd, _ := strconv.Atoi(flag.Arg(5))
	// redisPid, _ = getRedisPid(redisHostPort, passwd)

	// 参数7 qps限制
	if len(flag.Args()) > 6 {
		qps, _ := strconv.Atoi(flag.Arg(6))
		if qps > 0 {
			rl = ratelimit.New(qps)
		}
	}

	//监控磁盘名称
	if len(flag.Args()) > 7 {
		diskName = flag.Arg(7)
	} else {
		diskName = "sdb"
	}

	//开始时间
	startTS := time.Now()

	dobench(redisHostPort, passwd, count, nthd, cmd, cmd_args, key_prefix, loops)
	timeSpan := time.Now().Sub(startTS)
	qps := float64(count) / timeSpan.Seconds()

	fmt.Printf("bench with %v keys, got QPS: %v \n", count, qps, loops)
	time.Sleep(2 * time.Second)
}
