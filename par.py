"""pmap: map a top-level function over items on every core (processes, not threads -- the work is
CPU-bound Python, which threads cannot run in parallel). Results come back in input order."""
import os
from concurrent.futures import ProcessPoolExecutor


def pmap(fn, items, chunksize=16):
    items = list(items)
    if len(items) < 2 * chunksize:
        return [fn(x) for x in items]
    with ProcessPoolExecutor(os.cpu_count()) as ex:
        return list(ex.map(fn, items, chunksize=chunksize))
