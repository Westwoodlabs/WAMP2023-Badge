import threading
import asyncio
import websockets
import json
from websockets.exceptions import ConnectionClosed
from db import TagDb


class Web:
    def __init__(self, tag_db: TagDb):
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        self.clients = []
        self.tag_db = tag_db
        self.tag_db.on_update(self._on_tag_update)

    def _run(self):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        start_server = websockets.serve(self._handle_client, "localhost", 8765)
        loop.run_until_complete(start_server)
        loop.run_forever()

    async def _handle_client(self, websocket, path):
        self.clients.append(websocket)
        print(f"Got connection from {websocket.remote_address}")
        try:
            await self._send_tag_update_async(websocket, self.tag_db.tags)
            while True:
                message = await websocket.recv()
                print(f"Received message from client: {message}")
                # Process the received message or perform any desired actions
                response = f"Server received message: {message}"
                await websocket.send(response)
                print(f"Sent response to client: {response}")
        except ConnectionClosed:
            print(f"Connection to {websocket.remote_address} closed")
            self.clients.remove(websocket)

    def _on_tag_update(self, tags):
        for client in self.clients:
            self._send_tag_update(client, tags)

    def _send_tag_update(self, client, tags):
        asyncio.get_event_loop().run_until_complete(self._send_tag_update_async(client, tags))

    async def _send_tag_update_async(self, client, tags):
        try:
            data = json.dumps({"tag_updates": tags})
            await client.send(data)
        except Exception as e:
            print(f"tag_update send failed {e}")
            pass
