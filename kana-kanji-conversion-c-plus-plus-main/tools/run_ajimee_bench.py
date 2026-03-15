#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Run AJIMEE-Bench against an external IME candidate generator.

- Reads AJIMEE-Bench JWTD_v2/v1/evaluation_items.json
- Converts Katakana input -> Hiragana (simple Unicode block mapping)
- Calls your CLI per item to get N-best candidates
- Computes:
  - Acc@1
  - Acc@N (N is the requested n-best, i.e., --n)
  - Acc@K / MRR@K (K is --k)
  - MinCER@1 (minimum CER between best candidate and any reference)

Progress:
  --progress : print progress to stderr
  --every N  : print progress every N items (default 1)

Failure export:
  --fail_jsonl PATH : write failed items as jsonl (based on --fail_mode)
  --fail_mode       : choose criteria for "failed"
      acc1   = acc@1 miss (or generator error)
      accn   = acc@N miss (or generator error)
      either = acc@1 OR acc@N miss (or generator error)  [default]
      both   = acc@1 AND acc@N miss (or generator error)  (i.e., "missing in top-N")
      error  = generator error only

Notes:
- Candidate parsing assumes your CLI prints lines like:
    1    <candidate>    score=...
  (e.g., with --show_bunsetsu)
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple


# ---------------------------
# Text utilities
# ---------------------------

def kata_to_hira(s: str) -> str:
    """
    Convert Katakana to Hiragana by Unicode codepoint shift.
    Keeps non-katakana characters as-is.
    """
    out: List[str] = []
    for ch in s:
        o = ord(ch)
        # Katakana block: U+30A1..U+30F6 (ァ..ヶ)
        if 0x30A1 <= o <= 0x30F6:
            out.append(chr(o - 0x60))
        else:
            out.append(ch)
    return "".join(out)


def levenshtein(a: str, b: str) -> int:
    """Classic DP Levenshtein distance (edit distance)."""
    if a == b:
        return 0
    if not a:
        return len(b)
    if not b:
        return len(a)

    # Ensure b is shorter for less memory
    if len(a) < len(b):
        a, b = b, a

    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, start=1):
        cur = [i]
        for j, cb in enumerate(b, start=1):
            ins = cur[j - 1] + 1
            dele = prev[j] + 1
            sub = prev[j - 1] + (0 if ca == cb else 1)
            cur.append(min(ins, dele, sub))
        prev = cur
    return prev[-1]


def cer(ref: str, hyp: str) -> float:
    """Character Error Rate: edit_distance / len(ref)."""
    if len(ref) == 0:
        return float("inf")
    return levenshtein(ref, hyp) / len(ref)


def min_cer(refs: List[str], hyp: str) -> float:
    return min(cer(r, hyp) for r in refs) if refs else float("inf")


# ---------------------------
# Candidate parsing
# ---------------------------

# Matches lines like:
# 1       今日の天気      score=4686      type=1  L=...
_LINE_RE = re.compile(r"^\s*(\d+)\s+(.+?)\s+score=")


def parse_candidates_from_astar_stdout(stdout: str, topn: int) -> List[str]:
    cands: List[str] = []
    for line in stdout.splitlines():
        m = _LINE_RE.match(line)
        if not m:
            continue
        cand = m.group(2).strip()
        if cand:
            cands.append(cand)
        if len(cands) >= topn:
            break
    return cands


# ---------------------------
# Metrics
# ---------------------------

def acc_at_1(refs: List[str], cands: List[str]) -> int:
    return 1 if cands and cands[0] in refs else 0


def acc_at_k(refs: List[str], cands: List[str], k: int) -> int:
    s = set(refs)
    return 1 if any(c in s for c in cands[:k]) else 0


def mrr_at_k(refs: List[str], cands: List[str], k: int) -> float:
    s = set(refs)
    for i, c in enumerate(cands[:k], start=1):
        if c in s:
            return 1.0 / i
    return 0.0


# ---------------------------
# Result struct
# ---------------------------

@dataclass
class ItemResult:
    index: str
    subset: str
    q_kata: str
    q_hira: str
    refs: List[str]
    cands: List[str]
    ok: bool
    error: Optional[str]
    acc1: int
    accn: int
    acck: int
    mrrk: float
    mincer1: float


# ---------------------------
# Runner
# ---------------------------

def run_one(cmd_tmpl: str, q_hira: str, n: int, beam: int, timeout: int) -> Tuple[List[str], Optional[str], str, str, int]:
    """
    Returns (candidates, error, raw_stdout, raw_stderr, returncode).
    Parses candidates even if returncode != 0 (best-effort).
    """
    cmd = cmd_tmpl.format(q=q_hira, n=n, beam=beam)
    args = shlex.split(cmd)

    try:
        cp = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return [], f"timeout>{timeout}s", "", "", -1
    except Exception as e:
        return [], f"exec_error:{e}", "", "", -1

    stdout = cp.stdout or ""
    stderr = cp.stderr or ""
    rc = cp.returncode

    cands = parse_candidates_from_astar_stdout(stdout, n)

    if rc != 0:
        msg = (stderr.strip().replace("\n", "\\n"))[:200]
        return cands, f"nonzero_exit:{rc} stderr={msg}", stdout, stderr, rc

    if not cands:
        return [], "no_candidates_parsed", stdout, stderr, rc

    return cands, None, stdout, stderr, rc


