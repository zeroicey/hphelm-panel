#!/usr/bin/perl
# probe-remote.pl —— 远程指标探针（**在被测机**上跑，只读 /proc 与 /sys）
#
# 它不需要被安装：hpopi5b-panel 把这份脚本从 stdin 喂给 ssh，远端只是 `perl -`。
# 手动调试：
#   ssh -T -o BatchMode=yes hpcore 'perl -' < tools/probe-remote.pl
#
# 输出契约（1 Hz，stdout 只允许下面四行；解析器在 src/remote.c）：
#   S <ts> <host> <cpu%> <ncore> <uptime> <memT> <memU> <swapT> <swapU> <disk%> <diskGB>
#     <rxBps> <txBps> <tempC> <load1> <load5> <load15> <freqMHz> <dock> <nproc> <gov> <iface> <ip>
#     <memCachedMB> <psiCpu> <psiMem> <psiIo> <nthreads> <rxTotalMB> <txTotalMB> <netErr> <netDrop>
#     <diskReadBps> <diskWriteBps>   （整盘 /sys/block/*/stat 求和，跳 loop/ram/zram/mtd 等）
#   ⚠️ 新字段**只能追加在末尾**：解析器对缺失字段取 0，老探针 + 新面板不会崩。
#   C <core0%> <core1%> ...           （ncore 个）
#   P <nproc> <pid>:<rssMiB>:<cpu%>:<name>,...   （每 3 秒一次，按 RSS 降序，最多 24 条；面板已不画，留给调试）
#   Q <pid>:<rssMiB>:<cpu%>:<name>,...            （每 3 秒一次，按 CPU% 降序、同值比 RSS，最多 40 条）
#                                                 ⚠️ 2026-11-06 起 P 行从 3 字段改成 4 字段（中间插了 cpu%
#   T <总台数> <在线数> <本机IP> <name>:<os>:<直连0|1>:<DERP区域>:<在线0|1>:<离线秒>:<rxBps>:<txBps>,...
#                                                 （每 3 秒一次，在线优先、再按流量降序，最多 10 台；
#                                                  来自 tailscaled 的 localapi，不 fork tailscale CLI）
#   E                                  （一帧结束 ⇒ 只有收到 E 才提交，半帧不会被显示）
#
# ⛔ 三条硬约束（这台是**生产机**）：
#   ① 只读 /proc /sys 与两个**只读** localapi（tailscaled 的 unix socket）；不写文件、不装东西、不起服务、不开端口
#   ② 每帧成本必须远低于 1% of one core（实测 ~0.3%）
#   ③ 名字里的 `:` `,` 要替换掉（它是 P/Q/T 行的分隔符）
use strict;
use warnings;
use Socket;
use JSON::PP;      # 5.14 起是核心模块
use Time::Local;   # timegm()

$| = 1;

sub slurp {
    my ($p) = @_;
    open my $f, '<', $p or return undef;
    local $/;
    my $c = <$f>;
    close $f;
    return $c;
}

