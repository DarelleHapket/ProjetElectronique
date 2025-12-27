// ==========================================
// VARIABLES GLOBALES
// ==========================================
let datas = {
    isA: false,
    temp: 0,
    hum: 0,
    sm: 0,
    fl: 0,
    mnlOvrr: false,
    mnlVent: false,
    seuils: {
        sT: null,
        sH: null,
        sSm: null,
        sF: null,
    },
    his: [],
    nAlrt: null
};

let isFirstData = true;
let ws = null;
let reconnectAttempts = 0;
const MAX_RECONNECT_DELAY = 10000;  // 10 secondes maximum
const BASE_RECONNECT_DELAY = 1000;  // départ à 1 seconde

// ==========================================
// FONCTION DE CONNEXION WEBSOCKET AVEC RECONNEXION
// ==========================================
function connectWebSocket() {
    // Éviter les connexions multiples simultanées
    if (ws && (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)) {
        return;
    }

    ws = new WebSocket(`ws://${window.location.host}/ws`);

    ws.onopen = () => {
        console.log("WebSocket connecté avec succès");
        reconnectAttempts = 0;     // Reset du compteur
        isFirstData = true;        // On attend le premier message complet (avec historique)
        showLoader("Chargement des données en cours...");
    };

    ws.onclose = () => {
        console.log("WebSocket fermé → lancement reconnexion");
        ws = null;
        attemptReconnect();
    };

    ws.onerror = (err) => {
        console.error("Erreur WebSocket :", err);
        // onerror est souvent suivi de onclose → pas besoin d'action supplémentaire ici
    };

    ws.onmessage = async (event) => {
        try {
            const text = await new Blob([event.data]).text();
            const received = JSON.parse(text);

            if (isFirstData) {
                // Première réception après (re)connexion : on prend tout (historique inclus)
                datas = received;
                isFirstData = false;

            } else {
                // Mises à jour suivantes
                if (received?.nAlrt) {
                    datas?.his.unshift(received.nAlrt); // Nouvelle alerte en haut
                    // Limite l'historique côté client pour éviter la surcharge
                    if (datas?.his?.length > 50) {
                        datas.his.pop();
                    }
                }
                // Fusion sans écraser l'historique
                datas = { ...datas, ...received, nAlrt: null };
            }
            hideLoader();
            refreshView();
        } catch (e) {
            console.error("Erreur lors du parsing du message WebSocket :", e);
        }
    };
}

// ==========================================
// RECONNEXION EXPONENTIELLE
// ==========================================
function attemptReconnect() {
    const delay = Math.min(
        BASE_RECONNECT_DELAY * Math.pow(2, reconnectAttempts),
        MAX_RECONNECT_DELAY
    );

    reconnectAttempts++;
    const seconds = (delay / 1000)?.toFixed(1);

    showLoader(`
        Connexion perdue<br>
        Reconnexion dans ${seconds}s...<br>
        (tentative ${reconnectAttempts})
    `);

    setTimeout(() => {
        console.log(`Tentative de reconnexion n°${reconnectAttempts}...`);
        connectWebSocket();
    }, delay);
}

// ==========================================
// DÉTECTION ALERTES
// ==========================================
function detectAlerts() {
    const alerts = [];

    if (datas.temp >= datas.seuils.sT) {
        alerts.push({ icon: '🌡️', label: 'Température élevée', value: `${datas.temp?.toFixed(1)}°C (≥ ${datas.seuils.sT}°C)` });
    }
    if (datas.hum >= datas.seuils.sH) {
        alerts.push({ icon: '💧', label: 'Humidité élevée', value: `${datas.hum?.toFixed(1)}% (≥ ${datas.seuils.sH}%)` });
    }
    if (datas.sm >= datas.seuils.sSm) {
        alerts.push({ icon: '💨', label: 'Fumée / Gaz détecté', value: `${datas.sm} ppm (≥ ${datas.seuils.sSm} ppm)` });
    }
    if (datas.fl <= datas.seuils.sF) {
        alerts.push({ icon: '🔥', label: 'Flamme détectée', value: `Valeur: ${datas.fl} (≤ ${datas.seuils.sF})` });
    }

    return alerts;
}

