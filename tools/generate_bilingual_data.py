from pathlib import Path
import json
import os
import stat
import tempfile
import time

R = Path(__file__).resolve().parents[1]
cat = json.loads((R / 'data-source/catalog.generated.json').read_text(encoding='utf-8'))


def units(text):
    data = text.encode('utf-16-le')
    return [data[i] | data[i + 1] << 8 for i in range(0, len(data), 2)]


pool = []
offs = {}


def intern(text):
    text = text or ''
    if text in offs:
        return offs[text]
    offset = len(pool)
    offs[text] = offset
    pool.extend(units(text))
    pool.append(0)
    return offset


symbols = cat['symbols']
idx = {item['text']: i for i, item in enumerate(symbols)}
srows = [
    (
        intern(item['text']),
        intern(item.get('name_zh', '')),
        intern(item.get('name_en') or item.get('name', '')),
        len(units(item['text'])),
        1 if item.get('emoji') else 0,
    )
    for item in symbols
]

crows = []
grows = []
irefs = []
for ci, category in enumerate(cat['categories']):
    first_group = len(grows)
    for group in category['groups']:
        first_item = len(irefs)
        items = group.get('items', [])
        irefs.extend(idx[text] for text in items)
        grows.append(
            (
                ci,
                int(group.get('cols', 0)),
                int(group.get('row', 0)),
                1 if group.get('spacer') else 0,
                intern(group.get('title', '')),
                first_item,
                len(items),
            )
        )
    crows.append((intern(category['name']), intern(category.get('desc', '')), first_group, len(category['groups'])))

dinds = [idx[text] for text in cat['default_common'] if text in idx]


def origin_normalized_text(text):
    """Normalize presentation-only differences for source-location fallback."""
    return ''.join(
        ch for ch in text
        if ord(ch) != 0xFE0F and not (0x1F3FB <= ord(ch) <= 0x1F3FF)
    )


# Pick one useful source location for every compiled symbol.  The generated
# “全部符号” category is deliberately last in the preference order, otherwise
# every search result would report that synthetic category instead of the
# hand-maintained source category.  “补充符号” is used only when no regular
# visible category contains the symbol.
origin_rows = [None] * len(symbols)
category_priorities = []
for excluded in ({'全部符号', '补充符号'}, {'全部符号'}, set()):
    for ci, category in enumerate(cat['categories']):
        if category['name'] in excluded or ci in category_priorities:
            continue
        category_priorities.append(ci)

for ci in category_priorities:
    category = cat['categories'][ci]
    first_group = crows[ci][2]
    for relative_group_index, group in enumerate(category['groups']):
        if group.get('spacer'):
            continue
        group_index = first_group + relative_group_index
        row_number = int(group.get('row', 0) or 0)
        for item_index, text in enumerate(group.get('items', [])):
            symbol_index = idx[text]
            if origin_rows[symbol_index] is None:
                origin_rows[symbol_index] = (
                    ci,
                    row_number,
                    item_index + 1,
                    0,
                    group_index,
                    item_index,
                )

# Hidden skin-tone/presentation variants may not be referenced directly by a
# category row.  Reuse the source position of their base variant when possible.
normalized_origins = {}
for symbol_index, row in enumerate(origin_rows):
    if row is not None:
        normalized_origins.setdefault(origin_normalized_text(symbols[symbol_index]['text']), row)
for symbol_index, row in enumerate(origin_rows):
    if row is None:
        row = normalized_origins.get(origin_normalized_text(symbols[symbol_index]['text']))
        if row is not None:
            origin_rows[symbol_index] = row

origin_rows = [
    row if row is not None else (0xFFFF, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF)
    for row in origin_rows
]


def symbol_hash(text):
    value = 2166136261
    for unit in units(text):
        value ^= unit
        value = (value * 16777619) & 0xFFFFFFFF
    return value


hash_size = 1
while hash_size < max(8, len(symbols) * 2):
    hash_size <<= 1
hash_table = [0] * hash_size
for symbol_index, item in enumerate(symbols):
    slot = symbol_hash(item['text']) & (hash_size - 1)
    while hash_table[slot]:
        slot = (slot + 1) & (hash_size - 1)
    hash_table[slot] = symbol_index + 1


def format_array(items, formatter, per_line):
    if not items:
        return '    0,'
    return '\n'.join(
        '    ' + ', '.join(formatter(item) for item in items[start:start + per_line]) + ','
        for start in range(0, len(items), per_line)
    )


