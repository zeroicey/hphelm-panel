#!/usr/bin/env python3
"""fetch-trending.py —— 把 GitHub Trending 榜单抓成一行一条的 TSV

为什么是「抓网页」而不是 API：**GitHub 没有 trending 的 API**。
`/search/repositories?q=created:>…&sort=stars` 给的是「新建且涨星」，不是 Trending
（老仓库翻红它看不见）。Trending 只存在于 HTML 页面里，官方也不打算给接口。

输出格式（面板只认这个，不解析 JSON、不带依赖）：

    # github-trending daily 2026-11-05T06:12:03+00:00 16
    owner/repo\tPython\t2,050\t12,345\t描述…

用法：
    fetch-trending.py OUT.tsv [--since daily|weekly] [--language rust]

部署：systemd 的 hpopi5b-trending.timer 每 3 小时跑一次；面板只读缓存文件，
**网络故障与面板渲染互相隔离**（抓不到就保留上一份，面板照常显示）。
"""
import html
import os
import re
import sys
import tempfile
import time
import unicodedata
import urllib.request

URL = "https://github.com/trending"
UA = "hpopi5b-panel trending fetcher (https://github.com/zeroicey/hpopi5b-panel)"

BLOCK = re.compile(r'<article class="Box-row">(.*?)</article>', re.S)
HREF = re.compile(r'<h2[^>]*>.*?href="/([^"/]+/[^"/]+)"', re.S)
DESC = re.compile(r'<p class="col-9[^"]*">(.*?)</p>', re.S)
LANG = re.compile(r'itemprop="programmingLanguage">([^<]+)<')
TODAY = re.compile(r'([\d,]+)\s+stars today')
TOTAL = re.compile(r'href="/[^"]+/stargazers"[^>]*>.*?</svg>\s*([\d,]+)', re.S)
TAG = re.compile(r'<[^>]+>')
WS = re.compile(r'\s+')


def clean(s: str) -> str:
    s = html.unescape(TAG.sub(" ", s))
    out = []
    for ch in s:
        if unicodedata.category(ch) in ("So", "Sk", "Cf"):  # emoji / 变体选择符 / ZWJ
            continue
        out.append(ch)
    return WS.sub(" ", "".join(out)).strip()


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    out_path = args[0]
    since = "daily"
    language = ""
    for i, a in enumerate(args):
        if a == "--since" and i + 1 < len(args):
            since = args[i + 1]
        if a == "--language" and i + 1 < len(args):
            language = args[i + 1]

    url = f"{URL}?since={since}"
    if language:
        url += "&language=" + language
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=25) as r:
        page = r.read().decode("utf-8", "replace")

    rows = []
    for b in BLOCK.findall(page):
        m = HREF.search(b)
        if not m:
            continue
        name = m.group(1)
        d = DESC.search(b)
        desc = clean(d.group(1)) if d else ""
        if len(desc) > 160:
            desc = desc[:157] + "…"
        lang = LANG.search(b)
        today = TODAY.search(b)
        total = TOTAL.search(b)
        rows.append((name, clean(lang.group(1)) if lang else "", today.group(1) if today else "0",
                     total.group(1) if total else "0", desc))

    if not rows:
        print("没抓到任何条目（页面结构变了？）", file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(out_path) or ".", prefix=".trending.")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(f"# github-trending {since} {time.strftime('%Y-%m-%dT%H:%M:%S%z')} {len(rows)}\n")
            for row in rows:
                f.write("\t".join(row) + "\n")
        os.replace(tmp, out_path)  # 原子替换：面板永远读到完整文件
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)

    print(f"trending: {len(rows)} 条 → {out_path} ({since}{'/' + language if language else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main())