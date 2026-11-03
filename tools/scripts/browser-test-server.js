'use strict';

const fs = require('node:fs/promises');
const http = require('node:http');

// Serve only the single-file test artifact. The game and fixtures must still
// embed their own runtime assets; this endpoint provides no dependency files.
async function serveHtml(filename, { isolated = true } = {}) {
    const content = await fs.readFile(filename);
    const server = http.createServer((request, response) => {
        // Browsers probe this independently of the standalone artifact. Keep
        // the empty probe from becoming a misleading console resource error.
        if (request.url === '/favicon.ico') {
            response.writeHead(204);
            response.end();
            return;
        }
        if (request.url !== '/') {
            response.writeHead(404);
            response.end();
            return;
        }
        response.setHeader('Content-Type', 'text/html; charset=utf-8');
        if (isolated) {
            response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
            response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
        }
        response.end(content);
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', () => {
            server.removeListener('error', reject);
            resolve();
        });
    });
    return {
        url: 'http://127.0.0.1:' + server.address().port + '/',
        close: () => new Promise((resolve, reject) => server.close(error => {
            if (error) reject(error);
            else resolve();
        })),
    };
}

module.exports = { serveHtml };