# ---------------------------
# CLI
# ---------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()

    p.add_argument("--items", type=str, required=True, help="path to AJIMEE-Bench evaluation_items.json")
    p.add_argument("--subset", type=str, default="all", choices=["all", "no_context", "with_context"])

    p.add_argument("--n", type=int, default=10, help="N-best to request from generator (also used for Acc@N)")
    p.add_argument("--beam", type=int, default=50, help="beam size passed to generator template")
    p.add_argument("--k", type=int, default=10, help="K for Acc@K and MRR@K")
    p.add_argument("--timeout", type=int, default=10, help="per-item timeout seconds")

    p.add_argument("--cmd", type=str, required=True, help='command template; use {q}, {n}, {beam}')
    p.add_argument("--verbose", action="store_true")

    # progress
    p.add_argument("--progress", action="store_true", help="print progress to stderr")
    p.add_argument("--every", type=int, default=1, help="progress print interval (items), default 1")

    # outputs
    p.add_argument("--out_jsonl", type=str, default="", help="optional output jsonl path for ALL per-item results")
    p.add_argument("--fail_jsonl", type=str, default="", help="optional output jsonl path for FAILED items only (based on --fail_mode)")
    p.add_argument(
        "--fail_mode",
        type=str,
        default="either",
        choices=["acc1", "accn", "either", "both", "error"],
        help=(
            "criteria for writing to --fail_jsonl: "
            "acc1=acc@1 miss, accn=acc@N miss, either=acc1 OR accn, "
            "both=acc1 AND accn (i.e., missing in top-N), error=generator error only"
        ),
    )

    return p.parse_args()


# ---------------------------
# Helpers
# ---------------------------

def _progress_line(done: int, total: int, ok: int, acc1_hits: int, accn_hits: int, start_ts: float) -> str:
    elapsed = max(1e-9, time.time() - start_ts)
    it_s = done / elapsed
    eta = (total - done) / it_s if it_s > 0 else float("inf")
    return f"[{done}/{total}] ok={ok} acc1_hit={acc1_hits} accN_hit={accn_hits} it/s={it_s:.2f} eta={eta:.1f}s"


def _should_fail(mode: str, ok: bool, a1: int, an: int) -> bool:
    """
    Determine whether this item is considered "failed" for --fail_jsonl,
    based on --fail_mode.
    Note: generator error (ok=False) is always treated as failure except mode=error.
    """
    if mode == "error":
        return (not ok)

    # For other modes: always include generator errors
    if not ok:
        return True

    if mode == "acc1":
        return (a1 == 0)
    if mode == "accn":
        return (an == 0)
    if mode == "both":
        return (a1 == 0) and (an == 0)

    # default: either
    return (a1 == 0) or (an == 0)


def _subset_of(it: Dict[str, Any]) -> str:
    ctx = it.get("context_text", "")
    return "no_context" if ctx == "" else "with_context"


# ---------------------------
# Main
# ---------------------------

