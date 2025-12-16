 // ==========================================
        // VARIABLES GLOBALES
        // ==========================================
        let tempEmetteur = 20;
        let humidityEmetteur = 50;
        let smokeEmetteur = 0;

        let tempRecepteur = 20;
        let humidityRecepteur = 50;
        let smokeRecepteur = 0;

        // Seuils d'alerte
        let seuilTemp = 35;
        let seuilHumidity = 80;
        let seuilSmoke = 100;

        // ==========================================
        // NAVIGATION ENTRE VUES
        // ==========================================
        function switchView(viewName) {
            document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
            document.getElementById(viewName).classList.add('active');
            
            document.querySelectorAll('.nav-btn').forEach(btn => btn.classList.remove('active'));
            event.target.closest('.nav-btn').classList.add('active');
        }

        // ==========================================
        // MISE À JOUR DES SEUILS
        // ==========================================
        function updateThreshold(type) {
            if (type === 'temp') {
                seuilTemp = parseFloat(document.getElementById('seuilTemp').value);
                document.getElementById('display-temp').innerText = seuilTemp + '°C';
            } else if (type === 'humidity') {
                seuilHumidity = parseFloat(document.getElementById('seuilHumidity').value);
                document.getElementById('display-humidity').innerText = seuilHumidity + '%';
            } else if (type === 'smoke') {
                seuilSmoke = parseFloat(document.getElementById('seuilSmoke').value);
                document.getElementById('display-smoke').innerText = seuilSmoke + ' ppm';
            }
            
            console.log('Seuils mis à jour:', {seuilTemp, seuilHumidity, seuilSmoke});
            updateEmetteur();
            updateRecepteur();
        }

        // Fonction pour le bouton "Appliquer" (compatibilité)
        function changerSeuil() {
            updateThreshold('temp');
        }

        // ==========================================
        // DÉTECTION DES ALERTES
        // ==========================================
        function detectAlerts(temp, humidity, smoke) {
            const alerts = [];
            
            if (temp >= seuilTemp) {
                alerts.push({
                    type: 'temperature',
                    icon: '🌡️',
                    label: 'Alerte Température',
                    value: `${temp.toFixed(1)}°C (Seuil: ${seuilTemp}°C)`
                });
            }
            
            if (humidity >= seuilHumidity) {
                alerts.push({
                    type: 'humidity',
                    icon: '💧',
                    label: 'Alerte Humidité',
                    value: `${humidity.toFixed(1)}% (Seuil: ${seuilHumidity}%)`
                });
            }
            
            if (smoke >= seuilSmoke) {
                alerts.push({
                    type: 'smoke',
                    icon: '💨',
                    label: 'Alerte Fumée',
                    value: `${smoke.toFixed(0)} ppm (Seuil: ${seuilSmoke} ppm)`
                });
            }
            
            return alerts;
        }

        // ==========================================
        // AFFICHAGE DES ALERTES PUISSANT
        // ==========================================
        function displayAlerts(containerId, alerts, urgentMessage) {
            const container = document.getElementById(containerId);
            
            if (alerts.length === 0) {
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
            
            let alertsHTML = alerts.map(alert => `
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
                    <div class="urgent-message">
                        ${urgentMessage}
                    </div>
                </div>
            `;
        }

        // ==========================================
        // MISE À JOUR AFFICHAGE ÉMETTEUR
        // ==========================================
        function updateEmetteur() {
            // Afficher les valeurs
            document.getElementById('emetteur-temp').innerText = tempEmetteur.toFixed(1) + ' °C';
            document.getElementById('emetteur-humidity').innerText = humidityEmetteur.toFixed(1) + ' %';
            document.getElementById('emetteur-smoke').innerText = smokeEmetteur.toFixed(0) + ' ppm';

            // Détecter et afficher les alertes
            const alerts = detectAlerts(tempEmetteur, humidityEmetteur, smokeEmetteur);
            displayAlerts(
                'emetteur-alert-container', 
                alerts, 
                '⚠️ ACTION URGENTE DU MAINTENANCIER REQUISE ⚠️'
            );

            // Ajouter à l'historique
            addToHistory('emetteur', tempEmetteur, humidityEmetteur, smokeEmetteur);
        }

        // ==========================================
        // MISE À JOUR AFFICHAGE RÉCEPTEUR
        // ==========================================
        function updateRecepteur() {
            // Afficher les valeurs reçues
            document.getElementById('recepteur-temp').innerText = tempRecepteur.toFixed(1) + ' °C';
            document.getElementById('recepteur-humidity').innerText = humidityRecepteur.toFixed(1) + ' %';
            document.getElementById('recepteur-smoke').innerText = smokeRecepteur.toFixed(0) + ' ppm';

            // Détecter et afficher les alertes
            const alerts = detectAlerts(tempRecepteur, humidityRecepteur, smokeRecepteur);
            displayAlerts(
                'recepteur-alert-container', 
                alerts, 
                '🚨 DANGER - VEUILLEZ CONTACTER URGEMMENT LE MAINTENANCIER 🚨'
            );

            // Ajouter à l'historique
            addToHistory('recepteur', tempRecepteur, humidityRecepteur, smokeRecepteur);
        }

        // ==========================================
        // AJOUTER À L'HISTORIQUE
        // ==========================================
        function addToHistory(type, temp, humidity, smoke) {
            const historyList = document.getElementById(type + '-history');
            const li = document.createElement('li');
            li.className = 'history-item';
            
            const time = new Date().toLocaleTimeString('fr-FR');
            
            li.innerHTML = `
                <span class="history-time">${time}</span>
                <div class="history-values">
                    <span class="history-temp">${temp.toFixed(1)}°C</span>
                    <span class="history-humidity">${humidity.toFixed(1)}%</span>
                    <span class="history-smoke">${smoke.toFixed(0)} ppm</span>
                </div>
            `;
            
            historyList.insertBefore(li, historyList.firstChild);
            
            // Limiter à 50 entrées
            if (historyList.children.length > 50) {
                historyList.removeChild(historyList.lastChild);
            }
        }

        // ==========================================
        // TESTER UNE ALERTE
        // ==========================================
        function testAlerteEmetteur() {
            tempEmetteur = seuilTemp + 5;
            humidityEmetteur = seuilHumidity + 10;
            smokeEmetteur = seuilSmoke + 50;
            updateEmetteur();
            
            // Simuler la transmission via LoRa
            setTimeout(() => {
                tempRecepteur = tempEmetteur;
                humidityRecepteur = humidityEmetteur;
                smokeRecepteur = smokeEmetteur;
                updateRecepteur();
            }, 500);
        }

        // ==========================================
        // SIMULATION AUTOMATIQUE ÉMETTEUR
        // ==========================================
        setInterval(() => {
            // Température entre 18°C et 38°C
            tempEmetteur = 18 + Math.random() * 20;
            
            // Humidité entre 30% et 90%
            humidityEmetteur = 30 + Math.random() * 60;
            
            // Fumée entre 0 et 120 ppm (avec pics occasionnels)
            smokeEmetteur = Math.random() < 0.9 ? Math.random() * 50 : 80 + Math.random() * 40;
            
            updateEmetteur();
            
            // Simuler la transmission LoRa avec un léger délai
            setTimeout(() => {
                tempRecepteur = tempEmetteur + (Math.random() - 0.5) * 0.5;
                humidityRecepteur = humidityEmetteur + (Math.random() - 0.5) * 2;
                smokeRecepteur = smokeEmetteur + (Math.random() - 0.5) * 5;
                updateRecepteur();
            }, 300);
        }, 3000);

        // ==========================================
        // INITIALISATION
        // ==========================================
        updateEmetteur();
        updateRecepteur();