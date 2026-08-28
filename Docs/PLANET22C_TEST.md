# Test matériel ESI planet 22c

Ce test valide un seul planet 22c en AES67 stéréo à 48 kHz. Il ne constitue
pas encore une qualification de production. Noter les pertes, reconnexions,
état PTP et tout décrochage sonore pendant l’essai.

## 1. Préparer le réseau et le planet

1. Relier le Mac et le planet au même switch Ethernet filaire. Désactiver le
   Wi-Fi du Mac pendant le premier essai pour éviter toute ambiguïté de route.
2. Dans Dante Controller, régler le planet à 48 kHz et activer son mode AES67.
   Appliquer le redémarrage demandé par Dante Controller, le cas échéant.
3. L’activation du mode AES67 ne crée aucun flux audio. Dans Device View,
   cliquer sur **Create a new multicast flow** (icône de flow dans la barre
   d’outils), choisir le type **AES67**, puis créer un flow contenant les deux entrées
   analogiques. Relever exactement son adresse multicast, son port RTP et son
   payload type. Ne pas supposer que les valeurs du profil AES Bridge sont les
   valeurs attribuées par Dante Controller.
4. Vérifier qu’aucun autre émetteur n’utilise la même combinaison multicast et
   port.

## 2. Recevoir le planet dans AES Bridge

1. Ouvrir AES Bridge et choisir le profil **planet 22c**.
2. Choisir explicitement l’interface Ethernet du Mac.
3. Attendre l’apparition du flow du planet dans **Sessions découvertes** puis
   utiliser **Router** : pour le profil planet stéréo, cette action remplace le
   flux RX préconfiguré avec l’adresse, le port et le payload du SDP. Si SAP ne le découvre pas, recopier manuellement
   l’adresse multicast, le port, le payload et éventuellement l’adresse source
   relevés dans Dante Controller.
4. Conserver `L24`, `48 kHz`, `2 ch`, `48 samples`, PTP domaine 0 et commencer
   avec un tampon de 6 paquets.
5. Démarrer le moteur. Le compteur RX doit progresser, les pertes doivent
   rester à zéro et PTP doit finir par indiquer un verrouillage.
   Tant qu’aucune application Core Audio n’utilise AES Bridge, le ring RX peut
   se remplir ; cela est affiché comme **Core Audio inactif**, pas comme une
   perte RTP.
6. Dans Configuration Audio et MIDI ou dans un DAW, sélectionner **AES Bridge**
   et écouter/enregistrer ses entrées Core Audio 1 et 2. Une source branchée sur
   l’entrée analogique 1 du planet ne doit apparaître que sur le canal 1 ;
   répéter pour le canal 2.

## 3. Envoyer le Mac vers le planet

1. Dans AES Bridge, choisir pour le flow TX une adresse multicast libre, un
   port RTP et un payload dynamique. Le profil propose `239.69.83.83:5004` et
   payload 96, mais ces valeurs peuvent être changées.
2. Démarrer AES Bridge et attendre son annonce SAP/SDP.
3. Dans Dante Controller, rechercher l’émetteur AES67 **AES-Bridge-Outputs-1-2**
   et abonner les sorties analogiques 1 et 2 du planet à ses canaux 1 et 2.
4. Envoyer deux signaux identifiables et de faible niveau depuis les sorties
   Core Audio 1 et 2. Vérifier séparément les sorties analogiques du planet.

## 4. Contrôles de robustesse

- débrancher l’Ethernet dix secondes puis le rebrancher ; RX et TX doivent
  repartir et le compteur de reconnexions doit augmenter ;
- arrêter puis relancer le moteur depuis l’application ;
- laisser fonctionner au moins 30 minutes pour le premier essai, puis 8 heures
  avant toute utilisation importante ;
- conserver une capture des statistiques et exporter les SDP du planet et
  d’AES Bridge en cas d’échec.

Arrêter le test si le PTP ne verrouille pas, si les canaux sont inversés ou si
les pertes augmentent en continu. Le client PTP utilise encore des timestamps
logiciels et doit être considéré comme expérimental sur le matériel réel.

## Point de contrôle matériel du 28 août 2026

Un planet 22c et un Mac Apple Silicon reliés en Ethernet filaire ont validé la
réception suivante :

- flow Dante AES67/SAP `LTC : 2`, `239.69.74.153:5004`, payload 97 ;
- L24, 48 kHz, 2 canaux, 48 échantillons par paquet ;
- PTPv2 domaine 0 verrouillé, délai logiciel observé de 95 à 220 µs ;
- plus de 500 000 paquets RX et TX, sans perte RTP, paquet malformé, erreur
  PTP/SAP ni reconnexion pendant le point de contrôle ;
- périphérique Core Audio visible et valide en 64 entrées × 64 sorties à
  48 kHz, simultanément avec Dante Via ;
- capture Core Audio de huit secondes : signal LTC sur l’entrée 1
  (environ -49 dBFS RMS) et entrée 2 au plancher numérique (environ
  -110 dBFS RMS), confirmant que les canaux 1 et 2 ne sont pas croisés.

Ce résultat valide le chemin Planet → AES67 → moteur → Core Audio pour ce test.
Il ne valide pas encore la sortie analogique du planet, la reconnexion physique,
ni la stabilité de longue durée. Les compteurs de ring peuvent inclure les
62 canaux Core Audio inactifs du périphérique 64×64 ; ils ne doivent pas être
interprétés comme des pertes réseau sans corrélation avec `Pertes`.
