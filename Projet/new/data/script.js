// ==========================================
// DÉTECTION AUTOMATIQUE DU MODULE
// ==========================================
let currentModule = null; // 'emetteur' ou 'recepteur'

async function detectModule() {
    showLoader("Détection du module en cours...");

    try {
        const response = await fetch('http://192.168.4.1/whoami', {
            cache: 'no-store',
            signal: AbortSignal.timeout(3000) // timeout 3s
        });

        if (!response.ok) throw new Error("Pas de réponse du module");

        const data = await response.json();
        currentModule = data.module;

        // Met à jour le titre et le bouton sidebar
        document.querySelector('h1').innerHTML = `
            <img src="images/temperature.png" alt="Température" class="title-icon">
            ${currentModule === 'emetteur' ? 'Émetteur' : 'Récepteur'} - Temperature Management System
        `;

        document.querySelector('#module-name').textContent = 
            currentModule === 'emetteur' ? 'Émetteur (Salle Technique)' : 'Récepteur';

        // Active la bonne vue
        switchView(currentModule);

        hideLoader();

    } catch (err) {
        hideLoader();
        document.getElementById('error').style.display = 'block';
        document.getElementById('error').innerText = 
            'Impossible de détecter le module.\nVérifiez que vous êtes connecté au bon WiFi (SalleTech-Emetteur ou SalleTech-Recepteur).';
    }
}

// Appelle la détection au chargement
window.addEventListener('load', () => {
    detectModule();
    connectWebSocket(); // ton code existant
});

// ==========================================
// VARIABLES GLOBALES
// ==========================================
let datas = {
    isA: false,
    isRC: false,
    temp: 0,
    hum: 0,
    sm: 0,
    fl: 0,
    mnlOvrr: false,
    mnlVent: false,
    seuils: { sT: null, sH: null, sSm: null, sF: null },
    his: [],
    nAlrt: null
};

let isSendingCommand = false;
let isFirstData      = true;
let ws               = null;
let reconnectAttempts = 0;
const MAX_RECONNECT_DELAY = 10000;
const BASE_RECONNECT_DELAY = 1000;

// État précédent pour détecter les CHANGEMENTS (évite spam de notifs)
let prevState = {
    isA:      false,
    mnlOvrr:  false,
    mnlVent:  false,
    tempAlert: false,
    smokeAlert: false,
    flameAlert: false,
};

// Anti-spam : délai minimum entre deux notifications du même type (ms)
const NOTIF_COOLDOWN = 30000; // 30 secondes
const notifLastSent = {
    temp:  0,
    smoke: 0,
    flame: 0,
    ventil: 0,
};

// Référence au Service Worker enregistré
let swRegistration = null;

// ==========================================
// PWA — ENREGISTREMENT DU SERVICE WORKER
// ==========================================
async function registerServiceWorker() {
    if (!('serviceWorker' in navigator)) {
        console.warn('[PWA] Service Worker non supporté sur ce navigateur.');
        updateNotifStatusBar('unsupported');
        return;
    }
    try {
        swRegistration = await navigator.serviceWorker.register('/service-worker.js', { scope: '/' });
        console.log('[PWA] Service Worker enregistré :', swRegistration.scope);

        // Écouter les messages du SW (ex: SWITCH_VIEW depuis une notification cliquée)
        navigator.serviceWorker.addEventListener('message', (event) => {
            if (event.data?.type === 'SWITCH_VIEW') {
                switchView(event.data.view);
            }
        });

        // Gérer l'URL de démarrage depuis une notification (?view=...)
        const params = new URLSearchParams(window.location.search);
        if (params.get('view')) {
            switchView(params.get('view'));
        }

        // Demander la permission après installation du SW
        await requestNotificationPermission();

    } catch (err) {
        console.error('[PWA] Échec enregistrement Service Worker :', err);
        updateNotifStatusBar('error');
    }
}

// ==========================================
// NOTIFICATIONS — DEMANDE DE PERMISSION
// ==========================================
async function requestNotificationPermission() {
    if (!('Notification' in window)) {
        console.warn('[Notif] API Notification non disponible (iOS < 16.4 ?)');
        updateNotifStatusBar('unsupported');
        return 'unsupported';
    }

    let permission = Notification.permission;

    if (permission === 'default') {
        // Demander à l'utilisateur
        permission = await Notification.requestPermission();
    }

    updateNotifStatusBar(permission);
    return permission;
}

