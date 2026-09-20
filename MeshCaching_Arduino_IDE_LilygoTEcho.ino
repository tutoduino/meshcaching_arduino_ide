/**
 * @file    MeshCaching_Arduino_IDE_LilygoTEcho.ino
 * @brief   Geolocalisation d'un repeteur Meshcore (version LilyGO T-Echo)
 *
 * @details Affiche le RSSI (niveau de signal) et le temps ecoule depuis la
 *          derniere reception d'un paquet provenant d'un repeteur Meshcore
 *          specifique, sur un LilyGO T-Echo (nRF52840 + SX1262 + e-paper 1.54").
 *
 *          Portage du sketch original TutoDuino pour Heltec WiFi LoRa 32 V3.
 *
 * Carte Arduino : "Nordic nRF52840 DK (PCA10056)" (paquet Adafruit nRF52)
 * Bibliotheques : RadioLib, GxEPD2 (+ Adafruit GFX, Adafruit BusIO)
 *
 * @author  TutoDuino (portage T-Echo)
 * @see     https://tutoduino.fr/
 */

#include <SPI.h>
#include <math.h>
#include <stdarg.h>
#include <RadioLib.h>
#include <GxEPD2_BW.h>

// =====================================================================
// 1) BROCHAGE — LilyGO T-Echo (nRF52840 + SX1262 + e-paper GDEH0154D67)
// =====================================================================
// Numerotation Arduino : P0.x = x, P1.x = 32 + x
#define PINNUM(port, pin) ((port) * 32 + (pin))

// --- Radio SX1262 ---
#define LORA_MISO_PIN PINNUM(0, 23)
#define LORA_MOSI_PIN PINNUM(0, 22)
#define LORA_SCK_PIN PINNUM(0, 19)
#define LORA_CS_PIN PINNUM(0, 24)
#define LORA_RST_PIN PINNUM(0, 25)
#define LORA_BUSY_PIN PINNUM(0, 17)
#define LORA_DIO1_PIN PINNUM(0, 20)

// --- Ecran e-paper 200x200 (driver SSD1681) ---
// Attention : le tableau du README LilyGO inverse les libelles MOSI/MISO.
// La sortie de donnees du MCU vers l'ecran est bien P0.29.
#define EPD_MOSI_PIN PINNUM(0, 29)
#define EPD_SCK_PIN PINNUM(0, 31)
#define EPD_CS_PIN PINNUM(0, 30)
#define EPD_DC_PIN PINNUM(0, 28)
#define EPD_RST_PIN PINNUM(0, 2)
#define EPD_BUSY_PIN PINNUM(0, 3)
#define EPD_MISO_PIN PINNUM(1, 6)  // non utilise par l'ecran, mais requis par le constructeur SPIClass

#define SCREEN_WIDTH 200
#define SCREEN_HEIGHT 200
#define EPD_ROTATION 3  // passer a 1 si l'affichage apparait a l'envers

// --- Alimentation des peripheriques (equivalent du "Vext" du Heltec, actif a l'etat HAUT) ---
#define VEXT_PIN PINNUM(0, 12)

// --- LED (actives a l'etat BAS sur le T-Echo) ---
#define LED_GREEN_PIN PINNUM(1, 1)
#define LED_RED_PIN PINNUM(1, 3)
#define LED_BLUE_PIN PINNUM(0, 14)
#define LED_LEVEL_ON LOW
#define LED_LEVEL_OFF HIGH

// --- Bouton utilisateur (P1.10, actif a l'etat BAS) : declenche un ping ---
#define BUTTON_PIN PINNUM(1, 10)
#define BUTTON_COOLDOWN_MS 5000UL  // delai mini entre deux pings (duty cycle)

// =====================================================================
// 2) PARAMETRES RADIO — Meshcore Ile de France
// =====================================================================
#define LORA_FREQ_MHZ 869.618
#define LORA_BW_KHZ 62.5
#define LORA_SF 8
#define LORA_CR 8
#define LORA_TX_POWER 14
#define LORA_PREAMBLE_LEN 16       // preambule utilise par MeshCore
#define LORA_TCXO_VOLTAGE 1.8      // le T-Echo a un TCXO alimente par DIO3 en 1.8 V

