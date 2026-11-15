"""Rebuild the "By the numbers" block in Docs/DEVLOG.md (issue #121).

Only aggregate counts are written. No prompt or transcript text ever leaves
this script, so the output is safe for the public devlog.

Sources:
  * Claude Code transcripts (~/.claude/projects/<dir>/**/*.jsonl), one dir
    per surface (PowerShell, UE Terminal). Assistant usage is deduplicated on
    message id, because streamed / resumed sessions repeat the same message.
  * Optional claude.ai data export (conversations.json) for Desktop chats.
    Tokens there are estimated as chars / 4 and labelled as estimates.
  * Docs/manual_stats.json: hand-entered numbers (teammate tools, Meshy).
  * git + gh for repository numbers.

Usage (from the repo root):
  python Scripts/devlog_stats.py                 # rewrite the STATS block
  python Scripts/devlog_stats.py --print         # print only, write nothing
  python Scripts/devlog_stats.py --claude-export path/to/conversations.json
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEVLOG = REPO / "Docs" / "DEVLOG.md"
MANUAL = REPO / "Docs" / "manual_stats.json"
BEGIN, END = "<!-- STATS:BEGIN -->", "<!-- STATS:END -->"

# Jam window start, local time of this PC.
JAM_START = datetime(2026, 11, 12).astimezone()

# Claude Code project dirs -> surface label. Other dirs are not jam work.
SURFACES = {
    "E--Epic-Games-Unreal-Projects": "Claude Code (PowerShell)",
    "E--Epic-Games-Unreal-Projects-MegaJam26": "Claude Code (UE Terminal)",
}

NOISE_PREFIXES = ("<local-command", "<command-", "<system-reminder", "Caveat:")


def parse_ts(value: str | None) -> datetime | None:
    if not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone()
    except ValueError:
        return None


def is_real_prompt(rec: dict) -> bool:
    """A human-typed turn: not meta, not a tool result, not command noise."""
    if rec.get("isMeta") or rec.get("isSidechain"):
        return False
    content = rec.get("message", {}).get("content")
    if isinstance(content, str):
        return bool(content.strip()) and not content.lstrip().startswith(NOISE_PREFIXES)
    if isinstance(content, list):
        if any(isinstance(b, dict) and b.get("type") == "tool_result" for b in content):
            return False
        texts = [b.get("text", "") for b in content if isinstance(b, dict) and b.get("type") == "text"]
        return any(t.strip() and not t.lstrip().startswith(NOISE_PREFIXES) for t in texts)
    return False


def empty_bucket() -> dict:
    return {"sessions": set(), "prompts": 0, "turns": 0, "tool_calls": 0,
            "input": 0, "output": 0, "cache_write": 0, "cache_read": 0,
            "models": Counter()}


def scan_claude_code(root: Path) -> dict:
    """Return {surface: {"jam": bucket, "pre": bucket, "days": {date: bucket}}}."""
    result = {}
    for dirname, label in SURFACES.items():
        folder = root / dirname
        jam, pre, days = empty_bucket(), empty_bucket(), defaultdict(empty_bucket)
        usage_by_id: dict[str, tuple] = {}  # msg id -> (ts, model, usage, session)
        tool_ids: dict[str, tuple] = {}
        for path in sorted(folder.rglob("*.jsonl")) if folder.is_dir() else []:
            with path.open(encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    try:
                        rec = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    ts = parse_ts(rec.get("timestamp"))
                    if ts is None:
                        continue
                    session = rec.get("sessionId", path.stem)
                    if rec.get("type") == "user" and is_real_prompt(rec):
                        for b in (jam if ts >= JAM_START else pre, days[ts.date()] if ts >= JAM_START else None):
                            if b is not None:
                                b["prompts"] += 1
                                b["sessions"].add(session)
                    elif rec.get("type") == "assistant":
                        msg = rec.get("message", {})
                        model = msg.get("model", "")
                        if not msg.get("id") or model.startswith("<"):
                            continue
                        usage = msg.get("usage") or {}
                        prev = usage_by_id.get(msg["id"])
                        # Streamed entries share an id; keep the most complete usage.
                        if prev is None or usage.get("output_tokens", 0) >= prev[2].get("output_tokens", 0):
                            usage_by_id[msg["id"]] = (ts, model, usage, session)
                        for block in msg.get("content") or []:
                            if isinstance(block, dict) and block.get("type") == "tool_use":
                                tool_ids[block.get("id", "")] = (ts,)
        for ts, model, usage, session in usage_by_id.values():
            targets = [jam, days[ts.date()]] if ts >= JAM_START else [pre]
            for b in targets:
                b["turns"] += 1
                b["sessions"].add(session)
                b["models"][model] += 1
                b["input"] += usage.get("input_tokens", 0) or 0
                b["output"] += usage.get("output_tokens", 0) or 0
                b["cache_write"] += usage.get("cache_creation_input_tokens", 0) or 0
                b["cache_read"] += usage.get("cache_read_input_tokens", 0) or 0
        for (ts,) in tool_ids.values():
            for b in ([jam, days[ts.date()]] if ts >= JAM_START else [pre]):
                b["tool_calls"] += 1
        result[label] = {"jam": jam, "pre": pre, "days": dict(sorted(days.items()))}
    return result


def scan_claude_export(path: Path) -> dict | None:
    """claude.ai export: count jam-window conversations and messages."""
    if not path:
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    convs, human, assistant, chars_in, chars_out = set(), 0, 0, 0, 0
    for conv in data:
        for m in conv.get("chat_messages", []):
            ts = parse_ts(m.get("created_at"))
            if ts is None or ts < JAM_START:
                continue
            text = m.get("text") or "".join(
                b.get("text", "") for b in m.get("content", []) if isinstance(b, dict))
            convs.add(conv.get("uuid"))
            if m.get("sender") == "human":
                human += 1
                chars_in += len(text)
            else:
                assistant += 1
                chars_out += len(text)
    return {"conversations": len(convs), "prompts": human, "replies": assistant,
            "est_in": chars_in // 4, "est_out": chars_out // 4}


def run(*cmd: str) -> str:
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", check=True).stdout


def github_stats(ref: str) -> dict:
    days = Counter(run("git", "log", ref, "--date=format-local:%Y-%m-%d", "--format=%ad").split())
    bodies = run("git", "log", ref, "--format=%B%x1e").split("\x1e")
    coauth = sum(1 for b in bodies if re.search(r"co-authored-by:\s*claude", b, re.I))
    stats = {"commits": sum(days.values()), "commit_days": dict(sorted(days.items())),
             "claude_coauthored": coauth}
    try:
        prs = json.loads(run("gh", "pr", "list", "--state", "all", "--limit", "1000",
                             "--json", "number,author,state"))
        issues = json.loads(run("gh", "issue", "list", "--state", "all", "--limit", "1000",
                                "--json", "number,author,state"))
        stats["prs"] = len(prs)
        stats["prs_merged"] = sum(1 for p in prs if p["state"] == "MERGED")
        stats["pr_authors"] = Counter(p["author"]["login"] for p in prs)
        stats["issues"] = len(issues)
        stats["issues_closed"] = sum(1 for i in issues if i["state"] == "CLOSED")
        stats["issue_authors"] = Counter(i["author"]["login"] for i in issues)
    except (subprocess.CalledProcessError, FileNotFoundError):
        stats["prs"] = None
    files = run("git", "ls-tree", "-r", "--name-only", ref).splitlines()
    cpp = [f for f in files if f.startswith("Source/") and f.endswith((".h", ".cpp"))]
    stats["cpp_files"] = len(cpp)
    stats["cpp_lines"] = sum(run("git", "show", f"{ref}:{f}").count("\n") for f in cpp)
    stats["uassets"] = sum(1 for f in files if f.startswith("Content/MegaJam26/") and f.endswith(".uasset"))
    stats["umaps"] = sum(1 for f in files if f.startswith("Content/MegaJam26/") and f.endswith(".umap"))
    stats["scripts"] = sum(1 for f in files if f.startswith("Scripts/"))
    return stats


def n(v) -> str:
    return "n/a" if v is None else f"{v:,}"


def render(cc: dict, export: dict | None, manual: dict, gh: dict, ref: str) -> str:
    now = datetime.now().astimezone().strftime("%Y-%m-%d %H:%M %Z")
    out = [f"_Generated {now} by `Scripts/devlog_stats.py` from `{ref}`. Jam window starts "
           f"{JAM_START:%Y-%m-%d}; earlier activity is listed as pre-jam._", ""]

    out += ["### AI usage", "",
            "| Person | Tool | Sessions | Prompts | Model turns | Tool calls | Input tok | Output tok | Cache write | Cache read | Models |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|"]
    for label, s in cc.items():
        b = s["jam"]
        models = ", ".join(f"{m} ({c:,})" for m, c in b["models"].most_common())
        out.append(f"| Hendrixsan | {label} | {len(b['sessions'])} | {n(b['prompts'])} | {n(b['turns'])} | "
                   f"{n(b['tool_calls'])} | {n(b['input'])} | {n(b['output'])} | {n(b['cache_write'])} | "
                   f"{n(b['cache_read'])} | {models or '-'} |")
    if export:
        out.append(f"| Hendrixsan | Claude Desktop (chat) | {export['conversations']} | {n(export['prompts'])} | "
                   f"{n(export['replies'])} | - | ~{n(export['est_in'])} (est.) | ~{n(export['est_out'])} (est.) | - | - | from claude.ai export |")
    for row in manual.get("ai_tools", []):
        out.append("| " + " | ".join(str(row.get(k, "")) or "-" for k in (
            "person", "tool", "sessions", "prompts", "turns", "tool_calls", "input_tokens",
            "output_tokens", "cache_write", "cache_read", "models")) + " |")
    out += ["", "_Cache read is the context re-sent to the model on each turn and billed at a "
            "discount. It is reported separately so it doesn't inflate the other numbers. "
            "\"Model turns\" counts unique assistant messages._", ""]

    pre = {k: v["pre"] for k, v in cc.items() if v["pre"]["turns"] or v["pre"]["prompts"]}
    if pre:
        out.append("Pre-jam Claude Code activity (declared, not counted above): " + "; ".join(
            f"{k}: {len(b['sessions'])} sessions, {b['prompts']} prompts" for k, b in pre.items()) + ".")
        out.append("")

    out += ["#### Claude Code per day", "", "| Day | Sessions | Prompts | Model turns | Output tok |", "|---|---:|---:|---:|---:|"]
    merged = defaultdict(empty_bucket)
    for s in cc.values():
        for d, b in s["days"].items():
            m = merged[d]
            m["sessions"] |= b["sessions"]
            for k in ("prompts", "turns", "output"):
                m[k] += b[k]
    for d, b in sorted(merged.items()):
        out.append(f"| {d:%a %b %d} | {len(b['sessions'])} | {n(b['prompts'])} | {n(b['turns'])} | {n(b['output'])} |")
    out.append("")

    meshy = manual.get("meshy")
    if meshy:
        out += ["### Meshy (3D generation)", ""]
        out += [f"- {k.replace('_', ' ').capitalize()}: {v}" for k, v in meshy.items()]
        out.append("")

    out += ["### Repository", "",
            f"- **{n(gh['commits'])} commits** on main (" + ", ".join(
                f"{datetime.fromisoformat(d):%b %d}: {c}" for d, c in gh["commit_days"].items()) + ")",
            f"- **{n(gh['claude_coauthored'])}** commits carry a Claude co-author line"]
    if gh.get("prs") is not None:
        out += [f"- **{n(gh['prs'])} pull requests** ({gh['prs_merged']} merged): " + ", ".join(
                    f"{a} {c}" for a, c in gh["pr_authors"].most_common()),
                f"- **{n(gh['issues'])} issues** ({gh['issues_closed']} closed): " + ", ".join(
                    f"{a} {c}" for a, c in gh["issue_authors"].most_common())]
    out += [f"- **{n(gh['cpp_lines'])} lines of C++** in {gh['cpp_files']} files under `Source/`",
            f"- **{n(gh['uassets'])} assets** (`.uasset`) and {gh['umaps']} map{'s' if gh['umaps'] != 1 else ''} under `Content/MegaJam26/`",
            f"- **{gh['scripts']} scripts** in `Scripts/`"]
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--claude-export", type=Path, help="claude.ai export conversations.json")
    ap.add_argument("--projects", type=Path, default=Path.home() / ".claude" / "projects")
    ap.add_argument("--ref", default="origin/main", help="git ref for repo numbers")
    ap.add_argument("--print", action="store_true", help="print only, do not edit DEVLOG.md")
    args = ap.parse_args()

    manual = json.loads(MANUAL.read_text(encoding="utf-8")) if MANUAL.exists() else {}
    block = render(scan_claude_code(args.projects), scan_claude_export(args.claude_export),
                   manual, github_stats(args.ref), args.ref)
    if args.print:
        sys.stdout.reconfigure(encoding="utf-8")
        print(block)
        return 0
    text = DEVLOG.read_text(encoding="utf-8")
    if BEGIN not in text or END not in text:
        sys.exit(f"{DEVLOG} is missing the {BEGIN} / {END} markers")
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    DEVLOG.write_text(f"{head}{BEGIN}\n{block}\n{END}{tail}", encoding="utf-8", newline="\n")
    print(f"Updated {DEVLOG.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