parts = [
    '#include "symbol_data.h"\n#include <wchar.h>\n',
    'const WCHAR g_ys_string_pool[] = {\n' + format_array(pool, lambda value: f'0x{value:04X}', 12) + '\n};\n',
    'const YSSymbolRecord g_ys_symbols[] = {\n' + format_array(srows, lambda row: '{%du,%du,%du,%du,%du}' % row, 2) + '\n};\n',
    'const YSGroupRecord g_ys_groups[] = {\n' + format_array(grows, lambda row: '{%du,%du,%du,%du,%du,%du,%du}' % row, 2) + '\n};\n',
    'const YSCategoryRecord g_ys_categories[] = {\n' + format_array(crows, lambda row: '{%du,%du,%du,%du,0u}' % row, 2) + '\n};\n',
    'const YSSymbolOriginRecord g_ys_symbol_origins[] = {\n' + format_array(origin_rows, lambda row: '{%du,%du,%du,%du,%du,%du}' % row, 2) + '\n};\n',
    'const uint32_t g_ys_group_items[] = {\n' + format_array(irefs, lambda value: f'{value}u', 12) + '\n};\n',
    'const uint32_t g_ys_default_common_items[] = {\n' + format_array(dinds, lambda value: f'{value}u', 12) + '\n};\n',
    'static const uint32_t g_ys_symbol_hash_table[] = {\n' + format_array(hash_table, lambda value: f'{value}u', 16) + '\n};\n',
    'static uint32_t ys_symbol_hash_value(const WCHAR *text) {\n'
    '    uint32_t hash = 2166136261u;\n'
    '    if (!text) return 0u;\n'
    '    while (*text) { hash ^= (uint16_t)*text++; hash *= 16777619u; }\n'
    '    return hash;\n'
    '}\n'
    'int ys_symbol_index_from_text(const WCHAR *text) {\n'
    f'    const uint32_t mask = {hash_size - 1}u;\n'
    '    uint32_t slot, start, stored;\n'
    '    if (!text || !text[0]) return -1;\n'
    '    slot = ys_symbol_hash_value(text) & mask;\n'
    '    start = slot;\n'
    '    do {\n'
    '        stored = g_ys_symbol_hash_table[slot];\n'
    '        if (!stored) return -1;\n'
    '        if (wcscmp(ys_symbol_text(stored - 1u), text) == 0) return (int)(stored - 1u);\n'
    '        slot = (slot + 1u) & mask;\n'
    '    } while (slot != start);\n'
    '    return -1;\n'
    '}\n',
    f'const size_t g_ys_symbol_count = {len(symbols)}u;\n',
    f'const size_t g_ys_group_count = {len(grows)}u;\n',
    f'const size_t g_ys_category_count = {len(crows)}u;\n',
    f'const size_t g_ys_group_item_count = {len(irefs)}u;\n',
    f'const size_t g_ys_default_common_count = {len(dinds)}u;\n',
]


def make_writable(path: Path) -> None:
    """Clear a read-only attribute left by ZIP extraction, Git or editors."""
    if not path.exists():
        return
    try:
        current = path.stat().st_mode
        path.chmod(current | stat.S_IWRITE | stat.S_IREAD)
    except OSError:
        # The write loop below will report a useful final error if this fails.
        pass


def write_text_resilient(path: Path, text: str, encoding: str = 'ascii') -> None:
    """Write generated data safely on Windows.

    Antivirus scanners, file indexers and editors can briefly hold symbol_data.c
    after a previous generation.  A direct Path.write_text() then fails with
    PermissionError even though the directory is writable.  Use a temporary file,
    clear read-only attributes, retry replacement, and finally fall back to a
    direct write.  The old file remains intact until a complete new file exists.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = text.encode(encoding)
    last_error = None

    # Normal development often regenerates the catalog twice: once explicitly
    # and once through ``build.bat run``.  If the bytes are already identical,
    # do not touch the destination at all.  This avoids needless failures when
    # an editor, antivirus scanner or indexer temporarily opens symbol_data.c
    # without delete sharing on Windows.
    try:
        if path.exists() and path.read_bytes() == payload:
            print(f'{path.name} unchanged; skipped rewrite')
            return
    except OSError:
        # A later retry still gives a useful error when the file is genuinely
        # inaccessible.
        pass

    for attempt in range(12):
        make_writable(path)
        temp_path = None
        try:
            fd, temp_name = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=str(path.parent))
            temp_path = Path(temp_name)
            with os.fdopen(fd, 'wb') as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            make_writable(path)
            os.replace(temp_path, path)
            return
        except PermissionError as exc:
            last_error = exc
            if temp_path is not None:
                try:
                    temp_path.unlink(missing_ok=True)
                except OSError:
                    pass
            # A direct overwrite can succeed when rename/delete sharing is the
            # only restriction on the destination file.
            try:
                make_writable(path)
                with path.open('wb') as stream:
                    stream.write(payload)
                    stream.flush()
                return
            except PermissionError as direct_exc:
                last_error = direct_exc
            time.sleep(0.15 * (attempt + 1))
        except OSError as exc:
            last_error = exc
            if temp_path is not None:
                try:
                    temp_path.unlink(missing_ok=True)
                except OSError:
                    pass
            time.sleep(0.15 * (attempt + 1))

    raise PermissionError(
        f'cannot replace {path} after multiple retries. '
        'Close editors or security tools that exclusively lock the file, and '
        'make sure src\\symbol_data.c is not read-only. '
        f'Last error: {last_error}'
    )


output_path = R / 'src/symbol_data.c'
write_text_resilient(output_path, '\n'.join(parts), encoding='ascii')
print(f'generated {len(symbols)} bilingual symbol records')
