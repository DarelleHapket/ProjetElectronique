// ==========================================
// VARIABLES GLOBALES
// ==========================================
let datas = {
    success: true,
    isAlert: false,
    alertType: [],
    temp: 0,
    humidity: 0,
    smoke: 0,
    flame: 0,
    seuils: {
        seuilTemp: null,
        seuilHumidity: null,
        seuilSmoke: null,
        seuilFlame: null,
    },
    history: [],  // sera rempli par les données envoyées par l'ESP32 , uniquement a la premiere connexion
    newAlerte: null
};

let isFIrstData = true

// ==========================================
// CONNEXION WEBSOCKET
// ==========================================
const ws = new WebSocket(`ws://${window.location.host}/ws`);

ws.onopen = function () {
    console.log("Connexion WebSocket établie");
};

ws.onmessage = async function (event) {
    try {
        const text = await new Blob([event.data]).text();
        const receivedData = JSON.parse(text);

        if (isFIrstData) {
            datas = receivedData;
            isFIrstData = false;
        } else {
            if (receivedData?.newAlerte) {
                datas.history = [receivedData.newAlerte, ...datas.history];
            }

            datas = {
                ...datas,
                ...receivedData,
                newAlerte: null
            }
        }

        refreshAllViews();
    } catch (e) {
        console.error("Erreur lors du parsing des données WebSocket :", e);
    }
};

ws.onclose = function () {
    console.log("Connexion WebSocket fermée");
};

ws.onerror = function (error) {
    console.error("Erreur WebSocket :", error);
};

// ==========================================
// NAVIGATION ENTRE VUES
// ==========================================
function switchView(viewName) {
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    document.getElementById(viewName)?.classList.add('active');

    document.querySelectorAll('.nav-btn').forEach(btn => btn.classList.remove('active'));
    event?.target.closest('.nav-btn')?.classList.add('active');
}

// ==========================================
// MISE À JOUR DES SEUILS DEPUIS L’INTERFACE (local uniquement)
// ==========================================
function updateThreshold(type) {
    const input = document.getElementById(`seuil${type.charAt(0).toUpperCase() + type.slice(1)}`);
    if (!input) return;

    const newValue = parseFloat(input.value);
    if (!isNaN(newValue)) {
        datas.seuils[type] = newValue;

        // Mise à jour de l’affichage du seuil
        const displayId = `display-${type}`;
        const unit = type === 'temp' ? '°C' : type === 'smoke' ? ' ppm' : '%';
        document.getElementById(displayId).innerText = newValue + unit;
    }

    refreshAllViews();
}

// Compatibilité ancien bouton "Appliquer"
function changerSeuil() {
    updateThreshold('temp');
}

// ==========================================
// DÉTECTION DES ALERTES EN FONCTION DES DONNÉES ACTUELLES
// ==========================================
function detectAlerts() {
    const alerts = [];

    if (datas.seuilTemp >= datas.seuils.seuilTemp) {
        alerts.push({
            type: 'temperature',
            icon: '🌡️',
            label: 'Alerte Température',
            value: `${datas.temp.toFixed(1)}°C (Seuil: ${datas.seuils.seuilTemp}°C)`
        });
    }

    if (datas.humidity >= datas.seuils.seuilHumidity) {
        alerts.push({
            type: 'humidity',
            icon: '💧',
            label: 'Alerte Humidité',
            value: `${datas.humidity.toFixed(1)}% (Seuil: ${datas.seuils.seuilHumidity}%)`
        });
    }

    if (datas.smoke >= datas.seuils.seuilSmoke) {
        alerts.push({
            type: 'smoke',
            icon: '💨',
            label: 'Alerte Fumée/Gaz',
            value: `${datas.smoke.toFixed(0)} ppm (Seuil: ${datas.seuils.seuilSmoke} ppm)`
        });
    }

    if (datas.flame <= datas.seuils.seuilFlame) {
        alerts.push({
            type: 'flame',
            icon: '🔥',
            label: 'Alerte Flamme',
            value: `${datas.flame} (Seuil: ≤ ${datas.seuils.seuilFlame})`
        });
    }

    return alerts;
}

// ==========================================
// AFFICHAGE DES ALERTES
// ==========================================
function displayAlerts(containerId, urgentMessage) {
    const container = document.getElementById(containerId);
    const alerts = detectAlerts();

    if (alerts.length === 0 || !datas.isAlert) {
        container.innerHTML = `
            <div class="card">
                <h3>📊 État du Système</h3>
                <div style="margin-top: 20px;">
                    <span class="status-badge normal">Système Normal - Aucune Alerte</span>
                </div>
            </div>
        `;
        return;
    }

    const alertsHTML = alerts.map(alert => `
        <div class="alert-message">
            <div class="alert-icon">${alert.icon}</div>
            <div class="alert-type">
                <div class="alert-type-label">${alert.label}</div>
                <div class="alert-value">${alert.value}</div>
            </div>
        </div>
    `).join('');

    container.innerHTML = `
        <div class="alert-card">
            <h3>🚨 ALERTES ACTIVES (${alerts.length})</h3>
            ${alertsHTML}
            <div class="urgent-message">${urgentMessage}</div>
        </div>
    `;
}

