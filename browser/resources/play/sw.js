const CACHE = 'os999-arena-v1';
self.addEventListener('install', event => event.waitUntil((async () => {
  const cache = await caches.open(CACHE);
  await cache.addAll(['/play/', '/play/game.js', '/play/manifest.json', '/play/icon.svg', '/play/add.wasm']);
  await cache.put('/play/offline-check', new Response('offline-cache-ready', {headers:{'Content-Type':'text/plain'}}));
  await self.skipWaiting();
})()));
self.addEventListener('activate', event => event.waitUntil((async () => {
  for (const key of await caches.keys()) if (key.startsWith('os999-arena-') && key !== CACHE) await caches.delete(key);
  await self.clients.claim();
})()));
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin || !url.pathname.startsWith('/play/') || event.request.method !== 'GET') return;
  event.respondWith(caches.match(event.request).then(cached => cached || fetch(event.request)));
});
