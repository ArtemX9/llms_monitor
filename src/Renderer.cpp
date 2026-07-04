#include "Renderer.h"
#include "Config.h"
#define TITLE_FONT &FreeSansBold12pt7b
#define VALUE_FONT &FreeSansBold18pt7b
#define LABEL_FONT &FreeSans9pt7b

TFT_eSPI& Renderer::tft() { return _tft; }

void Renderer::init(uint8_t brightness) {
  _tft.init();
  _tft.setRotation(3);
  // Touch calibration for this specific XPT2046 panel (ESP32-32E CYD board),
  // obtained via TFT_eSPI's calibrateTouch() — the factory default mapping
  // does not match this unit's raw ADC range.
  uint16_t calData[5] = {433, 3490, 314, 3448, 5};
  _tft.setTouch(calData);
  _tft.fillScreen(TFT_BLACK);
  ledcSetup(BL_CHANNEL, BL_FREQ, BL_RES);
  ledcAttachPin(TFT_BL_PIN, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, brightness);
}

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
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(fg);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);
}

void Renderer::drawPill(int x, int y, int w, int h, const char* label) {
  uint16_t fill   = _tft.color565(48, 48, 48);
  uint16_t border = _tft.color565(105, 105, 105);
  _tft.fillRoundRect(x, y, w, h, h / 2, fill);
  _tft.drawRoundRect(x, y, w, h, h / 2, border);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);
}

// ── Shadcn-style palette (Settings screen) ────────────────────────────────────

uint16_t Renderer::colorScreenBg()    { return _tft.color565(8, 8, 10); }
uint16_t Renderer::colorCardBg()      { return _tft.color565(24, 24, 27); }
uint16_t Renderer::colorCardBorder()  { return _tft.color565(63, 63, 70); }
uint16_t Renderer::colorAccent()      { return _tft.color565(99, 102, 241); }
uint16_t Renderer::colorLabel()       { return _tft.color565(140, 140, 145); }
uint16_t Renderer::colorDestructive() { return _tft.color565(220, 38, 38); }

void Renderer::drawCard(int x, int y, int w, int h) {
  _tft.fillRoundRect(x, y, w, h, 6, colorCardBg());
  _tft.drawRoundRect(x, y, w, h, 6, colorCardBorder());
}

void Renderer::drawOutlineCard(int x, int y, int w, int h,
                                uint16_t border, uint16_t fg, const char* label) {
  _tft.fillRoundRect(x, y, w, h, 6, colorCardBg());
  _tft.drawRoundRect(x, y, w, h, 6, border);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(fg);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString(label, x + w / 2, y + h / 2);
  _tft.setTextFont(0);
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
  _sprite.draw(_tft);

  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("Usage", 160, 22);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.drawFastHLine(0, 44, 320, TFT_DARKGREY);

  // ── Session ────────────────────────────────────────────────────────────────
  uint16_t cSession = progressColor(d.claudeSession);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cSession);
  _tft.drawString(buf, 10, 52);
  _tft.setTextFont(0);

  drawPill(206, 52, 104, 28, "Session");

  drawProgressBar(10, 86, 300, 14, d.claudeSession, cSession);
  formatReset(buf, sizeof(buf), d.claudeReset);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString(buf, 10, 104);
  _tft.setTextFont(0);

  _tft.drawFastHLine(0, 128, 320, TFT_DARKGREY);

  // ── Weekly ─────────────────────────────────────────────────────────────────
  uint16_t cWeekly = progressColor(d.claudeWeekly);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(cWeekly);
  _tft.drawString(buf, 10, 136);
  _tft.setTextFont(0);

  drawPill(206, 136, 104, 28, "Weekly");

  drawProgressBar(10, 170, 300, 14, d.claudeWeekly, cWeekly);

  _tft.drawFastHLine(0, 198, 320, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,  206, 142, 30, "< Settings", f, b, TFT_WHITE);
    drawButton(170, 206, 142, 30, "Grok >",    f, b, TFT_WHITE);
  }

  _prev.claudeSession = d.claudeSession;
  _prev.claudeWeekly  = d.claudeWeekly;
  _prev.claudeReset   = d.claudeReset;
}

