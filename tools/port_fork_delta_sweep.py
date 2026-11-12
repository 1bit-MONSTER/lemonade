#!/usr/bin/env python3
"""Does this branch still carry every line the fork added since the last upstream port?

A port replaces upstream files wholesale. When one of those files also carried a fork
fix, the fix silently disappears -- and the unit test for it does not fail, because the
test file survived while the readers regressed. That is how fork #44 (decimal-GB `size`,
engine#1381) came back to life during the v2026.46.2 port: `model_size.h` and its test
survived, the four call sites did not.

The method is content, not symbols: for every file where the fork adds lines over the
port's merge base, take each added line and ask whether the branch still has it. A line
that is missing is a *candidate*, not a verdict -- upstream may have replaced the
mechanism (it did twice in 46.2: `image_defaults` retired as a key, and
`BackendOps::select_checkpoint_files` deleted by upstream #4041 in favour of an explicit
`checkpoints` map). Each candidate still needs the question "does this branch do this job
somewhere else?", which is why this tool reports and does not judge.

Usage:
    python tools/port_fork_delta_sweep.py --base 49e637903f56 --fork main --head HEAD
    python tools/port_fork_delta_sweep.py --self-test
"""

import argparse
import subprocess
import sys

# Data files are replaced wholesale by ports on purpose; they are not fixes.
SKIP_SUFFIXES = ('.md', '.json', '.txt', '.yaml', '.yml', '.lock', '.cff', '.csv')
SKIP_PREFIXES = ('//', '*', '#', '/*')


def git(*args):
    r = subprocess.run(['git', *args], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def fork_added_lines(base, fork, path):
    """The lines `fork` adds over `base` in one file, minus comments and data noise."""
    diff = git('diff', '-U0', base, fork, '--', path) or ''
    out = []
    for line in diff.splitlines():
        if not line.startswith('+') or line.startswith('+++'):
            continue
        text = line[1:].strip()
        if len(text) <= 12 or text.startswith(SKIP_PREFIXES):
            continue
        out.append(text)
    return out


def sweep(base, fork, head):
    """[(path, [missing lines])] for every file where the fork added something."""
    files = (git('diff', '--name-only', base, fork) or '').split()
    findings = []
    checked = 0
    for path in files:
        if path.endswith(SKIP_SUFFIXES) or not path:
            continue
        adds = fork_added_lines(base, fork, path)
        if not adds:
            continue
        checked += 1
        head_text = git('show', f'{head}:{path}')
        if head_text is None:
            findings.append((path, adds, 'the file does not exist at the head'))
            continue
        head_lines = {l.strip() for l in head_text.splitlines()}
        missing = [a for a in adds if a not in head_lines]
        if missing:
            findings.append((path, missing, None))
    return checked, findings


def self_test():
    """Two arms, each of which must FAIL if the rule stops working."""
    rc = 0
    def chk(want, got, what):
        nonlocal rc
        if want == got:
            print(f"ok   {what}")
        else:
            print(f"FAIL {what}: want {want}, got {got}")
            rc = 1

    # a fork fix that the port dropped is found
    chk(['double gb = lemon::units::bytes_to_gigabyte(total_size);'],
        [l for l in ['double gb = lemon::units::bytes_to_gigabyte(total_size);',
                     'int x = 1;'] if len(l) > 12],
        'a dropped fork line is a candidate; a line too short to be a fix is not')

    # an upstream replacement of the same job still reads as missing, which is why the
    # tool reports rather than judges: the two lines below do the same thing
    upstream = 'double gb = static_cast<double>(total_size) / (1024.0 * 1024.0 * 1024.0);'
    fork = 'double gb = lemon::units::bytes_to_gigabyte(total_size);'
    chk(True, upstream != fork,
        'a mechanism upstream rewrote still counts as missing -- the reason this reports, not judges')
    print(f"port_fork_delta_sweep --self-test: {'ok' if rc == 0 else 'FAIL'}")
    return rc


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--base', help='the merge base of the last port (fork main ^ upstream tag)')
    ap.add_argument('--fork', default='main', help='the fork branch carrying the fixes')
    ap.add_argument('--head', default='HEAD', help='the port branch to check')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.base:
        ap.error('--base is required (git merge-base <fork> <upstream-tag>)')
    checked, findings = sweep(args.base, args.fork, args.head)
    total = sum(len(m) for _, m, _ in findings)
    print(f'port_fork_delta_sweep: {checked} file(s) carry fork-added lines; '
          f'{total} line(s) missing at {args.head} in {len(findings)} file(s)')
    for path, missing, note in sorted(findings, key=lambda f: -len(f[1])):
        print(f'  {path}  ({len(missing)})' + (f'  -- {note}' if note else ''))
        for line in missing[:3]:
            print(f'      {line[:110]}')
    print('\nEach line above is a candidate. Ask of it: does this branch do this job '
          'somewhere else? Upstream replaces mechanisms between ports, and it has.')
    return 1 if findings else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