// ==========================================
// NOTIFICATIONS — MISE À JOUR BANDEAU SIDEBAR
// ==========================================
function updateNotifStatusBar(status) {
    const bar      = document.getElementById('notif-status');
    const icon     = document.getElementById('notif-status-icon');
    const text     = document.getElementById('notif-status-text');
    const btnEnable = document.getElementById('btn-enable-notif');

    if (!bar) return;

    // Retirer toutes les classes de statut
    bar.classList.remove('notif-granted', 'notif-denied', 'notif-unsupported', 'notif-unknown', 'notif-error');

    switch (status) {
        case 'granted':
            bar.classList.add('notif-granted');
            icon.textContent = '🔔';
            text.textContent = 'Notifications actives';
            btnEnable.style.display = 'none';
            break;
        case 'denied':
            bar.classList.add('notif-denied');
            icon.textContent = '🔕';
            text.textContent = 'Notifications bloquées';
            btnEnable.style.display = 'block';
            break;
        case 'unsupported':
            bar.classList.add('notif-unsupported');
            icon.textContent = '⚠️';
            text.textContent = 'Non supporté';
            btnEnable.style.display = 'none';
            break;
        case 'error':
            bar.classList.add('notif-error');
            icon.textContent = '❌';
            text.textContent = 'Erreur SW';
            btnEnable.style.display = 'none';
            break;
        default:
            bar.classList.add('notif-unknown');
            icon.textContent = '🔔';
            text.textContent = 'Notifications...';
            btnEnable.style.display = 'none';
    }
}

// ==========================================
// NOTIFICATIONS — ENVOI AU SERVICE WORKER
// ==========================================
function sendNotification(type, payload) {
    // Vérifications préalables
    if (Notification.permission !== 'granted') return;
    if (!swRegistration || !navigator.serviceWorker.controller) return;

    // Anti-spam : cooldown par type
    const now = Date.now();
    if (now - notifLastSent[type] < NOTIF_COOLDOWN) return;
    notifLastSent[type] = now;

    // Envoyer le message au SW qui affichera la notification
    navigator.serviceWorker.controller.postMessage({ type, payload });
}

// ==========================================
// DÉTECTION DES CHANGEMENTS → NOTIFICATIONS
// Appelé à chaque rafraîchissement de données
// ==========================================
function checkAndNotify() {
    if (isFirstData) return; // Pas de notif au premier chargement

    const s = datas.seuils;

    // --- Température ---
    const tempAlert = datas.temp >= s?.sT;
    if (tempAlert && !prevState.tempAlert) {
        sendNotification('NOTIFY_TEMP', {
            value: datas.temp?.toFixed(1),
            threshold: s?.sT
        });
    }
    prevState.tempAlert = tempAlert;

    // --- Fumée/Gaz ---
    const smokeAlert = datas.sm >= s?.sSm;
    if (smokeAlert && !prevState.smokeAlert) {
        sendNotification('NOTIFY_SMOKE', {
            value: datas.sm,
            threshold: s?.sSm
        });
    }
    prevState.smokeAlert = smokeAlert;

    // --- Flamme ---
    const flameAlert = datas.fl <= s?.sF;
    if (flameAlert && !prevState.flameAlert) {
        sendNotification('NOTIFY_FLAME', {
            value: datas.fl,
            threshold: s?.sF
        });
    }
    prevState.flameAlert = flameAlert;

    // --- Changement état ventilation ---
    const ventilChanged = (datas.mnlOvrr !== prevState.mnlOvrr) ||
                          (datas.mnlOvrr && datas.mnlVent !== prevState.mnlVent);
    if (ventilChanged) {
        let message;
        if (!datas.mnlOvrr) {
            message = 'Retour en mode automatique';
        } else {
            message = datas.mnlVent ? 'Ventilation forcée ON ✅' : 'Ventilation forcée OFF 🔴';
        }
        sendNotification('NOTIFY_VENTIL', { message });
    }
    prevState.mnlOvrr = datas.mnlOvrr;
    prevState.mnlVent = datas.mnlVent;
}

