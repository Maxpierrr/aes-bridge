# AES Bridge Control

Application de contrôle multiplateforme écrite avec Tauri 2, Rust et
TypeScript. Elle pilote le moteur réseau C++ AES Bridge sans effectuer de
traitement audio ou réseau dans l’interface graphique.

## Fonctions disponibles

- sélection explicite de l’interface réseau ;
- profils Raspberry Pi 8×8, ordinateur 64×64 et planet 22c ;
- édition des flux RX/TX, adresses, ports, groupes de 1/2/4/8 canaux,
  routage vers les 64 canaux et tampon anti-gigue ;
- validation avant enregistrement ou démarrage ;
- démarrage, arrêt propre et redémarrage du moteur ;
- compteurs RX, TX, pertes, erreurs, reconnexions et état PTP ;
- affichage des sessions SAP/SDP découvertes et import dans le routage RX ;
- profil de diagnostic 8×8 en boucle locale, sans trafic Ethernet.

Le moteur actuellement livré accepte au lancement une à huit banques
consécutives de huit canaux. L’interface prépare déjà les flux 1/2/4 canaux,
mais leur lancement restera refusé avec un message explicite tant que le
moteur C++ multi-flux variable n’aura pas été intégré.

## Développement macOS

Prérequis : Command Line Tools Apple, CMake, Node.js/npm et Rust/Cargo. Xcode
complet n’est pas nécessaire pour ce build de développement.

```sh
npm install
npm run build
cargo test --manifest-path src-tauri/Cargo.toml \
  --target-dir /private/tmp/aes-bridge-tauri-target
npm run bundle:mac:dev
```

Le build macOS compile automatiquement le moteur C++ et l’embarque comme
sidecar dans :

```text
/private/tmp/aes-bridge-tauri-target/debug/bundle/macos/AES Bridge.app
```

Le profil **Diagnostic local 8×8** permet ensuite de vérifier dans l’UI que
les compteurs RX et TX progressent ensemble, sans sélectionner une interface
Ethernet réelle.

## Distribution

Le bundle de développement est signé ad hoc, mais pas avec Developer ID et il
n’est pas notarié. Une distribution publique sans alerte Gatekeeper exigera un
compte Apple Developer, un certificat Developer ID Application, la
notarisation et Xcode complet.

Le port Windows de cette application Tauri est prévu, mais le périphérique
audio virtuel Windows et l’empaquetage de son backend ne sont pas encore
livrés.

## Licence

AES Bridge Control est distribué sous GNU GPL version 3 uniquement, comme le
reste du dérivé AES Bridge. Aucun code ou binaire propriétaire Merging n’est
utilisé.
