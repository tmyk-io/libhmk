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

#pragma once

#include "common.h"

//--------------------------------------------------------------------+
// Combo API
//--------------------------------------------------------------------+

/**
 * @brief Initialize the combo module
 */
void combo_init(void);

/**
 * @brief Clear combo runtime state
 *
 * Unregisters an active combo result if needed. Call on profile change or when
 * combos are updated via HID.
 */
void combo_clear(void);

/**
 * @brief Whether a key press should be buffered for combo detection
 *
 * @param layer Current layer at press time
 * @param key Key index
 *
 * @return true if layout should defer normal/AK processing to the combo module
 */
bool combo_wants_key(uint8_t layer, uint8_t key);

/**
 * @brief Process a key press that combo_wants_key accepted
 *
 * @param layer Layer snapshot
 * @param key Key index
 * @param keycode Keymap keycode snapshot for failure flush
 */
void combo_on_press(uint8_t layer, uint8_t key, uint8_t keycode);

/**
 * @brief Process a key release
 *
 * @param key Key index
 *
 * @return true if the combo module fully handled the release (layout should
 * skip normal unregister)
 */
bool combo_on_release(uint8_t key);

/**
 * @brief Tick pending combo timers and resolve / flush as needed
 */
void combo_tick(void);