// ==========================================
// NAVIGATION ENTRE LES VUES
// ==========================================
function switchView(viewName) {
    document.querySelectorAll('.view').forEach(v => v.style.display = 'none');
    document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));

    const target = document.getElementById(viewName);
    if (target) target.style.display = 'block';

    const btn = document.querySelector(`.nav-btn[onclick="switchView('${viewName}')"]`);
    if (btn) btn.classList.add('active');

    currentModule = viewName; // garde en mémoire
    refreshView();
}

let currentView = 'emetteur';

// ==========================================
// WEBSOCKET — CONNEXION AVEC RECONNEXION
// ==========================================
function connectWebSocket() {
    if (ws && (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)) return;

    ws = new WebSocket(`ws://${window.location.host}/ws`);

    ws.onopen = () => {
        console.log('[WS] Connecté');
        reconnectAttempts = 0;
        isFirstData = true;
        showLoader("Chargement des données en cours...");
    };

    ws.onclose = () => {
        console.log('[WS] Fermé → reconnexion');
        ws = null;
        attemptReconnect();
    };

    ws.onerror = (err) => {
        console.error('[WS] Erreur :', err);
    };

    ws.onmessage = async (event) => {
        try {
            const text     = await new Blob([event.data]).text();
            const received = JSON.parse(text);

            if (isFirstData) {
                datas       = received;
                isFirstData = false;
                // Initialiser prevState sans déclencher de notifs
                prevState.tempAlert  = datas.temp  >= datas.seuils?.sT;
                prevState.smokeAlert = datas.sm    >= datas.seuils?.sSm;
                prevState.flameAlert = datas.fl    <= datas.seuils?.sF;
                prevState.mnlOvrr    = datas.mnlOvrr;
                prevState.mnlVent    = datas.mnlVent;
            } else {
                if (received?.nAlrt) {
                    datas.his.unshift(received.nAlrt);
                    if (datas.his.length > 100) datas.his.pop();
                }
                datas = { ...datas, ...received, nAlrt: null };
                checkAndNotify(); // ← Vérifie s'il faut envoyer une notification
            }

            if (!isSendingCommand || (isSendingCommand && datas?.isRC)) {
                if (isSendingCommand) {
                    closeThresholdPopup();
                    isSendingCommand = false;
                }
                hideLoader();
            }

            refreshView();
        } catch (e) {
            console.error('[WS] Erreur parsing :', e);
        }
    };
}

// ==========================================
// WEBSOCKET — RECONNEXION EXPONENTIELLE
// ==========================================
function attemptReconnect() {
    const delay = Math.min(BASE_RECONNECT_DELAY * Math.pow(2, reconnectAttempts), MAX_RECONNECT_DELAY);
    reconnectAttempts++;
    showLoader(`Connexion perdue<br>Reconnexion dans ${(delay/1000).toFixed(1)}s...<br>(tentative ${reconnectAttempts})`);
    setTimeout(connectWebSocket, delay);
}

// ==========================================
// DÉTECTION ALERTES (pour l'affichage UI)
// ==========================================
function detectAlerts() {
    const alerts = [];
    if (datas?.temp >= datas?.seuils?.sT)
        alerts.push({ icon: '🌡️', label: 'Température élevée',   value: `${datas.temp?.toFixed(1)}°C (≥ ${datas?.seuils?.sT}°C)` });
    if (datas?.hum  >= datas?.seuils?.sH)
        alerts.push({ icon: '💧', label: 'Humidité élevée',       value: `${datas?.hum?.toFixed(1)}% (≥ ${datas?.seuils?.sH}%)` });
    if (datas?.sm   >= datas?.seuils?.sSm)
        alerts.push({ icon: '💨', label: 'Fumée / Gaz détecté',   value: `${datas?.sm} ppm (≥ ${datas?.seuils?.sSm} ppm)` });
    if (datas?.fl   <= datas?.seuils?.sF)
        alerts.push({ icon: '🔥', label: 'Flamme détectée',       value: `Valeur: ${datas?.fl} (≤ ${datas?.seuils?.sF})` });
    return alerts;
}

