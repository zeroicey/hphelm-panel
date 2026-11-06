# CPU 百分比在 `ondemand` + 空载下会差 5 倍：同一份代码量出 5.8% 和 3.7%

**日期：** 2026-11-06 · **作者：** 第十三轮（单机化） · **影响：** 所有「CPU 预算」结论

## 症状

同一份二进制、同一块屏、同一个 `--scale 1.48`，量出来的 CPU 是 **5.8%**；
把 cpufreq 固定到 `performance` 再量，同一个版本是 **3.7%**。差 1.6 倍；
而在机器被别的进程压满（频率顶到 1.8 GHz）时，另一份**更贵**的二进制只量到 **2.9%**。

⇒ 「新代码是不是变慢了」这个问题，在 `ondemand` 上根本回答不了。

## 根因

`hpopi5b` 的 cpufreq governor 是 `ondemand`，空载时 CPU 跑在**最低档 408 MHz**：

```sh
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor   # ondemand
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq   # 408000（空载）
```

面板是 1 Hz 的轻负载（每秒 ~16ms 的绘制），**抬不动频率** ⇒ 每秒那点工作全按 408 MHz 记 CPU-秒；
一旦有别的进程把频率顶上去，同样的工作按 1.8 GHz 记，数字立刻掉一半。
`/proc/<pid>/stat` 的 utime+stime 是**真实的 CPU 时间**，它不会骗人 —— 骗人的是「CPU 时间 ÷ 墙钟」
在变频机器上的可比性。

## 修法（量法，不是代码）

只能 **A/B**，而且必须固定频率：

```sh
G=/sys/devices/system/cpu/cpufreq/policy0/scaling_governor
echo performance > $G
measure() {  # $1 = 标签
  sleep 15                                  # 预热，跳过启动/字体加载
  P=$(pgrep -x hpopi5b-panel)
  j0=$(awk '{print $14+$15}' /proc/$P/stat); t0=$(date +%s%3N)
  sleep 60
  j1=$(awk '{print $14+$15}' /proc/$P/stat); t1=$(date +%s%3N)
  echo "$1: $(echo "scale=3; ($j1-$j0)*1000/($t1-$t0)" | bc)%"
}
# 换二进制前必须 systemctl stop，否则 install 撞 "Text file busy"（本轮踩过一次，
# 那一轮「旧版」其实还在跑新版，白量 90 秒）
measure "旧 .prev"; systemctl stop hpopi5b-kiosk; install -m755 NEW BIN; systemctl start hpopi5b-kiosk
measure "新版"
echo ondemand > $G
```

2026-11-06 实测（真屏 `:0`，两次均 1.8 GHz、各 60s）：旧三列 **3.47%** → 新单机 **3.68%**。

## 守卫

- `docs/verify.md` 五节已改成「只能 A/B + 固定频率」，并写清 408 MHz / 1.8 GHz 两个读数。
- `.ai/CURRENT_TASK.md` 的 Constraints 写明「`<1%` 在 hpopi5b 上量不出来」，只当相对门槛。
- 报 CPU 数字时**必须同时给出**：governor、`scaling_cur_freq`、窗口长度、`load average`。

## 连带教训（同一轮踩的）

量性能前先看有没有别人在烧核：

```sh
ps -eo pcpu,pid,comm --sort=-pcpu | head
```

本轮发现 `/tmp/render_tr` 的 **5 个孤儿进程各 100% CPU、已烧 3 天**（11-05 14:02 的测试残留），
`load average` 5.2。它们既让频率一直顶在高档（让测量「显得便宜」），又让任何墙钟计时失真。