sub trim { my $s = shift; $s =~ s/\s+$// if defined $s; $s }

# ---------------- CPU（一次读全核，比较两次快照） ----------------
sub cpu_snap {
    my $c = slurp('/proc/stat') // return ();
    my (@tot, @busy, $n);
    for my $line (split /\n/, $c) {
        next unless $line =~ /^cpu(\d*)\s+(.*)$/;
        my $idx = $1 eq '' ? 0 : $1 + 1;
        my @v = split ' ', $2;
        my $idle = ($v[3] // 0) + ($v[4] // 0);
        my $t = 0;
        $t += $_ for @v[0 .. ($#v < 9 ? $#v : 9)];
        $tot[$idx]  = $t;
        $busy[$idx] = $t - $idle;
        $n++ if $idx > 0;
    }
    return (\@tot, \@busy, $n);
}

# ---------------- 进程榜（两份：RSS 降序 / CPU 降序） ----------------
# CPU% = 两次采样之间 utime+stime 的差 / (USER_HZ × dt) × 100，可以 >100（多线程进程）。
# 榜的粒度就是 procs() 的调用间隔（3 秒均值）—— 面板这一项本来就 3 秒才刷一次。
# ⚠️ 首帧没有"上一帧"⇒ 全 0，这是诚实的空态；不要用 /proc/<pid>/stat 的累计值假装瞬时占用。
my %pj;      # pid => 上一帧的 utime+stime（jiffies）
my $pj_t;    # 上一帧的时刻（秒）
sub procs {
    opendir my $d, '/proc' or return (0, '', '');
    my @pids = grep { /^\d+$/ } readdir $d;
    closedir $d;
    my $hz = 100;    # USER_HZ 恒为 100（内核 ABI，与 CONFIG_HZ 无关）
    my $now = time;
    my $dt  = defined $pj_t ? $now - $pj_t : 0;
    $pj_t = $now;
    my (@all, %seen);
    for my $pid (@pids) {
        my $s = slurp("/proc/$pid/stat") // next;
        my ($comm, $rest) = $s =~ /^[^(]*\((.*)\)\s+(.*)$/s or next;
        my @f = split ' ', $rest;
        my $jif = ($f[11] // 0) + ($f[12] // 0);    # utime/stime = 整体第 14/15 字段
        my $d = 0;
        $d = ($jif - $pj{$pid}) / $hz / $dt * 100 if exists $pj{$pid} && $dt > 0;
        $seen{$pid} = $jif;
        $d = 0 if $d < 0;    # pid 复用/jiffies 回绕
        my $rss_pages = $f[21] // 0;
        # 名字里不允许出现空格/逗号/冒号（它们是 P/Q 行的分隔符）
        $comm =~ s/[\s;,:]/_/g;
        push @all, [ $rss_pages * 4096, $pid, $comm, $d ];    # 4 KiB/页（x86_64/arm64）
    }
    %pj = %seen;
    # 内存榜：<0.5 MiB 不进榜（否则一屏都是 rpm 小进程）
    my @by_mem = sort { $b->[0] <=> $a->[0] } grep { $_->[0] >= 512 * 1024 } @all;
    splice @by_mem, 24 if @by_mem > 24;
    # 单一榜：按 CPU% 降序（同值比 RSS）。面板只画这一条榜 ——
    # 2026-11-06 用户口径：「两个一模一样的列表」没必要。这样在空载时它会自然退化成
    # 「最大的几个进程」（cpu 全是 0.00 时按 RSS 排），原来想用两段榜表达的东西就这一条有了。
    my @by_cpu = sort { $b->[3] <=> $a->[3] || $b->[0] <=> $a->[0] } @all;
    splice @by_cpu, 40 if @by_cpu > 40;
    my $fmt = sub {
        return join(',', map { sprintf '%d:%.1f:%.1f:%s', $_->[1], $_->[0] / 1048576, $_->[3], $_->[2] } @_);
    };
    return (scalar @pids, $fmt->(@by_mem), $fmt->(@by_cpu));
}

# ---------------- Tailscale 尾网（tailscaled 的 localapi） ----------------
# 只读 localapi：GET /localapi/v0/status（socket 权限 0666 ⇒ **不需要 root**），
# **不 fork tailscale CLI**（那要起一个进程、几百毫秒，1Hz 的探针扛不住）。
# 契约：名字/系统/区域里空白、冒号、逗号换成 `_`（T 行按冒号切字段、按逗号分记录）。
# ⚠️ JSON 里对端列表的键是 `Peer`（单数），不是 `Peers` —— 写成 Peers 会静默得到 0 台。
my $TS_SOCK = '/var/run/tailscale/tailscaled.sock';
my $have_unix = eval { require IO::Socket::UNIX; 1 };
my (%ts_rx, %ts_tx);    # nodekey => 上一帧的累计字节
my $ts_t;               # 上一帧的时刻（秒）
my $ts_last = '';       # 上一帧成功的 T 行；tailscaled 临时不可用时沿用，不闪空

sub iso_epoch {
    my ($s) = @_;
    return undef
        unless defined $s
        && $s =~ /^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)(?:\.\d+)?([+-])(\d\d):(\d\d)$/;
    my $e = timegm($6, $5, $4, $3, $2 - 1, $1);
    return undef unless defined $e;
    my $off = $8 * 3600 + $9 * 60;
    return $7 eq '+' ? $e - $off : $e + $off;
}

sub ts_status {
    my $now = time;
    my $s;
    if ($have_unix && -S $TS_SOCK) {
        $s = IO::Socket::UNIX->new(Type => Socket::SOCK_STREAM(), Peer => $TS_SOCK);
    }
    return $ts_last unless $s;
    print $s "GET /localapi/v0/status HTTP/1.0\r\nHost: local-tailscaled.sock\r\nConnection: close\r\n\r\n";
    my $resp = '';
    while (1) {
        my $n = sysread($s, my $buf, 65536);
        last unless $n;
        $resp .= $buf;
        last if length $resp > 262144;    # 尾网再大也就几十 KB，防跑飞
    }
    close $s;
    return $ts_last unless $resp =~ /\AHTTP\/1\.[01] 200/;
    my $i = index($resp, "\r\n\r\n");
    return $ts_last if $i < 0;
    my $j = eval { JSON::PP->new->utf8->decode(substr($resp, $i + 4)) };
    return $ts_last unless $j && ref $j eq 'HASH';

    my $dt = defined $ts_t ? $now - $ts_t : 0;
    $ts_t = $now;
    my $peers = $j->{Peer} || {};
    my ($total, $online) = (1, ($j->{Self}{Online} ? 1 : 0));    # 含本机
    my (@rows, %seen);
    for my $k (keys %$peers) {
        my $p = $peers->{$k};
        $total++;
        my $on = $p->{Online} ? 1 : 0;
        $online++ if $on;
        my $last = 0;
        if (!$on) {
            my $t = iso_epoch($p->{LastSeen});
            $last = $now - $t if defined $t && $t > 0;
        }
        my ($rx, $tx) = ($p->{RxBytes} // 0, $p->{TxBytes} // 0);
        my ($rxr, $txr) = (0, 0);
        if ($dt > 0 && defined $ts_rx{$k} && $rx >= $ts_rx{$k} && $tx >= $ts_tx{$k}) {
            $rxr = ($rx - $ts_rx{$k}) / $dt;
            $txr = ($tx - $ts_tx{$k}) / $dt;
        }
        ($ts_rx{$k}, $ts_tx{$k}) = ($rx, $tx);
        $seen{$k} = 1;
        my $name = $p->{HostName} // '?';
        my $os   = $p->{OS}       // '?';
        my $rel  = $p->{Relay}    // '';
        my $dir  = ($p->{CurAddr} // '') ne '' ? 1 : 0;
        for ($name, $os, $rel) { s/[\s:,;|]+/_/g }
        push @rows, [$on, $rxr + $txr, $name, $os, $dir, $rel, $on, $last, $rxr, $txr];
    }
    # 已删除的设备：别让记忆表无限长
    for my $h (\%ts_rx, \%ts_tx) {
        my %keep = map { $_ => 1 } keys %seen;
        %$h = map { $_ => $h->{$_} } grep { $keep{$_} } keys %$h;
    }
    # 在线优先，再按流量降序，最后按名字（排序稳定 ⇒ 行不会每帧乱跳）
    my @top = (grep { defined } (sort {
                $b->[0] <=> $a->[0] || $b->[1] <=> $a->[1] || $a->[2] cmp $b->[2]
            } @rows)[0 .. 9]);
    my $ip = ($j->{TailscaleIPs} && $j->{TailscaleIPs}[0]) ? $j->{TailscaleIPs}[0] : '-';
    $ts_last = sprintf 'T %d %d %s %s', $total, $online, $ip,
        join(',', map { sprintf '%s:%s:%d:%s:%d:%d:%.0f:%.0f', @$_[2, 3, 4, 5, 6, 7, 8, 9] } @top);
    return $ts_last;
}

sub addrv4 {
    my ($n) = @_;
    socket(my $s, PF_INET, SOCK_DGRAM, 0) or return '';
    my $packed = pack('a16', $n);
    if (defined ioctl($s, 0x8915, $packed)) {    # SIOCGIFADDR
        close $s;
        return '' if substr($packed, 20, 2) eq "\0\0";
        return join '.', unpack 'C4', substr($packed, 20, 4);
    }
    close $s;
    return '';
}

# 默认路由所在网卡：/proc/net/route 里 Destination=00000000 的那一行。
# ⚠️ 2026-11-03 踩到：hpcore 的 enp3s0 上挂着渗透测试时代的残留地址 10.99.0.1/24，
#    而原先「按 en* 前缀挑第一个有 IPv4 的」正好挑中它 ⇒ 面板把生产机显示成
#    10.99.0.1/enp3s0。判据：显示的 IP 必须是「能出网的那块网卡」的地址。
sub default_route_iface {
    open my $r, '<', '/proc/net/route' or return '';
    my $hdr = <$r>;
    my $best = '';
    my $best_metric;
    while (my $line = <$r>) {
        my @f = split ' ', $line;
        next unless @f >= 8;
        next unless $f[1] eq '00000000';    # Destination = default
        my $metric = hex($f[6]);
        if (!defined $best_metric || $metric < $best_metric) {
            $best = $f[0];
            $best_metric = $metric;
        }
    }
    close $r;
    return $best;
}

sub iface_ip {
    # ① 优先：默认路由网卡（这才是「这台机对外是谁」）
    my $dr = default_route_iface();
    if ($dr ne '') {
        my $ip = addrv4($dr);
        return ($dr, $ip) if $ip ne '';
    }
    # ② 兜底：原来的启发式（按前缀排序挑第一个有 IPv4 的网卡）
    my $best = '';
    my $rank0 = 0;
    opendir my $d, '/sys/class/net' or return ('', '');
    my @names = grep { !/^(lo|tailscale.*|docker.*|veth.*|br-.*|virbr.*)$/ } readdir $d;
    closedir $d;
    for my $n (@names) {
        my $rank = $n =~ /^en/ ? 3 : $n =~ /^wl/ ? 2 : 1;
        next if $rank < $rank0 || ($rank == $rank0 && $best ne '');
        next if addrv4($n) eq '';
        $best  = $n;
        $rank0 = $rank;
    }
    return ($best, $best eq '' ? '' : addrv4($best));
}

# PSI：/proc/pressure/{cpu,memory,io} 第一行的 avg10
sub psi {
    my ($p) = @_;
    my $c = slurp($p) // return 0;
    return $c =~ /some\s+avg10=([\d.]+)/ ? $1 : 0;
}

# 温度：按名字找 k10temp（别写死 hwmonN）
my $tempfile;
for my $h (glob '/sys/class/hwmon/hwmon*') {
    my $name = slurp("$h/name") // '';
    next unless $name =~ /k10temp/;
    $tempfile = "$h/temp1_input";
    last;
}

# 整盘累计读写扇区（× 512 = 字节）。跳过的设备与 src/metrics.c 的 read_disk() 同口径，
# 否则本机列和远端列的曲线不可比：loop/ram/zram/nbd/sr/fd/mtdblock 不是真盘，
# dm-* 是映射层（与底层整盘重复计数）。分区不在 /sys/block 里，天然不重复。
sub disk_rw {
    my ($rd, $wr) = (0, 0);
    opendir my $d, '/sys/block' or return (0, 0);
    my @dev = grep { $_ ne '.' && $_ ne '..' && !/^(loop|ram|zram|nbd|sr|fd|dm-|mtdblock)/ }
        readdir $d;
    closedir $d;
    for my $n (@dev) {
        my $s = slurp("/sys/block/$n/stat") // next;
        my @f = split ' ', $s;
        next if @f < 7;
        $rd += $f[2];
        $wr += $f[6];
    }
    return ($rd, $wr);
}

my $host = trim(slurp('/proc/sys/kernel/hostname') // 'remote');
my ($iface, $ip) = iface_ip();
my ($rx_p, $tx_p, $drd_p, $dwr_p, $iter, $nproc_last, $dock) = (undef, undef, undef, undef, 0, 0, -1);
my $disk_stmt = '0:0';

while (1) {
    my ($t0, $b0, $ncore) = cpu_snap();
    select undef, undef, undef, 1.0;
    my ($t1, $b1) = cpu_snap();

    my ($cpu, @core) = (0);
    if (@$t1) {
        my $dt = $t1->[0] - $t0->[0];
        $cpu = $dt > 0 ? ($b1->[0] - $b0->[0]) / $dt * 100 : 0;
        for my $c (1 .. ($ncore // 0)) {
            my $d = ($t1->[$c] // 0) - ($t0->[$c] // 0);
            push @core, $d > 0 ? sprintf('%.0f', (($b1->[$c] // 0) - ($b0->[$c] // 0)) / $d * 100) : '0';
        }
    }

    my %mi;
    if (my $meminfo = slurp('/proc/meminfo')) {
        while ($meminfo =~ /^(\w+):\s+(\d+)/mg) { $mi{$1} = $2 }
    }
    my $uptime = (split ' ', slurp('/proc/uptime') // '0')[0] // 0;
    my $load   = slurp('/proc/loadavg') // '0 0 0';
    my ($l1, $l5, $l15) = ($load =~ /([\d.]+)\s+([\d.]+)\s+([\d.]+)/) ? ($1, $2, $3) : (0, 0, 0);
    my $gov   = trim(slurp('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor') // '-');
    my $freqk = trim(slurp('/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq') // 0) || 0;
    my $tv    = $tempfile ? slurp($tempfile) : undef;
    my $temp  = $tv ? $tv / 1000 : -1;
    $temp = -1 if $temp < 0 || $temp > 200;

    # 网络速率（/proc/net/dev 的累计字节，按两次快照相减）
    my ($rx, $tx) = (0, 0);
    if (my $nd = slurp('/proc/net/dev')) {
        for (split /\n/, $nd) {
            next unless /^\s*([^:\s]+):\s*(\d+)(?:\s+\d+){7}\s+(\d+)/;
            next unless $1 eq $iface;
            ($rx, $tx) = ($2, $3);
        }
    }
    my ($rx_bps, $tx_bps) = (0, 0);
    if (defined $rx_p) { $rx_bps = $rx - $rx_p; $tx_bps = $tx - $tx_p }
    ($rx_p, $tx_p) = ($rx, $tx);

    # 磁盘读写速率（扇区 × 512 ⇒ B/s；循环一圈就是 1 秒，与上面 rx/tx 同口径）
    my ($drd, $dwr) = disk_rw();
    my ($drd_bps, $dwr_bps) = (0, 0);
    if (defined $drd_p) {
        $drd_bps = ($drd - $drd_p) * 512;
        $dwr_bps = ($dwr - $dwr_p) * 512;
    }
    ($drd_p, $dwr_p) = ($drd, $dwr);

    # 磁盘（statvfs 没有核心模块 ⇒ 每 10 秒一次 df；fork 成本可忽略）
    my $disk = $disk_stmt;
    if ($iter % 10 == 0) {
        my $out   = `df -P / 2>/dev/null`;
        my @lines = grep { m{^/} } split /\n/, $out;
        my @f     = split ' ', ($lines[-1] // '');
        my ($pct) = ($f[4] // '0') =~ /(\d+)/;
        $disk = sprintf('%d:%.1f', $pct // 0, ($f[1] // 0) / 1048576);
    }
    $disk_stmt = $disk;

    # 容器数（cgroup 里有几个 docker-*.scope 就是几个容器；不 fork、不依赖 docker 权限）
    if ($iter % 10 == 0) {
        opendir my $cg, '/sys/fs/cgroup/system.slice';
        if ($cg) {
            my @sc = grep { /^docker-.*\.scope$/ } readdir $cg;
            closedir $cg;
            $dock = scalar @sc;
        } elsif ($dock < 0) {
            $dock = -1;    # 没有 cgroup v2 / 不是 docker ⇒ 面板不显示这一项
        }
    }

    my ($nproc, $top, $topcpu, $ts) = ('', '', '', '');
    if ($iter % 3 == 0) {
        ($nproc, $top, $topcpu) = procs();
        $nproc_last = $nproc if $nproc ne '';
        $ts = ts_status();
    }
    $nproc = $nproc_last // 0 if $nproc eq '';

    # 线程总数：/proc/loadavg 第 4 字段 "运行中/总数"
    my ($nthr) = ($load =~ m{/\s*(\d+)}) ? $1 : 0;

    # 网卡错误/丢弃（4 个小计数器）
    my ($rx_total, $tx_total) = ($rx / 1048576.0, $tx / 1048576.0);
    my ($nerr, $ndrop) = (0, 0);
    for my $k (qw(rx_errors tx_errors)) {
        my $v = slurp("/sys/class/net/$iface/statistics/$k");
        $nerr += $v if defined $v;
    }
    for my $k (qw(rx_dropped tx_dropped)) {
        my $v = slurp("/sys/class/net/$iface/statistics/$k");
        $ndrop += $v if defined $v;
    }

    my $psi_cpu = psi('/proc/pressure/cpu');
    my $psi_mem = psi('/proc/pressure/memory');
    my $psi_io  = psi('/proc/pressure/io');
    my $mem_cached = (($mi{Buffers} // 0) + ($mi{Cached} // 0) + ($mi{SReclaimable} // 0)) / 1024;

    my $mem_total = ($mi{MemTotal} // 0) / 1024;
    my $mem_avail = ($mi{MemAvailable} // 0) / 1024;
    my $sw_total  = ($mi{SwapTotal} // 0) / 1024;
    my $sw_free   = ($mi{SwapFree} // 0) / 1024;

    printf "S %d %s %.1f %d %.0f %.0f %.0f %.0f %.0f %s %.1f %.1f %.1f %s %s %s %.0f %d %d %s %s %s"
         . " %.0f %.1f %.1f %.1f %d %.0f %.0f %d %d %.0f %.0f\n",
        time, $host, $cpu, $ncore // 0, $uptime,
        $mem_total, $mem_total - $mem_avail, $sw_total, $sw_total - $sw_free,
        $disk, $rx_bps, $tx_bps, $temp, $l1, $l5, $l15,
        $freqk / 1000, $dock, $nproc, $gov, $iface, $ip,
        $mem_cached, $psi_cpu, $psi_mem, $psi_io, $nthr, $rx_total, $tx_total, $nerr, $ndrop,
        $drd_bps, $dwr_bps;
    print 'C ', join(' ', @core), "\n";
    print "P $nproc $top\n" if $top ne '';
    print "Q $topcpu\n" if $topcpu ne '';
    print "$ts\n" if $ts ne '';
    print "E\n";
    $iter++;
}