def main() -> int:
    args = parse_args()

    with open(args.items, "r", encoding="utf-8") as f:
        items: List[Dict[str, Any]] = json.load(f)

    # Filter by subset so progress total is accurate
    filtered: List[Dict[str, Any]] = []
    for it in items:
        subset = _subset_of(it)
        if args.subset != "all" and subset != args.subset:
            continue
        filtered.append(it)

    total = len(filtered)
    start_ts = time.time()

    results: List[ItemResult] = []

    ok_count = 0
    acc1_hits = 0
    accn_hits = 0

    fail_fh = open(args.fail_jsonl, "w", encoding="utf-8") if args.fail_jsonl else None
    all_fh = open(args.out_jsonl, "w", encoding="utf-8") if args.out_jsonl else None

    try:
        for idx, it in enumerate(filtered, start=1):
            subset = _subset_of(it)

            q_kata = it["input"]
            q_hira = kata_to_hira(q_kata)
            refs = it["expected_output"]

            cands, err, _raw_out, _raw_err, _rc = run_one(args.cmd, q_hira, args.n, args.beam, args.timeout)

            ok = err is None
            if ok:
                ok_count += 1

            a1 = acc_at_1(refs, cands) if ok else 0
            an = acc_at_k(refs, cands, args.n) if ok else 0  # Acc@N (N = --n)
            ak = acc_at_k(refs, cands, args.k) if ok else 0
            rr = mrr_at_k(refs, cands, args.k) if ok else 0.0
            mc1 = min_cer(refs, cands[0]) if (ok and cands) else float("inf")

            acc1_hits += a1
            accn_hits += an

            r = ItemResult(
                index=str(it.get("index", "")),
                subset=subset,
                q_kata=q_kata,
                q_hira=q_hira,
                refs=refs,
                cands=cands,
                ok=ok,
                error=err,
                acc1=a1,
                accn=an,
                acck=ak,
                mrrk=rr,
                mincer1=mc1,
            )
            results.append(r)

            # Write ALL results if requested
            if all_fh is not None:
                all_fh.write(
                    json.dumps(
                        {
                            "index": r.index,
                            "subset": r.subset,
                            "q_kata": r.q_kata,
                            "q_hira": r.q_hira,
                            "refs": r.refs,
                            "cands": r.cands,
                            "ok": r.ok,
                            "error": r.error,
                            "acc1": r.acc1,
                            f"acc@{args.n}": r.accn,
                            f"acc@{args.k}": r.acck,
                            f"mrr@{args.k}": r.mrrk,
                            "mincer1": r.mincer1,
                        },
                        ensure_ascii=False,
                    )
                    + "\n"
                )

            # Write FAILED only
            failed = _should_fail(args.fail_mode, ok, a1, an)
            if failed and fail_fh is not None:
                fail_fh.write(
                    json.dumps(
                        {
                            "index": r.index,
                            "subset": r.subset,
                            "q_kata": r.q_kata,
                            "q_hira": r.q_hira,
                            "refs": r.refs,
                            # keep a bounded list; include enough to inspect
                            "top_candidates": r.cands[: max(args.n, args.k)],
                            "ok": r.ok,
                            "error": r.error,
                            "acc1": r.acc1,
                            f"acc@{args.n}": r.accn,
                            f"acc@{args.k}": r.acck,
                            f"mrr@{args.k}": r.mrrk,
                            "mincer1": r.mincer1,
                            "fail_mode": args.fail_mode,
                        },
                        ensure_ascii=False,
                    )
                    + "\n"
                )

            # Optional verbose per-item debug
            if args.verbose and (failed or not ok):
                print(
                    f"[{subset}] index={r.index} ok={r.ok} "
                    f"acc1={r.acc1} acc@{args.n}={r.accn} acc@{args.k}={r.acck} "
                    f"mrr@{args.k}={r.mrrk:.3f} mincer1={r.mincer1:.3f}",
                    file=sys.stderr,
                )
                if r.error:
                    print(f"  error: {r.error}", file=sys.stderr)
                print(f"  q_kata: {r.q_kata}", file=sys.stderr)
                print(f"  q_hira: {r.q_hira}", file=sys.stderr)
                print(f"  refs  : {r.refs[:3]}{'...' if len(r.refs)>3 else ''}", file=sys.stderr)
                print(f"  top3  : {r.cands[:3]}", file=sys.stderr)

            # Progress
            if args.progress and (idx % max(1, args.every) == 0 or idx == total):
                print(_progress_line(idx, total, ok_count, acc1_hits, accn_hits, start_ts), file=sys.stderr)

        # Aggregate summary
        def agg(sub: str) -> Dict[str, float]:
            rs = [x for x in results if x.subset == sub]
            if not rs:
                return {"count": 0}
            count = len(rs)
            okc = sum(1 for x in rs if x.ok)
            acc1 = sum(x.acc1 for x in rs) / count
            accn = sum(x.accn for x in rs) / count
            acck = sum(x.acck for x in rs) / count
            mrrk = sum(x.mrrk for x in rs) / count
            mincers = [x.mincer1 for x in rs if x.mincer1 != float("inf")]
            mincer1 = (sum(mincers) / len(mincers)) if mincers else float("inf")
            return {
                "count": float(count),
                "ok": float(okc),
                "acc1": acc1,
                f"acc@{args.n}": accn,
                f"acc@{args.k}": acck,
                f"mrr@{args.k}": mrrk,
                "mincer1": mincer1,
            }

        a = agg("no_context")
        b = agg("with_context")

        elapsed = time.time() - start_ts
        print("=== AJIMEE-Bench results ===")
        if args.subset in ("all", "no_context"):
            print(
                f"[no_context]  count={int(a['count'])} ok={int(a['ok'])}  "
                f"acc1={a['acc1']:.4f}  acc@{args.n}={a[f'acc@{args.n}']:.4f}  "
                f"acc@{args.k}={a[f'acc@{args.k}']:.4f}  mrr@{args.k}={a[f'mrr@{args.k}']:.4f}  "
                f"mincer1={a['mincer1']:.4f}"
            )
        if args.subset in ("all", "with_context"):
            print(
                f"[with_context] count={int(b['count'])} ok={int(b['ok'])}  "
                f"acc1={b['acc1']:.4f}  acc@{args.n}={b[f'acc@{args.n}']:.4f}  "
                f"acc@{args.k}={b[f'acc@{args.k}']:.4f}  mrr@{args.k}={b[f'mrr@{args.k}']:.4f}  "
                f"mincer1={b['mincer1']:.4f}"
            )
        print(f"elapsed_sec={elapsed:.2f}")

        if args.fail_jsonl:
            print(f"failed_items_jsonl={args.fail_jsonl} (fail_mode={args.fail_mode})")
        if args.out_jsonl:
            print(f"all_items_jsonl={args.out_jsonl}")

        return 0

    finally:
        if fail_fh is not None:
            fail_fh.close()
        if all_fh is not None:
            all_fh.close()


if __name__ == "__main__":
    raise SystemExit(main())