// !! A ADAPTER : prefixe de la cle publique du repeteur MeshCore vise.
//const uint8_t TARGET_PUBKEY_PREFIX[] = { 0xC6, 0xF1 };
const uint8_t TARGET_PUBKEY_PREFIX[] = { 0xE0, 0xB6 };
//const uint8_t TARGET_PUBKEY_PREFIX[] = { 0x57, 0xDB };
#define TARGET_PUBKEY_PREFIX_LEN (sizeof(TARGET_PUBKEY_PREFIX))

// Detection des paquets FLOOD retransmis par le repeteur (identifies par le
// dernier hash du chemin, d'un seul octet en general).
//   1 = actif  : mises a jour frequentes, mais un AUTRE repeteur dont la cle
//                publique commence par le meme octet peut etre confondu
//                avec la cible (1 chance sur 256).
//   0 = inactif : on ne retient que les ANNONCES du repeteur (cle publique
//                complete, aucune ambiguite) et les reponses a notre ping.
//                Detection plus rare, mais sans aucune fausse alerte.
#define ACCEPT_RELAYED_FLOOD 1

// =====================================================================
// 3) FORMAT DE PAQUET MESHCORE (doc officielle meshcore-dev/MeshCore)
//    header (1 octet) = VV PPPP RR  (version / payload type / route type)
// =====================================================================
#define ROUTE_TYPE_TRANSPORT_FLOOD 0x00
#define ROUTE_TYPE_FLOOD 0x01
#define ROUTE_TYPE_DIRECT 0x02
#define ROUTE_TYPE_TRANSPORT_DIRECT 0x03

#define PAYLOAD_TYPE_ADVERT 0x04
#define PAYLOAD_TYPE_TRACE 0x09  // "trace un chemin" : c'est le ping natif de MeshCore

#define MAX_PACKET_LEN 256
#define ADVERT_PUBKEY_LEN 32  // une annonce contient la cle publique complete (32 octets)

// =====================================================================
// 4) ETAT GLOBAL DE L'APPLICATION
// =====================================================================
struct RepeaterStatus {
  bool hasPacket = false;  // avons-nous deja recu un paquet du repeteur cible ?
  unsigned long lastSeenMs = 0;
  float rssi = 0;
  float snr = 0;
};

RepeaterStatus targetStatus;

// Tag de la derniere requete TRACE envoyee, et instant d'envoi : permet de
// reconnaitre la reponse (qui renvoie ce meme tag).
uint32_t lastSentTag = 0;
unsigned long lastPingMs = 0;
#define TRACE_REPLY_TIMEOUT_MS 10000UL  // on n'accepte une reponse que dans les 10s suivant le ping

// --- Parametres de rafraichissement de l'e-paper ---
// L'e-paper est lent (~0.3 s en rafraichissement partiel, ~2 s en complet) et
// s'use si on le rafraichit trop souvent : on ne met PAS l'ecran a jour chaque
// seconde comme sur l'OLED, mais seulement toutes les DISPLAY_REFRESH_MS, et
// immediatement a chaque paquet recu du repeteur cible.
#define DISPLAY_REFRESH_MS 10000UL
#define FULL_REFRESH_EVERY 30  // un rafraichissement complet (anti-ghosting) tous les N partiels

// Deux bus SPI distincts : un pour la radio, un pour l'ecran.
SPIClass loraSPI(NRF_SPIM2, LORA_MISO_PIN, LORA_SCK_PIN, LORA_MOSI_PIN);
SPIClass epdSPI(NRF_SPIM3, EPD_MISO_PIN, EPD_SCK_PIN, EPD_MOSI_PIN);

SX1262 lora = new Module(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, loraSPI);
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));

// Ce drapeau est mis a "true" par l'interruption radio des qu'un paquet arrive.
// On le traite ensuite dans loop(), jamais dans l'interruption elle-meme.
volatile bool packetReceived = false;
void onPacketReceivedISR() {
  packetReceived = true;
}

