// -------------------------------
// VARIABLES DU SYSTÈME
// -------------------------------

// Température simulée (en attendant le vrai capteur)
let temperature = 20;

// Seuil d’alerte (modifiable via l’interface)
let seuil = 35;

// -------------------------------
// CHANGER LE SEUIL
// -------------------------------
function changerSeuil() {
  seuil = parseFloat(document.getElementById("seuilInput").value);
  console.log("Seuil changé :", seuil);
}

// -------------------------------
// TESTER UNE ALERTE MANUELLEMENT
// -------------------------------
function testAlerte() {
  temperature = seuil + 5; // force une alerte
  mettreAJourAffichage();
}

// -------------------------------
// FONCTION QUI MET À JOUR L'AFFICHAGE
// -------------------------------
function mettreAJourAffichage() {
  // Afficher la température
  document.getElementById("tempValue").innerText =
    temperature.toFixed(1) + " °C";

  // Détecter une alerte
  if (temperature >= seuil) {
    document.getElementById("status").innerText = "ALERTE !";
    document.getElementById("status").className = "status alert";

    // + Ici plus tard : envoyer l’alerte via LoRa
  } else {
    document.getElementById("status").innerText = "Normal";
    document.getElementById("status").className = "status ok";
  }
}

// -------------------------------
// SIMULATION AUTOMATIQUE
// (toutes les 2 secondes)
// -------------------------------
setInterval(() => {
  // Simule une température naturelle
  temperature = 20 + Math.random() * 15;

  // Met à jour l'affichage
  mettreAJourAffichage();
}, 2000);
