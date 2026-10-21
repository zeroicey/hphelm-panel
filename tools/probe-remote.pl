#!/usr/bin/perl
# probe-remote.pl —— 远程指标探针（**在被测机**上跑，只读 /proc 与 /sys）
#
# 它不需要被安装：hphelm-panel 把这份脚本从 stdin 喂给 ssh，远端只是 `perl -`。
# 手动调试：
#   ssh -T -o BatchMode=yes hpcore 'perl -' < tools/probe-remote.pl
#
# 输出契约（1 Hz，stdout 只允许下面四行；解析器在 src/remote.c）：
#   S <ts> <host> <cpu%> <ncore> <uptime> <memT> <memU> <swapT> <swapU> <disk%> <diskGB>
#     <rxBps> <txBps> <tempC> <load1> <load5> <load15> <freqMHz> <dock> <nproc> <gov> <iface> <ip>
#     <memCachedMB> <psiCpu> <psiMem> <psiIo> <nthreads> <rxTotalMB> <txTotalMB> <netErr> <netDrop>
#   ⚠️ 新字段**只能追加在末尾**：解析器对缺失字段取 0，老探针 + 新面板不会崩。
#   C <core0%> <core1%> ...           （ncore 个）
#   P <nproc> <pid>:<rssMiB>:<name>,...   （每 3 秒一次，最多 24 条）
#   E                                  （一帧结束 ⇒ 只有收到 E 才提交，半帧不会被显示）
#
# ⛔ 三条硬约束（这台是**生产机**）：
#   ① 只读 /proc /sys；不写文件、不装东西、不起服务、不开端口
#   ② 每帧成本必须远低于 1% of one core（实测 ~0.3%）
#   ③ 名字里的 `:` `,` 要替换掉（它是 P 行的分隔符）
use strict;
use warnings;
use Socket;

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

# ---------------- 进程榜（RSS 降序） ----------------
sub procs {
    opendir my $d, '/proc' or return (0, '');
    my @pids = grep { /^\d+$/ } readdir $d;
    closedir $d;
    my @rows;
    for my $pid (@pids) {
        my $s = slurp("/proc/$pid/stat") // next;
        my ($comm, $rest) = $s =~ /^[^(]*\((.*)\)\s+(.*)$/s or next;
        my @f = split ' ', $rest;
        my $rss_pages = $f[21] // 0;
        next if $rss_pages < 128;    # < 0.5 MiB 的不进榜
        # 名字里不允许出现空格/逗号/冒号（它们是 P 行的分隔符）
        $comm =~ s/[\s;,:]/_/g;
        push @rows, [$rss_pages * 4, $pid, $comm];    # 4 KiB/页（x86_64）
    }
    @rows = sort { $b->[0] <=> $a->[0] } @rows;
    splice @rows, 24 if @rows > 24;
    my @o = map { sprintf '%d:%.1f:%s', $_->[1], $_->[0] / 1024, $_->[2] } @rows;
    return (scalar @pids, join(',', @o));
}

# ---------------- 网卡（有全局 IPv4 的 en* > wl* > 其它） ----------------
sub iface_ip {
    my $best = '';
    my $rank0 = 0;
    opendir my $d, '/sys/class/net' or return ('', '');
    my @names = grep { !/^(lo|tailscale.*|docker.*|veth.*|br-.*|virbr.*)$/ } readdir $d;
    closedir $d;
    for my $n (@names) {
        my $rank = $n =~ /^en/ ? 3 : $n =~ /^wl/ ? 2 : 1;
        next if $rank < $rank0 || ($rank == $rank0 && $best ne '');
        socket(my $s, PF_INET, SOCK_DGRAM, 0) or next;
        my $packed = pack('a16', $n);
        next unless defined ioctl($s, 0x8915, $packed);    # SIOCGIFADDR
        close $s;
        next if substr($packed, 20, 2) eq "\0\0";          # 没有 IPv4
        $best  = $n;
        $rank0 = $rank;
    }
    my $ip = '';
    if ($best ne '') {
        socket(my $s, PF_INET, SOCK_DGRAM, 0);
        my $packed = pack('a16', $best);
        if (defined ioctl($s, 0x8915, $packed)) {
            $ip = join '.', unpack 'C4', substr($packed, 20, 4);
        }
        close $s;
    }
    return ($best, $ip);
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

my $host = trim(slurp('/proc/sys/kernel/hostname') // 'remote');
my ($iface, $ip) = iface_ip();
my ($rx_p, $tx_p, $iter, $nproc_last, $dock) = (undef, undef, 0, 0, -1);
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

    my ($nproc, $top) = ('', '');
    if ($iter % 3 == 0) { ($nproc, $top) = procs(); $nproc_last = $nproc if $nproc ne '' }
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
         . " %.0f %.1f %.1f %.1f %d %.0f %.0f %d %d\n",
        time, $host, $cpu, $ncore // 0, $uptime,
        $mem_total, $mem_total - $mem_avail, $sw_total, $sw_total - $sw_free,
        $disk, $rx_bps, $tx_bps, $temp, $l1, $l5, $l15,
        $freqk / 1000, $dock, $nproc, $gov, $iface, $ip,
        $mem_cached, $psi_cpu, $psi_mem, $psi_io, $nthr, $rx_total, $tx_total, $nerr, $ndrop;
    print 'C ', join(' ', @core), "\n";
    print "P $nproc $top\n" if $top ne '';
    print "E\n";
    $iter++;
}
