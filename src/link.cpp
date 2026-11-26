#include "link.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include "config.h"

namespace {
enum : uint8_t { P_HELLO = 1, P_TEAM, P_COMMIT, P_REVEAL, P_BYE };
enum : uint8_t { F_TEAM = 1, F_PREV = 2 };   // flags
const uint16_t MAGIC = 0xBB02;               // bump when the packet changes
const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct __attribute__((packed)) Pkt {
  uint16_t magic;
  uint8_t  type, flags;
  uint32_t from, to;        // session ids (to = 0: "haven't seen anyone yet")
  uint32_t seed, nonce32;   // sender's buddy seed, shared-seed contribution
  uint8_t  night, level;
  uint16_t turn;
  uint8_t  move;            // REVEAL: the throw. COMMIT: previous turn's throw
  uint8_t  nonce[8];        // ...and its nonce
  uint8_t  hash[16];        // COMMIT: hash of this turn's throw
};
struct Rx { uint8_t mac[6]; Pkt p; };

QueueHandle_t rxq = nullptr;
bool nowReady = false;
LinkState state = LINK_OFF;
uint32_t myId = 0, peerId = 0;
uint8_t peerMac[6];
bool peerLeft = false, cheat = false;
uint32_t lastHeard = 0, lastSend = 0;
Pkt out;
bool outBroadcast = true;

// Buddies.
bool teamSent = false, teamGot = false;
uint32_t mySeed = 0, myNonce32 = 0, pSeed = 0, pNonce32 = 0;
uint8_t myNight = 0, myLevel = 1, pNight = 0, pLevel = 1;

// Turns.
bool committed = false;
uint16_t myTurn = 0;
uint8_t myMove = 0, myNonce[8];
bool revealed = false;                 // my reveal for myTurn is out
bool havePrev = false;                 // my previous turn's reveal (rides on COMMITs)
uint16_t prevTurn = 0;
uint8_t prevMove = 0, prevNonce[8];
bool commitHave[2] = {false, false};   // friend's commits, slot = turn & 1
uint16_t commitTurn[2];
uint8_t commitHash[2][16];
bool revealHave = false;               // friend's reveal for revealTurn
uint16_t revealTurn = 0;
uint8_t revealMove = 0;

void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (!rxq || len != (int)sizeof(Pkt)) return;
  Rx r;
  memcpy(r.mac, mac, 6);
  memcpy(&r.p, data, sizeof(Pkt));
  if (r.p.magic != MAGIC) return;
  xQueueSend(rxq, &r, 0);
}

void hashThrow(uint32_t sender, uint16_t turn, uint8_t move, const uint8_t* nonce, uint8_t* out16) {
  uint8_t buf[15], d[32];
  memcpy(buf, &sender, 4);
  memcpy(buf + 4, &turn, 2);
  buf[6] = move;
  memcpy(buf + 7, nonce, 8);
  mbedtls_sha256_ret(buf, sizeof(buf), d, 0);
  memcpy(out16, d, 16);
}

void setChannel() {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(FRIEND_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t info{};
  memcpy(info.peer_addr, mac, 6);
  info.channel = 0;   // whatever channel we're on (FRIEND_CHANNEL)
  info.ifidx = WIFI_IF_STA;
  info.encrypt = false;
  esp_now_add_peer(&info);
}

Pkt blank(uint8_t type) {
  Pkt p{};
  p.magic = MAGIC;
  p.type = type;
  p.from = myId;
  p.to = peerId;
  if (teamSent) {
    p.flags |= F_TEAM;
    p.seed = mySeed; p.nonce32 = myNonce32; p.night = myNight; p.level = myLevel;
  }
  return p;
}

void sendOut() {
  if (outBroadcast) esp_now_send(BCAST, (const uint8_t*)&out, sizeof(out));
  else              esp_now_send(peerMac, (const uint8_t*)&out, sizeof(out));
  lastSend = millis();
}

void setOut(const Pkt& p, bool broadcast) { out = p; outBroadcast = broadcast; sendOut(); }

void sendPrevRevealAgain(uint16_t turn) {   // friend is behind: repeat an old reveal once
  if (!havePrev || prevTurn != turn) return;
  Pkt p = blank(P_REVEAL);
  p.turn = prevTurn; p.move = prevMove; memcpy(p.nonce, prevNonce, 8);
  esp_now_send(peerMac, (const uint8_t*)&p, sizeof(p));
}

void gotReveal(uint16_t turn, uint8_t move, const uint8_t* nonce) {
  if (revealHave && turn <= revealTurn) return;   // already have it (or something newer)
  int s = turn & 1;
  if (commitHave[s] && commitTurn[s] == turn) {
    uint8_t h[16];
    hashThrow(peerId, turn, move, nonce, h);
    if (memcmp(h, commitHash[s], 16) != 0) { cheat = true; state = LINK_LOST; return; }
  }
  revealHave = true;
  revealTurn = turn;
  revealMove = move;
}

void handle(const Rx& r) {
  const Pkt& p = r.p;
  if (p.from == myId) return;

  if (state == LINK_SEARCHING) {
    // Lock on to the first board we hear, then wait until it has heard us too.
    if (peerId != 0 && p.from != peerId) return;
    if (peerId == 0) {
      if (p.type != P_HELLO) return;
      peerId = p.from;
      memcpy(peerMac, r.mac, 6);
      addPeer(peerMac);
      setOut(blank(P_HELLO), true);   // now says "to = you"
    }
    if (p.to != myId) return;
    state = LINK_PAIRED;
    lastHeard = millis();
    setOut(blank(P_HELLO), false);
    // fall through: this packet may already carry a buddy or a throw
  }
  if (state != LINK_PAIRED || p.from != peerId || p.to != myId) return;
  lastHeard = millis();

  if (p.flags & F_TEAM) {
    teamGot = true;
    pSeed = p.seed; pNonce32 = p.nonce32; pNight = p.night; pLevel = p.level;
  }
  switch (p.type) {
    case P_BYE:
      peerLeft = true;
      state = LINK_LOST;
      return;
    case P_COMMIT: {
      int s = p.turn & 1;
      commitHave[s] = true;
      commitTurn[s] = p.turn;
      memcpy(commitHash[s], p.hash, 16);
      if (p.flags & F_PREV) gotReveal(p.turn - 1, p.move, p.nonce);
      if (committed && p.turn < myTurn) sendPrevRevealAgain(p.turn);
      return;
    }
    case P_REVEAL:
      gotReveal(p.turn, p.move, p.nonce);
      if (committed && p.turn < myTurn) sendPrevRevealAgain(p.turn);
      return;
    default:
      return;
  }
}
}  // namespace