// ==========================================
// AFFICHAGE ALERTES
// ==========================================
function displayAlerts() {
    const container = document.getElementById('alert-container');
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
        </div>
    `).join('');

    container.innerHTML = `
        <div class="alert-card">
            <h3>🚨 ALERTES ACTIVES (${alerts.length})</h3>
            ${alertsHTML}
            <div class="urgent-message">⚠️ INTERVENTION REQUISE ⚠️</div>
        </div>`;
}

// ==========================================
// RAFRAÎCHISSEMENT DE L'AFFICHAGE
// ==========================================
function refreshView() {
    // Température
    const tempDisplay = (datas.temp !== null && datas.temp !== undefined)
        ? datas.temp.toFixed(1) + ' °C'
        : '-- °C';
    document.getElementById('current-temp').innerText = tempDisplay;

    // Humidité
    const humDisplay = (datas.hum !== null && datas.hum !== undefined)
        ? datas.hum.toFixed(1) + ' %'
        : '-- %';
    document.getElementById('current-humidity').innerText = humDisplay;

    // Fumée
    const smokeDisplay = (datas.sm !== null && datas.sm !== undefined)
        ? datas.sm.toFixed(0) + ' ppm'
        : '-- ppm';
    document.getElementById('current-smoke').innerText = smokeDisplay;

    // Flamme (pas de toFixed, mais on garde une sécurité)
    const flameDisplay = datas.fl < datas.seuils.sF ? 'FLAMME !' : datas.fl;
    document.getElementById('current-flame').innerText = flameDisplay;

    document.getElementById('display-temp').innerText = datas.seuils.sT + ' °C';
    document.getElementById('display-humidity').innerText = datas.seuils.sH + ' %';
    document.getElementById('display-smoke').innerText = datas.seuils.sSm + ' ppm';
    document.getElementById('display-flame').innerText = '≤ ' + datas.seuils.sF;

    const statusText = datas.mnlOvrr
        ? (datas.mnlVent ? "FORCÉE ON" : "FORCÉE OFF")
        : "Automatique";
    const statusColor = datas.mnlOvrr
        ? (datas.mnlVent ? "#28a745" : "#dc3545")
        : "#666";

    const statusEl = document.getElementById('relay-status');
    statusEl.innerText = statusText;
    statusEl.style.color = statusColor;

    document.getElementById('ventil-on').style.opacity = datas.mnlOvrr && datas.mnlVent ? "1" : "0.6";
    document.getElementById('ventil-off').style.opacity = datas.mnlOvrr && !datas.mnlVent ? "1" : "0.6";

    displayAlerts();
    updateHistory();
}

// ==========================================
// HISTORIQUE
// ==========================================
function updateHistory() {
    const list = document.getElementById('history-list');
    list.innerHTML = '';

    if (datas.his.length === 0) {
        list.innerHTML = '<li style="color:#888; font-style:italic;">Aucune alerte enregistrée</li>';
        return;
    }

    datas.his.forEach(entry => {
        const typeLabel = {
            T: 'Température',
            H: 'Humidité',
            S: 'Fumée/Gaz',
            F: 'Flamme'
        }[entry.var] || entry.var;

        const li = document.createElement('li');
        li.className = 'history-item';
        li.innerHTML = `
            <span class="history-time">${entry.t || '??'}</span>
            <div class="history-event">
                <strong>${typeLabel}</strong>  
            </div>
            <div>
                ${entry.val?.toFixed(1) ?? '-'}
                ${entry.var === 'T' ? '°C' : entry.var === 'H' ? '%' : entry.var === 'S' ? 'ppm' : ''}
            </div>
        `;
        list.appendChild(li);
    });
}

// ==========================================
// POPUP SEUILS & COMMANDES
// ==========================================
function openThresholdPopup() {
    document.getElementById('popupSeuilTemp').value = datas.seuils.sT ?? '';
    document.getElementById('popupSeuilHumidity').value = datas.seuils.sH ?? '';
    document.getElementById('popupSeuilSmoke').value = datas.seuils.sSm ?? '';
    document.getElementById('popupSeuilFlame').value = datas.seuils.sF ?? '';
    document.getElementById('thresholdPopup').style.display = 'flex';
}

function closeThresholdPopup() {
    document.getElementById('thresholdPopup').style.display = 'none';
}

function saveThresholds() {
    const temp = parseFloat(document.getElementById('popupSeuilTemp').value);
    const hum = parseFloat(document.getElementById('popupSeuilHumidity').value);
    const smoke = parseInt(document.getElementById('popupSeuilSmoke').value);
    const flame = parseInt(document.getElementById('popupSeuilFlame').value);

    if (!isNaN(temp)) datas.seuils.sT = temp;
    if (!isNaN(hum)) datas.seuils.sH = hum;
    if (!isNaN(smoke)) datas.seuils.sSm = smoke;
    if (!isNaN(flame)) datas.seuils.sF = flame;

    showLoader("Envoi des nouveaux seuils...");

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ com: "upd_seuils", seuils: datas.seuils }));
    }

    setTimeout(() => hideLoader(), 5000);
    refreshView();
    closeThresholdPopup();
}

function sendManualCommand(override, state) {
    showLoader(
        override
            ? (state ? "Activation forcée de la ventilation..." : "Désactivation forcée...")
            : "Retour au mode automatique..."
    );

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({
            com: "mnl_vent",
            override: override,
            state: state
        }));
    }

    // setTimeout(() => hideLoader(), 3000);
}

// ==========================================
// GESTION DU LOADER
// ==========================================
function showLoader(message = "Connexion en cours...") {
    const loader = document.getElementById('global-loader');
    const text = document.getElementById('loader-text');
    text.innerHTML = message;
    loader.classList.remove('hidden');
}

function hideLoader() {
    document.getElementById('global-loader').classList.add('hidden');
}

// ==========================================
// INITIALISATION
// ==========================================
document.addEventListener("DOMContentLoaded", () => {
    showLoader("Connexion au serveur...");
    refreshView();           // Affichage initial (valeurs à 0)
    connectWebSocket();      // ← Lancement de la première connexion !
});