"""Validate diagnostic coverage and compare ordered seed tables by input read ID."""
import argparse
import json
from pathlib import Path
import struct
import sys


def _seed_tables(folder,row_limit=None):
    tables, workers = {}, {}
    for path in sorted(Path(folder).glob('seeds-*.jsonl')):
        worker = path.stem[len('seeds-'):]
        if not worker.isdecimal():
            raise ValueError('invalid seed-table worker file')
        worker_tables = {}
        worker_rows=0
        with path.open() as stream:
            for line in stream:
                row = json.loads(line)
                read_id = row['read_id']
                if type(read_id) is not int or not 0 <= read_id < 2**64 or read_id in tables:
                    raise ValueError('invalid or repeated seed-table read ID; remapping captures unsupported')
                if not isinstance(row['seeds'], list) or any(not isinstance(seed, list) or len(seed) != 7
                        or any(type(n) is not int or not 0 <= n < 2**64 for n in seed) for seed in row['seeds']):
                    raise ValueError('invalid seed table')
                worker_rows+=len(row['seeds'])
                if row_limit is not None and worker_rows>row_limit:
                    raise ValueError('seed rows exceed declared worker limit')
                tables[read_id] = row['seeds']
                worker_tables[read_id] = row['seeds']
        workers[worker] = worker_tables
    if not tables:
        raise ValueError('missing sampled seed tables')
    return tables, workers


def seed_tables(folder):
    return _seed_tables(folder)[0]


def sampled(read_id, modulus):
    """The producer's splitmix64 arithmetic wraps after each uint64 operation."""
    mask = 2**64-1
    value = (read_id+0x9e3779b97f4a7c15) & mask
    value = ((value ^ (value >> 30))*0xbf58476d1ce4e5b9) & mask
    value = ((value ^ (value >> 27))*0x94d049bb133111eb) & mask
    return ((value ^ (value >> 31)) % modulus) == 0


def _unsigned(value, name):
    if type(value) is not int or not 0 <= value < 2**64:
        raise ValueError(f'invalid diagnostic counter {name}')
    return value


def _query_records(folder, metadata, worker_stats):
    """Validate the whole native capture, including any tail beyond replay limits."""
    files={p.stem[len('queries-'):]:p for p in folder.glob('queries-*.bin')}
    if set(files)!=set(worker_stats):raise ValueError('query capture workers disagree with worker statistics')
    fields=('native_uint_bytes','query_bytes','match_bytes','byte_order')
    if any(key in metadata for key in fields):
        if any(key not in metadata for key in fields) or tuple(metadata[key] for key in fields[:3])!=(8,64,32):
            raise ValueError('unsupported native capture ABI')
        byte_order=metadata['byte_order'];abi_status='VERIFIED_DECLARED_ABI'
    else:
        byte_order=sys.byteorder;abi_status='UNVERIFIED_LEGACY_NATIVE_ABI'
    if byte_order not in ('little','big'):raise ValueError('unsupported capture byte order')
    endian='<' if byte_order=='little' else '>';counts={}
    for worker,path in files.items():
        stats=worker_stats[worker];count=0
        with path.open('rb') as stream:
            if stream.read(8)!=b'STARSD01':raise ValueError('unknown or truncated native capture format')
            while True:
                identity=stream.read(8)
                if not identity:break
                query=stream.read(64);match=stream.read(32)
                if len(identity)!=8 or len(query)!=64 or len(match)!=32:raise ValueError('truncated capture record header')
                read_id=struct.unpack(endian+'Q',identity)[0]
                offset,read_bytes,start,length,lower,upper,initial,forward=struct.unpack(endian+'8Q',query)
                best,first,last,multiplicity=struct.unpack(endian+'4Q',match)
                if (offset or not 0<read_bytes<=16*1024*1024 or not 0<length<=read_bytes
                        or start>=read_bytes or initial>length or forward>1 or lower>upper
                        or (length>read_bytes-start if forward else length>start+1)):
                    raise ValueError('invalid native captured query')
                if best>length or not lower<=first<=last<=upper or multiplicity!=last-first+1:
                    raise ValueError('invalid native captured search result')
                if not stats['first_observed_read']<=read_id<=stats['last_observed_read'] or not sampled(read_id,metadata['sample_modulus']):
                    raise ValueError('captured query read violates observed extent or deterministic sampling')
                remaining=read_bytes
                while remaining:
                    payload=stream.read(min(remaining,65536))
                    if not payload:raise ValueError('truncated captured read payload')
                    if max(payload)>5:raise ValueError('invalid captured read alphabet')
                    remaining-=len(payload)
                count+=1
        if count!=stats['recorded']:raise ValueError('captured record count disagrees with worker statistics')
        counts[worker]=count
    return dict(status='PASS_COMPLETE_NATIVE_RECORDS',abi_status=abi_status,workers=len(counts),records=sum(counts.values()))


