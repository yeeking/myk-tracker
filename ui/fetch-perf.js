#!/usr/bin/env node
// Simple Node.js fetch timing loop for GET /state

const http = require("http");

const HOST = "127.0.0.1";
const PORT = 8080;
const PATH = "/state";
const INTERVAL_MS = 1000;

function fetchOnce() {
  const start = process.hrtime.bigint();
  let bytes = 0;

  const req = http.request(
    { host: HOST, port: PORT, path: PATH, method: "GET" },
    (res) => {
      res.on("data", (chunk) => {
        bytes += chunk.length;
      });
      res.on("end", () => {
        const end = process.hrtime.bigint();
        const elapsedMs = Number(end - start) / 1e6;
        console.log(
          `Received ${bytes} bytes from http://${HOST}:${PORT}${PATH} in ${elapsedMs.toFixed(
            2
          )}ms`
        );
        setTimeout(fetchOnce, INTERVAL_MS);
      });
    }
  );

  req.on("error", (err) => {
    console.error("Request error:", err.message);
    setTimeout(fetchOnce, INTERVAL_MS);
  });

  req.end();
}

fetchOnce();