// =====================================================================
// 5) OUTILS : LOG SERIE, FORMATAGE, LED
// =====================================================================
// Petit printf pour le port serie (evite toute dependance a Serial.printf).
// Note : pas de %f ici, le support des flottants dans printf est optionnel
// sur nRF52 ; on formate les flottants a la main (voir fmtSnr).
void serialPrintf(const char *fmt, ...) {
  char b[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(b, sizeof(b), fmt, args);
  va_end(args);
  Serial.print(b);
}

// SNR avec une decimale, sans passer par %f
void fmtSnr(char *out, size_t n, float snr) {
  int t = (int)lroundf(snr * 10.0f);
  snprintf(out, n, "%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
}

// Duree ecoulee lisible : "42 s", "3 min 05 s", "2 h 10 min"
void fmtElapsed(char *out, size_t n, unsigned long s) {
  if (s < 60) {
    snprintf(out, n, "%lu s", s);
  } else if (s < 3600) {
    snprintf(out, n, "%lu min %02lu s", s / 60, s % 60);
  } else {
    snprintf(out, n, "%lu h %02lu min", s / 3600, (s % 3600) / 60);
  }
}

void ledSet(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LED_LEVEL_ON : LED_LEVEL_OFF);
}

void blinkLed(uint8_t pin, int times, unsigned long ms) {
  for (int i = 0; i < times; i++) {
    ledSet(pin, true);
    delay(ms);
    ledSet(pin, false);
    delay(ms);
  }
}

// =====================================================================
// 6) AFFICHAGE E-PAPER
// =====================================================================
// Ecrit un texte centre horizontalement, coin haut-gauche a la ligne y.
void drawCentered(const char *txt, int16_t y, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  int16_t x = ((int16_t)SCREEN_WIDTH - (int16_t)w) / 2 - x1;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(txt);
}

// Message simple sur deux lignes (rafraichissement complet)
void showMessage(const char *ligne1, const char *ligne2) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    drawCentered(ligne1, 80, 2);
    drawCentered(ligne2, 104, 2);
  } while (display.nextPage());
}

void drawStatusScreen(bool fullRefresh) {
  char title[24];
  snprintf(title, sizeof(title), "REPETEUR %02X%02X", TARGET_PUBKEY_PREFIX[0], TARGET_PUBKEY_PREFIX[1]);

  if (fullRefresh) {
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  }

  // Prepare les textes AVANT la boucle de dessin (evite de les recalculer par page)
  char rssiStr[12], snrStr[12], snrLine[24], elapsedStr[24];
  if (targetStatus.hasPacket) {
    snprintf(rssiStr, sizeof(rssiStr), "%d", (int)lroundf(targetStatus.rssi));
    fmtSnr(snrStr, sizeof(snrStr), targetStatus.snr);
    snprintf(snrLine, sizeof(snrLine), "SNR %s dB", snrStr);
    fmtElapsed(elapsedStr, sizeof(elapsedStr), (millis() - targetStatus.lastSeenMs) / 1000);
  }

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    if (!targetStatus.hasPacket) {
      drawCentered("En attente de", 60, 2);
      drawCentered("paquets MeshCore", 84, 2);
      drawCentered(title, 130, 2);
    } else {
      // --- Titre : identifiant du repeteur surveille ---
      drawCentered(title, 4, 2);
      display.drawFastHLine(0, 26, SCREEN_WIDTH, GxEPD_BLACK);

      // --- RSSI en tres grand, centre ---
      drawCentered(rssiStr, 40, 6);
      drawCentered("dBm", 96, 2);
      display.drawFastHLine(0, 120, SCREEN_WIDTH, GxEPD_BLACK);

      // --- SNR ---
      drawCentered(snrLine, 128, 2);

      // --- Temps ecoule depuis le dernier paquet ---
      drawCentered("dernier paquet il y a", 152, 1);
      drawCentered(elapsedStr, 166, 3);
    }
  } while (display.nextPage());
}

// Gere l'alternance rafraichissements partiels / complet
void refreshDisplay(bool forceFull) {
  static uint8_t partialCount = 0;
  bool full = forceFull || partialCount >= FULL_REFRESH_EVERY;
  partialCount = full ? 0 : partialCount + 1;
  drawStatusScreen(full);
}

// =====================================================================
// 7) INITIALISATION MATERIELLE
// =====================================================================
void initBoard() {
  // Alimentation des peripheriques (radio, ecran...) : actif a l'etat haut
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, HIGH);

  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  ledSet(LED_GREEN_PIN, false);
  ledSet(LED_RED_PIN, false);
  ledSet(LED_BLUE_PIN, false);

  pinMode(BUTTON_PIN, INPUT_PULLUP);  // relie a la masse quand presse

  delay(150);  // laisse l'alimentation et le TCXO se stabiliser
}

