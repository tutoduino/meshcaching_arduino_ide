# Géolocaliser un répéteur MeshCore avec le RSSI

Deux sketches Arduino qui transforment une carte LoRa en **chercheur de répéteur MeshCore** : elle écoute le réseau, repère les paquets émis par *un* répéteur précis, et affiche en temps réel la puissance du signal reçu (RSSI), le rapport signal/bruit (SNR) et le temps écoulé depuis le dernier paquet. En vous déplaçant, vous suivez le RSSI comme un jeu du « chaud / froid » pour retrouver l'emplacement physique du répéteur.

| Carte | Sketch | Écran | Microcontrôleur |
|---|---|---|---|
| **Heltec WiFi LoRa 32 V3** | [`MeshCaching_Arduino_IDE_HeltecV3.ino`] | OLED SSD1306 128×64 | ESP32-S3 |
| **LilyGO T-Echo** | [`MeshCaching_Arduino_IDE_LilygoTEcho.ino`]| e-paper 1,54" 200×200 | nRF52840 |

Les deux sketches partagent la même logique (décodage des paquets MeshCore, comparaison avec le répéteur cible). Seuls le brochage, l'écran et quelques appels spécifiques au microcontrôleur diffèrent.

---

## Principe de fonctionnement

Un réseau MeshCore est constitué de nœuds (companions) et de **répéteurs** qui retransmettent les paquets. Chaque nœud est identifié par une clé publique de 32 octets ; dans les paquets en transit, il est représenté par un **hash** : le ou les premiers octets de cette clé.

Le sketch :

1. **écoute en continu** sur la fréquence et les paramètres LoRa du réseau ;
2. **décode l'en-tête MeshCore** de chaque paquet reçu (type de route, chemin parcouru, type de payload) ;
3. détermine si **l'émetteur du paquet** est le répéteur cible, uniquement dans les cas où cela peut être établi avec certitude (voir le tableau ci-dessous) ;
4. si oui, le paquet est retenu : on affiche son RSSI et son SNR, et on remet le compteur de temps à zéro.

Autrement dit, chaque paquet retenu prouve que le répéteur cible **vient d'émettre à portée directe de votre carte**, et le RSSI vous dit à quel point vous êtes près.

Les paquets au CRC invalide sont ignorés, pour éviter les fausses détections.

### Quels paquets sont retenus ?

C'est le point délicat : un paquet MeshCore ne contient pas toujours l'identité de celui qui vient de l'émettre. Le sketch ne retient donc que trois cas, et **refuse tout le reste** pour ne jamais afficher le RSSI d'un autre nœud.

| Paquet reçu | Retenu ? | Pourquoi |
|---|---|---|
| **Annonce (advert) sans saut** | ✅ oui | Le début du payload est la clé publique complète de l'émetteur : aucune ambiguïté. |
| **Réponse à votre ping TRACE** | ✅ oui | Reconnue par le tag aléatoire de la requête (valable 10 s). |
| **Paquet flood retransmis** (au moins un saut) | ✅ oui (désactivable) | Chaque répéteur ajoute son hash à la fin du chemin : le dernier hash est le dernier à avoir émis. Hash d'un octet, donc collision possible avec un autre répéteur (voir `ACCEPT_RELAYED_FLOOD`). |
| Paquet en route **directe** | ❌ non | Le chemin contient les sauts *restant à faire* (le prochain saut en tête, la destination à la fin), pas l'émetteur. |
| Message **adressé** au répéteur (requête, message privé…) | ❌ non | Le premier octet du payload est le hash du *destinataire*, pas de l'émetteur : il vient d'un smartphone, pas du répéteur. |
| Message de groupe, ACK… | ❌ non | L'émetteur n'y figure pas (premier octet = canal ou morceau de CRC). |

### Le « ping » (bouton)

Attendre passivement peut être long : un répéteur n'émet que lorsqu'il relaie du trafic ou envoie ses annonces périodiques. Un appui sur le bouton envoie donc un paquet **TRACE** (le mécanisme natif de MeshCore pour tracer un chemin) en *zéro saut* vers le répéteur. Le sketch mémorise le tag aléatoire de la requête et ne retient la réponse que si elle revient avec le même tag, dans les 10 secondes.

> ⚠️ Le ping est la partie la plus expérimentale : la réponse dépend de la version du firmware du répéteur et de sa configuration. S'il ne répond pas, l'écoute passive (annonces et trafic relayé) fonctionne quand même.

