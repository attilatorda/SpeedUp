#!/usr/bin/env python3
"""Builds the SpeedUp wiki: docs/wiki/pages/*.md -> docs/wiki/index.html.

One self-contained page (no build tools, no dependencies): a sidebar, one
view per wiki page (selected by #page-id), search over every page, an
"on this page" outline, and a light/dark switch that remembers the choice.

Page format: Markdown with a small header,

    ---
    title: Sorting          page title
    nav: Sorting            sidebar label (default: title)
    section: Algorithms     sidebar group
    order: 12               position (groups are ordered by their lowest page)
    ---

plus two block types of its own:

    ```api                  a reference entry: signature lines, then one
    int f(int x);           line of description (rendered like Doxygen's
    What f returns.         member boxes)
    ```

    ```version              a version card: "version | date | title",
    0.2.0 | 27 Oct | Name   then "- " bullet lines
    - What changed.
    ```

The Changelog page is CHANGELOG.md itself.

Usage:
    build_wiki.py                   writes docs/wiki/index.html
    build_wiki.py --fragment OUT    also writes the page without the document
                                    skeleton (for hosts that add their own)
"""
import argparse
import html
import re
from datetime import date
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PAGES = HERE / "pages"
OUT = HERE / "index.html"

# ---------------------------------------------------------------- markdown


def slug(text):
    s = re.sub(r"<[^>]+>", "", text)
    s = re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")
    return s or "section"


def inline(text):
    """Inline Markdown: `code`, **bold**, *emphasis*, [links](target)."""
    codes = []

    def keep_code(m):
        codes.append(f"<code>{html.escape(m.group(1))}</code>")
        return f"\x00{len(codes) - 1}\x00"

    text = re.sub(r"`([^`]+)`", keep_code, text)
    text = html.escape(text, quote=False)

    def link(m):
        label, target = m.group(1), m.group(2)
        if target.startswith("#"):
            return f'<a href="{html.escape(target)}">{label}</a>'
        return f'<a href="{html.escape(target)}" target="_blank" rel="noopener">{label}</a>'

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", text)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], text)


KEYWORDS = {
    "c": r"\b(?:void|int|char|const|static|inline|return|if|else|for|while|struct|typedef|unsigned|signed|"
         r"size_t|int32_t|int64_t|uint32_t|uint64_t|uint16_t|float|double|bool|auto|template|class|namespace|"
         r"typename|std|nullptr|NULL|true|false|using|public|private|virtual|override|sizeof|switch|case|break|"
         r"enum|record|new|import|var|List|long|def|from|assert|lambda)\b",
}


def highlight(code, lang):
    """A small highlighter: comments, strings, preprocessor lines, keywords, numbers."""
    if lang in ("", "text", "api"):
        return html.escape(code)
    hash_comments = lang in ("bash", "sh", "shell", "cmake", "python")
    comment = r"#[^\n]*" if hash_comments else r"//[^\n]*|/\*.*?\*/"
    strings = r'"(?:\\.|[^"\\\n])*"' + "|" + r"'(?:\\.|[^'\\\n])*'"
    parts = [rf"(?P<com>{comment})", rf"(?P<str>{strings})"]
    if not hash_comments:
        parts.append(r"(?P<pp>^[ \t]*#[a-z]+[^\n]*)")
    pattern = re.compile("|".join(parts), re.S | re.M)
    out, pos = [], 0
    for m in pattern.finditer(code):
        out.append(words(code[pos:m.start()], lang))
        kind = m.lastgroup
        out.append(f'<span class="tok-{kind}">{html.escape(m.group(0))}</span>')
        pos = m.end()
    out.append(words(code[pos:], lang))
    return "".join(out)


def words(text, lang):
    text = html.escape(text, quote=False)
    if lang in ("bash", "sh", "shell"):
        text = re.sub(r"(^|\n)(\s*)(cmake|ctest|python|python3|mvn|doxygen|cd)\b",
                      r'\1\2<span class="tok-kw">\3</span>', text)
    elif lang == "cmake":
        text = re.sub(r"\b(add_subdirectory|target_link_libraries|PRIVATE|PUBLIC)\b",
                      r'<span class="tok-kw">\1</span>', text)
    else:
        text = re.sub(KEYWORDS["c"], r'<span class="tok-kw">\g<0></span>', text)
    return re.sub(r"(?<![\w#.])(\d+(?:\.\d+)?)(?![\w])", r'<span class="tok-num">\1</span>', text)


