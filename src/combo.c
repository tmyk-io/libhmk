/*
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "combo.h"

#include "deferred_actions.h"
#include "eeconfig.h"
#include "hardware/hardware.h"
#include "keycodes.h"
#include "layout.h"

#define COMBO_BUF_MAX 8

typedef struct {
  uint8_t key;
  uint8_t layer;
  uint8_t keycode;
  uint32_t since;
  bool held;
} combo_buf_entry_t;

typedef struct {
  bool active;
  uint8_t combo_index;
  uint8_t anchor_key;
  uint8_t keycode;
  uint8_t members[COMBO_MAX_KEYS];
  uint8_t member_count;
} combo_active_t;

static combo_buf_entry_t combo_buf[COMBO_BUF_MAX];
static uint8_t combo_buf_len;
static combo_active_t combo_active;

static uint8_t combo_get_num_keys(const combo_t *combo) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < COMBO_MAX_KEYS; i++) {
    if (combo->keys[i] == COMBO_KEY_NONE)
      break;
    count++;
  }
  return count;
}

static bool combo_is_valid(const combo_t *combo) {
  if (combo->keycode == KC_NO || combo->keycode == KC_TRANSPARENT)
    return false;
  if (combo->layer >= NUM_LAYERS)
    return false;
  if (combo->term_ms < MIN_COMBO_TERM_MS || combo->term_ms > MAX_COMBO_TERM_MS)
    return false;

  const uint8_t n = combo_get_num_keys(combo);
  if (n < 2)
    return false;

  for (uint8_t i = 0; i < n; i++) {
    if (combo->keys[i] >= NUM_KEYS)
      return false;
    for (uint8_t j = i + 1; j < n; j++) {
      if (combo->keys[i] == combo->keys[j])
        return false;
    }
  }
  return true;
}

static bool combo_key_has_ak(uint8_t layer, uint8_t key) {
  for (uint32_t i = 0; i < NUM_ADVANCED_KEYS; i++) {
    const advanced_key_t *ak = &CURRENT_PROFILE.advanced_keys[i];
    if (ak->type == AK_TYPE_NONE || ak->layer != layer)
      continue;
    if (ak->key == key)
      return true;
    if (ak->type == AK_TYPE_NULL_BIND && ak->null_bind.secondary_key == key)
      return true;
  }
  return false;
}

static bool combo_has_ak_member(const combo_t *combo) {
  const uint8_t n = combo_get_num_keys(combo);
  for (uint8_t i = 0; i < n; i++) {
    if (combo_key_has_ak(combo->layer, combo->keys[i]))
      return true;
  }
  return false;
}

static bool combo_contains_key(const combo_t *combo, uint8_t key) {
  const uint8_t n = combo_get_num_keys(combo);
  for (uint8_t i = 0; i < n; i++) {
    if (combo->keys[i] == key)
      return true;
  }
  return false;
}

static bool combo_contains_all_buf_keys(const combo_t *combo) {
  for (uint8_t i = 0; i < combo_buf_len; i++) {
    if (!combo_contains_key(combo, combo_buf[i].key))
      return false;
  }
  return true;
}

static bool combo_is_complete(const combo_t *combo) {
  const uint8_t n = combo_get_num_keys(combo);
  for (uint8_t i = 0; i < n; i++) {
    bool found = false;
    for (uint8_t j = 0; j < combo_buf_len; j++) {
      if (combo_buf[j].held && combo_buf[j].key == combo->keys[i]) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }
  return true;
}

static bool combo_is_eligible(const combo_t *combo, uint8_t layer) {
  return combo_is_valid(combo) && combo->layer == layer &&
         !combo_has_ak_member(combo);
}

static int16_t combo_buf_find(uint8_t key) {
  for (uint8_t i = 0; i < combo_buf_len; i++) {
    if (combo_buf[i].key == key)
      return (int16_t)i;
  }
  return -1;
}

static bool combo_is_active_member(uint8_t key) {
  if (!combo_active.active)
    return false;
  for (uint8_t i = 0; i < combo_active.member_count; i++) {
    if (combo_active.members[i] == key)
      return true;
  }
  return false;
}

static void combo_tap(uint8_t key, uint8_t keycode) {
  const deferred_action_t action = {
      .type = DEFERRED_ACTION_TYPE_TAP,
      .key = key,
      .keycode = keycode,
  };
  deferred_action_push(&action);
}

static void combo_flush_buffer(void) {
  for (uint8_t i = 0; i < combo_buf_len; i++) {
    const combo_buf_entry_t *e = &combo_buf[i];
    if (e->held) {
      layout_set_active_keycode(e->key, e->keycode);
      layout_register(e->key, e->keycode);
    } else {
      combo_tap(e->key, e->keycode);
    }
  }
  combo_buf_len = 0;
}

static void combo_activate(uint8_t combo_index) {
  const combo_t *combo = &CURRENT_PROFILE.combos[combo_index];
  const uint8_t n = combo_get_num_keys(combo);

  combo_active.active = true;
  combo_active.combo_index = combo_index;
  combo_active.keycode = combo->keycode;
  combo_active.member_count = n;
  combo_active.anchor_key = combo->keys[0];
  for (uint8_t i = 0; i < n; i++)
    combo_active.members[i] = combo->keys[i];

  // Drop buffered entries that belong to this combo; any leftover (should be
  // none in normal operation) is flushed as normal keys.
  uint8_t new_len = 0;
  for (uint8_t i = 0; i < combo_buf_len; i++) {
    if (combo_contains_key(combo, combo_buf[i].key))
      continue;
    combo_buf[new_len++] = combo_buf[i];
  }
  combo_buf_len = new_len;
  if (combo_buf_len)
    combo_flush_buffer();

  layout_register(combo_active.anchor_key, combo_active.keycode);
}

static void combo_deactivate(void) {
  if (!combo_active.active)
    return;
  layout_unregister(combo_active.anchor_key, combo_active.keycode);
  memset(&combo_active, 0, sizeof(combo_active));
}

static uint16_t combo_clamp_term(uint16_t term_ms) {
  if (term_ms < MIN_COMBO_TERM_MS)
    return MIN_COMBO_TERM_MS;
  if (term_ms > MAX_COMBO_TERM_MS)
    return MAX_COMBO_TERM_MS;
  return term_ms;
}

static void combo_try_resolve(void) {
  if (combo_active.active || combo_buf_len == 0)
    return;

  const uint8_t layer = combo_buf[0].layer;
  const uint32_t since = combo_buf[0].since;

  int16_t best_index = -1;
  uint8_t best_len = 0;
  uint16_t max_possible_term = 0;
  bool has_possible = false;
  bool has_longer_incomplete = false;

  for (uint8_t i = 0; i < NUM_COMBOS; i++) {
    const combo_t *combo = &CURRENT_PROFILE.combos[i];
    if (!combo_is_eligible(combo, layer))
      continue;
    if (!combo_contains_all_buf_keys(combo))
      continue;

    has_possible = true;
    const uint8_t n = combo_get_num_keys(combo);
    const uint16_t term = combo_clamp_term(combo->term_ms);
    if (term > max_possible_term)
      max_possible_term = term;

    if (combo_is_complete(combo)) {
      if (n > best_len || (n == best_len && (best_index < 0 || i < (uint8_t)best_index))) {
        best_len = n;
        best_index = (int16_t)i;
      }
    }
  }

  if (best_index >= 0) {
    for (uint8_t i = 0; i < NUM_COMBOS; i++) {
      const combo_t *combo = &CURRENT_PROFILE.combos[i];
      if (!combo_is_eligible(combo, layer))
        continue;
      if (!combo_contains_all_buf_keys(combo))
        continue;
      const uint8_t n = combo_get_num_keys(combo);
      if (n > best_len && !combo_is_complete(combo)) {
        // Longer incomplete candidate still possible with current held keys as
        // a subset — wait for more keys or timeout.
        has_longer_incomplete = true;
        break;
      }
    }

    if (!has_longer_incomplete) {
      combo_activate((uint8_t)best_index);
      return;
    }
  }

  if (!has_possible || timer_elapsed(since) >= max_possible_term) {
    if (best_index >= 0)
      combo_activate((uint8_t)best_index);
    else
      combo_flush_buffer();
  }
}

void combo_init(void) { combo_clear(); }

void combo_clear(void) {
  combo_deactivate();
  combo_buf_len = 0;
}

bool combo_wants_key(uint8_t layer, uint8_t key) {
  if (key >= NUM_KEYS || layer >= NUM_LAYERS)
    return false;
  if (combo_active.active)
    return false;
  if (combo_key_has_ak(layer, key))
    return false;

  if (combo_buf_len > 0 && combo_buf[0].layer != layer)
    return false;

  for (uint8_t i = 0; i < NUM_COMBOS; i++) {
    const combo_t *combo = &CURRENT_PROFILE.combos[i];
    if (!combo_is_eligible(combo, layer))
      continue;
    if (!combo_contains_key(combo, key))
      continue;
    if (combo_buf_len == 0 || combo_contains_all_buf_keys(combo))
      return true;
  }
  return false;
}

void combo_on_press(uint8_t layer, uint8_t key, uint8_t keycode) {
  if (combo_buf_find(key) >= 0)
    return;

  if (combo_buf_len >= COMBO_BUF_MAX) {
    combo_flush_buffer();
    if (!combo_wants_key(layer, key)) {
      layout_set_active_keycode(key, keycode);
      layout_register(key, keycode);
      return;
    }
  }

  const uint32_t since =
      combo_buf_len > 0 ? combo_buf[0].since : timer_read();
  combo_buf[combo_buf_len++] = (combo_buf_entry_t){
      .key = key,
      .layer = layer,
      .keycode = keycode,
      .since = since,
      .held = true,
  };

  combo_try_resolve();
}

bool combo_on_release(uint8_t key) {
  if (combo_is_active_member(key)) {
    combo_deactivate();
    // Remaining members are still physically held but no longer suppressed;
    // they will not emit their keycodes for this stroke (QMK-like break).
    return true;
  }

  const int16_t idx = combo_buf_find(key);
  if (idx < 0)
    return false;

  combo_buf_entry_t *e = &combo_buf[idx];
  if (e->held) {
    e->held = false;
    combo_tap(e->key, e->keycode);
  }

  // Remove the released entry from the buffer.
  for (uint8_t i = (uint8_t)idx; i + 1 < combo_buf_len; i++)
    combo_buf[i] = combo_buf[i + 1];
  combo_buf_len--;

  if (combo_buf_len == 0)
    return true;

  // If nothing can still form a combo, flush remaining held keys as presses.
  bool any = false;
  const uint8_t layer = combo_buf[0].layer;
  for (uint8_t i = 0; i < NUM_COMBOS; i++) {
    const combo_t *combo = &CURRENT_PROFILE.combos[i];
    if (!combo_is_eligible(combo, layer))
      continue;
    if (combo_contains_all_buf_keys(combo)) {
      any = true;
      break;
    }
  }
  if (!any)
    combo_flush_buffer();
  else
    combo_try_resolve();

  return true;
}

void combo_tick(void) {
  if (!combo_active.active && combo_buf_len > 0)
    combo_try_resolve();
}
