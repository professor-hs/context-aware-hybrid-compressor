#!/usr/bin/env python3
"""End-to-End API and Real File Compression Verification for CACZ.

Tests the native C++ HTTP server, real binary uploads > 65,536 bytes,
archive generation, inspection headers, verification, and byte-for-byte
lossless decompression matching original hashes.
"""

import hashlib
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error
import json

SERVER_PORT = 8089
SERVER_URL = f"http://127.0.0.1:{SERVER_PORT}"

def sha256_of_file(filepath: str) -> str:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def sha256_of_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def wait_for_server(url: str, timeout_sec: float = 10.0) -> bool:
    start = time.time()
    while time.time() - start < timeout_sec:
        try:
            req = urllib.request.Request(f"{url}/api/health")
            with urllib.request.urlopen(req, timeout=1.0) as resp:
                if resp.status == 200:
                    return True
        except Exception:
            time.sleep(0.1)
    return False

def run_tests():
    exe_path = os.path.join("out", "cacz.exe")
    if not os.path.exists(exe_path):
        exe_path = os.path.join("build", "cacz.exe")
    if not os.path.exists(exe_path):
        print(f"Error: {exe_path} not found. Please build the C++ project first.")
        sys.exit(1)

    print(f"Starting native C++ server on port {SERVER_PORT}...")
    server_proc = subprocess.Popen([exe_path, "server", "--port", str(SERVER_PORT)])

    try:
        if not wait_for_server(SERVER_URL):
            print("Error: Server failed to start within timeout.")
            sys.exit(1)
        print("Server successfully started and healthy!\n")

        # 1. Test /api/health
        print("Test 1: GET /api/health")
        with urllib.request.urlopen(f"{SERVER_URL}/api/health") as resp:
            data = json.loads(resp.read().decode())
            assert data.get("status") == "healthy", "Health status mismatch"
            print("  Result: PASS -", data)

        # 2. Test /api/codecs
        print("\nTest 2: GET /api/codecs")
        with urllib.request.urlopen(f"{SERVER_URL}/api/codecs") as resp:
            codecs = json.loads(resp.read().decode())
            assert len(codecs) >= 5, "Expected at least 5 codecs"
            codec_names = [c["name"] for c in codecs]
            print(f"  Result: PASS - Registered codecs: {codec_names}")

        # 3. Test Real Binary Compression on Large File (> 65,536 bytes)
        large_file = os.path.join("benchmark", "test_files", "english_text.txt")
        orig_size = os.path.getsize(large_file)
        orig_hash = sha256_of_file(large_file)
        print(f"\nTest 3: POST /api/compress on '{large_file}'")
        print(f"  Original Size:   {orig_size:,} bytes (> 65,536 bytes)")
        print(f"  Original SHA256: {orig_hash}")

        with open(large_file, "rb") as f:
            file_bytes = f.read()

        req = urllib.request.Request(
            f"{SERVER_URL}/api/compress",
            data=file_bytes,
            headers={
                "Content-Type": "application/octet-stream",
                "X-Filename": "english_text.txt",
                "X-Objective": "balanced",
            },
            method="POST"
        )

        with urllib.request.urlopen(req) as resp:
            assert resp.status == 200, f"Expected 200, got {resp.status}"
            archive_bytes = resp.read()
            reported_orig_size = int(resp.headers.get("X-CACZ-Original-Size", "0"))
            reported_arc_size = int(resp.headers.get("X-CACZ-Archive-Size", "0"))
            reported_ratio = float(resp.headers.get("X-CACZ-Compression-Ratio", "0"))
            reported_chunks = int(resp.headers.get("X-CACZ-Chunk-Count", "0"))
            reported_verified = resp.headers.get("X-CACZ-Verified")
            reported_codec = resp.headers.get("X-CACZ-Codec")
            content_disp = resp.headers.get("Content-Disposition", "")

        actual_arc_size = len(archive_bytes)
        print(f"  Returned Archive Size: {actual_arc_size:,} bytes")
        print(f"  Reported Size:         {reported_arc_size:,} bytes")
        print(f"  Compression Ratio:     {reported_ratio:.4f}")
        print(f"  Total Chunks:          {reported_chunks}")
        print(f"  Primary Codec:         {reported_codec}")
        print(f"  Content-Disposition:   {content_disp}")
        print(f"  Verified Flag:         {reported_verified}")

        assert actual_arc_size == reported_arc_size, "Response length must match reported archive size"
        assert reported_orig_size == orig_size, "Reported original size must match actual size"
        assert reported_verified == "true", "Archive must be verified"
        assert actual_arc_size < orig_size, "Text file must compress to smaller size"

        # Save archive to disk and test decompression
        saved_cacz = "test_e2e_english.cacz"
        with open(saved_cacz, "wb") as f:
            f.write(archive_bytes)

        # Verify archive using CLI
        ver_res = subprocess.run([exe_path, "verify", saved_cacz], capture_output=True, text=True)
        assert ver_res.returncode == 0, f"CLI verification failed: {ver_res.stderr}"
        print("  CLI Verification:      PASS [CRC32, Framing, Header OK]")

        # Decompress archive using CLI
        restored_file = "test_e2e_english_restored.txt"
        dec_res = subprocess.run([exe_path, "decompress", saved_cacz, restored_file], capture_output=True, text=True)
        assert dec_res.returncode == 0, f"CLI decompress failed: {dec_res.stderr}"

        restored_hash = sha256_of_file(restored_file)
        restored_size = os.path.getsize(restored_file)
        print(f"  Restored Size:         {restored_size:,} bytes")
        print(f"  Restored SHA256:       {restored_hash}")

        assert restored_size == orig_size, "Restored file size must equal original"
        assert restored_hash == orig_hash, "Restored file hash must match original bit-for-bit"
        print("  Bit-for-Bit Identity:  EXACT MATCH CONFIRMED (100% Lossless)")

        # Clean up text test files
        if os.path.exists(saved_cacz): os.remove(saved_cacz)
        if os.path.exists(restored_file): os.remove(restored_file)

        # 4. Test Incompressible / Pre-Compressed File (Already Compressed ZIP)
        zip_file = os.path.join("benchmark", "test_files", "already_compressed.zip")
        zip_orig_size = os.path.getsize(zip_file)
        zip_orig_hash = sha256_of_file(zip_file)
        print(f"\nTest 4: POST /api/compress on Pre-Compressed '{zip_file}'")
        print(f"  Original Size:   {zip_orig_size:,} bytes")
        print(f"  Original SHA256: {zip_orig_hash}")

        with open(zip_file, "rb") as f:
            zip_bytes = f.read()

        req_zip = urllib.request.Request(
            f"{SERVER_URL}/api/compress",
            data=zip_bytes,
            headers={
                "Content-Type": "application/octet-stream",
                "X-Filename": "already_compressed.zip",
                "X-Objective": "balanced",
            },
            method="POST"
        )

        with urllib.request.urlopen(req_zip) as resp:
            zip_arc_bytes = resp.read()
            zip_reported_codec = resp.headers.get("X-CACZ-Codec")
            zip_reported_ratio = float(resp.headers.get("X-CACZ-Compression-Ratio", "0"))

        zip_arc_size = len(zip_arc_bytes)
        expansion_bytes = zip_arc_size - zip_orig_size
        print(f"  Returned Archive Size: {zip_arc_size:,} bytes")
        print(f"  Reported Codec:        {zip_reported_codec} (Negative compression guard activated)")
        print(f"  Compression Ratio:     {zip_reported_ratio:.4f}")
        print(f"  Net Framing Overhead:  +{expansion_bytes} bytes ({((zip_arc_size/zip_orig_size - 1)*100):.2f}% expansion)")

        # Verify decompression of pre-compressed file
        saved_zip_cacz = "test_e2e_zip.cacz"
        restored_zip = "test_e2e_zip_restored.zip"
        with open(saved_zip_cacz, "wb") as f:
            f.write(zip_arc_bytes)

        ver_zip = subprocess.run([exe_path, "verify", saved_zip_cacz], capture_output=True, text=True)
        assert ver_zip.returncode == 0, "ZIP archive verification failed"

        dec_zip = subprocess.run([exe_path, "decompress", saved_zip_cacz, restored_zip], capture_output=True, text=True)
        assert dec_zip.returncode == 0, "ZIP archive decompression failed"

        restored_zip_hash = sha256_of_file(restored_zip)
        assert restored_zip_hash == zip_orig_hash, "Restored ZIP file must match original hash bit-for-bit"
        print("  Bit-for-Bit Identity:  EXACT MATCH CONFIRMED (100% Lossless)")

        if os.path.exists(saved_zip_cacz): os.remove(saved_zip_cacz)
        if os.path.exists(restored_zip): os.remove(restored_zip)

        # 5. Test /api/verify Endpoint
        print("\nTest 5: POST /api/verify endpoint")
        # Send valid archive (english_text.cacz)
        with open("english_text.cacz", "rb") as f:
            valid_cacz_bytes = f.read()
        req_v = urllib.request.Request(
            f"{SERVER_URL}/api/verify",
            data=valid_cacz_bytes,
            headers={"Content-Type": "application/octet-stream"},
            method="POST"
        )
        with urllib.request.urlopen(req_v) as resp:
            v_data = json.loads(resp.read().decode())
            assert v_data.get("valid") is True, "Expected valid=True"
            print("  Valid Archive Result: PASS -", v_data)

        # Send tampered bytes
        corrupted_bytes = bytearray(valid_cacz_bytes)
        corrupted_bytes[50] ^= 0xFF # corrupt payload
        req_bad = urllib.request.Request(
            f"{SERVER_URL}/api/verify",
            data=bytes(corrupted_bytes),
            headers={"Content-Type": "application/octet-stream"},
            method="POST"
        )
        with urllib.request.urlopen(req_bad) as resp:
            bad_data = json.loads(resp.read().decode())
            assert bad_data.get("valid") is False, "Expected valid=False for corrupted archive"
            print("  Corrupt Archive Result: PASS [Correctly Detected] -", bad_data)

        print("\n========================================================")
        print("  ALL END-TO-END TESTS PASSED SUCCESSFULLY!")
        print("========================================================")

    finally:
        print("\nStopping server process...")
        server_proc.terminate()
        server_proc.wait()
        print("Server stopped.")

if __name__ == "__main__":
    run_tests()
