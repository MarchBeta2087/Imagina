#!/usr/bin/env python3
"""Checks that every string the front-ends pass to Tr()/TrW() is translated.

    python3 tools/check-translations.py [--quiet]

Exits with 1 when a string used in the source has no row in the table of
src/Language.cpp; strings in the table that nothing uses are only reported, since
a few of them are looked up indirectly (the menu and dialog texts of the Win32
front-end go through the same table).
"""
import pathlib
import re
import sys

Root = pathlib.Path(__file__).resolve().parent.parent
Table = Root / 'src' / 'Language.cpp'

# { Language::Chinese, "English", "translated" },
RowPattern = re.compile(r'\{\s*Language::(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
CallPattern = re.compile(r'\bTr(?:W)?\("((?:[^"\\]|\\.)*)"\)')


def main() -> int:
    table = Table.read_text(encoding='utf-8')
    rows = RowPattern.findall(table)
    if not rows:
        print(f'{Table}: no translation rows found', file=sys.stderr)
        return 1

    known = {row[1] for row in rows}

    used = set()
    for path in list(Root.glob('src/**/*.cpp')) + list(Root.glob('src/**/*.h')):
        text = path.read_text(encoding='utf-8', errors='replace')
        used.update(CallPattern.findall(text))

    missing = sorted(used - known)
    unused = sorted(known - used)

    print(f'{len(rows)} translations, {len(used)} strings used in the source')

    if missing:
        print(f'\n{len(missing)} string(s) without a translation:')
        for text in missing:
            print(f'  Tr("{text}")')
        return 1

    if unused and '--quiet' not in sys.argv:
        print(f'\n{len(unused)} translation(s) not passed to Tr() directly '
              f'(menu and dialog texts of the Win32 front-end are looked up by their visible text):')
        for text in unused[:20]:
            print(f'  "{text}"')
        if len(unused) > 20:
            print(f'  ... and {len(unused) - 20} more')

    print('\nall strings used in the source are translated')
    return 0


if __name__ == '__main__':
    sys.exit(main())
