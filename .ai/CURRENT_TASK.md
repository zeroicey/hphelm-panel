# Current task

<!-- The single live surface. Keep it under ~60 lines: it is read on every session start. -->

**Status:** 🟡 active · **Updated:** 2026-10-21

## Goal

把「改完代码就能安全上真屏」这条链交到项目自己的 AI 手上：本机部署可跑、判据可复现、记忆库可用。

## Checklist

- [x] `tools/deploy.sh` 本土化（原地七阶段部署 + 判据），本机三次真跑全绿
- [x] `/usr/local/bin/hphelm-panel.prev` 自动备份 ⇒ 回滚有凭据
- [x] `docs/deploy.md` 重写成「更新系统屏保」runbook（判据表 / 三层回滚 / 六件绝不能做的事）
- [x] `.ai/` 记忆库初始化并填入真实内容（`AGENTS.md` 契约保留原有正文）
- [x] `AGENTS.md` / `HANDOFF.md` / `README.md` 清掉「源码在 hpcore」的旧事实
- [ ] CPU 偏差定位（见 Blockers 第 1 条）
- [ ] GitHub 推送通路（见 Blockers 第 2 条）

## Code state

`main` · 工作树包含本轮未提交改动（deploy.sh 重写 + docs/ + `.ai/` + `.engram/`）。
已装二进制与源码**逐字节一致**；现场：`tty7` · `slick-greeter=0` · `hphelm-panel=1` · `sleep_off=off` · 帧差分 0。
没有半成品改动；`src/` 本轮未动。

## Blockers

- CPU 实测 **1.13%–1.60%** of one core，高于文档记录的 0.60%（2026-10-18）— waiting on 用 `HPHELM_PANEL_DEBUG=1` 把「重绘次数」与「单帧耗时」分开后定性
- `git push origin main` 在本机失败：hphelm 的 GitHub 身份只够推 `dotfiles`（deploy key 一仓一把）— waiting on 无（**用户 2026-10-21 裁决「先不管，提交留本地」**）⇒ 提交落地即可，别为此改动仓库配置

## Pitfall reminders for the current branch

- #15 绝不 `dm-tool` / `light-locker`：会把 lightdm 的 greeter 招回来并抢走 VT（`docs/pitfalls.md`）
- #16 `sleep_off=0` = 永不关屏；裸比较会让 0 变成「立即关屏」
- 部署脚本的三个假告警（`pgrep -q` 不存在 / 全文件 grep 兜底字符串 / CPU 窗口含启动秒）见 `.ai/pitfalls/cases/2026-10-21-deploy-script-false-alarms.md`

## Next action

在真机跑 `HPHELM_PANEL_DEBUG=1` 拿 60 秒仪表输出，记录 it/s、poll 结果分布、绘制/上屏耗时，
再与 `docs/verify.md` 第五节的口径对齐 —— **先量再改**，不要因为数字比旧文档大就先动渲染代码。