void linkStart() {
  if (!rxq) rxq = xQueueCreate(12, sizeof(Rx));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  setChannel();
  if (!nowReady) {
    nowReady = esp_now_init() == ESP_OK;
    if (nowReady) esp_now_register_recv_cb(onRecv);
  }
  addPeer(BCAST);
  xQueueReset(rxq);
  do { myId = esp_random(); } while (myId == 0);
  peerId = 0;
  peerLeft = cheat = false;
  teamSent = teamGot = false;
  committed = revealed = havePrev = revealHave = false;
  commitHave[0] = commitHave[1] = false;
  state = nowReady ? LINK_SEARCHING : LINK_LOST;
  if (nowReady) setOut(blank(P_HELLO), true);
}

void linkStop(bool sayBye) {
  if (state == LINK_PAIRED && sayBye) {
    Pkt p = blank(P_BYE);
    for (int i = 0; i < 3; i++) { esp_now_send(peerMac, (const uint8_t*)&p, sizeof(p)); delay(15); }
  }
  if (peerId && esp_now_is_peer_exist(peerMac)) esp_now_del_peer(peerMac);
  state = LINK_OFF;
}

void linkPoll() {
  if (state == LINK_OFF) return;
  Rx r;
  while (rxq && xQueueReceive(rxq, &r, 0) == pdTRUE) handle(r);
  if (state == LINK_LOST) return;
  uint32_t now = millis();
  if (state == LINK_PAIRED && now - lastHeard > LINK_TIMEOUT_MS) { state = LINK_LOST; return; }

  // Both commits in for this turn: reveal mine.
  int s = myTurn & 1;
  if (state == LINK_PAIRED && committed && !revealed && commitHave[s] && commitTurn[s] == myTurn) {
    Pkt p = blank(P_REVEAL);
    p.turn = myTurn; p.move = myMove; memcpy(p.nonce, myNonce, 8);
    revealed = true;
    setOut(p, false);
  }
  if (now - lastSend > LINK_RESEND_MS) sendOut();
}

LinkState linkState()   { return state; }
bool linkPeerLeft()     { return peerLeft; }
bool linkCheated()      { return cheat; }

void linkSendTeam(uint32_t seed, bool night, uint8_t level) {
  mySeed = seed; myNight = night; myLevel = level;
  myNonce32 = esp_random();
  teamSent = true;
  committed = revealed = havePrev = revealHave = false;
  commitHave[0] = commitHave[1] = false;
  setOut(blank(P_TEAM), false);
}

bool     linkHaveTeam()   { return teamGot; }
uint32_t linkPeerSeed()   { return pSeed; }
bool     linkPeerNight()  { return pNight; }
uint8_t  linkPeerLevel()  { return pLevel; }
uint32_t linkBattleSeed() { return myNonce32 ^ pNonce32; }

void linkCommit(uint16_t turn, uint8_t move) {
  if (committed && revealed && turn == myTurn + 1) {   // last turn's reveal rides along
    havePrev = true; prevTurn = myTurn; prevMove = myMove; memcpy(prevNonce, myNonce, 8);
  }
  myTurn = turn;
  myMove = move;
  esp_fill_random(myNonce, sizeof(myNonce));
  committed = true;
  revealed = false;
  Pkt p = blank(P_COMMIT);
  p.turn = turn;
  hashThrow(myId, turn, move, myNonce, p.hash);
  if (havePrev && prevTurn + 1 == turn) {
    p.flags |= F_PREV;
    p.move = prevMove;
    memcpy(p.nonce, prevNonce, 8);
  }
  setOut(p, false);
}

bool linkTurnReady(uint16_t turn, uint8_t& theirMove) {
  if (!committed || myTurn != turn || !revealHave || revealTurn != turn || cheat) return false;
  theirMove = revealMove;
  return true;
}
