#!/usr/bin/env python3
"""Плотность условий по файлам и подсистемам.

Инструмент диагностики, а не стиля: высокая КОНЦЕНТРАЦИЯ условий в большом
файле означает, что у кода нет единственной ответственности — то есть для этой
части системы не сформулирована модель, и агент заполнил пустоту частными
случаями.

Читать так:
  * абсолютное число условий в файле — главный сигнал (см. столбец «условий»);
  * плотность на 100 строк — вторичный, у маленьких чисто-математических
    файлов она законно высокая (core/torus.h — образец, а не проблема);
  * case/switch считается отдельно: диспатч по перечислению обычно легитимен.

Запуск: python3 tools/branch_density.py [корень=src]
"""
import re
import sys
import glob
import os
import collections


def strip_comments(text):
    """Настоящий лексер вместо пары regex'ов.

    Прежняя версия (re.sub(r'/\\*.*?\\*/') + split('//')) ловилась на `/*`,
    стоящий ВНУТРИ //-комментария или строки: в main.cpp она молча съедала
    ~600 строк живого кода (26 КБ, от строки 78 до 673) — весь смоук-парсер
    просто не учитывался. Состояний пять: код, //-комментарий, /*-комментарий,
    строка, символьный литерал.
    """
    out = []
    i, n = 0, len(text)
    CODE, LINE, BLOCK, STR, CHR = range(5)
    st = CODE
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ''
        if st == CODE:
            if c == '/' and nxt == '/':
                st = LINE; i += 2; continue
            if c == '/' and nxt == '*':
                st = BLOCK; i += 2; continue
            if c == '"':
                st = STR
            elif c == "'":
                st = CHR
            out.append(c)
        elif st == LINE:
            if c == '\n':
                st = CODE; out.append(c)
        elif st == BLOCK:
            if c == '*' and nxt == '/':
                st = CODE; i += 2; continue
            if c == '\n':
                out.append(c)  # сохранить нумерацию строк
        elif st in (STR, CHR):
            # содержимое строк не попадает в счёт (в логах живут "if (", "||")
            if c == '\\':
                i += 2; continue
            if (st == STR and c == '"') or (st == CHR and c == "'"):
                st = CODE; out.append(c)
            elif c == '\n':
                out.append(c)
        i += 1
    return ''.join(out)


def code_lines(text):
    return [l.rstrip() for l in strip_comments(text).split('\n') if l.strip()]


def measure(path):
    lines = code_lines(open(path, errors='replace').read())
    body = '\n'.join(lines)
    conds = (len(re.findall(r'\bif\s*\(', body))
             + len(re.findall(r'\?[^:;]{1,60}:', body))
             + len(re.findall(r'&&|\|\|', body)))
    cases = len(re.findall(r'\bcase\s', body))
    return len(lines), conds, cases


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'src'
    rows = []
    for f in glob.glob(os.path.join(root, '**', '*.cpp'), recursive=True):
        n, c, sw = measure(f)
        if n >= 80:
            rows.append((c, n, sw, f))
    if not rows:
        print(f'нет файлов под {root}')
        return
    total = sum(r[0] for r in rows)
    rows.sort(reverse=True)

    print(f'Всего условий: {total} в {sum(r[1] for r in rows)} строках\n')
    print(f'{"файл":44}{"строк":>7}{"условий":>9}{"/100":>7}{"case":>6}{"% всех":>8}')
    print('-' * 81)
    for c, n, sw, f in rows[:20]:
        print(f'{f:44}{n:7}{c:9}{100*c/n:7.1f}{sw:6}{100*c/total:7.1f}%')

    print(f'\n→ верхние 2 файла держат {100*sum(r[0] for r in rows[:2])/total:.0f}% всех условий проекта\n')

    groups = collections.defaultdict(lambda: [0, 0])
    for c, n, sw, f in rows:
        key = os.path.relpath(f, root).split(os.sep)[0]
        groups[key][0] += n
        groups[key][1] += c
    print(f'{"подсистема":22}{"строк":>8}{"условий":>9}{"/100":>7}')
    print('-' * 46)
    for k, (n, c) in sorted(groups.items(), key=lambda x: -x[1][1] / max(1, x[1][0])):
        print(f'{k:22}{n:8}{c:9}{100*c/n:7.1f}')


if __name__ == '__main__':
    main()
