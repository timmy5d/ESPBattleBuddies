#include "render.h"

namespace {

// Where the face and head sit for each body shape, in a 100-unit box centred
// on (0,0). top/bottom are the body's edges, halfW its half-width at the
// widest point and topW its half-width near the top.
struct BodyGeo { int8_t top, bottom, faceY, halfW, topW; };
const BodyGeo GEO[BODY_COUNT] = {
  {-24, 40,   2, 34, 20},   // round
  {-32, 42, -10, 34, 14},   // pear
  {-36, 40, -12, 22, 14},   // tall bean
  {-24, 38,   2, 34, 26},   // block
  {-20, 42,   2, 24, 14},   // blob
  {-28, 40,  -6, 26, 14},   // oval
};

struct Painter {
  TFT_eSPI& g;
  int cx, cy;
  float k;
  int X(float v) const { return cx + (int)lroundf(v * k); }
  int Y(float v) const { return cy + (int)lroundf(v * k); }
  int L(float v) const { int r = (int)lroundf(v * k); return r < 1 ? 1 : r; }

  void circle(float x, float y, float r, uint16_t c) { g.fillCircle(X(x), Y(y), L(r), c); }
  void ellipse(float x, float y, float rx, float ry, uint16_t c) { g.fillEllipse(X(x), Y(y), L(rx), L(ry), c); }
  void rrect(float x0, float y0, float x1, float y1, float r, uint16_t c) {
    g.fillRoundRect(X(x0), Y(y0), X(x1) - X(x0), Y(y1) - Y(y0), L(r), c);
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) {
    g.fillTriangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), c);
  }
  void line(float x0, float y0, float x1, float y1, float w, uint16_t c) {
    // Thick line drawn as a row of dots; fine at these sizes and works on sprites.
    int steps = max(abs(X(x1) - X(x0)), abs(Y(y1) - Y(y0))) + 1;
    int r = max(0, L(w) / 2);
    for (int i = 0; i <= steps; i++) {
      float t = (float)i / steps;
      int px = X(x0 + (x1 - x0) * t), py = Y(y0 + (y1 - y0) * t);
      if (r == 0) g.drawPixel(px, py, c); else g.fillCircle(px, py, r, c);
    }
  }
};

void drawBodyShape(Painter& p, uint8_t body, float grow, uint16_t c) {
  const float o = grow;
  switch (body) {
    case BODY_ROUND: p.ellipse(0, 8, 34 + o, 32 + o, c); break;
    case BODY_PEAR:  p.ellipse(0, 18, 34 + o, 24 + o, c); p.circle(0, -10, 22 + o, c); break;
    case BODY_TALL:  p.rrect(-22 - o, -36 - o, 22 + o, 40 + o, 20 + o, c); break;
    case BODY_BLOCK: p.rrect(-34 - o, -24 - o, 34 + o, 38 + o, 8 + o, c); break;
    case BODY_BLOB:  p.ellipse(0, 26, 40 + o, 16 + o, c); p.circle(0, 4, 24 + o, c); break;
    default:         p.ellipse(0, 6, 26 + o, 34 + o, c); break;
  }
}

