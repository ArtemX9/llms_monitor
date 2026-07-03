#include "AnimatedSprite.h"
#include <esp_random.h>
#include <cmath>

namespace {
  constexpr int   LANE_H  = 44;
  constexpr int   SPRITE_Y = 2;
  constexpr float START_X = -33.0f;
  constexpr float REST_X  = 5.0f;
  // TODO: tune on hardware to clear the "Usage" title's left edge.
  // LANE_W (below) is derived from this value — if the title clips, adjust
  // TITLE_X and LANE_W will follow automatically; re-verify both on hardware.
  constexpr float TITLE_X = 80.0f;

  constexpr int SPRITE_W = 30; // 10 cols * 3px
  constexpr int LANE_W   = int(TITLE_X) + SPRITE_W + 5; // clear of the sprite's rightmost extent with margin

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
  tft.fillRect(0, 0, LANE_W, LANE_H, TFT_BLACK);

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

  for (int r = 0; r < 12; r++) {
    for (int c = 0; c < 10; c++) {
      uint8_t v = pgm_read_byte(&frame[r][c]);
      if (v) tft.fillRect(drawX + c * 3, drawY + r * 3, 3, 3, v == 1 ? body : dark);
    }
  }

  _redrawNeeded = false;
}
