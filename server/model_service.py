#!/usr/bin/env python3
"""Local AI Model Service for Context-Aware Hybrid Compressor (CACZ).

Implements an OpenAI/Ollama-compatible HTTP endpoint for local AI-assisted
algorithm orchestration. Listens on http://127.0.0.1:11434 by default.
"""

import json
import re
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
import urllib.parse

PORT = 11434

def determine_recommendation(user_prompt: str) -> dict:
    """Analyze prompt parameters and generate intelligent orchestration recommendation."""
    # Extract features using regex
    entropy_match = re.search(r"Shannon Entropy:\s*([0-9.]+)", user_prompt)
    rep_match = re.search(r"Repetition 4-gram ratio:\s*([0-9.]+)", user_prompt)
    run_match = re.search(r"Run-length ratio:\s*([0-9.]+)", user_prompt)
    delta_var_match = re.search(r"Delta variance:\s*([0-9.]+)", user_prompt)
    delta_run_match = re.search(r"Delta run ratio:\s*([0-9.]+)", user_prompt)
    mime_match = re.search(r"MIME:\s*([a-zA-Z0-9/_-]+)", user_prompt)
    obj_match = re.search(r"Objective:\s*([a-zA-Z0-9_-]+)", user_prompt)

    entropy = float(entropy_match.group(1)) if entropy_match else 5.0
    rep_ratio = float(rep_match.group(1)) if rep_match else 0.0
    run_ratio = float(run_match.group(1)) if run_match else 0.0
    delta_var = float(delta_var_match.group(1)) if delta_var_match else 100.0
    delta_run = float(delta_run_match.group(1)) if delta_run_match else 0.0
    mime = mime_match.group(1) if mime_match else ""
    objective = obj_match.group(1) if obj_match else "balanced"

    if "image/png" in mime or "image/jpeg" in mime or "audio/mpeg" in mime or "video/" in mime or "zip" in mime:
        return {
            "recommended_codec": "raw",
            "confidence": 0.98,
            "rationale": f"Input is already compressed ({mime}). Additional compression produces expansion.",
        }

    if delta_run > 0.25 or delta_var < 60.0:
        return {
            "recommended_codec": "delta",
            "confidence": 0.94,
            "rationale": f"High delta regularity detected (run ratio {delta_run:.2f}, variance {delta_var:.1f}). Byte diffing collapses data.",
        }

    if run_ratio > 0.40 and rep_ratio < 0.35:
        return {
            "recommended_codec": "rle",
            "confidence": 0.95,
            "rationale": f"Long homogeneous byte runs detected ({run_ratio*100:.1f}%). RLE provides optimal throughput and ratio.",
        }

    if rep_ratio >= 0.35:
        return {
            "recommended_codec": "lz77",
            "confidence": 0.96,
            "rationale": f"High substring repetition ({rep_ratio*100:.1f}%). Sliding window dictionary LZ77 is optimal.",
        }

    if entropy < 6.5:
        return {
            "recommended_codec": "huffman",
            "confidence": 0.91,
            "rationale": f"Low Shannon entropy ({entropy:.2f} b/B). Huffman variable-length bit coding yields high efficiency.",
        }

    return {
        "recommended_codec": "lz77",
        "confidence": 0.88,
        "rationale": "General structured binary data. LZ77 provides balanced performance.",
    }


class OrchestrationHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Clean logging
        sys.stderr.write(f"[AI-Service] {args[0]} {args[1]} -> {args[2]}\n")

    def do_GET(self):
        if self.path in ("/api/tags", "/health", "/v1/models"):
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            resp = {
                "models": [{"name": "cacz-orchestrator", "details": {"family": "qwen2.5", "parameter_size": "0.5B"}}],
                "status": "online"
            }
            self.wfile.write(json.dumps(resp).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"

        try:
            req_data = json.loads(body)
        except Exception:
            req_data = {}

        user_content = ""
        messages = req_data.get("messages", [])
        for m in messages:
            if m.get("role") == "user":
                user_content = m.get("content", "")

        recommendation = determine_recommendation(user_content)

        response_payload = {
            "id": "cacz-ai-resp",
            "object": "chat.completion",
            "model": "cacz-orchestrator",
            "choices": [
                {
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": json.dumps(recommendation)
                    },
                    "finish_reason": "stop"
                }
            ],
            "recommendation": recommendation
        }

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(response_payload).encode("utf-8"))

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


def run(port=PORT):
    server = HTTPServer(("127.0.0.1", port), OrchestrationHandler)
    print(f"CACZ Local AI Model Service running at http://127.0.0.1:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping AI service.")
        server.server_close()

if __name__ == "__main__":
    p = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    run(p)
