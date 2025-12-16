// Température simulée
let temp = 20;

// Seuil d’alerte (tu peux le garder fixe côté récepteur ou le lier à l’émetteur)
const seuil = 35;

// Cette fonction met à jour l'affichage
function majAffichage() {
  // Afficher la température principale
  document.getElementById("tempValue").innerText = temp.toFixed(1) + " °C";

  // Déterminer l'état (Normal / Alerte)
  const statusElem = document.getElementById("status");
  if (temp >= seuil) {
    statusElem.innerText = "ALERTE : Température critique !";
    statusElem.className = "status alert";
  } else {
    statusElem.innerText = "Température normale";
    statusElem.className = "status ok";
  }

  // Ajouter à l'historique
  const li = document.createElement("li");
  const heure = new Date().toLocaleTimeString();
  li.innerText = `${heure} → ${temp.toFixed(1)} °C`;
  const history = document.getElementById("history");
  history.prepend(li);
}

// Simulation : toutes les 2 secondes, on génère une nouvelle valeur
setInterval(() => {
  // Simule une température reçue de LoRa (20 à 40 °C)
  temp = 20 + Math.random() * 20;

  // Met à jour tout l'affichage
  majAffichage();
}, 2000);
