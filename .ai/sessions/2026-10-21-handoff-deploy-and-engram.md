# 会话交接：交接轮（部署链本土化 + engram 记忆库）

**Date:** 2026-10-21 · **Outcome:** ✅ 完成（有一个待查项与一个未解缺口） · **Prev session:** 第五轮「立为独立项目」

## 用户要什么（原话）

> 「给 hphelm panel 交接如何部署更新项目屏保」「我要让他们项目专用的 agent 进行开发
> 他们开发完也知道如何进行更新系统屏保不用麻烦你了」「给他内置 engram 初始化生成相关内容」

⇒ 本轮**不是**做功能，是把「改完 → 上真屏 → 验完 → 能回滚」这条链交出去。

## 做了什么

1. **`tools/deploy.sh` 重写为本机原地七阶段部署**。旧版在本机必失败（`ssh hphelm` 无 Host 别名）。
   新增 `--check` / `--no-restart`；安装前自动留 `/usr/local/bin/hphelm-panel.prev`。
2. **`docs/deploy.md` 重写成「更新系统屏保」runbook**：屏保到底是什么 / 文件清单 / 开发循环 /
   七阶段 / 判据表 / 三层回滚 / 六件绝不能做的事 / 交付清单 / 历史说明。
3. **`.ai/` 记忆库**（engram v0.5.0，本地构建）：`init --notes "…"` 生成，然后填了
   `CURRENT_TASK` · `ARCHITECTURE` · 两个 runbook · 三条 decision · 一个新坑案例 · 本文件。
   `AGENTS.md` 末尾追加了 engram 契约（正文保留），并新增「接手第一小时读什么」。
4. **文档纠偏**：删掉 `AGENTS.md` / `README.md` / `HANDOFF.md` / `docs/deploy.md` / `docs/pitfalls.md` 里
   「源码在 hpcore、rsync 到 hphelm」的旧事实。

## 判据（三次真跑，都在这台机上）

```
VT=tty7 · slick-greeter=0 · hphelm-panel=1
源码⇄已装 逐字节一致 · 助手 4755 root:root · sleep_off=off（永不关屏）
帧差分 >30% 变化帧 = 0 · 日志无当前实例的崩溃兜底
CPU = 1.13% / 1.60% of one core（20s 窗口，含/不含热身）
```

## 没解决的

1. **CPU 1.1~1.6%，文档记录是 0.60%** —— 机理未定。下一步用 `HPHELM_PANEL_DEBUG=1` 分「重绘次数」与「单帧耗时」。
2. **推不上 GitHub**：hphelm 的 `id_ed25519` 是 `dotfiles` 的 deploy key（一仓一把），
   `git push origin main` 权限失败 ⇒ 本轮只提交到本地。解法二选一：给本仓加 write deploy key，
   或换有账号级密钥的机器推（hphelm 无 `gh` 登录）。
3. 本轮**没动** `src/`，所以没有 UI 变更需要用户过目。

## 下一会话从哪里起

读 `.ai/CURRENT_TASK.md`（Goal / Blockers / Next action）→ 再读 `docs/deploy.md`。
真正的下一步是**量**，不是改：先拿 60 秒仪表输出，再决定要不要碰渲染代码。
