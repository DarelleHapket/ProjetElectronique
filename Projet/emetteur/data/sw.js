// ==========================================
// SERVICE WORKER — Temperature Management System
// Version du cache : incrémenter à chaque déploiement
// ==========================================
const CACHE_NAME = 'temp-system-v1';

// Fichiers à mettre en cache pour le mode offline
const ASSETS_TO_CACHE = [
    '/',
    '/index.html',
    '/script.js',
    '/style.css',
    '/manifest.json',
    '/images/temperature.png',
    '/images/humidity.png',
    '/images/smoke.png',
    '/images/flame.png',
    '/images/dashboard.png',
    '/images/podcast.png',
    '/images/icons/icon-192x192.png',
    '/images/icons/icon-512x512.png'
];

// ==========================================
// INSTALLATION — mise en cache des assets
// ==========================================
self.addEventListener('install', (event) => {
    console.log('[SW] Installation...');
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => {
            console.log('[SW] Mise en cache des assets');
            // addAll échoue si un fichier manque → on utilise add individuel pour la robustesse
            return Promise.allSettled(
                ASSETS_TO_CACHE.map(url => cache.add(url).catch(err => {
                    console.warn(`[SW] Impossible de cacher ${url}:`, err);
                }))
            );
        })
    );
    // Force activation immédiate sans attendre la fermeture des onglets
    self.skipWaiting();
});

// ==========================================
// ACTIVATION — nettoyage des anciens caches
// ==========================================
self.addEventListener('activate', (event) => {
    console.log('[SW] Activation...');
    event.waitUntil(
        caches.keys().then((cacheNames) => {
            return Promise.all(
                cacheNames
                    .filter(name => name !== CACHE_NAME)
                    .map(name => {
                        console.log('[SW] Suppression ancien cache:', name);
                        return caches.delete(name);
                    })
            );
        })
    );
    // Prendre le contrôle immédiatement de tous les onglets ouverts
    self.clients.claim();
});

// ==========================================
// FETCH — stratégie Cache First puis Network
// Les WebSocket (/ws) ne passent PAS par fetch → aucun conflit
// ==========================================
self.addEventListener('fetch', (event) => {
    // Ignorer les requêtes non-GET et les WebSocket
    if (event.request.method !== 'GET') return;
    if (event.request.url.includes('/ws')) return;

    event.respondWith(
        caches.match(event.request).then((cachedResponse) => {
            if (cachedResponse) {
                // Trouvé en cache → retour immédiat + mise à jour silencieuse en arrière-plan
                fetch(event.request)
                    .then(networkResponse => {
                        if (networkResponse && networkResponse.status === 200) {
                            caches.open(CACHE_NAME).then(cache => {
                                cache.put(event.request, networkResponse.clone());
                            });
                        }
                    })
                    .catch(() => {}); // Silencieux si hors ligne
                return cachedResponse;
            }
            // Pas en cache → réseau
            return fetch(event.request).catch(() => {
                // Hors ligne et pas en cache → page offline basique
                if (event.request.destination === 'document') {
                    return caches.match('/index.html');
                }
            });
        })
    );
});

// ==========================================
// MESSAGES — reçoit les données de script.js
// pour déclencher les notifications locales
// ==========================================
self.addEventListener('message', (event) => {
    const { type, payload } = event.data || {};

    switch (type) {

        // --- Alerte température ---
        case 'NOTIFY_TEMP':
            showNotification(
                `🌡️ Alerte ${payload.module}`,
                `Température : ${payload.value}°C (seuil : ${payload.threshold}°C)`,
                { data: { view: payload.module } }
            );
            break;

        // --- Alerte fumée/gaz ---
        case 'NOTIFY_SMOKE':
            showNotification(
                '💨 Alerte Fumée / Gaz',
                `Fumée : ${payload.value} ppm (seuil : ${payload.threshold} ppm)`,
                {
                    icon: '/images/smoke.png',
                    badge: '/images/icons/icon-72x72.png',
                    tag: 'alert-smoke',
                    renotify: true,
                    vibrate: [300, 100, 300, 100, 300],
                    data: { view: 'emetteur' }
                }
            );
            break;

        // --- Alerte flamme ---
        case 'NOTIFY_FLAME':
            showNotification(
                '🔥 FLAMME DÉTECTÉE',
                `Valeur capteur : ${payload.value} (seuil ≤ ${payload.threshold}) — DANGER IMMÉDIAT`,
                {
                    icon: '/images/flame.png',
                    badge: '/images/icons/icon-72x72.png',
                    tag: 'alert-flame',
                    renotify: true,
                    vibrate: [500, 100, 500, 100, 500, 100, 500],
                    requireInteraction: true,   // reste visible jusqu'à interaction
                    data: { view: 'emetteur' }
                }
            );
            break;

        // --- Changement état ventilateur ---
        case 'NOTIFY_VENTIL':
            showNotification(
                '🎛️ Ventilation',
                payload.message,
                {
                    icon: '/images/icons/icon-192x192.png',
                    badge: '/images/icons/icon-72x72.png',
                    tag: 'ventil-state',
                    renotify: true,
                    vibrate: [100, 50, 100],
                    data: { view: 'emetteur' }
                }
            );
            break;
    }
});

// ==========================================
// HELPER — affiche une notification
// ==========================================
function showNotification(title, body, options = {}) {
    const defaultOptions = {
        body,
        silent: false,
        timestamp: Date.now(),
        actions: [
            { action: 'open', title: '📊 Ouvrir' },
            { action: 'dismiss', title: '✕ Ignorer' }
        ]
    };
    self.registration.showNotification(title, { ...defaultOptions, ...options });
}

// ==========================================
// CLIC SUR NOTIFICATION — ouvre / focus l'app
// ==========================================
self.addEventListener('notificationclick', (event) => {
    event.notification.close();

    if (event.action === 'dismiss') return;

    const targetView = event.notification.data?.view || 'emetteur';

    event.waitUntil(
        clients.matchAll({ type: 'window', includeUncontrolled: true }).then((clientList) => {
            // Si un onglet de l'app est déjà ouvert → focus + navigation
            for (const client of clientList) {
                if (client.url.includes(self.location.origin) && 'focus' in client) {
                    client.focus();
                    client.postMessage({ type: 'SWITCH_VIEW', view: targetView });
                    return;
                }
            }
            // Sinon → ouvrir un nouvel onglet
            if (clients.openWindow) {
                return clients.openWindow(`/?view=${targetView}`);
            }
        })
    );
});