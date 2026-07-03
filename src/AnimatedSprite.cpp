#include "AnimatedSprite.h"
#include <esp_random.h>
#include <cmath>

namespace {
  constexpr int   SPRITE_Y = 2;
  constexpr float START_X = -33.0f;
  constexpr float REST_X  = 5.0f;
  // TODO: tune on hardware to clear the "Usage" title's left edge.
  constexpr float TITLE_X = 80.0f;

  constexpr int SPRITE_W = 30; // 10 cols * 3px
  constexpr int SPRITE_H = 36; // 12 rows * 3px

  constexpr unsigned long ENTER_DUR_MS       = 5000;
  constexpr unsigned long PAUSE_MS           = 2000;
  constexpr unsigned long LEG_DUR_MS         = 3000;
  constexpr unsigned long FRAME_INTERVAL_MS  = 250;
  constexpr unsigned long REROLL_INTERVAL_MS = 300000UL;
  constexpr unsigned long MIN_TICK_MS        = 80;

  constexpr int HOP_STEP_PX   = 6;
  constexpr int HOP_BOUNCE_PX = 3;
  constexpr int NUM_FRAMES    = 4;

  float easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
  }
}

void AnimatedSprite::reroll(unsigned long nowMs) {
  _character    = esp_random() % NUM_CHARACTERS;
  _frame        = 0;
  _lastFrameMs  = nowMs;
  _lastRerollMs = nowMs;
  if (CHARACTERS[_character].motion == SpriteMotion::STATIC) {
    _phase = Phase::Idle;
    _x     = REST_X;
  } else {
    _phase        = Phase::Entering;
    _phaseStartMs = nowMs;
    _x            = START_X;
  }
}

void AnimatedSprite::tick(unsigned long nowMs) {
  _redrawNeeded = false;

  if (!_initialized) {
    _initialized = true;
    reroll(nowMs);
    _lastTickMs   = nowMs;
    _redrawNeeded = true;
    return;
  }

  if (nowMs - _lastRerollMs >= REROLL_INTERVAL_MS) {
    reroll(nowMs);
    _redrawNeeded = true;
  }

  if (nowMs - _lastTickMs < MIN_TICK_MS) return;
  _lastTickMs = nowMs;

  if (nowMs - _lastFrameMs >= FRAME_INTERVAL_MS) {
    _frame       = (_frame + 1) % NUM_FRAMES;
    _lastFrameMs = nowMs;
    _redrawNeeded = true;
  }

  float newX = _x;
  switch (_phase) {
    case Phase::Idle:
      newX = REST_X;
      break;
    case Phase::Entering: {
      float p = float(nowMs - _phaseStartMs) / ENTER_DUR_MS;
      if (p >= 1.0f) {
        newX = REST_X;
        _phase = Phase::AtLeft;
        _phaseStartMs = nowMs;
      } else {
        newX = START_X + (REST_X - START_X) * easeInOutQuad(p);
      }
      break;
    }
    case Phase::AtLeft:
      newX = REST_X;
      if (nowMs - _phaseStartMs >= PAUSE_MS) {
        _phase = Phase::ToTitle;
        _phaseStartMs = nowMs;
      }
      break;
    case Phase::ToTitle: {
      float p = float(nowMs - _phaseStartMs) / LEG_DUR_MS;
      if (p >= 1.0f) {
        newX = TITLE_X;
        _phase = Phase::AtTitle;
        _phaseStartMs = nowMs;
      } else {
        newX = REST_X + (TITLE_X - REST_X) * easeInOutQuad(p);
      }
      break;
    }
    case Phase::AtTitle:
      newX = TITLE_X;
      if (nowMs - _phaseStartMs >= PAUSE_MS) {
        _phase = Phase::ToLeft;
        _phaseStartMs = nowMs;
      }
      break;
    case Phase::ToLeft: {
      float p = float(nowMs - _phaseStartMs) / LEG_DUR_MS;
      if (p >= 1.0f) {
        newX = REST_X;
        _phase = Phase::AtLeft;
        _phaseStartMs = nowMs;
      } else {
        newX = TITLE_X + (REST_X - TITLE_X) * easeInOutQuad(p);
      }
      break;
    }
  }

  if (newX != _x) {
    _x = newX;
    _redrawNeeded = true;
  }
}

void AnimatedSprite::draw(TFT_eSPI& tft) {
  const uint16_t body = tft.color565(210, 90, 42);
  const uint16_t dark = tft.color565(130, 50, 15);

  const Character&    ch    = CHARACTERS[_character];
  const SpriteFrame&   frame = *ch.frames[_frame];

  int drawX = (int)_x;
  int drawY = SPRITE_Y;

  if (ch.motion == SpriteMotion::PATROL_HOP) {
    long step = lroundf(drawX / (float)HOP_STEP_PX);
    drawX = (int)(step * HOP_STEP_PX);
    drawY = SPRITE_Y - ((step % 2 == 0) ? 0 : HOP_BOUNCE_PX);
  }

  // pushImage()'s raw pixel path (pushPixels() with the default _swapBytes=false)
  // writes the buffer's bytes as-is with no swap, unlike fillRect()'s pushBlock()
  // which always byte-swaps internally. color565() returns host (little-endian)
  // byte order, so values stored here must be pre-swapped to the wire's
  // big-endian order or the colors come out wrong.
  const uint16_t bodyWire = (body >> 8) | (body << 8);
  const uint16_t darkWire = (dark >> 8) | (dark << 8);

  static uint16_t buf[SPRITE_H][SPRITE_W];
  for (int r = 0; r < 12; r++) {
    for (int c = 0; c < 10; c++) {
      uint8_t v = pgm_read_byte(&frame[r][c]);
      uint16_t color = v == 0 ? TFT_BLACK : (v == 1 ? bodyWire : darkWire);
      for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
          buf[r * 3 + i][c * 3 + j] = color;
        }
      }
    }
  }

  tft.startWrite();
  // Erase only the sprite's previous footprint, not a large fixed-size lane.
  // The buffer above already carries the correct final color for every pixel
  // in the sprite's own footprint (including black for transparent cells), so
  // the only region that ever needs a separate clear is wherever the sprite
  // used to be but no longer is (relevant when it's moved since the last
  // draw). A wide unconditional pre-clear was the real cause of the visible
  // black flash on every redraw: it deliberately painted the whole lane black
  // as a first, visible step before the sprite reappeared, every single time
  // the picture changed — not a hardware timing race, which is why neither
  // batching into one transaction nor consolidating to one pushImage() call
  // (both tried previously) changed anything.
  if (_hasDrawn && (_lastDrawX != drawX || _lastDrawY != drawY)) {
    tft.fillRect(_lastDrawX, _lastDrawY, SPRITE_W, SPRITE_H, TFT_BLACK);
  }
  tft.pushImage(drawX, drawY, SPRITE_W, SPRITE_H, &buf[0][0]);
  tft.endWrite();

  _lastDrawX = drawX;
  _lastDrawY = drawY;
  _hasDrawn  = true;

  _redrawNeeded = false;
}