def api_block(lines):
    sigs, desc = lines[:-1], lines[-1]
    if not sigs:
        sigs, desc = lines, ""
    names = []
    for s in sigs:
        m = re.search(r"([A-Za-z_][\w:]*)\s*\(", s) or re.search(r"([A-Za-z_]\w*)\s*;?\s*$", s)
        if m:
            names.append(m.group(1).split("::")[-1])
    anchor = slug(names[0]) if names else "member"
    sig_html = "\n".join(highlight(s, "c") for s in sigs)
    title = html.escape(names[0]) if names else ""
    return (f'<div class="member" id="api-{anchor}"><div class="member-name">{title}</div>'
            f'<pre class="member-proto"><code>{sig_html}</code></pre>'
            f'<div class="member-doc">{inline(desc)}</div></div>')


def version_block(lines):
    head = [p.strip() for p in lines[0].split("|")]
    version, when, title = (head + ["", "", ""])[:3]
    items = "".join(f"<li>{inline(l[2:].strip())}</li>" for l in lines[1:] if l.startswith("- "))
    return (f'<div class="version"><div class="version-head"><span class="version-tag">{html.escape(version)}</span>'
            f'<span class="version-title">{html.escape(title)}</span>'
            f'<span class="version-date">{html.escape(when)}</span></div><ul>{items}</ul></div>')


def table(rows):
    def cells(row):
        return [c.strip() for c in row.strip().strip("|").split("|")]
    head, body = cells(rows[0]), [cells(r) for r in rows[2:]]
    out = ['<div class="table-wrap"><table><thead><tr>']
    out += [f"<th>{inline(c)}</th>" for c in head]
    out.append("</tr></thead><tbody>")
    for r in body:
        out.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>")
    out.append("</tbody></table></div>")
    return "".join(out)


def markdown(text):
    lines = text.split("\n")
    out, i = [], 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            lang = line[3:].strip()
            body = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                body.append(lines[i])
                i += 1
            i += 1
            if lang == "api":
                out.append(api_block([b for b in body if b.strip()]))
            elif lang == "version":
                out.append(version_block([b for b in body if b.strip()]))
            else:
                label = f'<span class="code-lang">{html.escape(lang)}</span>' if lang else ""
                out.append(f'<div class="code">{label}<pre><code>{highlight(chr(10).join(body), lang)}</code></pre></div>')
            continue
        m = re.match(r"(#{1,4})\s+(.*)", line)
        if m:
            level = max(2, len(m.group(1)))  # the page title is the only h1
            content = inline(m.group(2))
            out.append(f'<h{level} id="{slug(m.group(2))}">{content}</h{level}>')
            i += 1
            continue
        if line.startswith("|") and i + 1 < len(lines) and re.match(r"^\|[\s:|-]+\|$", lines[i + 1].strip()):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(lines[i])
                i += 1
            out.append(table(rows))
            continue
        if re.match(r"^(- |\d+\. )", line):
            ordered = bool(re.match(r"^\d+\. ", line))
            items = []
            while i < len(lines) and (re.match(r"^(- |\d+\. )", lines[i]) or
                                      (lines[i].startswith("  ") and items)):
                if re.match(r"^(- |\d+\. )", lines[i]):
                    items.append(re.sub(r"^(- |\d+\. )", "", lines[i]))
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            tag = "ol" if ordered else "ul"
            out.append(f"<{tag}>" + "".join(f"<li>{inline(it)}</li>" for it in items) + f"</{tag}>")
            continue
        if line.startswith(">"):
            quote = []
            while i < len(lines) and lines[i].startswith(">"):
                quote.append(lines[i][1:].strip())
                i += 1
            out.append(f"<blockquote><p>{inline(' '.join(quote))}</p></blockquote>")
            continue
        if not line.strip():
            i += 1
            continue
        para = []
        while i < len(lines) and lines[i].strip() and not re.match(r"^(```|#{1,4}\s|\||- |\d+\. |>)", lines[i]):
            para.append(lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(para))}</p>")
    return "\n".join(out)


# ---------------------------------------------------------------- pages


