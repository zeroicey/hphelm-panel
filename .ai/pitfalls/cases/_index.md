# 坑索引（本文件的权威在 `docs/pitfalls.md`）

`.ai/pitfalls/cases/` 只放**新**的、代码级或部署级的解码案例；历史 16 条坑的完整正文在
[`../../docs/pitfalls.md`](../../docs/pitfalls.md)（每条都带判据）。别在这里复制第二份。

## 必读的四条（碰了就会让用户看到不该看到的东西）

| # | 坑 | 一句话判据 |
| --- | --- | --- |
| 13 | 认证复用 `login` PAM 栈 | 输错 3 次 ⇒ 账号被 faillock 锁 ⇒ autologin 失败 ⇒ VT 被 greeter 抢 |
| 14 | 守护/兜底逻辑的三个自伤 | 失败判据用**运行时长**；兜底只许 `xset dpms force off` |
| 15 | 绝不用 `light-locker` / `dm-tool lock` | 两把锁 ⇒ 登录两遍；`dm-tool lock` 缺 `XDG_SEAT_PATH` 会静默失败 |
| 16 | `sleep_off = 0` 曾被写成「立即关屏」 | 裸比较 `idle >= 0` 恒真 ⇒ 判据必须 `CFG_SLEEP_OFF > 0 && …` |

## 本目录的案例

| 文件 | 一句话 |
| --- | --- |
| `2026-10-21-deploy-script-false-alarms.md` | 部署脚本三个假告警：`pgrep -q` 不存在 / 日志全文件 grep / CPU 窗口含启动秒 |
| `2026-10-21-benchmarking-cpu-under-foreign-load.md` | 在 load 23 的机器上量面板 CPU，数字全部作废；A/B 必须同窗口交替 |
| `2026-10-21-remote-feed-hangs-without-frames.md` | 「ssh 连上了但没数据」永不重连 ⇒ 面板永久显示离线；且运行态零日志 |
| `2026-10-21-three-column-layout-silent-misrenders.md` | 四个静默错版：相对/绝对坐标混用 · -1 兼职两种语义 · UTF-8 截断切半 · 固定高度表多算一块 |
