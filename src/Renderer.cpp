#include "Renderer.h"
#include "Config.h"
#define TITLE_FONT &FreeSansBold12pt7b

TFT_eSPI& Renderer::tft() { return _tft; }

void Renderer::init(uint8_t brightness) {
  ledcSetup(BL_CHANNEL, BL_FREQ, BL_RES);
  ledcAttachPin(TFT_BL_PIN, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, brightness);
  _tft.init();
  _tft.setRotation(3);
  _tft.fillScreen(TFT_BLACK);
}

// Pixel-art sprite: 10 cols × 12 rows, each cell = 3×3 px → 30×36 px total
// 0=transparent, 1=body, 2=accent
static const uint8_t SPR[12][10] PROGMEM = {
  {0,1,0,0,0,0,0,0,1,0},   // antennae
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,1,1,1,1,1,1,0,0},   // head
  {0,1,1,1,1,1,1,1,1,0},
  {1,1,2,1,1,1,1,2,1,1},   // eyes
  {1,1,1,1,1,1,1,1,1,1},
  {0,1,2,1,1,1,1,2,1,0},   // smile
  {0,0,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,0},   // body
  {1,1,1,1,1,1,1,1,1,1},
  {0,0,1,1,0,0,1,1,0,0},   // legs
  {0,0,1,1,0,0,1,1,0,0},
};

static void formatReset(char* buf, size_t n, int minutes) {
  if (minutes >= 60) snprintf(buf, n, "Resets in %dh %dm", minutes / 60, minutes % 60);
  else               snprintf(buf, n, "Resets in %d min", minutes);
}

// ── Primitives ────────────────────────────────────────────────────────────────

uint16_t Renderer::progressColor(int pct) {
  if (pct < 50) return TFT_GREEN;
  if (pct < 80) return TFT_ORANGE;
  return TFT_RED;
}

void Renderer::drawProgressBar(int x, int y, int w, int h, int pct, uint16_t color) {
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  _tft.drawRect(x, y, w, h, TFT_DARKGREY);
  int fill = (w - 2) * pct / 100;
  _tft.fillRect(x + 1, y + 1, fill,               h - 2, color);
  _tft.fillRect(x + 1 + fill, y + 1, w - 2 - fill, h - 2, TFT_BLACK);
}

void Renderer::drawButton(int x, int y, int w, int h,
                           const char* label, uint16_t fill, uint16_t border, uint16_t fg) {
  _tft.fillRoundRect(x, y, w, h, 5, fill);
  _tft.drawRoundRect(x, y, w, h, 5, border);
  _tft.setTextColor(fg);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2, 2);
  _tft.setTextDatum(TL_DATUM);
}

void Renderer::drawPill(int x, int y, int w, int h, const char* label) {
  uint16_t fill   = _tft.color565(48, 48, 48);
  uint16_t border = _tft.color565(105, 105, 105);
  _tft.fillRoundRect(x, y, w, h, h / 2, fill);
  _tft.drawRoundRect(x, y, w, h, h / 2, border);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2, 2);
  _tft.setTextDatum(TL_DATUM);
}

// ── Status screens ────────────────────────────────────────────────────────────

void Renderer::showConnecting() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("Connecting...", 60, 100, 4);
}

void Renderer::showWifiFailed() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_RED);
  _tft.drawString("WiFi failed", 60, 90, 4);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString("Retrying in 30s...", 70, 130, 2);
}

void Renderer::showServerError() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_RED);
  _tft.drawString("Server error", 60, 90, 4);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString("Retrying...", 100, 130, 2);
}

void Renderer::showRebooting() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("Rebooting...", 60, 100, 4);
}

// ── Claude ────────────────────────────────────────────────────────────────────