// ==========================================
// AFFICHAGE ALERTES UI
// ==========================================
function displayAlerts(prefix) {
    const container = document.getElementById(`${prefix}-alert-container`);
    if (!container) return;

    const alerts = detectAlerts();

    if (alerts.length === 0 || !datas.isA) {
        container.innerHTML = `
            <div class="card">
                <h3>📊 Système Normal</h3>
                <div class="status-badge normal">Aucune alerte active</div>
            </div>`;
        return;
    }

    const alertsHTML = alerts.map(a => `
        <div class="alert-message">
            <div class="alert-icon">${a.icon}</div>
            <div class="alert-type">
                <div class="alert-type-label">${a.label}</div>
                <div class="alert-value">${a.value}</div>
            </div>
        </div>`).join('');

    container.innerHTML = `
        <div class="alert-card">
            <h3>🚨 ALERTES ACTIVES (${alerts.length})</h3>
            ${alertsHTML}
            <div class="urgent-message">⚠️ INTERVENTION REQUISE ⚠️</div>
        </div>`;
}

// ==========================================
// RAFRAÎCHISSEMENT GLOBAL
// ==========================================
function refreshView() {
    refreshEmetteur();
    refreshRecepteur();
}

function refreshEmetteur() {
    const el = id => document.getElementById(id);
    el('emetteur-temp').innerText    = formatVal(datas?.temp, 1, '°C', '-- °C');
    el('emetteur-humidity').innerText = formatVal(datas?.hum, 1, '%',  '-- %');
    el('emetteur-smoke').innerText   = formatVal(datas?.sm,  0, 'ppm','-- ppm');
    el('emetteur-flame').innerText   = datas?.fl <= datas?.seuils?.sF ? 'FLAMME !' : (datas?.fl ?? '--');

    el('emetteur-display-temp').innerText     = (datas?.seuils?.sT  ?? '---') + ' °C';
    el('emetteur-display-humidity').innerText = (datas?.seuils?.sH  ?? '---') + ' %';
    el('emetteur-display-smoke').innerText    = (datas?.seuils?.sSm ?? '---') + ' ppm';
    el('emetteur-display-flame').innerText    = '≤ ' + (datas?.seuils?.sF ?? '---');

    const statusText  = datas.mnlOvrr ? (datas.mnlVent ? "FORCÉE ON" : "FORCÉE OFF") : "Automatique";
    const statusColor = datas.mnlOvrr ? (datas.mnlVent ? "#28a745" : "#dc3545") : "#666";
    el('relay-status').innerText      = statusText;
    el('relay-status').style.color    = statusColor;
    el('ventil-on').style.opacity     = datas.mnlOvrr && datas.mnlVent  ? "1" : "0.6";
    el('ventil-off').style.opacity    = datas.mnlOvrr && !datas.mnlVent ? "1" : "0.6";

    displayAlerts('emetteur');
    updateHistory('emetteur-history-list');
}

function refreshRecepteur() {
    const el = id => document.getElementById(id);
    el('recepteur-temp').innerText     = formatVal(datas?.temp, 1, '°C', '-- °C');
    el('recepteur-humidity').innerText = formatVal(datas?.hum, 1, '%',  '-- %');
    el('recepteur-smoke').innerText    = formatVal(datas?.sm,  0, 'ppm','-- ppm');
    el('recepteur-flame').innerText    = datas?.fl ?? '--';

    el('recepteur-display-temp').innerText     = (datas?.seuils?.sT  ?? '---') + ' °C';
    el('recepteur-display-humidity').innerText = (datas?.seuils?.sH  ?? '---') + ' %';
    el('recepteur-display-smoke').innerText    = (datas?.seuils?.sSm ?? '---') + ' ppm';
    el('recepteur-display-flame').innerText    = '≤ ' + (datas?.seuils?.sF ?? '---');

    const statusText  = datas.mnlOvrr ? (datas.mnlVent ? "FORCÉE ON" : "FORCÉE OFF") : "Automatique";
    const statusColor = datas.mnlOvrr ? (datas.mnlVent ? "#28a745" : "#dc3545") : "#666";
    el('recepteur-relay-status').innerText   = statusText;
    el('recepteur-relay-status').style.color = statusColor;
    el('recepteur-ventil-on').style.opacity  = datas.mnlOvrr && datas.mnlVent  ? "1" : "0.6";
    el('recepteur-ventil-off').style.opacity = datas.mnlOvrr && !datas.mnlVent ? "1" : "0.6";

    displayAlerts('recepteur');
    updateHistory('recepteur-history-list');
}

