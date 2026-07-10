import('ws').then(({ default: WebSocket }) => {
  const ws = new WebSocket('ws://localhost:3000/api/engine');

  ws.on('open', () => {
    console.log('Connected');
    ws.send(JSON.stringify({ type: 'newgame' }));
  });

  ws.on('message', (data) => {
    console.log('Received:', data.toString());
    const msg = JSON.parse(data.toString());
    if (msg.type === 'ready') {
      console.log('Engine ready, sending move e2e4');
      ws.send(JSON.stringify({ type: 'move', fen: 'startpos', moves: ['e2e4'] }));
    }
  });

  ws.on('error', console.error);
});
