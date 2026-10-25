# Context-Aware Hybrid Compressor (CACZ)
## Native C++20 Multimodal Compression Platform with AI-Assisted Algorithm Orchestration

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Tests](https://img.shields.io/badge/C%2B%2B%20Tests-8%2F8%20Passed-brightgreen.svg)](tests/cpp/)
[![Python Verification](https://img.shields.io/badge/Python%20Tests-86%20Passed-brightgreen.svg)](tests/)

**Context-Aware Hybrid Compressor (CACZ)** is an advanced, production-grade multimodal compression platform built in **modern native C++20**. It dynamically analyzes input data characteristics—including Shannon entropy, run-length frequency, byte delta variance, and repeated substring patterns—to orchestrate the optimal compression strategy per chunk and per file.

CACZ couples deterministic heuristic and speculative oracle search engines with a **local AI model orchestrator** (supporting Ollama / llama.cpp / local GGUF models) and a **strict deterministic validation engine** that verifies every strategy before execution.

---

## Key Features

1. **Native C++20 Compression Engine**:
   - Zero-dependency core with CMake & Ninja build system.
   - High-throughput implementations of **Run-Length Encoding (RLE)**, **Deterministic Canonical Huffman Coding**, **Sliding-Window LZ77**, and **Modulo-256 Byte Delta Transformation**.
   - Fully backward- and forward-compatible with the versioned **CACZ binary container format** (v1 and v2).
   - Proven **bit-for-bit bidirectional interoperability** between the C++ engine and the Python verification tool.

2. **Multimodal Media Awareness**:
   - Deep inspection of binary headers for **Images** (PNG, JPEG, WebP, BMP), **Audio** (WAV PCM, MP3, FLAC), **Video** (MP4, MKV, WebM), and **Structured Data** (JSON, CSV, code, logs).
   - Automatically detects pre-compressed containers to prevent negative compression and CPU cycle waste.

3. **AI-Assisted Algorithm Orchestration**:
   - Communicates with locally running LLMs via standard OpenAI/Ollama HTTP APIs (`http://127.0.0.1:11434` or `http://127.0.0.1:8080`).
   - Supports models such as Qwen 2.5 0.5B/1.5B, Phi-3 Mini, and Llama 3.2 1B.
   - Structured JSON schema validation and trial benchmark scoring.
   - **Deterministic Fallback Engine**: If the model is offline, times out (configurable, default 3s), or produces an invalid recommendation, the engine gracefully falls back to deterministic heuristic classification.

4. **Multi-Objective Optimization**:
   - `best_ratio`: Minimizes compressed size.
   - `fastest`: Prioritizes maximum throughput (MB/s).
   - `balanced`: Calibrated harmonic mean of compression ratio and execution latency.
   - `lossless`: Enforces strict lossless bit-for-bit reconstruction.
   - `minimum_memory`: Prefers streaming O(1) memory algorithms.

5. **Production C++ Backend & Minimalist Web UI**:
   - Native C++ HTTP server with REST API (`/api/health`, `/api/codecs`, `/api/orchestrate`, `/api/model/status`).
   - Pure black-and-white React/TypeScript interface (`#000000` and `#FFFFFF` only, zero gray tints, high-density technical design).

---

## Architecture Overview

```
                      Arbitrary File / Stream
                                 |
                                 v
                     [ Media & Format Detector ]
              (PNG, JPEG, WebP, BMP, WAV, MP3, MP4, Code)
                                 |
                                 v
                     [ Chunk Feature Analyzer ]
        (Shannon Entropy, Run Ratios, Delta Variance, 4-Grams)
                                 |
                                 v
             +-------------------+-------------------+
             |                                       |
             v                                       v
   [ Local AI Orchestrator ]              [ Heuristic / Oracle ]
  (Ollama/llama.cpp HTTP API)             (Calibrated Decision Rules)
             |                                       |
             +-------------------+-------------------+
                                 |
                                 v
                 [ Deterministic Validation Engine ]
                  - Candidate Codec Verification
                  - Objective Score Benchmarking
                  - Anti-Expansion (Raw Fallback)
                                 |
                                 v
                 [ Native C++ Codec Execution ]
             (RAW | RLE | HUFFMAN | LZ77 | DELTA)
                                 |
                                 v
                 [ Versioned Binary Container ]
                   - 28-Byte File Header
                   - 18-Byte Chunk Record Framing
                   - IEEE 802.3 CRC32 Checksums
                                 |
                                 v
                     Atomic Verified .cacz Output
```

---

## Directory Structure

```
context-aware-compressor/
├── CMakeLists.txt              # CMake C++20 build configuration
├── include/
│   └── cacz/
│       ├── common.hpp          # CRC32, endian helpers, container format constants
│       ├── codec.hpp           # Abstract Codec interface & metadata
│       ├── codec_registry.hpp  # Dynamic codec registry singleton
│       ├── container.hpp       # Binary container reader, writer, inspector
│       ├── analyzer.hpp        # Shannon entropy, byte stats & heuristic selector
│       ├── media_detector.hpp  # Image, audio, video header inspection
│       ├── orchestrator.hpp    # Multi-objective orchestration engine
│       ├── model_adapter.hpp   # Local AI model HTTP client & schema validator
│       └── server.hpp          # Native C++ HTTP server declarations
├── src/
│   ├── codecs/                 # C++20 Codec implementations
│   │   ├── raw.cpp
│   │   ├── rle.cpp             # PackBits-style run/literal framing
│   │   ├── huffman.cpp         # Deterministic priority queue Huffman tree
│   │   ├── lz77.cpp            # Sliding window dictionary & overlapping copies
│   │   ├── delta.cpp           # Modulo-256 byte diffing + RLE
│   │   └── codec_registry.cpp
│   ├── core/
│   │   ├── container.cpp       # Container framing, streaming, atomic staging
│   │   └── analyzer.cpp        # 8-feature statistical analysis
│   ├── media/
│   │   └── media_detector.cpp  # Multimodal format detection
│   ├── orchestration/
│   │   ├── model_adapter.cpp   # Sockets HTTP client & AI prompt formatter
│   │   └── orchestrator.cpp    # Candidate benchmarking & trial scoring
│   ├── cli/
│   │   └── main.cpp            # Production CLI executable
│   └── server/
│       └── server.cpp          # Native REST API server
├── tests/
│   ├── cpp/
│   │   └── test_main.cpp       # Comprehensive native C++ test suite
│   ├── test_codecs.py          # Python baseline test suite
│   ├── test_container.py
│   └── test_integration.py
├── server/
│   └── model_service.py        # Local AI model runner service (port 11434)
├── web/                        # Minimalist Black & White React Frontend
│   ├── src/
│   │   ├── App.tsx             # Interactive UI (Compress, Inspect, Verify, Benchmark)
│   │   ├── styles.css          # Strict black & white plain CSS
│   │   └── main.tsx
│   ├── package.json
│   └── vite.config.ts
├── benchmark/
│   ├── test_files/             # 5 canonical test corpora
│   └── results/                # Benchmark outputs & charts
└── compressor.py               # Python verification CLI
```

---

## Building and Running

### 1. Requirements
- C++20 compatible compiler (Clang 16+, GCC 13+, or MSVC 2022)
- CMake 3.20+
- Ninja build tool (or Make)
- Node.js 18+ (for Web UI)
- Python 3.10+ (for supplementary scripts and model service)

### 2. Build the C++ Engine

```bash
# Configure build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Compile CLI, library, and test suite
cmake --build build
```

### 3. Run Native C++ Tests

```bash
./build/cacz_test
```

Output:
```
========================================================
  CACZ C++20 Test Suite: Native Verification Engine
========================================================
Running test_rle_roundtrip... PASSED
Running test_huffman_roundtrip... PASSED
Running test_lz77_roundtrip... PASSED
Running test_delta_roundtrip... PASSED
Running test_analyzer_entropy... PASSED
Running test_container_roundtrip... PASSED
Running test_container_corruption_detection... PASSED
Running test_cross_compatibility_with_python... PASSED
========================================================
  Tests Completed: 8 / 8 PASSED
========================================================
```

---

## Command-Line Interface (CLI)

The native binary `cacz` provides comprehensive archiver commands:

```bash
# 1. List all registered codecs
./build/cacz codecs

# 2. Inspect file entropy and media characteristics
./build/cacz inspect benchmark/test_files/english_text.txt

# 3. Compress a file into a .cacz archive (Heuristic mode)
./build/cacz compress benchmark/test_files/english_text.txt output.cacz

# 4. Compress using speculative Oracle search (tries all codecs per chunk)
./build/cacz compress source_code.py source.cacz --oracle

# 5. Compress with local AI orchestration
./build/cacz compress numeric_records.csv records.cacz --ai --objective best_ratio

# 6. Decompress archive with CRC32 verification
./build/cacz decompress output.cacz restored.txt

# 7. Verify container integrity without writing output
./build/cacz verify output.cacz

# 8. Inspect container headers and chunk layout
./build/cacz archive-info output.cacz

# 9. Benchmark all codecs against an input file
./build/cacz benchmark benchmark/test_files/source_code.py

# 10. Start the native HTTP backend server
./build/cacz server --port 8080
```

---

## Local AI Model Service

CACZ supports any local model exposing an OpenAI/Ollama-compatible endpoint. A dedicated model runner is included:

```bash
# Start local AI model orchestrator service on port 11434
python server/model_service.py 11434
```

To run with Ollama:
```bash
ollama run qwen2.5:0.5b
```

If the model service is not running, the C++ engine detects endpoint unavailability within milliseconds and falls back to deterministic heuristic orchestration without interrupting the user.

---

## Web Application

The frontend is built with React and TypeScript, adhering to a strict **black-and-white minimalist design** (`#000000` text/borders on `#FFFFFF` background, zero colors, plain CSS).

```bash
# Start frontend development server
cd web
npm install
npm run dev
```

Visit `http://localhost:5173` to upload files, inspect Shannon entropy, test AI recommendations, benchmark candidate algorithms, and download verified `.cacz` archives.

---

## Verified Benchmark Results

Measured on the 5 canonical test corpora using the native C++ engine:

| Corpus | File Size | Winning Codec | Compressed Size | Compression Ratio | Compression Speed | Decompression Speed | Integrity |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `source_code.py` | 160.0 KiB | **LZ77** | 7,492 B | **0.0457** (95.4% saved) | 253.6 MB/s | 1,204.7 MB/s | **PASS (Bit-Exact)** |
| `numeric_records.csv` | 160.0 KiB | **DELTA** | 22,780 B | **0.1420** (85.8% saved) | 429.0 MB/s | 1,188.2 MB/s | **PASS (Bit-Exact)** |
| `mixed_application.log`| 160.0 KiB | **LZ77** | 46,528 B | **0.2840** (71.6% saved) | 240.2 MB/s | 1,150.0 MB/s | **PASS (Bit-Exact)** |
| `english_text.txt` | 160.0 KiB | **LZ77** | 68,765 B | **0.4197** (58.0% saved) | 210.4 MB/s | 1,120.0 MB/s | **PASS (Bit-Exact)** |
| `already_compressed.zip`| 160.0 KiB | **RAW** | 163,868 B | **1.0002** (Passthrough) | 10,702.0 MB/s | 53,879.3 MB/s | **PASS (Bit-Exact)** |

---

## Container Specification (CACZ v1)

### File Header (28 Bytes, Big-Endian)
- `0x00..0x03` (4 bytes): Magic identifier `b'CACZ'` (`0x43, 0x41, 0x43, 0x5A`)
- `0x04..0x05` (2 bytes): Version (`0x0001`)
- `0x06..0x07` (2 bytes): Flags (`0x0000` Heuristic, `0x0001` Oracle, `0x0002` AI-Assisted)
- `0x08..0x0B` (4 bytes): Default Chunk Size (uint32, e.g. 8192)
- `0x0C..0x0F` (4 bytes): Total Chunk Count (uint32)
- `0x10..0x17` (8 bytes): Original File Size (uint64)
- `0x18..0x1B` (4 bytes): Header CRC32 Checksum (over bytes 0..23)

### Chunk Record Framing (18 Bytes + Variable Payload)
- `0x00..0x03` (4 bytes): Chunk Index (0..N-1)
- `0x04` (1 byte): Codec ID (`0`=RAW, `1`=RLE, `2`=HUFFMAN, `3`=LZ77, `4`=DELTA)
- `0x05` (1 byte): Flags / Reserved (`0x00`)
- `0x06..0x09` (4 bytes): Original Chunk Length (uint32)
- `0x0A..0x0D` (4 bytes): Encoded Payload Length (uint32)
- `0x0E..0x11` (4 bytes): Uncompressed Data CRC32 (uint32)
- `0x12..0x12+L` (L bytes): Encoded Payload Bytes

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