void drawHead(Painter& p, const Monster& m, const BodyGeo& b, uint16_t outline) {
  const float t = b.top, w = b.topW;
  const uint16_t acc = m.accent, accOut = shade565(m.accent, 45);
  switch (m.head) {
    case HEAD_HORNS:
      for (int s = -1; s <= 1; s += 2) {
        p.tri(s * (w * 0.5f - 6), t + 8, s * (w * 0.5f + 8), t + 6, s * (w * 0.6f + 10), t - 20, accOut);
        p.tri(s * (w * 0.5f - 4), t + 8, s * (w * 0.5f + 6), t + 6, s * (w * 0.6f + 9), t - 16, acc);
      }
      break;
    case HEAD_ANTENNA:
      for (int s = -1; s <= 1; s += 2) {
        p.line(s * 7, t + 6, s * 14, t - 18, 3, outline);
        p.circle(s * 14, t - 20, 5.5f, accOut);
        p.circle(s * 14, t - 20, 4, acc);
      }
      break;
    case HEAD_EARS:
      for (int s = -1; s <= 1; s += 2) {
        p.tri(s * (w * 0.2f), t + 10, s * (w + 6), t + 10, s * (w * 0.8f + 2), t - 20, outline);
        p.tri(s * (w * 0.2f + 2), t + 10, s * (w + 3), t + 10, s * (w * 0.8f + 2), t - 16, m.color);
        p.tri(s * (w * 0.4f + 2), t + 8, s * (w * 0.9f), t + 8, s * (w * 0.8f + 1), t - 8, acc);
      }
      break;
    case HEAD_SPIKES:
      for (int i = -1; i <= 1; i++) {
        p.tri(i * 12 - 8, t + 8, i * 12 + 8, t + 8, i * 12, t - (i == 0 ? 18 : 12), accOut);
        p.tri(i * 12 - 6, t + 8, i * 12 + 6, t + 8, i * 12, t - (i == 0 ? 15 : 9), acc);
      }
      break;
    case HEAD_CREST:
      for (int i = -1; i <= 1; i++) {
        p.ellipse(i * 8, t - 6 - (i == 0 ? 4 : 0), 5, 12, accOut);
        p.ellipse(i * 8, t - 6 - (i == 0 ? 4 : 0), 3.5f, 10.5f, acc);
      }
      break;
    default: break;
  }
}

void drawEyes(Painter& p, const Monster& m, const BodyGeo& b, bool blink) {
  const uint16_t dark = rgb565(20, 16, 30), white = rgb565(250, 250, 245);
  const float r = m.eyeStyle == EYE_BIG ? 9 : m.eyeStyle == EYE_DOT ? 6 : 7.5f;
  const float spacing = m.eyes == 3 ? 16 : 20;
  for (int i = 0; i < m.eyes; i++) {
    float x = (i - (m.eyes - 1) / 2.0f) * spacing;
    float y = b.faceY - (m.eyes == 3 && i == 1 ? 4 : 0);
    if (blink) { p.line(x - r, y, x + r, y, 2.5f, dark); continue; }
    p.circle(x, y, r + 1.5f, dark);
    p.circle(x, y, r, white);
    p.circle(x + 1, y + 1, r * 0.5f, dark);
    if (m.eyeStyle == EYE_BIG) p.circle(x - r * 0.3f, y - r * 0.35f, r * 0.25f, white);
    if (m.eyeStyle == EYE_SLEEPY) {
      p.rrect(x - r - 2, y - r - 2, x + r + 2, y - 1, 1, m.color);
      p.line(x - r, y - 1, x + r, y - 1, 2, dark);
    }
    if (m.eyeStyle == EYE_ANGRY) {
      float s = (x <= 0) ? 1 : -1;   // brows slope down towards the middle
      if (m.eyes == 1) s = 1;
      p.tri(x - r - 2, y - r - 4 + (s > 0 ? -3 : 3), x + r + 2, y - r - 4 + (s > 0 ? 3 : -3), x, y - r + 1, dark);
    }
  }
}

void drawMouth(Painter& p, const Monster& m, const BodyGeo& b) {
  const uint16_t dark = rgb565(40, 16, 30), white = rgb565(250, 250, 245);
  const float y = b.faceY + (m.eyeStyle == EYE_BIG ? 18 : 15);
  switch (m.mouth) {
    case MOUTH_SMILE:
      p.circle(0, y - 2, 8, dark);
      p.circle(0, y - 6, 8.5f, m.pattern == PAT_BELLY ? shade565(m.color, 150) : m.color);
      break;
    case MOUTH_FANGS:
      p.rrect(-10, y - 4, 10, y + 4, 3, dark);
      p.tri(-8, y - 4, -3, y - 4, -5.5f, y + 2, white);
      p.tri(3, y - 4, 8, y - 4, 5.5f, y + 2, white);
      break;
    case MOUTH_O:
      p.ellipse(0, y, 4.5f, 5.5f, dark);
      break;
    case MOUTH_ZIGZAG:
      p.line(-10, y, -5, y + 4, 2.5f, dark);
      p.line(-5, y + 4, 0, y, 2.5f, dark);
      p.line(0, y, 5, y + 4, 2.5f, dark);
      p.line(5, y + 4, 10, y, 2.5f, dark);
      break;
    default: {  // beak
      uint16_t beak = rgb565(255, 175, 60);
      p.tri(-9, y - 5, 9, y - 5, 0, y + 7, shade565(beak, 60));
      p.tri(-7, y - 4, 7, y - 4, 0, y + 5, beak);
      break;
    }
  }
}

}  // namespace

