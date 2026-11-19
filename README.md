# searchx

![CI](https://github.com/vidyakark/searchx/actions/workflows/ci.yml/badge.svg)

A from-scratch full-text search engine in C++17 — compressed inverted index, BM25 ranking,
boolean and phrase queries, a multi-threaded HTTP server, and a web UI. No external dependencies.

```
$ searchx search corpus.idx '"quick brown" -fox'
3464 matches in 1.386 ms
```

## Features

| Area | What's implemented |
|---|---|
| **Indexing** | Parallel tokenization (multi-threaded, batched so memory stays bounded), stop-word removal, lightweight stemming |
| **Index format** | Per-term posting lists: delta-encoded doc IDs, term frequencies and word positions, all **variable-byte compressed** |
| **Fast intersection** | **Skip pointers** (one per 64 postings) + rarest-term-first ordering; leapfrog join for phrase queries |
| **Storage** | Index saved to one file and **memory-mapped** (`mmap`) on load — starts instantly, OS page cache shared |
| **Ranking** | **BM25** (k1 = 1.2, b = 0.75) with top-k selection via `nth_element` |
| **Queries** | `a b` (AND), `a OR b`, `a NOT b` / `a -b`, `"exact phrase"`, parentheses |
| **Serving** | Thread-pool HTTP server, JSON API, web UI, thread-safe **LRU result cache** |
| **Quality** | Unit tests, ASan/UBSan clean, parser fuzzed with 200k random queries, GitHub Actions CI |

## Quick start

```bash
make                      # or: cmake -B build && cmake --build build
make test

# Try it on the bundled sample corpus
./build/searchx index data/sample.tsv sample.idx
./build/searchx search sample.idx '"phrase queries"'
./build/searchx serve sample.idx 8080     # open http://127.0.0.1:8080
```

Corpus format is one document per line: `title<TAB>text`.

### Benchmark on a large corpus

```bash
./build/gen_corpus 200000 corpus.tsv       # synthetic Zipfian corpus (265 MB)
./build/searchx index corpus.tsv corpus.idx
./build/bench corpus.idx 20000 4           # queries, threads
```

### HTTP API

```
GET /search?q=<query>&k=<1..100>   -> {"total":..,"tookMs":..,"cached":..,"results":[{"id","title","snippet","score"}]}
GET /stats                         -> index size and cache hit/miss counters
```

### Docker

```bash
docker build -t searchx .
docker run -p 8080:8080 searchx
```

## Benchmarks

200,000 documents, 21.2 M postings, 43 K unique terms (synthetic Zipfian corpus). Mixed workload: 50% two-term AND,
20% three-term AND, 15% OR, 10% phrase, 5% with NOT; popularity-skewed so repeated queries occur like real traffic.
Measured on **Intel Core i5-13450HX (16 logical cores, WSL2)** with 4 query threads.

| Mode | QPS | mean | p50 | p95 | p99 |
|---|---:|---:|---:|---:|---:|
| No cache | 28,647 | 133 µs | 12.9 µs | 226 µs | 2.2 ms |
| LRU cache (77.5% hit rate) | 111,433 | 34 µs | 0.7 µs | 35 µs | 281 µs |

| Metric | Result |
|---|---|
| Index build (200 K docs, all cores) | 5.7 s |
| Postings size | 109.9 MB compressed vs 293.6 MB uncompressed (**2.7x smaller**) |

Reproduce with `make && ./build/gen_corpus 200000 corpus.tsv && ./build/searchx index corpus.tsv corpus.idx && ./build/bench corpus.idx 20000 4`.
## How it works

```
 text ──► tokenize ──► per-term posting lists ──► varbyte + skip table ──► index file ──► mmap
                                                                                 │
 query ──► parse (AST) ──► evaluate (set ops over postings) ──► BM25 top-k ◄─────┘
                                         ▲
                             LRU cache ──┘
```

**Posting layout** — `[docId delta][tf][position bytes][position deltas...]`, every field a varint.
Storing the byte length of the positions lets the iterator hop over them when only doc IDs matter.

**Skip pointers** — every 64th posting has a `{docId, byteOffset, prevDocId}` entry in a table in front of the list.
`advance(target)` binary-searches that table and jumps straight to the right block, so intersecting a rare
term with a very common one costs roughly the *rare* term's length, not the common one's.

**Phrase queries** — positions count every word including stop words, so `"over the lazy"` matches
text containing exactly that, while `"over lazy"` does not.

**Thread safety** — the index is immutable after build/load, so queries run lock-free; only the LRU cache takes a mutex.

## Project layout

```
include/searchx/   varbyte, tokenizer, index (+PostingIter), query, engine, lru, corpus
src/               implementation, HTTP server, CLI
tools/             gen_corpus (synthetic data), bench (latency/QPS)
tests/             unit tests (varint, tokenizer, queries, skip pointers, persistence)
```

## Limitations / ideas for next steps

- ASCII-only tokenizer and a simple suffix stemmer (not full Porter/Snowball)
- Posting lists are varbyte; **SIMD-BP128 / PForDelta** would decompress faster
- Block-Max WAND or MaxScore for dynamic pruning on large OR queries (the p99 tail)
- Incremental indexing / segment merging (currently build-once)
- Typo tolerance (edit-distance / n-gram index), query-time snippets with highlighting
- Index file assumes little-endian

## License

MIT