void Renderer::updateClaude(const UsageData& d) {
  char buf[24];
  if (d.claudeSession != _prev.claudeSession) {
    uint16_t c = progressColor(d.claudeSession);
    _tft.fillRect(10, 52, 190, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
    _tft.drawString(buf, 10, 52);
    _tft.setTextFont(0);
    drawProgressBar(10, 86, 300, 14, d.claudeSession, c);
    _prev.claudeSession = d.claudeSession;
  }
  if (d.claudeReset != _prev.claudeReset) {
    _tft.fillRect(10, 104, 260, 16, TFT_BLACK);
    _tft.setFreeFont(LABEL_FONT);
    _tft.setTextColor(TFT_DARKGREY);
    formatReset(buf, sizeof(buf), d.claudeReset);
    _tft.drawString(buf, 10, 104);
    _tft.setTextFont(0);
    _prev.claudeReset = d.claudeReset;
  }
  if (d.claudeWeekly != _prev.claudeWeekly) {
    uint16_t c = progressColor(d.claudeWeekly);
    _tft.fillRect(10, 136, 190, 28, TFT_BLACK);
    _tft.setFreeFont(VALUE_FONT);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
    _tft.drawString(buf, 10, 136);
    _tft.setTextFont(0);
    drawProgressBar(10, 170, 300, 14, d.claudeWeekly, c);
    _prev.claudeWeekly = d.claudeWeekly;
  }
}

// ── Grok ──────────────────────────────────────────────────────────────────────

void Renderer::drawGrok(const UsageData& d) {
  char buf[8];
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("GROK BUILD", 10, 10, 4);
  _tft.drawFastHLine(0, 44, 320, TFT_DARKGREY);

  uint16_t cTokens = progressColor(d.grokTokens);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Tokens", 10, 58, 2);
  _tft.setTextColor(cTokens);
  snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
  _tft.drawString(buf, 240, 50, 4);
  drawProgressBar(10, 86, 300, 14, d.grokTokens, cTokens);

  _tft.drawFastHLine(0, 112, 320, TFT_DARKGREY);

  uint16_t cReqs = progressColor(d.grokRequests);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Requests", 10, 126, 2);
  _tft.setTextColor(cReqs);
  snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
  _tft.drawString(buf, 240, 118, 4);
  drawProgressBar(10, 154, 300, 14, d.grokRequests, cReqs);

  _tft.drawFastHLine(0, 180, 320, TFT_DARKGREY);
  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   190, 142, 34, "< Claude",   f, b, TFT_WHITE);
    drawButton(170, 190, 142, 34, "Settings >", f, b, TFT_WHITE);
  }

  _prev.grokTokens   = d.grokTokens;
  _prev.grokRequests = d.grokRequests;
}

void Renderer::updateGrok(const UsageData& d) {
  char buf[8];
  if (d.grokTokens != _prev.grokTokens) {
    uint16_t c = progressColor(d.grokTokens);
    _tft.fillRect(240, 50, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokTokens);
    _tft.drawString(buf, 240, 50, 4);
    drawProgressBar(10, 86, 300, 14, d.grokTokens, c);
    _prev.grokTokens = d.grokTokens;
  }
  if (d.grokRequests != _prev.grokRequests) {
    uint16_t c = progressColor(d.grokRequests);
    _tft.fillRect(240, 118, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.grokRequests);
    _tft.drawString(buf, 240, 118, 4);
    drawProgressBar(10, 154, 300, 14, d.grokRequests, c);
    _prev.grokRequests = d.grokRequests;
  }
}

// ── Settings ──────────────────────────────────────────────────────────────────