void drawMonster(TFT_eSPI& g, const Monster& m, int cx, int cy, int size, bool blink) {
  Painter p{g, cx, cy, size / 100.0f};
  const BodyGeo& b = GEO[m.body];
  const uint16_t outline = shade565(m.color, 35);
  const uint16_t dark = shade565(m.color, 70);

  // Behind the body: wings, tail, head features.
  if (m.limbs == LIMB_WINGS) {
    for (int s = -1; s <= 1; s += 2) {
      p.tri(s * b.halfW * 0.5f, b.faceY + 12, s * (b.halfW + 24), b.faceY - 14, s * (b.halfW + 14), b.faceY + 30, shade565(m.accent, 45));
      p.tri(s * b.halfW * 0.5f, b.faceY + 12, s * (b.halfW + 20), b.faceY - 10, s * (b.halfW + 12), b.faceY + 26, m.accent);
    }
  }
  if (m.tail != TAIL_NONE) {
    float x0 = b.halfW * 0.6f, y0 = b.bottom - 10, x1 = b.halfW + 14, y1 = b.bottom - 28;
    p.line(x0, y0, x1, y1, 9, outline);
    p.line(x0, y0, x1, y1, 6, dark);
    if (m.tail == TAIL_FLAME) {
      p.tri(x1 - 7, y1 + 2, x1 + 7, y1 + 2, x1 + 2, y1 - 16, rgb565(250, 120, 30));
      p.tri(x1 - 4, y1 + 1, x1 + 4, y1 + 1, x1 + 1, y1 - 9, rgb565(255, 230, 90));
    }
  }
  drawHead(p, m, b, outline);

  // Body with an outline.
  drawBodyShape(p, m.body, 2.5f, outline);
  drawBodyShape(p, m.body, 0, m.color);

  // Markings.
  if (m.pattern == PAT_BELLY) {
    float h = b.bottom - b.faceY;
    p.ellipse(0, b.faceY + h * 0.62f, b.halfW * 0.55f, h * 0.36f, shade565(m.color, 150));
  } else if (m.pattern == PAT_SPOTS) {
    uint32_t s = m.seed ^ 0x5B075u;
    for (int i = 0; i < 4; i++) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      float x = ((int)(s % 100) - 50) / 100.0f * b.halfW * 1.1f;
      float y = b.faceY + 22 + (int)((s >> 8) % 100) / 100.0f * (b.bottom - b.faceY - 30);
      p.circle(x, y, 3 + (s >> 16) % 3, shade565(m.accent, 90));
    }
  } else if (m.pattern == PAT_STRIPES) {
    for (int i = -1; i <= 1; i++) p.ellipse(i * 7, b.top + 9, 2, 6, dark);
  }

  // Front limbs.
  if (m.limbs == LIMB_FEET) {
    for (int s = -1; s <= 1; s += 2) {
      p.ellipse(s * b.halfW * 0.5f, b.bottom, 11.5f, 7.5f, outline);
      p.ellipse(s * b.halfW * 0.5f, b.bottom, 10, 6, dark);
    }
  } else if (m.limbs == LIMB_ARMS) {
    for (int s = -1; s <= 1; s += 2) {
      p.ellipse(s * (b.halfW + 3), b.faceY + 22, 7.5f, 11.5f, outline);
      p.ellipse(s * (b.halfW + 3), b.faceY + 22, 6, 10, dark);
    }
  }

  drawEyes(p, m, b, blink);
  drawMouth(p, m, b);

  if (m.night) {  // little crescent moon mark for night forms
    p.circle(b.halfW * 0.45f, b.top + 12, 4, rgb565(240, 230, 160));
    p.circle(b.halfW * 0.45f + 2, b.top + 11, 3.5f, m.color);
  }
}