---

## Ce qu'il vous faut

- Une carte **Heltec WiFi LoRa 32 V3** ou un **LilyGO T-Echo** (bande 868 MHz pour l'Europe).
- **Son antenne LoRa, toujours branchée avant de mettre sous tension** : émettre sans antenne peut endommager l'étage radio.
- Un câble USB-C pour le flashage.
- **Sous Linux, pour le T-Echo** : l'outil `adafruit-nrfutil` (voir [Avant de lancer l'IDE Arduino](#avant-de-lancer-lide-arduino-linux)).
- Le **préfixe de clé publique du répéteur** à trouver (voir plus bas).
- Un smartphone avec GPS (application de cartographie) pour noter vos positions de mesure.
- Pour aller plus loin : une **antenne directive** (Yagi) en connectique adaptée à votre carte. Sur le T-Echo, l'antenne est intégrée au boîtier, donc le Heltec est plus pratique pour ça.

---

## Installation et flashage

### Bibliothèques communes (Gestionnaire de bibliothèques Arduino)

- **RadioLib** (jgromes)

### Heltec WiFi LoRa 32 V3

1. Installer le support des cartes Heltec/ESP32 dans l'IDE Arduino et choisir la carte **Heltec WiFi LoRa 32(V3)**.
2. Installer les bibliothèques : **Adafruit SSD1306**, **Adafruit GFX** (et Adafruit BusIO, installée en dépendance).
3. Ouvrir `rssi-meshcore-repeater/rssi-meshcore-repeater.ino`, brancher la carte, téléverser.

### LilyGO T-Echo

#### Avant de lancer l'IDE Arduino (Linux)

Pour fabriquer le paquet de flashage du T-Echo, l'IDE appelle l'outil **`adafruit-nrfutil`**. Sous Linux, il doit être installé **et visible de l'IDE avant de compiler**, sinon la compilation se termine par l'erreur `exec: "adafruit-nrfutil": executable file not found in $PATH` (voir [Dépannage](#dépannage)). Dans un terminal :

```bash
sudo apt install pipx        # seulement si pipx n'est pas déjà installé (Debian/Ubuntu)
pipx install adafruit-nrfutil
pipx ensurepath
sudo ln -s ~/.local/bin/adafruit-nrfutil /usr/local/bin/adafruit-nrfutil
```

| Commande | Rôle |
|---|---|
| `pipx install adafruit-nrfutil` | installe l'outil dans un environnement isolé (exécutable dans `~/.local/bin/`) |
| `pipx ensurepath` | ajoute `~/.local/bin` au `PATH`, **mais seulement pour les nouveaux terminaux** |
| `sudo ln -s …` | crée un lien global pour que l'IDE trouve l'outil même s'il ne reprend pas le `PATH` du terminal (IDE en Flatpak, Snap, AppImage, ou lancée depuis le menu des applications) |

Le lien est créé avec le **chemin explicite** `~/.local/bin/adafruit-nrfutil` : il fonctionne donc même si le terminal en cours n'a pas encore pris en compte `pipx ensurepath`. Si `ln` répond `File exists`, le lien est déjà en place.

Vérifiez ensuite :

```bash
adafruit-nrfutil --help
```

Si l'aide s'affiche, **lancez (ou relancez) l'IDE Arduino**. Si elle était déjà ouverte, fermez-la complètement d'abord.

> Sous Windows et macOS, l'outil est normalement fourni avec le paquet *Adafruit nRF52* et cette étape n'est pas nécessaire. Si l'erreur apparaît quand même, les mêmes commandes s'appliquent.

#### Installation et téléversement

1. Dans *Fichier → Préférences → URL de gestionnaire de cartes supplémentaires*, ajouter :
   ```
   https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
   ```
2. Dans le gestionnaire de cartes, installer **Adafruit nRF52 by Adafruit**, puis choisir la carte **Nordic nRF52840 DK (PCA10056)**.
3. Installer les bibliothèques : **GxEPD2**, **Adafruit GFX**, **Adafruit BusIO**.
4. Ouvrir `rssi-meshcore-repeater-techo/rssi-meshcore-repeater-techo.ino`.
5. **Double-cliquer sur le bouton reset** (en haut à gauche) pour passer en mode DFU : un lecteur USB apparaît. Téléverser ensuite normalement.

> **Alternative sans passer par le téléversement de l'IDE** : convertir le `.hex` compilé en fichier `.uf2` et le copier sur le T-Echo comme sur une clé USB. Voir la section suivante.

---

## Générer un fichier UF2 (T-Echo)

Le T-Echo embarque un bootloader Adafruit qui accepte le **glisser-déposer d'un fichier `.uf2`**. Vous pouvez donc flasher la carte sans l'IDE Arduino, et partager un firmware prêt à l'emploi. L'IDE produit un fichier `.hex` à la compilation : il suffit de le convertir.

> ⚠️ **Le préfixe du répéteur est compilé dans le firmware** (`TARGET_PUBKEY_PREFIX`). Un `.uf2` ne cible donc que le répéteur pour lequel il a été compilé : renseignez le bon préfixe *avant* de compiler (voir [Configuration](#configuration--cibler-votre-répéteur)).

### 1. Récupérer le fichier `.hex`

Compilez le sketch dans l'IDE Arduino (*Croquis → Vérifier/Compiler*), puis :

- soit **Croquis → Exporter les binaires compilés** (`Ctrl+Alt+S`) : les fichiers sont copiés dans le sous-dossier `build/<carte>/` du dossier du sketch ;
- soit récupérez-le dans le dossier temporaire de compilation. Sous Linux : `~/.cache/arduino/sketches/<code>/` (`ls -t ~/.cache/arduino/sketches/` : le plus récent est le vôtre). Avec une IDE installée en Flatpak : `~/.var/app/cc.arduino.IDE2/cache/arduino/sketches/<code>/`.

Le fichier se nomme `<nom-du-sketch>.ino.hex`, par exemple `rssi-meshcore-repeater-techo.ino.hex`. Seul le `.hex` est utile ici (le `.zip` sert à l'IDE pour son propre téléversement).

### 2. Installer l'outil de conversion (une seule fois)

```bash
# Récupérer les outils UF2 de Microsoft
git clone https://github.com/microsoft/uf2.git

# Copier le script de conversion dans votre dossier personnel
cp uf2/utils/uf2conv.py ~/

# Télécharger le fichier de familles de puces, à placer À CÔTÉ du script
wget -O ~/uf2families.json https://raw.githubusercontent.com/microsoft/uf2/master/utils/uf2families.json
```

Le fichier `uf2families.json` est **indispensable** : sans lui, le script s'arrête avec `FileNotFoundError: … uf2families.json`. Il doit se trouver dans le même dossier que `uf2conv.py`.

> Le dépôt cloné contient déjà les deux fichiers dans `uf2/utils/`. On peut donc aussi se passer de la copie et du `wget`, et appeler directement `python3 uf2/utils/uf2conv.py …`. Sans `wget`, `curl -o ~/uf2families.json <même URL>` fait la même chose.

### 3. Convertir le `.hex` en `.uf2`

Placez-vous dans le dossier qui contient le `.hex` (ou donnez son chemin complet), puis :

```bash
python3 ~/uf2conv.py rssi-meshcore-repeater-techo.ino.hex -c -f 0xADA52840 -o firmware.uf2
```

| Option | Rôle |
|---|---|
| `-c` | convertir seulement (ne pas tenter de flasher) |
| `-f 0xADA52840` | identifiant de famille de puce : **nRF52840** |
| `-o firmware.uf2` | nom du fichier produit |

Le script doit afficher quelque chose comme :

```
Converting to uf2, output size: …, start address: 0x26000
Wrote … bytes to firmware.uf2
```

L'adresse de départ `0x26000` est l'emplacement où le bootloader Adafruit attend l'application. **Si vous voyez une adresse très différente (par exemple `0x0`), ne flashez pas** : le fichier ne correspond pas à une application pour cette carte. Pour ce sketch, le `.uf2` fait de l'ordre de 200 Ko.

### 4. Flasher le T-Echo

1. Branchez le T-Echo en USB.
2. **Double-cliquez sur le bouton reset** (en haut à gauche) : un lecteur USB apparaît.
3. Copiez `firmware.uf2` dans ce lecteur (glisser-déposer).
4. Le lecteur disparaît et la carte redémarre seule. Un message d'erreur de copie à cet instant est normal.

Vous pouvez publier le `.uf2` dans les *Releases* de votre dépôt GitHub pour que d'autres puissent flasher sans compiler, en précisant à quel répéteur (préfixe) il est destiné.

---

## Configuration : cibler votre répéteur

### 1. Trouver le préfixe du répéteur

Il s'agit des **premiers octets de la clé publique** du répéteur, en hexadécimal. On les trouve dans l'application MeshCore, sur la fiche du répéteur (clé publique) ; les premiers octets de la clé forment le préfixe. Exemple : une clé commençant par `E0B6...` donne le préfixe `E0 B6`.

### 2. Le renseigner dans le sketch

Dans le fichier `.ino`, modifier la ligne :

```cpp
const uint8_t TARGET_PUBKEY_PREFIX[] = { 0xE0, 0xB6 };
```

Le premier octet est utilisé pour le ping et pour comparer les hash de chemin (souvent d'un seul octet). Le second octet ne sert qu'à départager les annonces, qui contiennent la clé complète.

> **Collisions possibles** : un hash d'un seul octet ne compte que 256 valeurs. Dans une zone dense, un autre *répéteur* peut partager le même premier octet et être pris pour la cible lorsqu'il retransmet un paquet flood. Si vous constatez des détections douteuses, mettez `ACCEPT_RELAYED_FLOOD` à `0` : seules les annonces (clé complète) et les réponses au ping seront alors retenues, sans aucune ambiguïté.

### 3. Vérifier les paramètres radio

Les valeurs par défaut correspondent au réseau MeshCore **Île-de-France** (preset « EU/UK narrow ») :

```cpp
#define LORA_FREQ_MHZ 869.618
#define LORA_BW_KHZ   62.5
#define LORA_SF       8
#define LORA_CR       8      // codage 4/8
```

**Ils doivent être identiques à ceux de votre réseau local**, sinon vous n'entendrez rien. Adaptez-les si vous êtes dans une autre région.

---

## Utilisation sur le terrain

1. **Brancher l'antenne**, puis alimenter la carte (USB ou batterie).
2. Au démarrage :
   - **Heltec** : `Initialisation…` puis `En attente de paquets MeshCore`.
   - **T-Echo** : l'écran affiche `Init radio / patientez...` (LED rouge allumée), puis la LED verte fait deux flashs et l'écran passe à `En attente de paquets MeshCore` avec le préfixe recherché. Si la radio n'est pas détectée, l'initialisation peut durer 10 à 20 s avant un message d'erreur (voir [Dépannage](#dépannage)).
3. Placez-vous dans une zone où vous pensez capter le répéteur (en hauteur, dégagé). Dès qu'un paquet du répéteur est reçu :
   - l'écran affiche le RSSI, le SNR et le temps écoulé ;
   - sur T-Echo, la LED bleue clignote ; sur Heltec, l'écran clignote (inversion).
4. **Pour forcer une réponse**, appuyez sur le bouton :
   - Heltec : bouton **PRG** ;
   - T-Echo : **bouton utilisateur** (pas le bouton reset/DFU). La LED rouge est allumée pendant l'émission.
5. Déplacez-vous et surveillez l'évolution du RSSI (voir la méthode ci-dessous).

Un délai minimal de 5 secondes est imposé entre deux pings pour ménager le duty cycle.

> **Réglementation** : en Europe, la sous-bande autour de 869,4–869,65 MHz est soumise à un duty cycle de 10 % et à une puissance maximale. N'abusez pas du bouton, et vérifiez les règles applicables chez vous.

---

## Lire l'écran

### Heltec V3 (OLED)

```
REPETEUR E0B6

     -87 dBm

SNR:7.5dB          12s
```

- **RSSI** en grand au centre (dBm) ;
- **SNR** en bas à gauche ;
- **temps écoulé** depuis le dernier paquet du répéteur en bas à droite ;
- rafraîchi **chaque seconde** ; l'écran clignote 3 fois à chaque nouveau paquet.

### T-Echo (e-paper)

L'écran affiche le titre `REPETEUR E0B6`, le **RSSI** en très gros, le **SNR**, puis le **temps écoulé** (`42 s`, `3 min 05 s`, `2 h 10 min`).

L'e-paper est lent et ne se rafraîchit pas en continu :

- mise à jour **immédiate** à chaque nouveau paquet du répéteur ;
- sinon, mise à jour du compteur **toutes les 10 secondes** ;
- rafraîchissement complet (anti-ghosting, ~2 s) tous les 30 rafraîchissements partiels.

Le **temps écoulé** est essentiel : un RSSI de `-85 dBm` vieux de 3 minutes ne dit rien de votre position actuelle.

**Signaux des LED du T-Echo**

| Signal | Signification |
|---|---|
| LED rouge allumée en continu au démarrage | initialisation de la radio en cours |
| 2 flashs verts | radio initialisée, écoute démarrée |
| LED rouge qui clignote + `Erreur LoRa` à l'écran | échec de l'initialisation de la radio (le code d'erreur est aussi répété sur le port série) |
| 2 flashs bleus | paquet du répéteur cible détecté |
| LED rouge pendant quelques instants | émission d'un ping |

---

## Méthode pour localiser le répéteur

### Repères de lecture

Les valeurs indiquées sont indicatives, elles dépendent des antennes et de l'environnement.

| RSSI | Interprétation |
|---|---|
| supérieur à −80 dBm | très proche ou vue directe dégagée |
| −80 à −105 dBm | portée confortable |
| −105 à −120 dBm | signal faible, en limite de portée |
| SNR inférieur à ≈ −10 dB | le SF8 ne décode plus : les paquets disparaissent |

### Technique 1 : « chaud / froid » (rapide)

1. Repérez une zone où le répéteur est reçu.
2. Avancez dans une direction pendant quelques dizaines de mètres en surveillant le RSSI **et le temps écoulé**.
3. Si le RSSI monte, continuez ; s'il baisse, changez de cap.
4. Répétez jusqu'à atteindre le maximum : le répéteur est à proximité immédiate (souvent en hauteur : toit, mât, pylône, château d'eau).

### Technique 2 : mesures à plusieurs points (plus rigoureux)

1. Choisissez **au moins 3 points de mesure** répartis autour de la zone supposée, éloignés les uns des autres.
2. À chaque point, restez immobile ~1 minute, **notez plusieurs RSSI** (le signal fluctue : faites une moyenne) et relevez la position GPS avec votre téléphone.
3. Reportez sur une carte : les points avec les RSSI les plus forts sont les plus proches. Le répéteur se trouve dans le secteur où les « cercles » de proximité se recoupent.
4. Affinez en refaisant des mesures dans ce secteur.

### Technique 3 : radiogoniométrie avec antenne directive

Avec une antenne Yagi branchée sur le Heltec :

1. En un point donné, **tournez lentement l'antenne** et repérez la direction qui donne le RSSI maximal : c'est un **relèvement** vers le répéteur.
2. Notez ce cap (boussole du téléphone).
3. Recommencez depuis un **second point** éloigné (idéalement à ~90° du premier). L'intersection des deux relèvements donne l'emplacement estimé.
4. Un troisième relèvement valide le résultat.

Astuce : votre **corps** atténue le signal. Tenir la carte contre soi, puis la tourner en vous tournant, permet aussi de trouver un maximum grossier sans antenne directive.

### Conseils pour de meilleures mesures

- **Ne vous fiez pas à un seul paquet** : moyennez plusieurs mesures.
- Tenez la carte **à la même hauteur et dans la même orientation** d'un point à l'autre.
- Préférez des **points dégagés** : un bâtiment, un arbre ou une voiture faussent beaucoup les mesures.
- Le RSSI ne suit pas une belle courbe en ville (réflexions, masquage) : il sert à **comparer** des positions entre elles, pas à mesurer une distance exacte. En espace libre, on perd en théorie environ 6 dB à chaque doublement de la distance.
- Restez **respectueux** : un répéteur est souvent sur une propriété privée ou sur un site sensible. La géolocalisation sert à comprendre la couverture, pas à s'introduire quelque part.

---

## Différences entre les deux cartes

| | Heltec WiFi LoRa 32 V3 | LilyGO T-Echo |
|---|---|---|
| MCU | ESP32-S3 | nRF52840 |
| Radio | SX1262 | SX1262 |
| Écran | OLED SSD1306 (128×64) | e-paper 1,54" (200×200, GDEH0154D67) |
| Rafraîchissement écran | chaque seconde | 10 s (ou immédiat sur nouveau paquet) |
| Bouton de ping | PRG (GPIO 0) | bouton utilisateur (P1.10) |
| Retour visuel | clignotement de l'écran | LED bleue (réception), LED rouge (émission) |
| Alimentation périphériques | broche Vext (GPIO 36, niveau bas) | deux broches, P0.12 et P0.13 (niveau haut) |
| Bus SPI | un seul (`SPI`) | deux : écran (`NRF_SPIM2`) et radio (`NRF_SPIM3`) |
| Aléatoire | `esp_random()` | `random()` initialisé avec l'ID de la puce |
| Bibliothèque écran | Adafruit SSD1306 | GxEPD2 |
| TCXO / commutateur d'antenne | valeurs par défaut de RadioLib | TCXO 1,8 V et DIO2 comme commutateur RF |
| Antenne | connecteur, externe possible | intégrée au boîtier |
| Température d'utilisation de l'écran | non critique | écran donné pour 0 °C à 50 °C, plus lent par grand froid |
| Autonomie | courte (écran allumé en continu) | longue (e-paper) |

### Brochage

**Heltec V3**

| Fonction | GPIO |
|---|---|
| LoRa CS / SCK / MOSI / MISO | 8 / 9 / 10 / 11 |
| LoRa RST / BUSY / DIO1 | 12 / 13 / 14 |
| OLED SDA / SCL / RST | 17 / 18 / 21 |
| Vext (alimentation OLED) | 36 |
| Bouton PRG | 0 |

**T-Echo** (numérotation Arduino : P0.x = x, P1.x = 32 + x)

| Fonction | Broche |
|---|---|
| LoRa CS / SCK / MOSI / MISO | P0.24 / P0.19 / P0.22 / P0.23 |
| LoRa RST / BUSY / DIO1 | P0.25 / P0.17 / P0.20 |
| e-paper CS / DC / RST / BUSY | P0.30 / P0.28 / P0.02 / P0.03 |
| e-paper SCK / MOSI | P0.31 / P0.29 |
| Alimentation périphériques | P0.12 et P0.13 (niveau haut, les deux sont nécessaires) |
| LED verte / rouge / bleue | P1.01 / P1.03 / P0.14 (actives niveau bas) |
| Bouton utilisateur | P1.10 (actif niveau bas) |

---

## Paramètres modifiables

Tous sont en tête de sketch (`#define`).

| Paramètre | Rôle | Sketch |
|---|---|---|
| `TARGET_PUBKEY_PREFIX` | préfixe du répéteur à chercher | les deux |
| `LORA_FREQ_MHZ`, `LORA_BW_KHZ`, `LORA_SF`, `LORA_CR` | paramètres radio du réseau | les deux |
| `LORA_TX_POWER` | puissance d'émission du ping (dBm) | les deux |
| `ACCEPT_RELAYED_FLOOD` | `1` : retient aussi les paquets flood retransmis par le répéteur (plus de mises à jour, risque de confusion entre répéteurs de même premier octet). `0` : uniquement annonces et réponses au ping, sans ambiguïté | les deux |
| `TRACE_REPLY_TIMEOUT_MS` | fenêtre d'acceptation de la réponse au ping | les deux |
| `DISPLAY_REFRESH_MS` | période de rafraîchissement du compteur | T-Echo |
| `FULL_REFRESH_EVERY` | fréquence du rafraîchissement complet | T-Echo |
| `BUTTON_COOLDOWN_MS` | délai minimal entre deux pings | T-Echo |
| `EPD_ROTATION` | orientation de l'écran (3 ou 1) | T-Echo |

---

## Limites à connaître

- Le RSSI affiché est celui du **dernier paquet retenu** seulement ; il varie d'un paquet à l'autre.
- Seuls les paquets **décodés correctement** (CRC valide) sont pris en compte : dans une zone très faible, on peut être à portée sans rien afficher.
- Pour les paquets flood retransmis, la reconnaissance par **hash d'un octet** peut confondre deux répéteurs qui commencent par le même octet (désactivable avec `ACCEPT_RELAYED_FLOOD 0`).
- Les paquets en **route directe** et les messages de groupe ne sont jamais retenus, car leur émetteur n'est pas identifiable. La détection est donc plus rare qu'avec « tout accepter », mais ce qui est affiché vient bien du répéteur.
- Le RSSI **ne donne pas une distance précise**, uniquement une tendance.
- Le **ping TRACE** est expérimental (voir plus haut) et sollicite le répéteur : à utiliser avec modération.
- Le sketch ne s'intéresse qu'à **un seul répéteur** à la fois.

---

## Dépannage

| Symptôme | Piste |
|---|---|
| `exec: "adafruit-nrfutil": executable file not found in $PATH` à la compilation (T-Echo) | Le sketch a bien compilé : c'est l'étape finale de création du paquet de flashage qui échoue. Installer l'outil et le rendre visible de l'IDE : voir [Avant de lancer l'IDE Arduino](#avant-de-lancer-lide-arduino-linux), puis relancer l'IDE. |
| Rien ne s'affiche (Heltec) | Vérifier la broche Vext et l'adresse I2C de l'OLED (0x3C). |
| Écran à l'envers (T-Echo) | Passer `EPD_ROTATION` à 1. |
| `Erreur LoRa` au démarrage | Radio non alimentée ou mal initialisée. Vérifier le brochage, et sur T-Echo les broches d'alimentation P0.12 et P0.13 ainsi que le TCXO (1,8 V). Code `-2` : radio non détectée (alimentation, brochage). |
| T-Echo bloqué sur « Init radio » (ou « Initialisation ») | Si la radio n'est pas détectée, RadioLib réessaie une dizaine de fois : l'erreur peut n'apparaître qu'après 10 à 20 s. Attendre 30 s : un message `Erreur LoRa` avec son code doit s'afficher, et la LED rouge clignote. Deux flashs verts signalent au contraire une radio initialisée. Le code est aussi répété chaque seconde sur le port série. |
| `FileNotFoundError: … uf2families.json` lors de la conversion UF2 | Le fichier `uf2families.json` n'est pas dans le même dossier que `uf2conv.py`. Le télécharger (voir [Générer un fichier UF2](#générer-un-fichier-uf2-t-echo)) ou lancer le script depuis `uf2/utils/`. |
| Adresse de départ UF2 très basse (`0x0`) | Ne pas flasher : vérifier qu'il s'agit bien du `.hex` du sketch (et non d'un autre fichier), puis reconvertir. |
| Le lecteur USB du T-Echo n'apparaît pas | Double-cliquer plus rapidement sur le bouton reset (en haut à gauche), et vérifier le câble USB (certains câbles ne transmettent pas les données). |
| Aucun paquet reçu | Vérifier fréquence/BW/SF/CR (doivent être ceux de votre réseau), la présence de l'antenne, et que du trafic existe autour de vous. |
| Paquets reçus, mais jamais « REPETEUR CIBLE » | Préfixe erroné, ou répéteur hors de portée directe. Ouvrir le moniteur série (115200 bauds) pour voir les paquets passer. |
| Détections douteuses | Un autre répéteur au même premier octet : passer `ACCEPT_RELAYED_FLOOD` à `0`. |
| Détections trop rares | Normal si le répéteur n'a pas de trafic à relayer et annonce peu souvent : appuyer sur le bouton pour envoyer un ping, ou laisser `ACCEPT_RELAYED_FLOOD` à `1`. |
| Pas de réponse au ping | Firmware ou configuration du répéteur ; se rabattre sur l'écoute passive. |

**Si l'IDE Arduino ne trouve pas `adafruit-nrfutil` alors qu'il est installé** (la commande fonctionne dans un terminal mais pas dans l'IDE : c'est un problème de `PATH`) :

- vérifier son emplacement : `ls ~/.local/bin/adafruit-nrfutil` (installation par `pipx` ou `pip --user`) ;
- créer le lien global **avec le chemin explicite**, puis **redémarrer l'IDE** :
  ```bash
  sudo ln -s ~/.local/bin/adafruit-nrfutil /usr/local/bin/adafruit-nrfutil
  ```
- si `which adafruit-nrfutil` ne renvoie **rien** alors que l'outil est installé, c'est que le terminal a été ouvert avant `pipx ensurepath` : ouvrir un nouveau terminal, ou utiliser le chemin explicite ci-dessus (un `ln -s "$(which …)"` échouerait avec « impossible de créer le lien symbolique … → '' ») ;
- sans `pipx` : `pip3 install --user adafruit-nrfutil`. Avec un Python récent, `pip` peut répondre `externally-managed-environment` : utiliser alors `pipx` (voir plus haut), ou ajouter `--break-system-packages` ;
- une IDE installée en Flatpak, Snap ou AppImage n'hérite pas toujours du `PATH` du terminal : le lien global règle le problème, ou lancer l'IDE depuis un terminal.

Le **moniteur série (115200 bauds)** affiche chaque paquet reçu (longueur, RSSI, SNR). Lorsque la détection réussit, la ligne se termine par `[REPETEUR CIBLE : annonce]`, `[REPETEUR CIBLE : relais flood]` ou `[REPETEUR CIBLE : reponse TRACE]` selon la raison ; les autres paquets sont marqués `(ignore)`. Au démarrage, il affiche aussi `Initialisation LoRa...` puis `LoRa OK` (ou l'erreur et son code). C'est le meilleur outil de diagnostic.

---

## Structure du dépôt

```
.
├── README.md
├── rssi-meshcore-repeater/
│   └── rssi-meshcore-repeater.ino          # Heltec WiFi LoRa 32 V3
└── rssi-meshcore-repeater-techo/
    └── rssi-meshcore-repeater-techo.ino    # LilyGO T-Echo
```

L'IDE Arduino exige que chaque sketch soit dans un dossier du même nom que son fichier `.ino`. Les firmwares `.uf2` compilés (voir [Générer un fichier UF2](#générer-un-fichier-uf2-t-echo)) peuvent être publiés dans les *Releases* GitHub plutôt que versionnés dans le dépôt.

---

## Historique des changements

**Correctif : faux positifs sur des paquets qui ne viennent pas du répéteur.** La fonction `packetComesFromTarget()` d'origine retenait des paquets dont l'émetteur n'était pas le répéteur, et affichait leur RSSI. Deux causes :

1. Pour un paquet **en route directe**, elle lisait le dernier hash du chemin comme « dernier répéteur traversé ». Or en route directe le chemin est la liste des sauts *restant à faire* : ce hash est la destination, pas l'émetteur.
2. Pour un paquet sans saut, elle comparait le **premier octet du payload** au répéteur en le prenant pour le hash de l'émetteur. C'est en réalité le hash du *destinataire* (requêtes, messages privés), le hash du canal (messages de groupe) ou un morceau de CRC (ACK). Toute requête *adressée* au répéteur, par exemple depuis un smartphone, était donc prise pour un paquet du répéteur.

Désormais, seuls sont retenus les paquets dont l'émetteur est établi (annonce, réponse TRACE, relais flood), voir [Quels paquets sont retenus ?](#quels-paquets-sont-retenus-). Le moniteur série indique la raison de chaque détection. Le sketch T-Echo reprend exactement la même logique.

**Correctif T-Echo : radio non initialisée, écran bloqué sur « Initialisation ».** Trois changements, d'après un exemple T-Echo qui fonctionne :

1. **Deux broches d'alimentation** : la radio nécessite P0.12 **et** P0.13 à l'état haut (le premier sketch n'activait que P0.12, ce qui suffisait à l'écran mais pas à la radio). P0.13 est déduite d'exemples LilyGO : à ajuster si votre révision de carte diffère.
2. **Attribution des bus SPI** : écran sur `NRF_SPIM2`, radio sur `NRF_SPIM3`, avec démarrage explicite du SPI de la radio avant `lora.begin()`.
3. **Diagnostic au démarrage** : message `Init radio`, LED rouge pendant l'initialisation, deux flashs verts en cas de succès, et en cas d'échec un `Erreur LoRa` avec son code, répété chaque seconde sur le port série. RadioLib peut mettre 10 à 20 s à signaler une radio absente : l'écran ne fige donc plus sans explication.

**Ajout : génération d'un fichier UF2** à partir du `.hex` de l'IDE, pour flasher le T-Echo par glisser-déposer (voir [Générer un fichier UF2](#générer-un-fichier-uf2-t-echo)).

---

## Crédits

- Sketch d'origine (Heltec V3) : [TutoDuino](https://tutoduino.fr/).
- Portage T-Echo : adaptation du brochage, de la radio (TCXO, DIO2) et de l'affichage e-paper.
- Format des paquets : documentation du projet [MeshCore](https://github.com/meshcore-dev/MeshCore).
- Brochage et documentation du T-Echo : dépôt [Xinyuan-LilyGO/T-Echo](https://github.com/Xinyuan-LilyGO/T-Echo). L'attribution des bus SPI et l'alimentation ont été recoupées avec l'exemple [LoRa-TEcho-APRSTracker](https://github.com/F4AVI/LoRa-TEcho-APRSTracker) (F4AVI).
- Bibliothèques : [RadioLib](https://github.com/jgromes/RadioLib), [GxEPD2](https://github.com/ZinggJM/GxEPD2), [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306).
- Conversion `.hex` → `.uf2` : [microsoft/uf2](https://github.com/microsoft/uf2) (`uf2conv.py`).
- Flashage sous Linux : [adafruit-nrfutil](https://github.com/adafruit/Adafruit_nRF52_nrfutil).

## Licence

À définir par le propriétaire du dépôt.
