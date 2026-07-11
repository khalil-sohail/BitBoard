import('ws').then(({ default: WebSocket }) => {
  const port = process.env.PORT || process.env.FRONTEND_PORT || process.env.BACKEND_PORT || '3000';
  const url = process.env.WS_URL || `ws://localhost:${port}/api/engine`;
  const ws = new WebSocket(url);
  let nextRequestId = 1;

  ws.on('open', () => {
    console.log('Connected');
    ws.send(JSON.stringify({ type: 'newgame' }));
  });

  ws.on('message', (data) => {
    console.log('Received:', data.toString());
    const msg = JSON.parse(data.toString());
    if (msg.type === 'ready') {
      console.log('Engine ready, sending move e2e4');
      ws.send(JSON.stringify({
        type: 'move',
        requestId: nextRequestId++,
        fen: 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
        moves: ['e2e4'],
      }));
    }
  });

  ws.on('error', console.error);
});
