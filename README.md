# Géolocaliser un répéteur MeshCore avec le RSSI

Deux sketches Arduino qui transforment une carte LoRa en **chercheur de répéteur MeshCore** : elle écoute le réseau, repère les paquets émis par *un* répéteur précis, et affiche en temps réel la puissance du signal reçu (RSSI), le rapport signal/bruit (SNR) et le temps écoulé depuis le dernier paquet. En vous déplaçant, vous suivez le RSSI comme un jeu du « chaud / froid » pour retrouver l'emplacement physique du répéteur.

| Carte | Sketch | Écran | Microcontrôleur |
|---|---|---|---|
| **Heltec WiFi LoRa 32 V3** | [MeshCaching_Arduino_IDE_HeltecV3.ino] | OLED SSD1306 128×64 | ESP32-S3 |
| **LilyGO T-Echo** | [MeshCaching_Arduino_IDE_LilygoTEcho.ino] | e-paper 1,54" 200×200 | nRF52840 |

Les deux sketches partagent la même logique (décodage des paquets MeshCore, comparaison avec le répéteur cible). Seuls le brochage, l'écran et quelques appels spécifiques au microcontrôleur diffèrent.


## Principe de fonctionnement

Un réseau MeshCore est constitué de nœuds (companions) et de **répéteurs** qui retransmettent les paquets. Chaque nœud est identifié par une clé publique de 32 octets ; dans les paquets en transit, il est représenté par un **hash** : le ou les premiers octets de cette clé.

Le sketch :

1. **écoute en continu** sur la fréquence et les paramètres LoRa du réseau ;
2. **décode l'en-tête MeshCore** de chaque paquet reçu (type de route, chemin parcouru, type de payload) ;
3. identifie **le dernier nœud qui a retransmis le paquet** (dernier hash du chemin, ou émetteur lui-même pour une annonce sans saut) ;
4. si ce nœud correspond au **préfixe de clé publique du répéteur cible**, le paquet est retenu : on affiche son RSSI et son SNR, et on remet le compteur de temps à zéro.

Autrement dit, chaque paquet retenu prouve que le répéteur cible **vient d'émettre à portée directe de votre carte**, et le RSSI vous dit à quel point vous êtes près.

Les paquets au CRC invalide sont ignorés, pour éviter les fausses détections.

### Le « ping » (bouton)

Attendre passivement peut être long : un répéteur n'émet que lorsqu'il relaie du trafic ou envoie ses annonces périodiques. Un appui sur le bouton envoie donc un paquet **TRACE** (le mécanisme natif de MeshCore pour tracer un chemin) en *zéro saut* vers le répéteur. Le sketch mémorise le tag aléatoire de la requête et ne retient la réponse que si elle revient avec le même tag, dans les 10 secondes.

---

## Ce qu'il vous faut

- Une carte **Heltec WiFi LoRa 32 V3** ou un **LilyGO T-Echo** (bande 868 MHz pour l'Europe).
- **Son antenne LoRa, toujours branchée avant de mettre sous tension** : émettre sans antenne peut endommager l'étage radio.
- Un câble USB-C pour le flashage.
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

1. Dans *Fichier → Préférences → URL de gestionnaire de cartes supplémentaires*, ajouter :
   ```
   https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
   ```
2. Dans le gestionnaire de cartes, installer **Adafruit nRF52 by Adafruit**, puis choisir la carte **Nordic nRF52840 DK (PCA10056)**.
3. Installer les bibliothèques : **GxEPD2**, **Adafruit GFX**, **Adafruit BusIO**.
4. Ouvrir `rssi-meshcore-repeater-techo/rssi-meshcore-repeater-techo.ino`.
5. **Double-cliquer sur le bouton reset** (en haut à gauche) pour passer en mode DFU : un lecteur USB apparaît. Téléverser ensuite normalement.

---

## Configuration : cibler votre répéteur

### 1. Trouver le préfixe du répéteur

Il s'agit des **premiers octets de la clé publique** du répéteur, en hexadécimal. On les trouve dans l'application MeshCore, sur la fiche du répéteur (clé publique) ; les premiers octets de la clé forment le préfixe. Exemple : une clé commençant par `E0B6...` donne le préfixe `E0 B6`.

### 2. Le renseigner dans le sketch

Dans le fichier `.ino`, modifier la ligne :

```cpp
const uint8_t TARGET_PUBKEY_PREFIX[] = { 0xE0, 0xB6 };
```
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
2. L'écran affiche `En attente de paquets MeshCore` et le préfixe recherché.
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
| Alimentation périphériques | broche Vext (GPIO 36, niveau bas) | broche P0.12 (niveau haut) |
| Bus SPI | un seul (`SPI`) | deux : radio (`NRF_SPIM2`) et écran (`NRF_SPIM3`) |
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
| Alimentation périphériques | P0.12 (niveau haut) |
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
| `TRACE_REPLY_TIMEOUT_MS` | fenêtre d'acceptation de la réponse au ping | les deux |
| `DISPLAY_REFRESH_MS` | période de rafraîchissement du compteur | T-Echo |
| `FULL_REFRESH_EVERY` | fréquence du rafraîchissement complet | T-Echo |
| `BUTTON_COOLDOWN_MS` | délai minimal entre deux pings | T-Echo |
| `EPD_ROTATION` | orientation de l'écran (3 ou 1) | T-Echo |

---

## Limites à connaître

- Le RSSI affiché est celui du **dernier paquet retenu** seulement ; il varie d'un paquet à l'autre.
- Seuls les paquets **décodés correctement** (CRC valide) sont pris en compte : dans une zone très faible, on peut être à portée sans rien afficher.
- La reconnaissance par **hash d'un octet** peut produire de fausses détections en zone dense.
- Le RSSI **ne donne pas une distance précise**, uniquement une tendance.
- Le **ping TRACE** est expérimental (voir plus haut) et sollicite le répéteur : à utiliser avec modération.
- Le sketch ne s'intéresse qu'à **un seul répéteur** à la fois.

---

## Dépannage

| Symptôme | Piste |
|---|---|
| Rien ne s'affiche (Heltec) | Vérifier la broche Vext et l'adresse I2C de l'OLED (0x3C). |
| Écran à l'envers (T-Echo) | Passer `EPD_ROTATION` à 1. |
| `Erreur LoRa` au démarrage | Radio non alimentée ou mal initialisée. Vérifier le brochage, et sur T-Echo la broche P0.12 (alimentation) et le TCXO (1,8 V). |
| Aucun paquet reçu | Vérifier fréquence/BW/SF/CR (doivent être ceux de votre réseau), la présence de l'antenne, et que du trafic existe autour de vous. |
| Paquets reçus, mais jamais « REPETEUR CIBLE » | Préfixe erroné, ou répéteur hors de portée directe. Ouvrir le moniteur série (115200 bauds) pour voir les paquets passer. |
| Fausses détections | Collision de hash d'un octet : se fier au SNR/RSSI cohérents dans la durée, ou aux annonces. |
| Pas de réponse au ping | Firmware ou configuration du répéteur ; se rabattre sur l'écoute passive. |

Le **moniteur série (115200 bauds)** affiche chaque paquet reçu (longueur, RSSI, SNR) et marque `[REPETEUR CIBLE]` lorsque la détection réussit. C'est le meilleur outil de diagnostic.

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

L'IDE Arduino exige que chaque sketch soit dans un dossier du même nom que son fichier `.ino`.

---

## Crédits

- [TutoDuino](https://tutoduino.fr/).

## Licence
MIT
