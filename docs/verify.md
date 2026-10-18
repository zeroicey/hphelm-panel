# 验证：看不到图，怎么确定 UI 是对的

> **这是本项目最重要的方法论文件。** 维护这个项目的 AI 通常**无法看图**（模型不支持图像输入），
> 而用户能直接看屏幕。所以「我觉得这样好看」不算完成 —— 必须给出**可复现的像素判据**。
> 相关：[`design.md`](design.md)（设计值）· [`tools/`](../tools/)（脚本）

---

## 一、四类探针（按用途选）

| 工具 | 用途 | 典型输出 |
| --- | --- | --- |
| `tools/render-ascii.py` | 整屏压成 ASCII（亮度字符 + `C`=彩色） | 一眼看出「哪块空、哪块叠、哪块越界」 |
| `tools/render-region.py` | 局部放大（可 1px 一格） | 看某控件形状/对齐是否合理 |
| `tools/measure.py` | **量几何**：色块游程 / 墨迹带 / 颜色 bbox | 柱宽 88px、间距 5px、行距 57.6px |
| `tools/probe-frames` | **抓连续帧做逐像素差分** | 「有无半成品帧」= 「有没有闪」 |

```sh
python3 tools/render-ascii.py   assets/panel.png 22 22
python3 tools/render-region.py  assets/panel.png 1360 80 1900 430 10
python3 tools/measure.py        assets/panel.png --runs 240 --bands
DISPLAY=:0 tools/probe-frames   60
```

---

## 二、判据一：不闪（**每轮必跑**）

**判据**：抓面板窗口连续帧、逐像素比对相邻帧，**`>30%` 像素变化的帧必须是 0**。

```
$ DISPLAY=:0 tools/probe-frames 60
frame 12 diffpx=       0 ( 0.00%)
frame 13 diffpx=   35124 ( 1.69%)  bbox=(52,177)-(1503,584)   ← 1Hz 内容更新，正常
...
---- frames with >30% pixels changed: 0                      ← 通过
```

**「闪」的指纹**（三条同时出现才是）：

1. 单帧变化 **>30%** 像素，且 bbox 横跨整屏（不是某个小块）；
2. **连续两帧都变**（A→B→A 的振荡，而不是单调更新）；
3. 把中间那帧单独存成 PNG，**文件明显更小**（只有底色/没有文字），像素上「盒子/文字消失」。

实测对照（修双缓冲前）：`1205996`、`1286011` 像素变化 / 每秒 2 次 / 中间帧 67KB vs 前后 177KB。

---

## 三、判据二：不空转（**改了主循环/渲染必跑**）

**判据**：`HPHELM_PANEL_DEBUG=1` 下每秒应打印

```
主循环 2 it/s · 事件 0 · poll[可读 0/超时 2/其他 0] · 重绘 1 · 采样 1 · 类型:
```

- `poll` **必须全是「超时」**、事件类型直方图应接近空。出现大量 `NoExpose` / 可读 ⇒ 自激空转。
- `it/s` 应为 2（面板态 poll 500ms）；突然变几十 ⇒ 有东西在叫醒循环。
- `重绘` ≈ 1/秒（内容签名生效）；等于 `it/s` ⇒ 签名没起作用，在无意义重绘。

配套用 `tools/probe-frames` 侧看「内容变化率」：**应为 1 次/秒**。

---

## 四、判据三：几何（**改了版式必跑**）

**原则：设计值是算出来的，实测值必须逐位吻合。**

### 4.1 横向几何 —— 色块游程（run-length）

取某一行，把颜色相同的连续像素合并，打印 `(起点, 终点, 宽, 颜色)`：

```
$ python3 tools/measure.py assets/panel.png --runs 240
y=240 行色块游程：
  x  27- 114 宽  88 rgb(37,39,55)     ← 每核柱轨道
  x 116- 120 宽   5 rgb(9,15,26)      ← 间距
  …
  x 690- 777 宽  88 rgb(37,39,55)
```

期望：8 根柱 × 88px + 7 个 5px 间距（逻辑 60px 柱 / 4px 间距 × 1.48 ≈ 88/5.9）。

### 4.2 纵向节奏 —— 墨迹带（ink bands）

统计每行「非背景像素数」，超过阈值的连续行就是一条「墨迹带」（一行文字）：

