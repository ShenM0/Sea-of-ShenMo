/**
 * @file led.c
 * @author Lu Yongping (Lucas@hiwonder.com)
 * @brief 实现硬件无关的LED灯闪烁控制
 * @version 0.1
 * @date 20234-01-13
 *
 * @copyright Copyright (c) 2023
 *
 */

#include "buzzer.h"
#include "hiwonder_err.h"

static void refresh(BuzzerObjectTypeDef *self) {
  /* 状态机处理 */
  if (self->ticks_setted) {
    self->freq = self->freq_set;
    self->ticks_on = self->ticks_on_set;
    self->ticks_off = self->ticks_off_set;
    self->repeat = self->repeat_set;
    self->stage = 0;
    self->ticks_setted = false;
    if (self->ticks_on > 0) {
      self->set_freq(self, self->freq);
      self->stage = 0;
    } else {
      self->set_freq(self, 0);
      self->stage = 1;
    }
    self->ticks = self->get_ticks();
  }
  if (self->repeat != 0) {
    uint32_t current_ticks = self->get_ticks();
    switch ((int)self->stage) {
    case 0: { /* 等待周期结束 */
      if ((current_ticks - self->ticks) >= self->ticks_on) {
        if (self->ticks_off > 0) {
          self->set_freq(self, 0);
          self->ticks = current_ticks;
          self->stage = 1;
        }
      }
      break;
    }
    case 1: {
      if ((current_ticks - self->ticks) >= self->ticks_off) {
        if (self->repeat > 0) {
          self->repeat--;
        }
        if (self->repeat == 0) {
          self->stage = 3;
        } else {
          if (self->ticks_on > 0 && self->repeat != 0) {
            self->set_freq(self, self->freq);
            self->ticks = current_ticks;
            self->stage = 0;
          }
        }
      }
      break;
    }
    default:
      break;
    }
  }
}

static int beep(BuzzerObjectTypeDef *self, uint32_t freq, uint32_t ticks_on, uint32_t ticks_off, uint32_t repeat) {
  self->freq_set = freq;
  self->ticks_on_set = ticks_on;
  self->ticks_off_set = ticks_off;
  self->repeat_set = repeat == 0 ? -1 : repeat;
  self->ticks_setted = true;
  return HIWONDER_OK;
}

void buzzer_new(BuzzerObjectTypeDef *self, const BuzzerObjectInitTypeDef *config) {
  self->ticks_on = 0;
  self->ticks_off = 0;
  self->repeat = 0;
  self->freq = 0;
  self->ticks_on_set = 0;
  self->ticks_off_set = 0;
  self->repeat_set = 0;
  self->ticks_setted = false;
  self->stage = 0;

  self->beep = beep;
  self->refresh = refresh;

  self->set_freq = config->set_freq;
  self->get_ticks = config->get_ticks;
}
