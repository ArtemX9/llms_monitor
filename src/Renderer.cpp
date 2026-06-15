#include "Renderer.h"
#include "Config.h"

TFT_eSPI& Renderer::tft() { return _tft; }

void Renderer::init(uint8_t brightness) {
  ledcSetup(BL_CHANNEL, BL_FREQ, BL_RES);
  ledcAttachPin(TFT_BL_PIN, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, brightness);
  _tft.init();
  _tft.setRotation(3);
  _tft.fillScreen(TFT_BLACK);
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
  char buf[20];
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE);
  _tft.setTextSize(1);
  _tft.drawString("CLAUDE CODE", 10, 8, 4);
  _tft.drawFastHLine(0, 38, 320, TFT_DARKGREY);

  uint16_t cSession = progressColor(d.claudeSession);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Session", 10, 50, 2);
  _tft.setTextColor(cSession);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
  _tft.drawString(buf, 248, 44, 4);
  drawProgressBar(10, 70, 300, 14, d.claudeSession, cSession);
  snprintf(buf, sizeof(buf), "Resets in %d min", d.claudeReset);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString(buf, 10, 88, 2);

  _tft.drawFastHLine(0, 108, 320, TFT_DARKGREY);

  uint16_t cWeekly = progressColor(d.claudeWeekly);
  _tft.setTextColor(TFT_CYAN);
  _tft.drawString("Weekly", 10, 120, 2);
  _tft.setTextColor(cWeekly);
  snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
  _tft.drawString(buf, 248, 114, 4);
  drawProgressBar(10, 140, 300, 14, d.claudeWeekly, cWeekly);

  _tft.drawFastHLine(0, 168, 320, TFT_DARKGREY);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString("< Settings", 10, 178, 2);
  _tft.drawString("Grok >", 244, 178, 2);

  _prev.claudeSession = d.claudeSession;
  _prev.claudeWeekly  = d.claudeWeekly;
  _prev.claudeReset   = d.claudeReset;
}

void Renderer::updateClaude(const UsageData& d) {
  char buf[20];
  if (d.claudeSession != _prev.claudeSession) {
    uint16_t c = progressColor(d.claudeSession);
    _tft.fillRect(248, 44, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeSession);
    _tft.drawString(buf, 248, 44, 4);
    drawProgressBar(10, 70, 300, 14, d.claudeSession, c);
    _prev.claudeSession = d.claudeSession;
  }
  if (d.claudeReset != _prev.claudeReset) {
    _tft.fillRect(10, 88, 220, 16, TFT_BLACK);
    _tft.setTextColor(TFT_DARKGREY);
    snprintf(buf, sizeof(buf), "Resets in %d min", d.claudeReset);
    _tft.drawString(buf, 10, 88, 2);
    _prev.claudeReset = d.claudeReset;
  }
  if (d.claudeWeekly != _prev.claudeWeekly) {
    uint16_t c = progressColor(d.claudeWeekly);
    _tft.fillRect(248, 114, 72, 28, TFT_BLACK);
    _tft.setTextColor(c);
    snprintf(buf, sizeof(buf), "%d%%", d.claudeWeekly);
    _tft.drawString(buf, 248, 114, 4);
    drawProgressBar(10, 140, 300, 14, d.claudeWeekly, c);
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
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString("< Claude", 10, 168, 2);
  _tft.drawString("Settings >", 200, 168, 2);

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

  _tft.drawFastHLine(0, 215, 320, TFT_DARKGREY);
  _tft.setTextColor(TFT_DARKGREY);
  _tft.drawString("< Grok", 10, 222, 2);
  _tft.drawString("Claude >", 234, 222, 2);
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