void initDisplay() {
  Serial.println(F("Initialisation e-paper..."));
  display.epd2.selectSPI(epdSPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(0);
  display.setRotation(EPD_ROTATION);
  display.setTextWrap(false);
  display.setTextColor(GxEPD_BLACK);
}

void initRadio() {
  Serial.println(F("Initialisation LoRa..."));

  // Le bus SPI (loraSPI) est demarre par RadioLib dans begin().
  int state = lora.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                         RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER,
                         LORA_PREAMBLE_LEN, LORA_TCXO_VOLTAGE);
  if (state != RADIOLIB_ERR_NONE) {
    serialPrintf("Erreur LoRa : %d\n", state);
    char code[16];
    snprintf(code, sizeof(code), "code %d", state);
    showMessage("Erreur LoRa", code);
    while (true) { delay(1000); }
  }

  // Le commutateur d'antenne du T-Echo est pilote par la broche DIO2 du SX1262
  lora.setDio2AsRfSwitch(true);

  lora.setOutputPower(LORA_TX_POWER);
  lora.setDio1Action(onPacketReceivedISR);

  state = lora.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    serialPrintf("Erreur startReceive : %d\n", state);
  }
}

// =====================================================================
// 8) ENVOI D'UN "PING" (paquet TRACE) VERS LE REPETEUR CIBLE
// =====================================================================
// MeshCore a une fonctionnalite native pour ca : le payload TRACE (0x09),
// concu pour tracer un chemin et collecter le SNR de chaque saut. Avec un
// seul noeud dans la liste, c'est l'equivalent MeshCore d'un ping.
// Format : [tag 4o][auth_code 4o][flags 1o][liste de hash a tracer].
//
// Ici on vise un seul repeteur, en "zero-hop" (path_len=0 au niveau du
// paquet) : le paquet est emis une seule fois, sans etre relaye plus loin.
void sendTracePing() {
  uint8_t buf[16];
  size_t offset = 0;

  // --- Header du paquet : payload TRACE + route DIRECT ---
  buf[offset++] = (PAYLOAD_TYPE_TRACE << 2) | ROUTE_TYPE_DIRECT;

  // --- path_length du paquet (niveau routage) : 0 = zero-hop ---
  buf[offset++] = 0x00;

  // --- Payload TRACE ---
  // Tag aleatoire non nul (0 signifie "aucun ping envoye" dans ce programme)
  uint32_t tag;
  do {
    tag = ((uint32_t)random(0x10000) << 16) | (uint32_t)random(0x10000);
  } while (tag == 0);
  memcpy(buf + offset, &tag, sizeof(tag));
  offset += sizeof(tag);

  lastSentTag = tag;
  lastPingMs = millis();

  uint32_t authCode = 0;  // pas de code d'authentification particulier
  memcpy(buf + offset, &authCode, sizeof(authCode));
  offset += sizeof(authCode);

  buf[offset++] = 0x00;  // flags, reserve pour l'instant

  // Liste des noeuds a tracer : un seul, le repeteur cible
  buf[offset++] = TARGET_PUBKEY_PREFIX[0];

  serialPrintf("Envoi TRACE (tag=%08lX) vers REPETEUR %02X...\n",
               (unsigned long)tag, TARGET_PUBKEY_PREFIX[0]);

  ledSet(LED_RED_PIN, true);  // LED rouge pendant l'emission
  int state = lora.transmit(buf, offset);
  ledSet(LED_RED_PIN, false);

  if (state != RADIOLIB_ERR_NONE) {
    serialPrintf("Erreur d'emission : %d\n", state);
  }

  // transmit() declenche aussi une interruption DIO1 de "fin d'emission",
  // qui peut avoir arme packetReceived a tort : on l'ignore avant de
  // repasser en ecoute.
  packetReceived = false;
  lora.startReceive();
}