// ==========================================
// UTILITAIRES
// ==========================================
function formatVal(val, decimals, unit, fallback) {
    if (val === null || val === undefined) return fallback;
    return val.toFixed(decimals) + ' ' + unit;
}

function updateHistory(listId) {
    const list = document.getElementById(listId);
    if (!list) return;
    list.innerHTML = '';

    if (!datas.his || datas.his.length === 0) {
        list.innerHTML = '<li style="color:#888; font-style:italic;">Aucune alerte enregistrée</li>';
        return;
    }

    datas.his.forEach(entry => {
        const typeLabel = { T: 'Température', H: 'Humidité', S: 'Fumée/Gaz', F: 'Flamme' }[entry.var] || entry.var;
        const unit      = entry.var === 'T' ? '°C' : entry.var === 'H' ? '%' : entry.var === 'S' ? 'ppm' : '';
        const li = document.createElement('li');
        li.className = 'history-item';
        li.innerHTML = `
            <span class="history-time">${entry.t || '??'}</span>
            <div class="history-event"><strong>${typeLabel}</strong></div>
            <div>${entry.val?.toFixed(1) ?? '-'} ${unit}</div>`;
        list.appendChild(li);
    });
}

// ==========================================
// POPUP SEUILS
// ==========================================
function openThresholdPopup() {
    document.getElementById('popupSeuilTemp').value     = datas?.seuils?.sT  ?? '';
    document.getElementById('popupSeuilHumidity').value = datas?.seuils?.sH  ?? '';
    document.getElementById('popupSeuilSmoke').value    = datas?.seuils?.sSm ?? '';
    document.getElementById('popupSeuilFlame').value    = datas?.seuils?.sF  ?? '';
    document.getElementById('thresholdPopup').style.display = 'flex';
}

function closeThresholdPopup() {
    document.getElementById('thresholdPopup').style.display = 'none';
}

function saveThresholds() {
    const temp  = parseFloat(document.getElementById('popupSeuilTemp').value);
    const hum   = parseFloat(document.getElementById('popupSeuilHumidity').value);
    const smoke = parseInt(document.getElementById('popupSeuilSmoke').value);
    const flame = parseInt(document.getElementById('popupSeuilFlame').value);

    if (!isNaN(temp))  datas.seuils.sT  = temp;
    if (!isNaN(hum))   datas.seuils.sH  = hum;
    if (!isNaN(smoke)) datas.seuils.sSm = smoke;
    if (!isNaN(flame)) datas.seuils.sF  = flame;

    showLoader("Envoi des nouveaux seuils...");

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ com: "upd_seuils", seuils: datas?.seuils }));
        isSendingCommand = true;
    }
}

// ==========================================
// COMMANDE MANUELLE VENTILATION
// ==========================================
function sendManualCommand(override, state) {
    showLoader(
        override
            ? (state ? "Activation forcée de la ventilation..." : "Désactivation forcée...")
            : "Retour au mode automatique..."
    );
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ com: "mnl_vent", override, state }));
        isSendingCommand = true;
    }
}

// ==========================================
// LOADER
// ==========================================
function showLoader(message = "Connexion en cours...") {
    document.getElementById('loader-text').innerHTML = message;
    document.getElementById('global-loader').classList.remove('hidden');
}

function hideLoader() {
    document.getElementById('global-loader').classList.add('hidden');
}

// ==========================================
// INITIALISATION
// ==========================================
document.addEventListener("DOMContentLoaded", () => {
    showLoader("Connexion au serveur...");
    refreshView();
    registerServiceWorker(); // ← Enregistre le SW + demande permission notifs
    connectWebSocket();
});


// ==========================================
// PROPOSITION INSTALLATION PWA (guide l'utilisateur)
// ==========================================
let deferredPrompt = null;

window.addEventListener('beforeinstallprompt', (e) => {
    e.preventDefault();
    deferredPrompt = e;
    // Affiche un message ou bouton personnalisé si tu veux
    console.log('PWA peut être installée !');
});

function installPWA() {
    if (deferredPrompt) {
        deferredPrompt.prompt();
        deferredPrompt.userChoice.then((choiceResult) => {
            if (choiceResult.outcome === 'accepted') {
                console.log('PWA installée avec succès');
            }
            deferredPrompt = null;
        });
    }
}

