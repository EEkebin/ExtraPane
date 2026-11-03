"""ExtraPane calendar versions and shared Windows/build metadata."""
from datetime import date
from pathlib import Path
import argparse
import json
import os
import re

ROOT = Path(__file__).resolve().parent.parent

def parse(value):
    match = re.fullmatch(r'(\d{4})\.(\d{2})\.(\d{2})(?:\.([2-9]\d*|1\d+))?(?:-pre\.(0|[1-9]\d*)(?:\.([1-9]\d*))?)?', value)
    if not match:
        raise ValueError('Expected YYYY.MM.DD[.revision][-pre.run.attempt]')
    year, month, day = map(int, match.group(1, 2, 3))
    date(year, month, day)
    revision = int(match[4] or 0)
    if revision > 65535:
        raise ValueError('Windows version revision must be at most 65535')
    return f'{year}.{month}.{day}.{revision}'

def current():
    value = os.environ.get('EXTRAPANE_VERSION') or (ROOT / 'VERSION').read_text().strip()
    parse(value)
    return value

def metadata(day, channel, run, attempt, revision=0, title='', explanation=''):
    base = date.fromisoformat(day).strftime('%Y.%m.%d')
    if revision:
        if revision < 2: raise ValueError('Same-day revisions start at 2')
        base += f'.{revision}'
    if channel == 'pre':
        if int(run) < 1 or int(attempt) < 1: raise ValueError('Run and attempt must be positive')
        version = f'{base}-pre.{run}.{attempt}'
        title = f'v{version}'
        explanation = ''
    elif channel == 'stable':
        version = base
        title, explanation = title.strip(), explanation.strip()
        if not title or not explanation:
            raise ValueError('Stable releases need a playful title and a short name explanation')
        if '\n' in title or '\r' in title or len(title) > 100:
            raise ValueError('Release title must be one short line')
    else: raise ValueError('Unknown release channel')
    return dict(version=version, numeric=parse(version), tag=f'v{version}', title=title,
                explanation=explanation, prerelease=channel == 'pre')

def header(path):
    value = current()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#pragma once\n#define EXTRAPANE_VERSION "' + value + '"\n'
                    '#define EXTRAPANE_VERSION_W L"' + value + '"\n'
                    '#define EXTRAPANE_VERSION_NUM ' + parse(value).replace('.', ',') + '\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--header', type=Path)
    parser.add_argument('--metadata', action='store_true')
    parser.add_argument('--numeric', action='store_true')
    args = parser.parse_args()
    if args.header: header(args.header)
    elif args.metadata:
        result = metadata(os.environ['RELEASE_DATE'], os.environ.get('RELEASE_CHANNEL') or 'pre',
                          os.environ['GITHUB_RUN_NUMBER'], os.environ['GITHUB_RUN_ATTEMPT'],
                          int(os.environ.get('RELEASE_REVISION') or 0),
                          os.environ.get('RELEASE_TITLE', ''), os.environ.get('RELEASE_EXPLANATION', ''))
        Path('release-metadata.json').write_text(json.dumps(result, indent=2))
        with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
            for key in ('version', 'numeric', 'tag'): f.write(f'{key}={result[key]}\n')
        with open(os.environ['GITHUB_ENV'], 'a') as f:
            f.write(f"EXTRAPANE_VERSION={result['version']}\nEXTRAPANE_NUMERIC_VERSION={result['numeric']}\n")
    else: print(parse(current()) if args.numeric else current())