```
$ python3 tools/measure.py assets/panel.png --bands
y 501- 528 高 28      ← 第 1 行进程
y 560- 586 高 27      ← 第 2 行（行距 57.6px ≈ 逻辑 39）
…共 10 条，间距均匀
```

期望：**行数 = `L_PROC_ROWS`**，间距均匀（不均匀 = 行高算错/越界）。

### 4.3 「某颜色画在哪」—— 按颜色 bbox + 逐行分布

```sh
# 用容差取色（抗抗锯齿），打印 bbox 与逐行 x 范围
magick shot.png -crop WxH+X+Y +repage -depth 8 rgb:- | python3 …
```

用途：确认某条曲线**画满了整段宽度**、某强调色**只出现在该出现的地方**。

⚠️ **别用「整屏扫某颜色」来定位局部**：别的栏里可能有同色元素（例如 peach 既在「↑ 速率」也在曲线里），
结果会误导。**先按区域 crop，再取色。**

---

## 五、判据四：性能（**每轮报数字**）

```sh
P=$(pgrep -x hphelm-panel)
read u1 s1 < <(awk '{print $14,$15}' /proc/$P/stat); sleep 20
read u2 s2 < <(awk '{print $14,$15}' /proc/$P/stat)
python3 -c "
hz=$(getconf CLK_TCK); d=($u2+$s2)-($u1+$s1)
print('%.2f%% of one core' % (d*1000.0/hz/20000*100))"
```

期望：**< 1%**（现状 0.60%）。

内存：`grep -E '^(Pss|Pss_Anon)' /proc/$P/smaps_rollup` ⇒ Pss_Anon ≈ 5.4 MiB（PSS 总量会到 25 MiB，
那大部分是 X/picom/pango 的**共享库页**，不是我们的）。

### ⚠️ 单位陷阱（我自己犯过，错 10 倍）

`CLK_TCK = 100` ⇒ **1 jiffie = 10ms**。10 秒窗口的百分比 = `delta / 10`，**不是** `delta / 100`。
我第一版把「空转 33~49%」写成「3.3~4.9%」、把「0.60%」写成「0.08%」并已写进文档。
⇒ **落任何数字进文档前，先把单位和公式写出来对一遍**；百分比一律由「毫秒 / 窗口毫秒」现算。

---

## 六、判据五：红线没被碰（**每轮必跑，10 秒）**

```sh
cat /sys/class/tty/tty0/active     # tty7（不是 tty8！tty8 = greeter 抢了 VT）
pgrep -cx slick-greeter            # 0
pgrep -cx hphelm-panel             # 1（多了 ⇒ 单实例锁失效）
md5sum hphelm-panel /usr/local/bin/hphelm-panel   # 一致 ⇒ 装的是新二进制
tail -5 /tmp/hphelm-panel.log      # 没有「关屏兜底」「连续 3 次」等兜底痕迹
```

---

## 七、开发时的隔离（**不要一上来动真屏**）

```sh
Xvfb :9 -screen 0 1920x1080x24 -ac &
DISPLAY=:9 ./hphelm-panel --state panel --lock /tmp/xv9.lock
DISPLAY=:9 import -window $(DISPLAY=:9 xdotool search --name hphelm-panel | tail -1) /tmp/shot.png
```

- **必须用 `--lock`** 换个锁路径：默认锁是全机 `/tmp` 的，否则 Xvfb 实例会因「已有实例」直接退出。
- ⚠️ **杀进程用 `pkill -x hphelm-panel` 会连真机实例一起杀**（同名）——
  真机上那台是守护循环托管的，杀了会被自动拉起、用户屏幕会闪一下。
  **测试时按 `DISPLAY` 挑 pid**：`grep -l 'DISPLAY=:9' /proc/*/environ` 或直接记住自己起的 pid。
- `Xvfb` 没有 GPU/picom，**观感类判断要在真机复核**（帧差分、性能、几何三类判据两边都有效）。

---

## 八、截图入库

```sh
# 真机或 Xvfb 抓 1920x1080 → 压成 PNG → 放 assets/（README 用）
magick shot-panel.png -strip -quality 90 assets/panel.png
```

截图只用于 README 展示；**判据不依赖截图**（判据是脚本输出的数字）。
