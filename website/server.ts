import { createServer } from 'http';
import { parse } from 'url';
import next from 'next';
import { WebSocketServer } from 'ws';
import { enginePool } from './src/lib/engine-session';

const dev = process.env.NODE_ENV !== 'production';
const hostname = process.env.HOSTNAME || '0.0.0.0';
const port = parseInt(process.env.PORT || '3000', 10);

if (process.env.BACKEND_ONLY === 'true') {
  const server = createServer((req, res) => {
    res.statusCode = 200;
    res.end('Backend OK');
  });

  const wss = new WebSocketServer({ noServer: true });

  server.on('upgrade', (request, socket, head) => {
    const { pathname } = parse(request.url!);
    if (pathname === '/api/engine') {
      wss.handleUpgrade(request, socket, head, (ws) => {
        wss.emit('connection', ws, request);
      });
    } else {
      socket.destroy();
    }
  });

  wss.on('connection', (ws, req) => {
    const sessionId = Math.random().toString(36).substring(2, 15);
    enginePool.handleConnection(ws, sessionId);
  });

  server.listen(port, () => {
    console.log(`> Backend WS server ready on http://${hostname}:${port}`);
  });
} else {
  // when using middleware `hostname` and `port` must be provided below
  const app = next({ dev, hostname, port });
  const handle = app.getRequestHandler();

  app.prepare().then(() => {
    const server = createServer(async (req, res) => {
      try {
        const parsedUrl = parse(req.url!, true);
        await handle(req, res, parsedUrl);
      } catch (err) {
        console.error('Error occurred handling', req.url, err);
        res.statusCode = 500;
        res.end('internal server error');
      }
    });

    const wss = new WebSocketServer({ noServer: true });

    const upgradeHandler = app.getUpgradeHandler();

    server.on('upgrade', (request, socket, head) => {
      const { pathname } = parse(request.url!);

      if (pathname === '/api/engine') {
        wss.handleUpgrade(request, socket, head, (ws) => {
          wss.emit('connection', ws, request);
        });
      } else {
        upgradeHandler(request, socket, head);
      }
    });

    wss.on('connection', (ws, req) => {
      const sessionId = Math.random().toString(36).substring(2, 15);
      enginePool.handleConnection(ws, sessionId);
    });

    server.listen(port, () => {
      console.log(`> Ready on http://${hostname}:${port}`);
    });
  });
}
