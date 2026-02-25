// ==========================================
// VARIABLES GLOBALES
// ==========================================
let datas = {
    isA: false,
    isRC: false, //commande recu
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
        tel: null,
    },
    his: [],
    nAlrt: null
};
let isSendingCommand = false;
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
                if (received.nAlrt) {
                    datas.his.unshift(received.nAlrt); // Nouvelle alerte en haut
                    // Limite l'historique côté client pour éviter la surcharge
                    if (datas.his.length > 100) {
                        datas.his.pop();
                    }
                }
                // Fusion sans écraser l'historique
                datas = { ...datas, ...received, nAlrt: null };
            }
            if (isFirstData || !isSendingCommand || (isSendingCommand && datas?.isRC)) {
                if (isSendingCommand) {
                    closeThresholdPopup()
                    isSendingCommand = false
                }
                hideLoader()
            }
            if (received?.event && received?.success) {
                if (isSendingCommand) {
                    closeThresholdPopup()
                    isSendingCommand = false
                }
                hideLoader()
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

    if (datas?.temp >= datas?.seuils?.sT) {
        alerts.push({ icon: '🌡️', label: 'Température élevée', value: `${datas.temp?.toFixed(1)}°C (≥ ${datas?.seuils?.sT}°C)` });
    }
    if (datas?.hum >= datas?.seuils?.sH) {
        alerts.push({ icon: '💧', label: 'Humidité élevée', value: `${datas?.hum?.toFixed(1)}% (≥ ${datas?.seuils?.sH}%)` });
    }
    if (datas?.sm >= datas?.seuils?.sSm) {
        alerts.push({ icon: '💨', label: 'Fumée / Gaz détecté', value: `${datas?.sm} ppm (≥ ${datas?.seuils?.sSm} ppm)` });
    }
    if (datas?.fl <= datas?.seuils?.sF) {
        alerts.push({ icon: '🔥', label: 'Flamme détectée', value: `Valeur: ${datas?.fl} (≤ ${datas?.seuils?.sF})` });
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

    // Ajout du bouton STOP BUZZER uniquement en cas d'alerte
    const buzzerButton = `
        <div style="margin-top: 16px; text-align: center;">
            <button class="btn btn-danger" onclick="stopBuzzer()">
                🔇 Arrêter le Buzzer
            </button>
        </div>
    `;

    container.innerHTML = `
        <div class="alert-card">
            <h3>🚨 ALERTES ACTIVES (${alerts.length})</h3>
            ${alertsHTML}
            <div class="urgent-message">⚠️ INTERVENTION REQUISE ⚠️</div>
            ${buzzerButton}
        </div>`;
}

// ==========================================
// RAFRAÎCHISSEMENT DE L'AFFICHAGE
// ==========================================
function refreshView() {
    // Température
    const tempDisplay = (datas?.temp !== null && datas?.temp !== undefined)
        ? datas.temp.toFixed(1) + ' °C'
        : '-- °C';
    document.getElementById('current-temp').innerText = tempDisplay;

    // Humidité
    const humDisplay = (datas?.hum !== null && datas?.hum !== undefined)
        ? datas?.hum.toFixed(1) + ' %'
        : '-- %';
    document.getElementById('current-humidity').innerText = humDisplay;

    // Fumée
    const smokeDisplay = (datas?.sm !== null && datas?.sm !== undefined)
        ? datas?.sm.toFixed(0) + ' ppm'
        : '-- ppm';
    document.getElementById('current-smoke').innerText = smokeDisplay;

    // Flamme (pas de toFixed, mais on garde une sécurité)
    const flameDisplay = datas?.fl < datas?.seuils?.sF ? 'FLAMME !' : datas?.fl;
    document.getElementById('current-flame').innerText = flameDisplay;

    document.getElementById('display-temp').innerText = datas?.seuils?.sT + ' °C';
    document.getElementById('display-humidity').innerText = datas?.seuils?.sH + ' %';
    document.getElementById('display-smoke').innerText = datas?.seuils?.sSm + ' ppm';
    document.getElementById('display-flame').innerText = '≤ ' + datas?.seuils?.sF;
    document.getElementById('display-tel').innerText = ' ' + datas?.seuils?.tel;

    // const statusText = datas.mnlOvrr
    //     ? (datas.mnlVent ? "FORCÉE ON" : "FORCÉE OFF")
    //     : "Automatique";
    // const statusColor = datas.mnlOvrr
    //     ? (datas.mnlVent ? "#28a745" : "#dc3545")
    //     : "#666";

    // const statusEl = document.getElementById('relay-status');
    // statusEl.innerText = statusText;
    // statusEl.style.color = statusColor;

    const powerEl = document.getElementById('power-status');
    if (powerEl) {
        const isOn = !!datas.pwrOn;
        powerEl.textContent = isOn ? 'EN SERVICE' : 'COUPÉE';
        powerEl.style.color = isOn ? '#28a745' : '#dc3545';
    }

    displayAlerts();
    updateHistory();
}

// ==========================================
// HISTORIQUE
// ==========================================
function updateHistory() {
    const list = document.getElementById('history-list');
    if (!list) return;

    list.innerHTML = '';

    if (!datas.his || datas.his.length === 0) {
        list.innerHTML = '<li style="color:#888; font-style:italic; text-align:center; padding:20px;">Aucune alerte enregistrée</li>';
        return;
    }

    // On trie du plus récent au plus ancien (si le serveur n'envoie pas déjà trié)
    const sortedHistory = [...datas.his].sort((a, b) => {
        // Comparaison simple sur la chaîne "MM/YY HH:MM" → fonctionne car format fixe
        return b.t.localeCompare(a.t);
    });

    sortedHistory.forEach(entry => {
        // Détection si c'est une alerte critique
        const isCritical = entry.crit === true;

        // Traduction du type d'alerte
        let typeLabel = '';
        let unit = '';
        let valueDisplay = entry.val != null ? Number(entry.val).toFixed(1) : '—';

        switch (entry.var) {
            case 'T':
                typeLabel = 'Température élevée';
                unit = '°C';
                break;
            case 'T+':
                typeLabel = 'Température CRITIQUE';
                unit = '°C';
                break;
            case 'H':
                typeLabel = 'Humidité élevée';
                unit = '%';
                break;
            case 'S':
                typeLabel = 'Fumée / Gaz';
                unit = '';
                break;
            case 'S++':
                typeLabel = 'Fumée TRÈS ÉLEVÉE';
                unit = '';
                break;
            case 'F':
                typeLabel = 'Détection flamme';
                unit = '';
                break;
            case 'F!':
                typeLabel = 'FLAMME CONFIRMÉE';
                unit = '';
                break;
            default:
                typeLabel = entry.var || 'Inconnu';
                unit = '';
        }

        // Création de l'élément
        const li = document.createElement('li');
        li.className = 'history-item';
        if (isCritical) {
            li.classList.add('critical');
        }

        li.innerHTML = `
            <div class="history-left">
                <span class="history-time">${entry.t || '??/?? ??:??'}</span>
                <span class="history-type ${isCritical ? 'critical-label' : ''}">
                    ${typeLabel}
                </span>
            </div>
            <div class="history-right">
                <span class="history-value">${valueDisplay}${unit}</span>
                ${isCritical ? '<span class="critical-badge">CRITIQUE</span>' : ''}
            </div>
        `;

        list.appendChild(li);
    });
}

// ==========================================
// POPUP SEUILS & COMMANDES
// ==========================================
function openThresholdPopup() {
    document.getElementById('popupSeuilTemp').value = datas?.seuils?.sT ?? '';
    document.getElementById('popupSeuilHumidity').value = datas?.seuils?.sH ?? '';
    document.getElementById('popupSeuilSmoke').value = datas?.seuils?.sSm ?? '';
    document.getElementById('popupSeuilFlame').value = datas?.seuils?.sF ?? '';
    document.getElementById('popupTel').value = datas?.seuils?.tel ?? '';
    document.getElementById('thresholdPopup').style.display = 'flex';
}

function closeThresholdPopup() {
    document.getElementById('thresholdPopup').style.display = 'none';
}

function saveThresholds() {
    // Récupération des valeurs
    const tempInput = document.getElementById('popupSeuilTemp');
    const humInput = document.getElementById('popupSeuilHumidity');
    const smokeInput = document.getElementById('popupSeuilSmoke');
    const flameInput = document.getElementById('popupSeuilFlame');
    const telInput = document.getElementById('popupTel');

    const temp = tempInput.value.trim() !== '' ? parseFloat(tempInput.value) : NaN;
    const hum = humInput.value.trim() !== '' ? parseFloat(humInput.value) : NaN;
    const smoke = smokeInput.value.trim() !== '' ? parseInt(smokeInput.value, 10) : NaN;
    const flame = flameInput.value.trim() !== '' ? parseInt(flameInput.value, 10) : NaN;
    const tel = telInput.value.trim();

    // ───────────────────────────────────────────────
    //              VALIDATIONS
    // ───────────────────────────────────────────────
    let hasError = false;
    let errorMessages = [];

    // Température
    if (tempInput.required && (isNaN(temp) || temp < 0 || temp > 100)) {
        hasError = true;
        errorMessages.push("Température : entre 0 et 100 °C");
        tempInput.classList.add('input-error');
    } else {
        tempInput.classList.remove('input-error');
    }

    // Humidité
    if (humInput.required && (isNaN(hum) || hum < 0 || hum > 100)) {
        hasError = true;
        errorMessages.push("Humidité : entre 0 et 100 %");
        humInput.classList.add('input-error');
    } else {
        humInput.classList.remove('input-error');
    }

    // Fumée / Gaz
    if (smokeInput.required && (isNaN(smoke) || smoke < 0 || smoke > 5000)) {
        hasError = true;
        errorMessages.push("Fumée/Gaz : entre 0 et 5000 ppm");
        smokeInput.classList.add('input-error');
    } else {
        smokeInput.classList.remove('input-error');
    }

    // Flamme (seuil bas = détection forte)
    if (flameInput.required && (isNaN(flame) || flame < 0 || flame > 5000)) {
        hasError = true;
        errorMessages.push("Seuil flamme : entre 0 et 5000");
        flameInput.classList.add('input-error');
    } else {
        flameInput.classList.remove('input-error');
    }

    // Téléphone camerounais → exactement 9 chiffres
    const telClean = tel.replace(/\s+/g, ''); // enlève espaces
    if (telInput.required) {
        if (!/^[0-9]{9}$/.test(telClean)) {
            hasError = true;
            errorMessages.push("Numéro : exactement 9 chiffres (ex: 695680531)");
            telInput.classList.add('input-error');
        } else {
            telInput.classList.remove('input-error');
        }
    } else {
        telInput.classList.remove('input-error');
    }

    // ───────────────────────────────────────────────
    //  Si erreur → on arrête + on affiche les messages
    // ───────────────────────────────────────────────
    if (hasError) {
        // Option 1 : afficher dans une alerte simple (rapide)
        alert("Veuillez corriger les erreurs suivantes :\n• " + errorMessages.join("\n• "));

        // Option 2 (mieux) : créer un bloc d'erreur dans le popup (recommandé)
        // Exemple :
        // let errorDiv = document.getElementById('popup-errors');
        // if (!errorDiv) {
        //     errorDiv = document.createElement('div');
        //     errorDiv.id = 'popup-errors';
        //     errorDiv.className = 'error-messages';
        //     document.querySelector('#thresholdPopup .popup-body').prepend(errorDiv);
        // }
        // errorDiv.innerHTML = errorMessages.map(msg => `<div>⚠️ ${msg}</div>`).join('');

        return; // ← très important : on n'envoie rien
    }

    // ───────────────────────────────────────────────
    //  Tout est valide → on met à jour l'objet
    // ───────────────────────────────────────────────
    if (!isNaN(temp)) datas.seuils.sT = temp;
    if (!isNaN(hum)) datas.seuils.sH = hum;
    if (!isNaN(smoke)) datas.seuils.sSm = smoke;
    if (!isNaN(flame)) datas.seuils.sF = flame;

    // On stocke le numéro nettoyé (sans espaces)
    if (telClean) {
        datas.seuils.tel = telClean;
    }

    // Feedback visuel + envoi
    showLoader("Modification en cours...");

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({
            com: "upd_conf",
            seuils: datas.seuils
        }));
        isSendingCommand = true;
    } else {
        alert("Connexion au serveur perdue.\nLes modifications n'ont pas pu être envoyées.");
        hideLoader();
    }
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
        isSendingCommand = true
    }
}

function stopBuzzer() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        alert("Connexion au module perdue. Impossible d'arrêter le buzzer.");
        return;
    }

    showLoader("Arrêt du buzzer en cours...");

    ws.send(JSON.stringify({
        com: "off_buzzer"
    }));

    // Feedback visuel immédiat (optionnel)
    setTimeout(() => {
        hideLoader();
        // On peut aussi mettre à jour un état local temporaire si besoin
    }, 800);
}

function forceCutPower() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        alert("Connexion au module perdue.");
        return;
    }
    showLoader("Coupure du courant en cours...");
    ws.send(JSON.stringify({ com: "force_cut" }));
    isSendingCommand = true;
}

function restorePower() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        alert("Connexion au module perdue.");
        return;
    }
    showLoader("Rétablissement du courant en cours...");
    ws.send(JSON.stringify({ com: "restore_pwr" }));
    isSendingCommand = true;
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

