#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Structural sanity check for C++ sources: strips comments/string literals and
verifies brace/paren/bracket balance. Not a compiler, just a fast guard."""
import sys, io


def strip(src: str) -> str:
    out = []
    i, n = 0, len(src)
    state = None
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ''
        if state is None:
            if c == '/' and nxt == '/':
                state = 'line'; i += 2; continue
            if c == '/' and nxt == '*':
                state = 'block'; i += 2; continue
            if c == '"':
                state = 'str'; out.append(' '); i += 1; continue
            if c == "'":
                state = 'chr'; out.append(' '); i += 1; continue
            out.append(c); i += 1; continue
        if state == 'line':
            if c == '\n':
                state = None; out.append('\n')
            i += 1; continue
        if state == 'block':
            if c == '*' and nxt == '/':
                state = None; i += 2; continue
            if c == '\n':
                out.append('\n')
            i += 1; continue
        if c == '\\':
            i += 2; continue
        if (state == 'str' and c == '"') or (state == 'chr' and c == "'"):
            state = None
        i += 1
    return ''.join(out)


def main(paths):
    bad = 0
    for p in paths:
        with io.open(p, encoding='utf-8') as f:
            src = f.read()
        stack, line, err = [], 1, None
        pairs = {')': '(', '}': '{', ']': '['}
        for ch in strip(src):
            if ch == '\n':
                line += 1
            elif ch in '({[':
                stack.append((ch, line))
            elif ch in ')}]':
                if not stack or stack[-1][0] != pairs[ch]:
                    err = 'unmatched %r at line %d' % (ch, line); break
                stack.pop()
        if err is None and stack:
            err = 'unclosed %r opened at line %d' % (stack[-1][0], stack[-1][1])
        if err:
            bad += 1
        print(('OK   ' if err is None else 'FAIL ') + p + ('' if not err else '  -> ' + err))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