def read_page(path):
    text = path.read_text(encoding="utf-8")
    meta = {}
    if text.startswith("---"):
        head, text = text[3:].split("\n---", 1)
        for line in head.strip().splitlines():
            key, _, value = line.partition(":")
            meta[key.strip()] = value.strip()
    return {"id": path.stem, "title": meta.get("title", path.stem), "nav": meta.get("nav", meta.get("title")),
            "section": meta.get("section", "Other"), "order": int(meta.get("order", 99)), "body": text.strip()}


def changelog_page():
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    text = re.sub(r"^# Changelog\s*\n", "", text)
    text = re.sub(r"^\[[^\]]+\]: \S+\s*$", "", text, flags=re.M)  # link reference lines
    text = re.sub(r"\[(\d+\.\d+\.\d+|Unreleased)\]", r"\1", text)
    return {"id": "changelog", "title": "Changelog", "nav": "Changelog", "section": "Project", "order": 35,
            "body": "The complete record, read from `CHANGELOG.md`. In short: [Versions](#versions).\n\n" + text}


def version():
    v = (ROOT / "include" / "speedup" / "version.h").read_text(encoding="utf-8")
    parts = [re.search(rf"SPEEDUP_VERSION_{k}\s+(\d+)", v).group(1) for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


# ---------------------------------------------------------------- template

STYLE = r"""
:root {
  /* Doxygen's reference look, quieter: a pale sidebar, member boxes, one blue accent. */
  --bg: #fcfcfd; --panel: #f3f5f8; --raised: #ffffff; --ink: #18202b; --ink-2: #485365;
  --muted: #778294; --line: #e1e5eb; --accent: #2b5cad; --accent-soft: #e7eef9;
  --code-bg: #f3f5f8; --tok-kw: #7b3fa0; --tok-str: #2c7a4b; --tok-com: #7b8696;
  --tok-num: #ad5717; --tok-pp: #2b5cad;
  --sans: "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, "Cascadia Mono", Consolas, monospace;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #11151b; --panel: #161b23; --raised: #1a2029; --ink: #e3e8ef; --ink-2: #b2bbca;
    --muted: #808b9c; --line: #272e39; --accent: #8db2ec; --accent-soft: #1c2940;
    --code-bg: #171d26; --tok-kw: #c79ae8; --tok-str: #8fcd9e; --tok-com: #7d8899;
    --tok-num: #e2a26c; --tok-pp: #8db2ec; color-scheme: dark;
  }
}
:root[data-theme="dark"] {
  --bg: #11151b; --panel: #161b23; --raised: #1a2029; --ink: #e3e8ef; --ink-2: #b2bbca;
  --muted: #808b9c; --line: #272e39; --accent: #8db2ec; --accent-soft: #1c2940;
  --code-bg: #171d26; --tok-kw: #c79ae8; --tok-str: #8fcd9e; --tok-com: #7d8899;
  --tok-num: #e2a26c; --tok-pp: #8db2ec; color-scheme: dark;
}
:root[data-theme="light"] { color-scheme: light; }
* { box-sizing: border-box; }
[hidden] { display: none !important; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.6 var(--sans); }
body::before { content: ""; position: fixed; inset: 0 0 auto; height: env(safe-area-inset-top, 0px); z-index: 10; background: var(--panel); }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; border-radius: 3px; }

.topbar { position: sticky; top: env(safe-area-inset-top, 0px); z-index: 5; display: flex; align-items: center; gap: 14px;
  height: 52px; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px));
  background: var(--panel); border-bottom: 1px solid var(--line); }
.brand { display: flex; align-items: baseline; gap: 8px; color: var(--ink); font-weight: 600; font-size: 16px; white-space: nowrap; }
.brand:hover { text-decoration: none; }
.brand-mark { font-family: var(--mono); font-size: 13px; color: var(--accent); }
.chip { font: 500 11.5px/1 var(--mono); color: var(--ink-2); border: 1px solid var(--line); border-radius: 999px; padding: 4px 8px; background: var(--raised); }
.spacer { flex: 1; }
.search { position: relative; width: min(340px, 40vw); }
.search input { width: 100%; height: 32px; padding: 0 10px 0 30px; font: 14px var(--sans); color: var(--ink);
  background: var(--raised); border: 1px solid var(--line); border-radius: 7px; }
.search input::placeholder { color: var(--muted); }
.search svg { position: absolute; left: 9px; top: 9px; width: 14px; height: 14px; stroke: var(--muted); }
.results { position: absolute; top: 38px; left: 0; right: 0; max-height: 60vh; overflow-y: auto; background: var(--raised);
  border: 1px solid var(--line); border-radius: 8px; box-shadow: 0 8px 28px rgba(0,0,0,.14); padding: 4px; }
.result { display: block; width: 100%; text-align: left; padding: 8px 10px; border: 0; border-radius: 6px; background: none; color: var(--ink); font: inherit; cursor: pointer; }
.result:hover, .result.active { background: var(--accent-soft); }
.result b { display: block; font-size: 14px; font-weight: 600; }
.result span { display: block; font-size: 12.5px; color: var(--ink-2); }
.result mark { background: none; color: var(--accent); font-weight: 600; }
.icon-btn { display: inline-flex; align-items: center; justify-content: center; gap: 6px; height: 32px; padding: 0 10px;
  font: 13px var(--sans); color: var(--ink-2); background: var(--raised); border: 1px solid var(--line); border-radius: 7px; cursor: pointer; }
.icon-btn:hover { color: var(--ink); border-color: var(--muted); }
.icon-btn svg { width: 15px; height: 15px; stroke: currentColor; fill: none; stroke-width: 1.8; }
.menu-btn { display: none; }

.layout { display: grid; grid-template-columns: 240px minmax(0, 1fr) 200px; max-width: 1320px; margin: 0 auto; }
.sidebar { position: sticky; top: calc(52px + env(safe-area-inset-top, 0px)); align-self: start; height: calc(100vh - 52px);
  overflow-y: auto; padding: 22px 12px 40px max(16px, env(safe-area-inset-left, 0px)); border-right: 1px solid var(--line); }
.nav-group { margin-bottom: 18px; }
.nav-group h4 { margin: 0 0 6px 10px; font: 600 11px/1.4 var(--sans); letter-spacing: .08em; text-transform: uppercase; color: var(--muted); }
.nav-group a { display: block; padding: 4px 10px; border-radius: 6px; color: var(--ink-2); font-size: 14px; }
.nav-group a:hover { background: var(--panel); color: var(--ink); text-decoration: none; }
.nav-group a[aria-current="page"] { background: var(--accent-soft); color: var(--accent); font-weight: 600; }

main { min-width: 0; padding: 28px 40px 64px; }
.page { max-width: 760px; }
.crumb { font-size: 12.5px; color: var(--muted); margin-bottom: 4px; }
.page h1 { font-size: 30px; line-height: 1.2; margin: 0 0 18px; font-weight: 600; letter-spacing: -.01em; text-wrap: balance; }
.page h2 { font-size: 20px; margin: 34px 0 10px; padding-bottom: 6px; border-bottom: 1px solid var(--line); font-weight: 600; text-wrap: balance; }
.page h3 { font-size: 16px; margin: 24px 0 8px; font-weight: 600; }
.page p, .page li { color: var(--ink); }
.page p { margin: 0 0 12px; }
.page ul, .page ol { margin: 0 0 14px; padding-left: 22px; }
.page li { margin: 3px 0; }
.page code { font: 13px var(--mono); background: var(--code-bg); border: 1px solid var(--line); border-radius: 4px; padding: 1px 5px; overflow-wrap: anywhere; }
.page blockquote { margin: 0 0 14px; padding: 8px 14px; border-left: 3px solid var(--accent); background: var(--panel); border-radius: 0 6px 6px 0; }
.code { position: relative; margin: 0 0 16px; }
.code pre, .member-proto { margin: 0; overflow-x: auto; background: var(--code-bg); border: 1px solid var(--line); border-radius: 8px; padding: 12px 14px; }
.code pre code, .member-proto code { font: 13px/1.55 var(--mono); background: none; border: 0; padding: 0; color: var(--ink); }
.code-lang { position: absolute; top: 6px; right: 10px; font: 11px var(--mono); color: var(--muted); }
.tok-kw { color: var(--tok-kw); } .tok-str { color: var(--tok-str); } .tok-com { color: var(--tok-com); font-style: italic; }
.tok-num { color: var(--tok-num); } .tok-pp { color: var(--tok-pp); }
.table-wrap { overflow-x: auto; margin: 0 0 16px; border: 1px solid var(--line); border-radius: 8px; }
table { border-collapse: collapse; width: 100%; font-size: 14px; }
th, td { text-align: left; padding: 7px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { background: var(--panel); font-weight: 600; color: var(--ink-2); font-size: 13px; }
tr:last-child td { border-bottom: 0; }
td code { white-space: nowrap; }

.member { margin: 0 0 14px; border: 1px solid var(--line); border-radius: 8px; overflow: hidden; background: var(--raised); }
.member-name { padding: 6px 14px; font: 600 13px var(--mono); color: var(--accent); background: var(--accent-soft); border-bottom: 1px solid var(--line); }
.member .member-proto { border: 0; border-radius: 0; border-bottom: 1px solid var(--line); }
.member-doc { padding: 10px 14px; font-size: 14px; color: var(--ink-2); }

.version { margin: 0 0 14px; border: 1px solid var(--line); border-radius: 8px; padding: 12px 16px; background: var(--raised); }
.version-head { display: flex; flex-wrap: wrap; align-items: baseline; gap: 6px 12px; margin-bottom: 6px; }
.version-tag { font: 600 13px var(--mono); color: var(--accent); background: var(--accent-soft); padding: 2px 8px; border-radius: 5px; }
.version-title { font-weight: 600; }
.version-date { margin-left: auto; font-size: 12.5px; color: var(--muted); font-variant-numeric: tabular-nums; }
.version ul { margin: 0; padding-left: 20px; }
.version li { font-size: 14px; color: var(--ink-2); }

.toc { position: sticky; top: calc(52px + env(safe-area-inset-top, 0px)); align-self: start; padding: 28px max(16px, env(safe-area-inset-right, 0px)) 40px 8px; font-size: 13px; }
.toc h4 { margin: 0 0 8px; font: 600 11px/1.4 var(--sans); letter-spacing: .08em; text-transform: uppercase; color: var(--muted); }
.toc button { display: block; width: 100%; text-align: left; border: 0; background: none; padding: 3px 0 3px 10px; color: var(--ink-2);
  font: inherit; cursor: pointer; border-left: 1px solid var(--line); overflow-wrap: anywhere; }
.toc button.sub { padding-left: 22px; }
.toc button:hover { color: var(--accent); border-left-color: var(--accent); }
.footer { max-width: 760px; margin-top: 48px; padding-top: 14px; border-top: 1px solid var(--line); font-size: 12.5px; color: var(--muted); }

@media (max-width: 1100px) { .layout { grid-template-columns: 230px minmax(0, 1fr); } .toc { display: none; } }
@media (max-width: 820px) {
  .layout { grid-template-columns: minmax(0, 1fr); }
  .sidebar { position: fixed; z-index: 4; top: calc(52px + env(safe-area-inset-top, 0px)); left: 0; width: min(280px, 86vw); background: var(--panel);
    transform: translateX(-102%); transition: transform .18s ease; }
  .sidebar.open { transform: none; box-shadow: 0 10px 40px rgba(0,0,0,.25); }
  .menu-btn { display: inline-flex; }
  main { padding: 22px max(16px, env(safe-area-inset-right, 0px)) 56px max(16px, env(safe-area-inset-left, 0px)); }
  .search { width: auto; flex: 1; }
  .brand-name, .chip { display: none; }
  .theme-label { display: none; }
}
@media (prefers-reduced-motion: reduce) { .sidebar { transition: none; } }
"""

SCRIPT = r"""
(function () {
  var root = document.documentElement;
  var pages = Array.prototype.slice.call(document.querySelectorAll('.page'));
  var byId = {};
  pages.forEach(function (p) { byId[p.dataset.page] = p; });
  var navLinks = Array.prototype.slice.call(document.querySelectorAll('.nav-group a'));
  var toc = document.getElementById('toc-list');
  var sidebar = document.getElementById('sidebar');

  // ---- theme: system by default, a click picks the other one and remembers it
  var themeBtn = document.getElementById('theme');
  function effective() {
    var t = root.getAttribute('data-theme');
    if (t === 'dark' || t === 'light') return t;
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }
  function paintThemeButton() {
    var dark = effective() === 'dark';
    themeBtn.querySelector('.sun').hidden = !dark;
    themeBtn.querySelector('.moon').hidden = dark;
    themeBtn.querySelector('.theme-label').textContent = dark ? 'Light' : 'Dark';
    themeBtn.setAttribute('aria-label', dark ? 'Switch to light mode' : 'Switch to dark mode');
  }
  try { var saved = localStorage.getItem('speedup-wiki-theme'); if (saved === 'dark' || saved === 'light') root.setAttribute('data-theme', saved); } catch (e) {}
  themeBtn.addEventListener('click', function () {
    var next = effective() === 'dark' ? 'light' : 'dark';
    root.setAttribute('data-theme', next);
    try { localStorage.setItem('speedup-wiki-theme', next); } catch (e) {}
    paintThemeButton();
  });
  if (window.matchMedia) {
    var mq = window.matchMedia('(prefers-color-scheme: dark)');
    if (mq.addEventListener) mq.addEventListener('change', paintThemeButton);
  }
  paintThemeButton();

  // ---- routing: #page-id selects a page
  function show(id, keepScroll) {
    if (!byId[id]) id = 'home';
    pages.forEach(function (p) { p.hidden = p.dataset.page !== id; });
    navLinks.forEach(function (a) {
      if (a.getAttribute('href') === '#' + id) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
    });
    buildToc(byId[id]);
    sidebar.classList.remove('open');
    if (!keepScroll) window.scrollTo(0, 0);
  }
  function buildToc(page) {
    toc.replaceChildren();
    var heads = page.querySelectorAll('h2, h3');
    document.getElementById('toc').hidden = heads.length < 2;
    Array.prototype.forEach.call(heads, function (h) {
      var b = document.createElement('button');
      b.type = 'button';
      b.textContent = h.textContent;
      if (h.tagName === 'H3') b.className = 'sub';
      b.addEventListener('click', function () { h.scrollIntoView({ behavior: 'smooth', block: 'start' }); });
      toc.appendChild(b);
    });
  }
  window.addEventListener('hashchange', function () { show(location.hash.slice(1)); });
  show(location.hash.slice(1), true);

  document.getElementById('menu').addEventListener('click', function () { sidebar.classList.toggle('open'); });

  // ---- search over every page's text
  var input = document.getElementById('q');
  var box = document.getElementById('results');
  var index = pages.map(function (p) {
    return { id: p.dataset.page, title: p.querySelector('h1').textContent, text: p.textContent.replace(/\s+/g, ' ') };
  });
  var active = 0, hits = [];
  function snippet(text, term) {
    var i = text.toLowerCase().indexOf(term);
    var start = Math.max(0, i - 40);
    var s = (start ? '…' : '') + text.slice(start, i + 80) + '…';
    return s;
  }
  function render() {
    box.replaceChildren();
    hits.forEach(function (h, k) {
      var b = document.createElement('button');
      b.type = 'button';
      b.className = 'result' + (k === active ? ' active' : '');
      var t = document.createElement('b'); t.textContent = h.title;
      var s = document.createElement('span'); s.textContent = h.snip;
      b.append(t, s);
      b.addEventListener('mousedown', function (e) { e.preventDefault(); go(h.id); });
      box.appendChild(b);
    });
    if (!hits.length) {
      var none = document.createElement('div');
      none.className = 'result';
      none.textContent = 'No page mentions that.';
      box.appendChild(none);
    }
    box.hidden = false;
  }
  function go(id) { input.value = ''; box.hidden = true; input.blur(); location.hash = id; if (location.hash === '#' + id) show(id); }
  input.addEventListener('input', function () {
    var q = input.value.trim().toLowerCase();
    if (!q) { box.hidden = true; return; }
    var terms = q.split(/\s+/);
    hits = index.filter(function (p) {
      var hay = (p.title + ' ' + p.text).toLowerCase();
      return terms.every(function (t) { return hay.indexOf(t) >= 0; });
    }).map(function (p) {
      var inTitle = p.title.toLowerCase().indexOf(terms[0]) >= 0;
      return { id: p.id, title: p.title, snip: snippet(p.text, terms[0]), rank: inTitle ? 0 : 1 };
    }).sort(function (a, b) { return a.rank - b.rank; }).slice(0, 8);
    active = 0;
    render();
  });
  input.addEventListener('keydown', function (e) {
    if (e.key === 'ArrowDown' && hits.length) { active = (active + 1) % hits.length; render(); e.preventDefault(); }
    else if (e.key === 'ArrowUp' && hits.length) { active = (active + hits.length - 1) % hits.length; render(); e.preventDefault(); }
    else if (e.key === 'Enter' && hits[active]) { go(hits[active].id); }
    else if (e.key === 'Escape') { input.value = ''; box.hidden = true; input.blur(); }
  });
  input.addEventListener('blur', function () { setTimeout(function () { box.hidden = true; }, 120); });
  document.addEventListener('keydown', function (e) {
    if (e.key === '/' && document.activeElement !== input) { e.preventDefault(); input.focus(); }
  });
})();
"""

ICON_SEARCH = '<svg viewBox="0 0 24 24" fill="none" stroke-width="2" aria-hidden="true"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></svg>'
ICON_SUN = '<svg class="sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4.2"/><path d="M12 2.5v2.2M12 19.3v2.2M4.6 4.6l1.6 1.6M17.8 17.8l1.6 1.6M2.5 12h2.2M19.3 12h2.2M4.6 19.4l1.6-1.6M17.8 6.2l1.6-1.6"/></svg>'
ICON_MOON = '<svg class="moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5Z"/></svg>'
ICON_MENU = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M4 12h16M4 17h16"/></svg>'


def build(pages, ver):
    sections = {}
    for p in sorted(pages, key=lambda p: p["order"]):
        sections.setdefault(p["section"], []).append(p)
    nav = []
    for name, items in sorted(sections.items(), key=lambda kv: kv[1][0]["order"]):
        links = "".join(f'<a href="#{p["id"]}">{html.escape(p["nav"])}</a>' for p in items)
        nav.append(f'<div class="nav-group"><h4>{html.escape(name)}</h4>{links}</div>')
    views = []
    for p in sorted(pages, key=lambda p: p["order"]):
        crumb = "" if p["id"] == "home" else f'<div class="crumb">{html.escape(p["section"])}</div>'
        title = p["title"] if p["id"] != "home" else "SpeedUp"
        views.append(f'<article class="page" data-page="{p["id"]}" hidden>{crumb}<h1>{html.escape(title)}</h1>\n'
                     f'{markdown(p["body"])}</article>')
    today = date.today().isoformat()
    return f"""<title>SpeedUp Wiki</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>{STYLE}</style>
<header class="topbar">
  <button class="icon-btn menu-btn" id="menu" type="button" aria-label="Show pages">{ICON_MENU}</button>
  <a class="brand" href="#home"><span class="brand-mark">&lt;/&gt;</span><span class="brand-name">SpeedUp</span></a>
  <span class="chip">v{ver}</span>
  <span class="spacer"></span>
  <div class="search">{ICON_SEARCH}<input id="q" type="search" placeholder="Search the wiki  ( / )" autocomplete="off" aria-label="Search the wiki"><div class="results" id="results" hidden></div></div>
  <button class="icon-btn" id="theme" type="button">{ICON_SUN}{ICON_MOON}<span class="theme-label">Dark</span></button>
</header>
<div class="layout">
  <nav class="sidebar" id="sidebar" aria-label="Wiki pages">{''.join(nav)}</nav>
  <main>
{chr(10).join(views)}
    <div class="footer">SpeedUp {ver} · generated from <code>docs/wiki/pages/</code> on {today} by <code>docs/wiki/build_wiki.py</code></div>
  </main>
  <aside class="toc" id="toc" aria-label="On this page"><h4>On this page</h4><div id="toc-list"></div></aside>
</div>
<script>{SCRIPT}</script>
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fragment", help="also write the page without <!doctype>/<html>/<head>/<body>")
    args = parser.parse_args()
    pages = [read_page(p) for p in sorted(PAGES.glob("*.md"))] + [changelog_page()]
    ids = [p["id"] for p in pages]
    assert len(ids) == len(set(ids)), "duplicate page ids"
    ver = version()
    body = build(pages, ver)
    head, rest = body.split("<style>", 1)
    style, rest = rest.split("</style>", 1)
    full = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
            f"{head}<style>{style}</style>\n</head>\n<body>{rest}</body>\n</html>\n")
    OUT.write_text(full, encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(pages)} pages)")
    if args.fragment:
        Path(args.fragment).write_text(body, encoding="utf-8", newline="\n")
        print(f"wrote {args.fragment}")


if __name__ == "__main__":
    main()