void Renderer::drawSettings(uint8_t brightness, unsigned long fetchInterval, bool ledEnabled) {
  _tft.fillScreen(colorScreenBg());
  _tft.setFreeFont(TITLE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.drawString("SETTINGS", 10, 8);
  _tft.setTextFont(0);

  // ── Brightness card ────────────────────────────────────────────────────────
  drawCard(6, 44, 308, 56);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Brightness", 10, 48);
  _tft.setTextFont(0);

  _tft.fillRoundRect(10, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(10, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("-", 35, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.fillRoundRect(260, 62, 50, 32, 5, colorCardBg());
  _tft.drawRoundRect(260, 62, 50, 32, 5, colorCardBorder());
  _tft.setFreeFont(VALUE_FONT);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextDatum(MC_DATUM);
  _tft.drawString("+", 285, 78);
  _tft.setTextFont(0);
  _tft.setTextDatum(TL_DATUM);

  _tft.drawRoundRect(68, 62, 182, 32, 5, colorCardBorder());
  int bfill = 180 * brightness / 255;
  _tft.fillRect(69, 63, bfill,               30, colorAccent());
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, colorCardBg());

  // ── Refresh card ───────────────────────────────────────────────────────────
  drawCard(6, 110, 308, 58);
  _tft.setFreeFont(LABEL_FONT);
  _tft.setTextColor(colorLabel());
  _tft.drawString("Refresh", 10, 114);
  _tft.setTextFont(0);
  drawIntervalButtons(fetchInterval);

  // ── LED toggle + Reboot cards ──────────────────────────────────────────────
  drawLedToggle(ledEnabled);
  drawOutlineCard(165, 180, 145, 32, colorDestructive(), colorDestructive(), "REBOOT");

  {
    uint16_t f = _tft.color565(32,32,32), b = _tft.color565(90,90,90);
    drawButton(8,   216, 142, 22, "< Grok",   f, b, TFT_WHITE);
    drawButton(170, 216, 142, 22, "Claude >", f, b, TFT_WHITE);
  }
}

void Renderer::drawIntervalButtons(unsigned long fetchInterval) {
  const unsigned long intervals[3] = {30000, 60000, 120000};
  const char*         labels[3]    = {"30s", "60s", "120s"};
  const int           btnX[3]      = {10, 115, 220};
  for (int i = 0; i < 3; i++) {
    bool sel = (fetchInterval == intervals[i]);
    uint16_t border = sel ? colorAccent() : colorCardBorder();
    uint16_t fg     = sel ? colorAccent() : TFT_WHITE;
    _tft.fillRoundRect(btnX[i], 130, 95, 32, 5, colorCardBg());
    _tft.drawRoundRect(btnX[i], 130, 95, 32, 5, border);
    _tft.setFreeFont(LABEL_FONT);
    _tft.setTextColor(fg);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(labels[i], btnX[i] + 47, 146);
    _tft.setTextFont(0);
    _tft.setTextDatum(TL_DATUM);
  }
}

void Renderer::drawLedToggle(bool ledEnabled) {
  uint16_t c = ledEnabled ? colorAccent() : colorCardBorder();
  drawOutlineCard(10, 180, 145, 32, c, c, ledEnabled ? "LED: ON" : "LED: OFF");
}

// ── Public update API ─────────────────────────────────────────────────────────

void Renderer::switchTo(int screen, const UsageData& data,
                        uint8_t brightness, unsigned long fetchInterval, bool ledEnabled) {
  if      (screen == 0) drawClaude(data);
  else if (screen == 1) drawGrok(data);
  else                  drawSettings(brightness, fetchInterval, ledEnabled);
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
  _tft.fillRect(69, 63, bfill,               30, colorAccent());
  _tft.fillRect(69 + bfill, 63, 180 - bfill, 30, colorCardBg());
}

void Renderer::updateLedToggle(bool ledEnabled) {
  drawLedToggle(ledEnabled);
}

void Renderer::updateIntervalButtons(unsigned long fetchInterval) {
  drawIntervalButtons(fetchInterval);
}

void Renderer::drawWifiIndicator(bool on) {
  _tft.fillCircle(310, 8, 4, on ? TFT_GREEN : TFT_BLACK);
}

void Renderer::tickSprite() {
  _sprite.tick(millis());
  if (_sprite.needsRedraw()) _sprite.draw(_tft);
}