void Renderer::drawClaude(const UsageData& d) {
  char buf[24];
  _tft.fillScreen(TFT_BLACK);

  // ── Header: sprite + title ─────────────────────────────────────────────────
  const uint16_t body = _tft.color565(210, 90, 42);
  const uint16_t dark = _tft.color565(130, 50, 15);
  for (int r = 0; r < 12; r++)
    for (int c = 0; c < 10; c++) {
      uint8_t v = pgm_read_byte(&SPR[r][c]);
      if (v) _tft.fillRect(5 + c * 3, 2 + r * 3, 3, 3, v == 1 ? body : dark);
    }

  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("Usage", 160, 19);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.drawFastHLine(0, 38, 320, TFT_DARKGREY);

  // ── Session ────────────────────────────────────────────────────────────────
  uint16_t cSession = progressColor(d.claudeSession);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
  _tft.setTextColor(cSession);
  _tft.drawString(buf, 10, 44, 4);

  drawPill(212, 44, 100, 26, "Session");

  drawProgressBar(10, 70, 300, 12, d.claudeSession, cSession);
  formatReset(buf, sizeof(buf), d.claudeReset);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString(buf, 10, 86, 2);

  _tft.drawFastHLine(0, 108, 320, TFT_DARKGREY);

  // ── Weekly ─────────────────────────────────────────────────────────────────
  uint16_t cWeekly = progressColor(d.claudeWeekly);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
  _tft.setTextColor(cWeekly);
  _tft.drawString(buf, 10, 114, 4);

  drawPill(212, 114, 100, 26, "Weekly");

  drawProgressBar(10, 140, 300, 12, d.claudeWeekly, cWeekly);

  _tft.drawFastHLine(0, 168, 320, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,  186, 142, 36, "< Settings", f, b, TFT_WHITE);
    drawButton(170, 186, 142, 36, "Grok >",    f, b, TFT_WHITE);
  }

  _prev.claudeSession = d.claudeSession;
  _prev.claudeWeekly  = d.claudeWeekly;
  _prev.claudeReset   = d.claudeReset;
}

void Renderer::updateClaude(const UsageData& d) {
  char buf[24];
  if (d.claudeSession != _prev.claudeSession) {
    uint16_t c = progressColor(d.claudeSession);
    _tft.fillRect(10, 44, 200, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
    _tft.drawString(buf, 10, 44, 4);
    drawProgressBar(10, 70, 300, 12, d.claudeSession, c);
    _prev.claudeSession = d.claudeSession;
  }
  if (d.claudeReset != _prev.claudeReset) {
    _tft.fillRect(10, 86, 260, 16, TFT_BLACK);
    _tft.setTextColor(TFT_DARKGREY);
    formatReset(buf, sizeof(buf), d.claudeReset);
    _tft.drawString(buf, 10, 86, 2);
    _prev.claudeReset = d.claudeReset;
  }
  if (d.claudeWeekly != _prev.claudeWeekly) {
    uint16_t c = progressColor(d.claudeWeekly);
    _tft.fillRect(10, 114, 200, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
    _tft.drawString(buf, 10, 114, 4);
    drawProgressBar(10, 140, 300, 12, d.claudeWeekly, c);
    _prev.claudeWeekly = d.claudeWeekly;
  }
}

// ── Grok ──────────────────────────────────────────────────────────────────────

void Renderer::drawGrok(const UsageData& d) {
  char buf[8];
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("GROK BUILD", 10, 8, 4);
  _tft.drawFastHLine(0, 38, 320, TFT_DARKGREY);

  uint16_t cTokens = progressColor(d.grokTokens);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Tokens", 10, 50, 2);
  _tft.setTextColor(cTokens);
  snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
  _tft.drawString(buf, 248, 44, 4);
  drawProgressBar(10, 70, 300, 14, d.grokTokens, cTokens);

  _tft.drawFastHLine(0, 98, 320, TFT_DARKGREY);

  uint16_t cReqs = progressColor(d.grokRequests);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Requests", 10, 110, 2);
  _tft.setTextColor(cReqs);
  snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
  _tft.drawString(buf, 248, 104, 4);
  drawProgressBar(10, 130, 300, 14, d.grokRequests, cReqs);

  _tft.drawFastHLine(0, 158, 320, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   181, 142, 36, "< Claude",   f, b, TFT_WHITE);
    drawButton(170, 181, 142, 36, "Settings >", f, b, TFT_WHITE);
  }

  _prev.grokTokens   = d.grokTokens;
  _prev.grokRequests = d.grokRequests;
}

void Renderer::updateGrok(const UsageData& d) {
  char buf[8];
  if (d.grokTokens != _prev.grokTokens) {
    uint16_t c = progressColor(d.grokTokens);
    _tft.fillRect(248, 44, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
    _tft.drawString(buf, 248, 44, 4);
    drawProgressBar(10, 70, 300, 14, d.grokTokens, c);
    _prev.grokTokens = d.grokTokens;
  }
  if (d.grokRequests != _prev.grokRequests) {
    uint16_t c = progressColor(d.grokRequests);
    _tft.fillRect(248, 104, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
    _tft.drawString(buf, 248, 104, 4);
    drawProgressBar(10, 130, 300, 14, d.grokRequests, c);
    _prev.grokRequests = d.grokRequests;
  }
}

// ── Settings ──────────────────────────────────────────────────────────────────

void Renderer::drawSettings(uint8_t brightness, unsigned long fetchInterval) {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextSize(1);
  _tft.drawString("SETTINGS", 10, 8, 4);
  _tft.drawFastHLine(0, 36, 320, TFT_DARKGREY);

  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Brightness", 10, 44, 2);
  _tft.drawRect(10, 58, 50, 32, TFT_DARKGREY);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("-", 22, 63, 4);
  _tft.drawRect(260, 58, 50, 32, TFT_DARKGREY);
  _tft.drawString("+", 272, 63, 4);
  _tft.drawRect(68, 58, 182, 32, TFT_DARKGREY);
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 59, bfill,               30, TFT_YELLOW);
  _tft.fillRect(69 + bfill, 59, 180 - bfill, 30, TFT_BLACK);

  _tft.drawFastHLine(0, 98, 320, TFT_DARKGREY);

  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Refresh", 10, 106, 2);
  const unsigned long intervals[3] = {30000, 60000, 120000};
  const char*         labels[3]    = {"30s", "60s", "120s"};
  const int           btnX[3]      = {10, 115, 220};
  for (int i = 0; i < 3; i++) {
    bool sel = (fetchInterval == intervals[i]);
    _tft.drawRect(btnX[i], 122, 95, 32, sel ? TFT_GREEN : TFT_DARKGREY);
    _tft.setTextColor(sel ? TFT_GREEN : TFT_WHITE);
    _tft.drawString(labels[i], btnX[i] + 22, 130, 2);
  }

  _tft.drawFastHLine(0, 162, 320, TFT_DARKGREY);
  _tft.drawRect(60, 175, 200, 32, TFT_RED);
  _tft.setTextColor(TFT_RED);
  _tft.drawString("REBOOT", 82, 180, 4);

  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   211, 142, 26, "< Grok",   f, b, TFT_WHITE);
    drawButton(170, 211, 142, 26, "Claude >", f, b, TFT_WHITE);
  }
}

