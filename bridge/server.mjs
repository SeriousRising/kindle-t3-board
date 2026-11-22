#!/usr/bin/env node
/**
 * Local LAN HTTP bridge for kindle-t3-board.
 * Zero external dependencies — runs on Node 24+ using built-in node:http & node:sqlite.
 *
 * Endpoints:
 *   GET /api/board      -> JSON board payload
 *   GET /api/board.tsv  -> Compact TSV wire format for the Kindle C client
 *   GET /api/health     -> Health & config summary
 */

import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { readT3Board, serializeBoardTsv } from "./t3_reader.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const configPath = process.env.T3_BOARD_CONFIG || path.join(__dirname, "config.json");

function loadConfig() {
  try {
    const raw = fs.readFileSync(configPath, "utf8");
    return JSON.parse(raw);
  } catch (err) {
    console.warn(`[bridge] Could not read ${configPath}, using defaults:`, err.message);
    return {};
  }
}

const initialConfig = loadConfig();
const HOST = process.env.T3_BOARD_HOST || initialConfig.host || "0.0.0.0";
const PORT = Number(process.env.T3_BOARD_PORT || initialConfig.port || 7373);

const server = http.createServer((req, res) => {
  const url = new URL(req.url || "/", `http://${req.headers.host || "localhost"}`);
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Cache-Control", "no-store");

  if (req.method !== "GET" && req.method !== "HEAD") {
    res.writeHead(405, { "Content-Type": "text/plain" });
    res.end("Method Not Allowed\n");
    return;
  }

  try {
    const cfg = loadConfig();

    if (url.pathname === "/api/health") {
      const body = JSON.stringify({ ok: true, time: new Date().toISOString(), port: PORT }) + "\n";
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
      res.end(body);
      return;
    }

    if (url.pathname === "/api/board" || url.pathname === "/") {
      const board = readT3Board(cfg);
      const body = JSON.stringify(board, null, 2) + "\n";
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
      res.end(body);
      return;
    }

    if (url.pathname === "/api/board.tsv") {
      const board = readT3Board(cfg);
      const body = serializeBoardTsv(board);
      res.writeHead(200, {
        "Content-Type": "text/plain; charset=utf-8",
        "Content-Length": Buffer.byteLength(body, "utf8"),
      });
      res.end(body);
      return;
    }

    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not Found\n");
  } catch (err) {
    console.error("[bridge] Error handling request:", err);
    res.writeHead(500, { "Content-Type": "text/plain" });
    res.end(`Internal Error: ${err.message}\n`);
  }
});

server.listen(PORT, HOST, () => {
  console.log(`[kindle-t3-board bridge] Listening on http://${HOST}:${PORT}`);
  console.log(`  JSON: http://127.0.0.1:${PORT}/api/board`);
  console.log(`  TSV : http://127.0.0.1:${PORT}/api/board.tsv`);
});