// =====================================================================
// 9) DECODAGE DU PAQUET MESHCORE
// =====================================================================
// Determine si le paquet a ete EMIS par le repeteur cible : c'est SON signal
// dont on veut afficher le RSSI. On ne retient donc un paquet que si son
// emetteur est identifiable avec certitude. Il n'y a que trois cas :
//
//  1. Reponse a notre ping TRACE : reconnue par son tag aleatoire.
//  2. ANNONCE (advert) sans saut : le payload commence par la cle publique
//     complete de l'emetteur, qui est forcement celui qui vient d'emettre.
//  3. Paquet FLOOD retransmis (au moins un saut) : chaque repeteur ajoute son
//     hash a la fin du chemin avant de retransmettre, donc le DERNIER hash est
//     le dernier repeteur qui a emis le paquet. (Voir ACCEPT_RELAYED_FLOOD.)
//
// Tout le reste est REFUSE, car l'emetteur n'est pas identifiable :
//  - Paquets en route DIRECT : le chemin contient les sauts qu'il RESTE a
//    faire (path[0] = prochain saut, dernier = destination finale), pas
//    l'emetteur. Un paquet destine "via" le repeteur ne vient pas de lui.
//  - Premier octet du payload : ce n'est PAS le hash de l'emetteur. Pour
//    REQ / RESPONSE / TXT_MSG / PATH c'est le hash du DESTINATAIRE (l'emetteur
//    vient ensuite), pour GRP_TXT le hash du canal, pour ACK un bout de CRC.
//    Un message ADRESSE au repeteur (ex. requete d'un smartphone) etait donc
//    pris a tort pour un paquet emis par lui.
//  - Messages de groupe, ACK, etc. : l'emetteur n'y figure pas du tout.
//
// Rappel du format d'un paquet MeshCore :
//   [header 1 octet] [transport_codes 0 ou 4 octets] [path_length 1 octet]
//   [path 0-64 octets] [payload 0-184 octets]
//
// "why" (optionnel) recoit la raison de la detection, pour le moniteur serie.
bool packetComesFromTarget(const uint8_t *packet, size_t len, const char **why) {
  if (len < 2) return false;

  uint8_t header = packet[0];
  uint8_t routeType = header & 0x03;
  uint8_t payloadType = (header >> 2) & 0x0F;

  size_t offset = 1;

  // Les modes "transport" ajoutent 4 octets de code de transport
  if (routeType == ROUTE_TYPE_TRANSPORT_FLOOD || routeType == ROUTE_TYPE_TRANSPORT_DIRECT) {
    offset += 4;
  }
  if (offset >= len) return false;

  // Octet path_length : bits 0-5 = nombre de sauts, bits 6-7 = (taille du hash - 1)
  uint8_t pathLengthByte = packet[offset];
  uint8_t hopCount = pathLengthByte & 0x3F;
  uint8_t hashSize = ((pathLengthByte >> 6) & 0x03) + 1;
  offset += 1;

  const uint8_t *pathBytes = packet + offset;
  offset += (size_t)hopCount * hashSize;
  if (offset > len) return false;  // paquet incoherent, on l'ignore

  const uint8_t *payload = packet + offset;
  size_t payloadLen = len - offset;

  // --- Cas 1 : reponse a notre TRACE ---
  // Un paquet TRACE ne contient PAS de hash en tete de payload : il commence
  // par un tag aleatoire de 4 octets. On compare son tag a celui de notre
  // dernier ping envoye.
  if (payloadType == PAYLOAD_TYPE_TRACE) {
    if (payloadLen < 4 || lastSentTag == 0) return false;
    if (millis() - lastPingMs > TRACE_REPLY_TIMEOUT_MS) return false;

    uint32_t receivedTag;
    memcpy(&receivedTag, payload, sizeof(receivedTag));
    if (receivedTag != lastSentTag) return false;
    if (why) *why = "reponse TRACE";
    return true;
  }

  // --- Cas 2 : annonce sans saut (peu importe la route : une annonce "zero-hop"
  //     est emise avec un chemin vide, et personne ne l'a retransmise) ---
  if (payloadType == PAYLOAD_TYPE_ADVERT && hopCount == 0) {
    if (payloadLen < ADVERT_PUBKEY_LEN) return false;
    // Comparaison sur TOUT le prefixe configure (cle complete cote paquet)
    if (memcmp(payload, TARGET_PUBKEY_PREFIX, TARGET_PUBKEY_PREFIX_LEN) != 0) return false;
    if (why) *why = "annonce";
    return true;
  }

  // --- Cas 3 : paquet FLOOD retransmis, dernier hash du chemin = dernier emetteur ---
#if ACCEPT_RELAYED_FLOOD
  bool isFlood = (routeType == ROUTE_TYPE_FLOOD || routeType == ROUTE_TYPE_TRANSPORT_FLOOD);
  if (isFlood && hopCount >= 1) {
    const uint8_t *lastHopId = pathBytes + (size_t)(hopCount - 1) * hashSize;
    size_t compareLen = min((size_t)hashSize, (size_t)TARGET_PUBKEY_PREFIX_LEN);
    if (memcmp(lastHopId, TARGET_PUBKEY_PREFIX, compareLen) != 0) return false;
    if (why) *why = "relais flood";
    return true;
  }
#endif

  // Tout le reste : emetteur non identifiable, on ne retient pas.
  return false;
}

