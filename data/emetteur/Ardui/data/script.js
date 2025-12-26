// ==========================================
// VARIABLES GLOBALES
// ==========================================
let datas = {
    success: true,
    isAlert: false,
    temp: 0,
    humidity: 0,
    smoke: 0,
    flame: 0,
    manualOverride: false,
    manualVentilState: false,
    seuils: {
        seuilTemp: null,
        seuilHumidity: null,
        seuilSmoke: null,
        seuilFlame: null,
    },
    history: [],
    newAlerte: null
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
                hideLoader();
            } else {
                // Mises à jour suivantes
                if (received.newAlerte) {
                    datas.history.unshift(received.newAlerte); // Nouvelle alerte en haut
                    // Limite l'historique côté client pour éviter la surcharge
                    if (datas.history.length > 100) {
                        datas.history.pop();
                    }
                }
                // Fusion sans écraser l'historique
                datas = { ...datas, ...received, newAlerte: null };
            }

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
    const seconds = (delay / 1000).toFixed(1);

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

    if (datas.temp >= datas.seuils.seuilTemp) {
        alerts.push({ icon: '🌡️', label: 'Température élevée', value: `${datas.temp.toFixed(1)}°C (≥ ${datas.seuils.seuilTemp}°C)` });
    }
    if (datas.humidity >= datas.seuils.seuilHumidity) {
        alerts.push({ icon: '💧', label: 'Humidité élevée', value: `${datas.humidity.toFixed(1)}% (≥ ${datas.seuils.seuilHumidity}%)` });
    }
    if (datas.smoke >= datas.seuils.seuilSmoke) {
        alerts.push({ icon: '💨', label: 'Fumée / Gaz détecté', value: `${datas.smoke} ppm (≥ ${datas.seuils.seuilSmoke} ppm)` });
    }
    if (datas.flame <= datas.seuils.seuilFlame) {
        alerts.push({ icon: '🔥', label: 'Flamme détectée', value: `Valeur: ${datas.flame} (≤ ${datas.seuils.seuilFlame})` });
    }

    return alerts;
}

// ==========================================
// AFFICHAGE ALERTES
// ==========================================
function displayAlerts() {
    const container = document.getElementById('alert-container');
    const alerts = detectAlerts();

    if (alerts.length === 0 || !datas.isAlert) {
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
    document.getElementById('current-temp').innerText = datas.temp.toFixed(1) + ' °C';
    document.getElementById('current-humidity').innerText = datas.humidity.toFixed(1) + ' %';
    document.getElementById('current-smoke').innerText = datas.smoke.toFixed(0) + ' ppm';
    document.getElementById('current-flame').innerText = datas.flame < datas.seuils.seuilFlame ? 'FLAMME !' : datas.flame;

    document.getElementById('display-temp').innerText = datas.seuils.seuilTemp + ' °C';
    document.getElementById('display-humidity').innerText = datas.seuils.seuilHumidity + ' %';
    document.getElementById('display-smoke').innerText = datas.seuils.seuilSmoke + ' ppm';
    document.getElementById('display-flame').innerText = '≤ ' + datas.seuils.seuilFlame;

    const statusText = datas.manualOverride
        ? (datas.manualVentilState ? "FORCÉE ON" : "FORCÉE OFF")
        : "Automatique";
    const statusColor = datas.manualOverride
        ? (datas.manualVentilState ? "#28a745" : "#dc3545")
        : "#666";

    const statusEl = document.getElementById('relay-status');
    statusEl.innerText = statusText;
    statusEl.style.color = statusColor;

    document.getElementById('ventil-on').style.opacity = datas.manualOverride && datas.manualVentilState ? "1" : "0.6";
    document.getElementById('ventil-off').style.opacity = datas.manualOverride && !datas.manualVentilState ? "1" : "0.6";

    displayAlerts();
    updateHistory();
}

// ==========================================
// HISTORIQUE
// ==========================================
function updateHistory() {
    const list = document.getElementById('history-list');
    list.innerHTML = '';

    if (datas.history.length === 0) {
        list.innerHTML = '<li style="color:#888; font-style:italic;">Aucune alerte enregistrée</li>';
        return;
    }

    datas.history.forEach(entry => {
        const typeLabel = {
            TEMPERATURE: 'Température',
            HUMIDITY: 'Humidité',
            SMOKE: 'Fumée/Gaz',
            FLAME: 'Flamme'
        }[entry.type] || entry.type;

        const li = document.createElement('li');
        li.className = 'history-item';
        li.innerHTML = `
            <span class="history-time">${entry.time || '??'}</span>
            <div class="history-event">
                <strong>${typeLabel}</strong> : ${entry.val?.toFixed(1) ?? '-'}
                ${entry.type === 'TEMPERATURE' ? '°C' : entry.type === 'HUMIDITY' ? '%' : entry.type === 'SMOKE' ? 'ppm' : ''}
            </div>
        `;
        list.appendChild(li);
    });
}

// ==========================================
// POPUP SEUILS & COMMANDES
// ==========================================
function openThresholdPopup() {
    document.getElementById('popupSeuilTemp').value = datas.seuils.seuilTemp ?? '';
    document.getElementById('popupSeuilHumidity').value = datas.seuils.seuilHumidity ?? '';
    document.getElementById('popupSeuilSmoke').value = datas.seuils.seuilSmoke ?? '';
    document.getElementById('popupSeuilFlame').value = datas.seuils.seuilFlame ?? '';
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

    if (!isNaN(temp)) datas.seuils.seuilTemp = temp;
    if (!isNaN(hum)) datas.seuils.seuilHumidity = hum;
    if (!isNaN(smoke)) datas.seuils.seuilSmoke = smoke;
    if (!isNaN(flame)) datas.seuils.seuilFlame = flame;

    showLoader("Envoi des nouveaux seuils...");

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ command: "update_thresholds", seuils: datas.seuils }));
    }

    // setTimeout(() => hideLoader(), 3000);
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
            command: "manual_ventil",
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