// ==========================================
// RAFRAÎCHISSEMENT COMPLET DES VUES
// ==========================================
function refreshAllViews() {
    // Valeurs actuelles
    document.getElementById('emetteur-temp').innerText = datas.temp.toFixed(1) + ' °C';
    document.getElementById('emetteur-humidity').innerText = datas.humidity.toFixed(1) + ' %';
    document.getElementById('emetteur-smoke').innerText = datas.smoke.toFixed(0) + ' ppm';
    document.getElementById('emetteur-flame').innerText = datas.flame.toFixed(0);

    document.getElementById('recepteur-temp').innerText = datas.temp.toFixed(1) + ' °C';
    document.getElementById('recepteur-humidity').innerText = datas.humidity.toFixed(1) + ' %';
    document.getElementById('recepteur-smoke').innerText = datas.smoke.toFixed(0) + ' ppm';

    // Seuils affichés
    document.getElementById('display-temp').innerText = datas.seuils.seuilTemp + '°C';
    document.getElementById('display-humidity').innerText = datas.seuils.seuilHumidity + '%';
    document.getElementById('display-smoke').innerText = datas.seuils.seuilSmoke + ' ppm';
    document.getElementById('display-flame').innerText = datas.seuils.seuilFlame;

    document.getElementById('emetteur-flame').innerText = datas.flame < datas.seuils.seuilFlame ? 'FLAMME !' : datas.flame;

    // Alertes
    displayAlerts('emetteur-alert-container', '⚠️ ACTION URGENTE DU MAINTENANCIER REQUISE ⚠️');
    displayAlerts('recepteur-alert-container', '🚨 DANGER - VEUILLEZ CONTACTER URGEMMENT LE MAINTENANCIER 🚨');


    // État du relais / ventilation
    const statusText = datas?.manualOverride
        ? (datas.manualVentilState ? "FORCÉE ON" : "FORCÉE OFF")
        : "Automatique";

    const statusColor = datas?.manualOverride
        ? (datas.manualVentilState ? "#28a745" : "#dc3545")
        : "#666";

    document.getElementById('relay-status').innerText = statusText;
    document.getElementById('relay-status').style.color = statusColor;

    // Optionnel : changer le style des boutons
    document.getElementById('ventil-on').style.opacity = datas?.manualOverride && datas.manualVentilState ? "1" : "0.6";
    document.getElementById('ventil-off').style.opacity = datas?.manualOverride && !datas.manualVentilState ? "1" : "0.6";

    // Historique
    updateHistory();
}

// ==========================================
// MISE À JOUR DE L’HISTORIQUE (version simplifiée)
// ==========================================
function updateHistory() {
    ['emetteur', 'recepteur'].forEach(prefix => {
        const historyList = document.getElementById(prefix + '-history');
        if (!historyList) return;

        historyList.innerHTML = '';

        // On affiche les alertes stockées (envoyées par l'ESP32)
        datas.history.forEach(entry => {
            const li = document.createElement('li');
            li.className = 'history-item';

            const typeLabel = entry.type === 'TEMPERATURE' ? 'Température' :
                entry.type === 'HUMIDITY' ? 'Humidité' :
                    entry.type === 'FLAME' ? 'Flame' :
                        entry.type === 'SMOKE' ? 'Fumée/Gaz' : entry.type;

            li.innerHTML = `
                <span class="history-time">${entry?.time}</span>
                <div class="history-event">
                    <strong>${typeLabel}</strong> dépassement : ${entry.val?.toFixed(1) ?? '-'} 
                    ${entry.type === 'TEMPERATURE' ? '°C' : entry.type === 'HUMIDITY' ? '%' : entry.type === 'HUMIDITY' ? 'ppm' : ''}
                </div>
            `;
            historyList.appendChild(li);
        });

        // Si historique vide
        if (datas.history.length === 0) {
            const li = document.createElement('li');
            li.textContent = "Aucune alerte enregistrée pour le moment";
            li.style.color = "#888";
            li.style.fontStyle = "italic";
            historyList.appendChild(li);
        }
    });
}


// ==========================================
// Gestion du popup des seuils
// ========================================== 
function openThresholdPopup() {
    // Remplir les champs avec les valeurs actuelles
    document.getElementById('popupSeuilTemp').value = datas.seuils.seuilTemp ?? 0;
    document.getElementById('popupSeuilHumidity').value = datas.seuils.seuilHumidity ?? 0;
    document.getElementById('popupSeuilSmoke').value = datas.seuils.seuilSmoke ?? 0;
    document.getElementById('popupSeuilFlame').value = datas.seuils.seuilFlame ?? 0;

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

    //  envoyer les nouveaux seuils à l'ESP32 via WebSocket
    ws.send(JSON.stringify({ command: "update_thresholds", seuils: datas.seuils }));

    // Mise à jour visuelle
    refreshAllViews();

    closeThresholdPopup();
}

function sendManualCommand(override, state) {
    const command = {
        command: "manual_ventil",
        override: override,    // true = activer le mode manuel
        state: state           // true = ON, false = OFF (ignoré si override=false)
    };
    ws.send(JSON.stringify(command));
}

// ==========================================
// INITIALISATION
// ==========================================
document.addEventListener("DOMContentLoaded", () => {
    refreshAllViews();

    // Optionnel : tentative de reconnexion automatique si WebSocket se coupe
    setInterval(() => {
        if (ws.readyState === WebSocket.CLOSED || ws.readyState === WebSocket.CLOSING) {
            console.log("Tentative de reconnexion WebSocket...");
            location.reload(); // ou implémenter une vraie reconnexion
        }
    }, 10000);
});