def summarize(folder):
    folder = Path(folder)
    worker_stats = {p.name[len('queries-'):-len('.bin.json')]: json.loads(p.read_text())
                    for p in sorted(folder.glob('queries-*.bin.json'))}
    if not worker_stats:
        raise ValueError('missing worker statistics')
    if any(not worker.isdecimal() for worker in worker_stats):
        raise ValueError('invalid worker statistics file')
    stats = list(worker_stats.values())
    metadata = json.loads((folder/'capture_metadata.json').read_text())
    modulus = _unsigned(metadata['sample_modulus'], 'sample_modulus')
    if not modulus or metadata.get('format') != 1 or metadata.get('sampling') != 'splitmix64(read_id)%modulus==0':
        raise ValueError('unsupported sampling contract')
    if any(s['sample_modulus'] != modulus for s in stats):
        raise ValueError('inconsistent sampling contract')
    row_limit=metadata.get('seed_row_limit_per_worker')
    if row_limit is not None and not _unsigned(row_limit,'seed_row_limit_per_worker'):
        raise ValueError('invalid seed row limit')
    tables, worker_tables = _seed_tables(folder,row_limit)
    if set(worker_tables) != set(worker_stats):
        raise ValueError('seed-table workers disagree with worker statistics')
    bounds = ('first_selected_read', 'last_selected_read', 'first_observed_read', 'last_observed_read')
    for worker, s in worker_stats.items():
        for key, value in s.items():
            if isinstance(value, int):
                _unsigned(value, key)
        if s.get('format') != 1 or s['limit_per_worker'] != metadata['limit_per_worker']:
            raise ValueError('inconsistent worker capture format or limit')
        if not 0 <= s['seed_reads'] <= s['reads_selected'] <= s['reads_seen']:
            raise ValueError('inconsistent selected-read counters')
        if s['seed_reads_dropped'] != s['reads_selected']-s['seed_reads']:
            raise ValueError('inconsistent dropped seed-read counter')
        if not 0 <= s['recorded'] <= s['selected'] <= s['extension_requests_seen']:
            raise ValueError('inconsistent selected-query counters')
        if s['dropped'] != s['selected']-s['recorded'] or s['recorded'] > s['limit_per_worker']:
            raise ValueError('inconsistent dropped query counter or capture limit')
        rows = worker_tables[worker]
        if len(rows) != s['seed_reads'] or sum(map(len, rows.values())) != s['seed_rows']:
            raise ValueError('seed tables disagree with worker counters')
        if row_limit is not None:
            if s.get('seed_row_limit_per_worker')!=row_limit or s['seed_rows']>row_limit:
                raise ValueError('seed rows exceed or disagree with declared worker limit')
        for key in bounds:
            if key not in s:
                raise ValueError('unverified observed input extent; regenerate diagnostics')
            _unsigned(s[key], key)
        if s['reads_seen'] and s['first_observed_read'] > s['last_observed_read']:
            raise ValueError('invalid observed input extent')
        if s['reads_selected'] and not s['first_observed_read'] <= s['first_selected_read'] <= s['last_selected_read'] <= s['last_observed_read']:
            raise ValueError('selected-read extent exceeds observed input')
        for read_id in rows:
            if not s['first_observed_read'] <= read_id <= s['last_observed_read']:
                raise ValueError('sampled read outside observed input extent')
            if not sampled(read_id, modulus):
                raise ValueError('sampled read violates deterministic hash selection')
        if rows and not s['seed_reads_dropped'] and (min(rows) != s['first_selected_read'] or max(rows) != s['last_selected_read']):
            raise ValueError('seed-table extent disagrees with selected-read counters')
        for key in ('interval_width_log2', 'query_length_log2', 'multiplicity_log2'):
            histogram = s[key]
            if not isinstance(histogram, list) or len(histogram) != 64:
                raise ValueError('invalid diagnostic histogram')
            if sum(_unsigned(n, key) for n in histogram) != s['extension_requests_seen']:
                raise ValueError('query histogram disagrees with extension count')
    totals = {key: sum(s[key] for s in stats) for key in stats[0]
              if isinstance(stats[0][key], int) and key not in
              ('format', 'sample_modulus', 'limit_per_worker', 'seed_row_limit_per_worker', *bounds)}
    histograms = {key: [sum(s[key][i] for s in stats) for i in range(64)]
                  for key in ('interval_width_log2', 'query_length_log2', 'multiplicity_log2')}
    if len(tables) != totals['seed_reads'] or sum(map(len, tables.values())) != totals['seed_rows']:
        raise ValueError('seed tables disagree with worker counters')
    if sum(histograms['interval_width_log2']) != totals['extension_requests_seen']:
        raise ValueError('query histogram disagrees with extension count')
    nonempty = [s for s in stats if s['reads_seen']]
    if not nonempty:
        raise ValueError('missing observed input extent')
    first_observed = min(s['first_observed_read'] for s in nonempty)
    last_observed = max(s['last_observed_read'] for s in nonempty)
    observed_span = last_observed-first_observed+1
    deciles = [0]*10
    for read_id in tables:
        deciles[min(9, (read_id-first_observed)*10//observed_span)] += 1
    map_ns = totals['sampled_map_ns']
    fractions = {key: totals[key]/map_ns if map_ns else None for key in
                 ('sampled_seed_ns', 'sampled_extension_ns', 'sampled_stitch_ns')}
    return dict(status='PASS_DIAGNOSTIC_CONTRACT', workers=len(stats), totals=totals,
                native_capture=_query_records(folder,metadata,worker_stats),
                seed_row_cap_status='VERIFIED_DECLARED_CAP' if row_limit is not None else 'UNVERIFIED_LEGACY_NO_ROW_CAP',
                histograms=histograms, sampled_read_deciles=deciles,
                first_sampled_read=min(tables), last_sampled_read=max(tables),
                first_observed_read=first_observed, last_observed_read=last_observed,
                observed_read_id_span=observed_span, coverage_extent='all observed input read IDs; independent of selected sample',
                eligible_for_screening=not (totals['dropped'] or totals['seed_reads_dropped']) and all(deciles),
                sampled_nested_cpu_fractions=fractions,
                timing_scope='nested instrumented worker intervals excluding capture IO; verify against normal wall time')


def compare(left, right):
    a, b = seed_tables(left), seed_tables(right)
    if a != b:
        ids = sorted(set(a)|set(b))
        first = next(i for i in ids if a.get(i) != b.get(i))
        raise ValueError(f'ordered seed mismatch at input read {first}')
    return dict(status='PASS_ORDERED_SEED_TABLES', reads=len(a), seeds=sum(map(len, a.values())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    print(json.dumps(compare(args.capture,args.compare) if args.compare else summarize(args.capture), indent=2))


if __name__ == '__main__':
    main()