// =====================================================================
// 10) TRAITEMENT D'UN PAQUET RECU
// =====================================================================
void handleIncomingPacket() {
  uint8_t buf[MAX_PACKET_LEN];
  size_t len = lora.getPacketLength();
  if (len == 0 || len > sizeof(buf)) {
    lora.startReceive();
    return;
  }

  int state = lora.readData(buf, len);
  float rssi = lora.getRSSI();
  float snr = lora.getSNR();

  // On ignore les paquets illisibles OU dont le CRC est invalide :
  // un paquet corrompu ne doit jamais etre interprete comme venant du
  // repeteur cible (risque de fausse detection).
  if (state != RADIOLIB_ERR_NONE) {
    if (state != RADIOLIB_ERR_CRC_MISMATCH) {
      serialPrintf("Erreur de reception : %d\n", state);
    }
    lora.startReceive();
    return;
  }

  const char *why = "";
  bool isTarget = packetComesFromTarget(buf, len, &why);

  char snrStr[12];
  fmtSnr(snrStr, sizeof(snrStr), snr);
  if (isTarget) {
    serialPrintf("Paquet recu : len=%u RSSI=%d dBm SNR=%s dB [REPETEUR CIBLE : %s]\n",
                 (unsigned)len, (int)lroundf(rssi), snrStr, why);
  } else {
    serialPrintf("Paquet recu : len=%u RSSI=%d dBm SNR=%s dB (ignore)\n",
                 (unsigned)len, (int)lroundf(rssi), snrStr);
  }

  // On remet la radio en ecoute AVANT le rafraichissement (lent) de l'e-paper,
  // pour ne pas rater les paquets suivants pendant que l'ecran se met a jour.
  lora.startReceive();

  if (isTarget) {
    targetStatus.hasPacket = true;
    targetStatus.lastSeenMs = millis();
    targetStatus.rssi = rssi;
    targetStatus.snr = snr;

    blinkLed(LED_BLUE_PIN, 2, 80);  // retour visuel immediat (l'e-paper est lent)
    refreshDisplay(false);          // mise a jour de l'ecran
  }
}

// =====================================================================
// 11) SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);  // attend le port USB max 2 s (fonctionne aussi sur batterie)

  initBoard();

  // Graine du generateur aleatoire (identifiant unique de la puce + horloge)
  randomSeed(NRF_FICR->DEVICEID[0] ^ NRF_FICR->DEVICEID[1] ^ micros());

  initDisplay();
  showMessage("Initialisation", "...");

  initRadio();

  Serial.println(F("En attente de paquets MeshCore..."));
  drawStatusScreen(true);  // ecran d'attente (rafraichissement complet)
}

void loop() {
  // Rafraichit l'ecran periodiquement (pour le compteur "temps ecoule"),
  // uniquement si un paquet du repeteur cible a deja ete vu.
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate >= DISPLAY_REFRESH_MS) {
    lastDisplayUpdate = millis();
    if (targetStatus.hasPacket) {
      refreshDisplay(false);
    }
  }

  // Appui sur le bouton utilisateur : on force un ping TRACE vers le repeteur
  // cible, au lieu d'attendre passivement son prochain paquet.
  // Temporisation entre deux envois pour respecter le duty cycle.
  static unsigned long lastButtonPress = 0;
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > BUTTON_COOLDOWN_MS) {
    lastButtonPress = millis();
    sendTracePing();
  }

  if (packetReceived) {
    packetReceived = false;
    handleIncomingPacket();
  }
}