// ── Public update API ─────────────────────────────────────────────────────────

void Renderer::switchTo(int screen, const UsageData& data,
                        uint8_t brightness, unsigned long fetchInterval) {
  if      (screen == 0) drawClaude(data);
  else if (screen == 1) drawGrok(data);
  else                  drawSettings(brightness, fetchInterval);
}

void Renderer::update(int screen, const UsageData& data, bool fullRedraw) {
  if (screen == 0) {
    if (fullRedraw) drawClaude(data); else updateClaude(data);
  } else if (screen == 1) {
    if (fullRedraw) drawGrok(data); else updateGrok(data);
  }
  // settings screen has no live data
}

void Renderer::updateBrightnessBar(uint8_t brightness) {
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 59, bfill,               30, TFT_YELLOW);
  _tft.fillRect(69 + bfill, 59, 180 - bfill, 30, TFT_BLACK);
}

void Renderer::updateIntervalButtons(unsigned long fetchInterval) {
  const unsigned long intervals[3] = {30000, 60000, 120000};
  const char*         labels[3]    = {"30s", "60s", "120s"};
  const int           btnX[3]      = {10, 115, 220};
  for (int i = 0; i < 3; i++) {
    bool sel = (fetchInterval == intervals[i]);
    _tft.fillRect(btnX[i] + 1, 123, 93, 30, TFT_BLACK);
    _tft.drawRect(btnX[i], 122, 95, 32, sel ? TFT_GREEN : TFT_DARKGREY);
    _tft.setTextColor(sel ? TFT_GREEN : TFT_WHITE);
    _tft.drawString(labels[i], btnX[i] + 22, 130, 2);
  }
}
