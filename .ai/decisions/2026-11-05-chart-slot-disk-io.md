# 第 4 格曲线从「温度」换成「磁盘 I/O 速率」

**Status:** ✅ ACCEPTED · **Date:** 2026-11-05 · **Deciders:** 用户（"可以"，m00071）+ 维护 AI · **Supersedes:** none

## Context

用户报障（m00001）：资源面板 hpcmd 列「磁盘上方那条折线图」不显示，hpcore 列有 —— 问是不是因为
hpcmd 是 hpcore 的虚拟系统。

实测：

- 那条曲线是**温度**。读取端两处都只认 `hwmon name == k10temp`：`src/metrics.c` 的 `read_temp()`、
  `tools/probe-remote.pl` 的 `temp_c()`。
- `hpcmd` 是 `hpcore` 上的 KVM guest（`sudo virsh list --all` ⇒ `hpcmd running`，`systemd-detect-virt` = kvm，
  DMI = QEMU）。它 `/sys/class/hwmon` **完全为空** ⇒ 永远没有温度。
- 部署机 `hpopi5b`（Pi）的 hwmon 叫 `soc_thermal`/`*_thermal`，也不叫 k10temp ⇒ **三列里只有 hpcore 画得出**。
- 顺带查出两个更大的洞：`read_disk()` 只认 `/sys/block/nvme0n*`（Pi 是 `mmcblk1`、hpcmd 是 `vda`）
  ⇒ 那两列连「SSD 累计读/写」都没有；`remote.c` 对探针的**追加段从未解析** ⇒ 缓存 / PSI / 线程数 /
  累计流量 / 丢错包 在远端列永远是 0（拿假 0 冒充真数据）。

## Options

| Option | Upside | Downside | Reversibility |
| --- | --- | --- | --- |
| A 磁盘读/写速率双曲线（选定） | 三机通用（`/sys/block` 整盘 stat 都有）；补的是面板上**唯一没有**的维度；位置就在磁盘块正上方，读图不用猜 | 约 70 行；共享量程时「写≪读」会贴基线 | easy（换回单曲线即可） |
| B PSI io 曲线 | 只 5 行，数据已在采 | 与脚注「压力 io」重复；语义抽象，不如 MB/s 直观 | easy |
| C load/ncore 归一化曲线 | 数据已有 | 核数差异大，跨列不可比 | easy |
| D 保持空白 + 诚实占位文案 | 零代码 | 浪费一整格；用户已经把它当「坏了」 | easy |

## Decision

- 第 4 格固定画**磁盘 I/O 双曲线**：teal = 读、peach = 写（与网络 ↓/↑ 同一套颜色语义），共享量程
  （`vmax = v_nice_ceil(max(r1, r2))`）。
- 该格**不设标题行**：颜色即图例，当前值写在下面的磁盘行（同色「读 X 写 Y」），慢变的累计 GB 进脚注。
- 温度降级为 CPU 行里的 `%.0f°C` 文本（本来就是），**不再占曲线格**。
- 数据采集改成扫 `/sys/block` 的**所有整盘**（跳过 `loop/ram/zram/nbd/sr/fd/dm-/mtdblock`），
  字段随之从 `have_ssd/ssd_*` 改名为 `have_disk/disk_*`（Pi 上那块是 SD/eMMC，叫 SSD 是错的）。