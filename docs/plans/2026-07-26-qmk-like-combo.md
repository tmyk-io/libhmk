# QMK-like Combo Implementation Plan

> **For implementers:** Execute this plan task-by-task. Complete each checkbox step, run the listed validation, and commit after each task.  
> **Repos:** Firmware work lives in `libhmk`. Configurator work lives in `hmkconf` (see sibling plan of the same name). Protocol and on-wire structs in this document are the contract both sides must match.  
> **Supersedes:** Incomplete WIP on `origin/add-combo` (global `combo_term` only, no layer/flags, no delayed key flush, no longest-match / adaptive fire, no hmkconf). Do not merge that branch as-is; reimplement against this plan on current `main`/`dev`.

**Goal:** Add QMK-like Combos: chord N physical keys within a per-combo term to emit one result keycode (hold until break); on failure, replay constituent keys so normal typing still works. Ship firmware + WebHID configurator together.

**Architecture:** Combos are a **separate profile subsystem** (not an Advanced Key type). `layout_task` consults a combo module before normal/AK processing for candidate keys. Config is `combo_t combos[NUM_COMBOS]` in `eeconfig_profile_t`, read/written via `GET/SET_COMBOS`. hmkconf exposes a dedicated Combos tab gated on firmware ≥ `0x0109`.

**Tech Stack:** libhmk C / PlatformIO; hmkconf SvelteKit 5 + bun; WebHID staged commands; wear-leveling EEPROM; verification via `pio run`, `bun lint`, `bun check` (no unit-test suite today).

## Global Constraints

- Dig decisions (binding):
  - Failure path: delay then emit normal keycodes (QMK-style buffer).
  - Separate `combos[]` subsystem + Combos UI tab + `GET/SET_COMBOS`.
  - Max **4** keys/combo; `NUM_COMBOS` schema **1–32**, board default **16**.
  - **Per-combo `term_ms`** (QMK `COMBO_TERM` / `COMBO_TERM_PER_COMBO`). Default **50**. No profile-global term field.
  - Same layer: key with an Advanced Key **cannot** be a combo member (UI reject + firmware ignore).
  - Overlap: **longest match**; same length → lower slot index wins.
  - Timing: **adaptive** — fire when complete if no longer incomplete superset candidate remains; else wait until relevant term(s).
  - Active combo: `layout_register` result on fire; **release when any constituent key releases**.
  - Layer: eligible only when `layout_get_current_layer() == combo.layer`.
  - Early release before resolve: cancel pending; released keys **tap**; still-held keys **press** with snapshotted keycode; that stroke no longer waits for combo.
  - Result keycode: same as Remap (`KC_NO` / transparent forbidden as result).
  - Buffer only keys that can still participate in at least one pending candidate.
  - Layer change during pending: keep **press-time snapshot** (layer + keycode per buffered key). Profile change: `combo_clear` + resync like AK.
  - MVP **out of scope** but reserved: `must_hold`, `must_tap`, `press_in_order`, `CM_ON`/`OFF`/`TOGG` behavior, `COMBO_ACTION`/strings.
  - Wire: `flags` byte with `COMBO_FLAG_MUST_HOLD = (1u << 0)` reserved (must be 0 in MVP). Reserve `SP_COMBO_ON/OFF/TOGGLE` keycodes (no-op in MVP).
- `EECONFIG_VERSION` bump `0x0105` → `0x0106` with migration; `FIRMWARE_VERSION` bump `0x0108` → `0x0109`.
- `sizeof(eeconfig_t) <= WL_VIRTUAL_SIZE` must hold (default 8192). Rough HE60 delta ≈ `16 * 9 * 4 = 576` bytes for default 16 combos × 9 bytes × 4 profiles.
- Match existing Prettier/ESLint/Svelte 5 runes style in hmkconf; `-Werror` in firmware.
- Do not commit generated `platformio.ini`, `.pio/`, `include/metadata.h`.

## Current Context

- Key path: `matrix_scan` → `layout_task` → AK or `layout_register` → HID (`src/layout.c`, `src/advanced_keys.c`).
- AK index map is one slot per `(layer, key)`; Null Bind is the only multi-key AK and is SOCD, not chord→keycode.
- Staged HID pattern for large blobs: `COMMAND_GET/SET_ADVANCED_KEYS`, `COMMAND_GET/SET_MACROS` (`include/commands.h`, `src/commands.c`).
- hmkconf feature gates: `featureVersionMap` in `src/lib/libhmk/index.ts`.
- No combo code on current `main`. `origin/add-combo` is a partial prototype to learn from, not to land.

## Shared Contract (both repos)

### `combo_t` (9 bytes, packed)

```c
#define COMBO_KEY_NONE        0xFF
#define COMBO_MAX_KEYS        4
#define DEFAULT_COMBO_TERM_MS 50
#define MIN_COMBO_TERM_MS     10
#define MAX_COMBO_TERM_MS     1000
#define COMBO_FLAG_MUST_HOLD  (1u << 0) /* reserved; MVP must write 0 */

typedef struct __attribute__((packed)) {
  uint8_t layer;                 /* 0 .. NUM_LAYERS-1 */
  uint8_t keycode;               /* result; KC_NO => empty/invalid */
  uint16_t term_ms;              /* per-combo term; default 50 */
  uint8_t keys[COMBO_MAX_KEYS];  /* physical indices; unused = COMBO_KEY_NONE; packed to front */
  uint8_t flags;                 /* bit0 must_hold reserved */
} combo_t;
```

**Valid combo:** `keycode != KC_NO`, `keys[0]` and `keys[1]` ≠ `COMBO_KEY_NONE`, all used keys distinct and `< NUM_KEYS`, `term_ms` in `[10, 1000]`, `layer < NUM_LAYERS`, `flags == 0` in MVP (non-zero reserved bits ignored or rejected on SET — prefer **reject unknown bits** on SET for forward safety).

**Empty slot:** `keys[0] == COMBO_KEY_NONE` (and `keycode == KC_NO` on reset).

### Protocol

| ID | Name | Notes |
|----|------|--------|
| 142 | `COMMAND_GET_COMBOS` | Staged read like macros/AK |
| 143 | `COMMAND_SET_COMBOS` | Staged write; on success `combo_clear()` |

Metadata JSON field: `numCombos` (from `keyboard.num_combos`).

Feature gate name: `combos` → firmware `0x0109`.

### Reserved keycodes (MVP no-op)

```c
SP_COMBO_ON     = 0xD5,
SP_COMBO_OFF    = 0xD6,
SP_COMBO_TOGGLE = 0xD7,
```

Mirror in hmkconf keycode metadata (can hide from picker until implemented, but enum values reserved).

### Runtime behavior (normative)

1. On press of key `k` at current layer `L`: resolve keymap keycode `kc` via existing `layout_get_keycode` and store snapshot `{key:k, layer:L, keycode:kc, t0}`.
2. If `k` has an AK on `L`, combo never buffers it (AK path unchanged).
3. Else if `k` can join any **eligible** combo candidate (valid, `combo.layer == L`, no AK on any member, consistent with current buffer), buffer `k` and do **not** `layout_register` yet.
4. Else normal press path.
5. **Adaptive resolve:** After each press/release/tick:
   - Consider candidates whose full key set ⊆ currently buffered+held chord keys for that snapshot layer.
   - If a complete candidate exists and no **strictly longer** incomplete candidate can still be completed with currently held keys + time remaining, activate the **longest** complete candidate (tie → lowest index): `layout_register(anchor_key, combo.keycode)`, mark members consumed, do not emit member keycodes.
   - If time since first buffered key of a pending group exceeds the **max `term_ms` among still-possible candidates** for that buffer (if none left, flush immediately): flush — for each buffered key still held → `layout_register(key, snap.keycode)`; for each already released → deferred **tap** of `snap.keycode`.
6. On release of member while combo **active:** `layout_unregister(anchor, combo.keycode)`, clear active; other members' releases are ignored for HID (already suppressed).
7. On release of member while **pending:** remove from buffer; if released before resolve, tap that key's snapshot; re-evaluate remaining.
8. Profile change / SET_COMBOS: `combo_clear()`; do not leave stuck HID keys (unregister active combo result if needed).

## File Structure

### libhmk

- Create: `include/combo.h` — API + runtime state types
- Create: `src/combo.c` — detect / buffer / fire / flush
- Modify: `include/common.h` — `combo_t`, `NUM_COMBOS` asserts, `FIRMWARE_VERSION 0x0109`
- Modify: `include/eeconfig.h` — `combos[NUM_COMBOS]` on profile; `EECONFIG_VERSION 0x0106`
- Modify: `src/eeconfig.c` — default empty combos
- Modify: `src/migration.c` — v1.6 migration append zeroed combos
- Modify: `include/commands.h` / `src/commands.c` — GET/SET_COMBOS staged
- Modify: `include/keycodes.h` — reserve `SP_COMBO_*`
- Modify: `src/layout.c` — integrate combo before AK/normal register
- Modify: `src/main.c` — `combo_init` if needed
- Modify: `scripts/schema/keyboard.py` — `num_combos`
- Modify: `scripts/make.py` — `NUM_COMBOS` define
- Modify: `scripts/metadata.py` — `numCombos`
- Modify: `keyboards/*/keyboard.json` — `"num_combos": 16`
- Modify: `README.md` — feature bullet

### hmkconf

- Create: `src/lib/libhmk/combos.ts` — zod schema + defaults
- Create: `src/lib/libhmk/commands/combos.ts` — GET/SET
- Create: `src/lib/configurator/combos/**` — tab UI (mirror advanced-keys structure)
- Create: `src/lib/configurator/queries/combos-query.svelte.ts`
- Modify: `src/lib/libhmk/index.ts` — limits + `featureVersionMap.combos`
- Modify: `src/lib/libhmk/commands/index.ts` — command IDs
- Modify: `src/lib/libhmk/keycodes.ts` — reserved SP codes
- Modify: `src/lib/keyboard/*` — interface + HMK/Demo implementations
- Modify: `src/lib/keyboard/metadata.ts` — `numCombos`
- Modify: `src/lib/configurator/lib/layout.ts` — sidebar tab
- Modify: `src/lib/configurator/context.svelte.ts` / `configurator.svelte` — tab wiring
- Modify: `README.md` — mention Combos

## Tasks

### Task 1: Schema, types, version bumps (libhmk)

**Objective:** Introduce `combo_t`, `NUM_COMBOS`, version bumps, and board metadata without runtime behavior yet.

**Files:**
- Modify: `include/common.h`, `include/eeconfig.h`, `include/keycodes.h`
- Modify: `scripts/schema/keyboard.py`, `scripts/make.py`, `scripts/metadata.py`
- Modify: `keyboards/he16/keyboard.json`, `keyboards/he60/keyboard.json`, `keyboards/he60-v2/keyboard.json`, `keyboards/m256-whe/keyboard.json`
- Modify: `src/eeconfig.c`, `src/migration.c`

**Interfaces:**
- Consumes: existing `eeconfig_profile_t` layout, migration pattern `v1_5_*`
- Produces: `combo_t`, `NUM_COMBOS`, `EECONFIG_VERSION 0x0106`, `FIRMWARE_VERSION 0x0109`, metadata `numCombos`

- [ ] **Step 1: Add schema + build define**

In `scripts/schema/keyboard.py` on `KeyboardKeyboard`:

```python
num_combos: int = Field(ge=1, le=32, default=16)
```

In `scripts/make.py` next to other `NUM_*` defines:

```python
build_flags.define("NUM_COMBOS", kb.num_combos)
```

In each `keyboards/*/keyboard.json` under `"keyboard"`:

```json
"num_combos": 16
```

In `scripts/metadata.py` metadata dict:

```python
"numCombos": kb_json.keyboard.num_combos,
```

- [ ] **Step 2: Add `combo_t` and version constants**

In `include/common.h`:

- Set `#define FIRMWARE_VERSION 0x0109`
- After `NUM_ADVANCED_KEYS` asserts, add `NUM_COMBOS` error + `_Static_assert(1 <= NUM_COMBOS && NUM_COMBOS <= 32, ...)`
- Add `#define COMBO_MAX_KEYS 4`, `COMBO_KEY_NONE`, flag + term constants, and `combo_t` as in Shared Contract

In `include/keycodes.h` after `SP_BOOT`:

```c
SP_COMBO_ON = 0xD5,
SP_COMBO_OFF = 0xD6,
SP_COMBO_TOGGLE = 0xD7,
```

In `include/eeconfig.h`:

- Add `combo_t combos[NUM_COMBOS];` to `eeconfig_profile_t` (after `advanced_keys` or after `macros` — prefer **after `macros`** to keep migration append simple if macros stay contiguous; if appending at end of profile before `tick_rate` is easier for memcpy migration, place `combos` immediately before `tick_rate` and document offsets in migration)
- Bump `#define EECONFIG_VERSION 0x0106`

Recommended profile field order after change:

```c
keymap, actuation_map, advanced_keys, macros, combos,
gamepad_buttons, gamepad_options, tick_rate
```

- [ ] **Step 3: Defaults + migration v1.6**

`eeconfig.c`: ensure new profile defaults zero/clear combos (`keys` = `0xFF`, `keycode` = `KC_NO`, `term_ms` = `DEFAULT_COMBO_TERM_MS`, `flags` = 0).

`migration.c`: add `v1_6_*` that copies v1.5 profile bytes then appends `NUM_COMBOS * sizeof(combo_t)` empty combos (or inserts at chosen offset). Update migrations table + `_Static_assert` on `offsetof(eeconfig_t, magic_end)`.

- [ ] **Step 4: Validate build**

```bash
cd /agent/repos/libhmk
pip install -r requirements.txt
pip install --upgrade platformio
export PATH="$HOME/.local/bin:$PATH"
python setup.py -k he60
pio run
```

Expected: success; `sizeof(eeconfig_t) <= WL_VIRTUAL_SIZE` assert passes.

- [ ] **Step 5: Commit**

```bash
git add include/common.h include/eeconfig.h include/keycodes.h \
  scripts/schema/keyboard.py scripts/make.py scripts/metadata.py \
  src/eeconfig.c src/migration.c keyboards/*/keyboard.json
git commit -m "feat(combo): add combo_t storage, num_combos, eeconfig v1.6"
```

### Task 2: HID commands GET/SET_COMBOS (libhmk)

**Objective:** Expose staged read/write of the combos array.

**Files:**
- Modify: `include/commands.h`, `src/commands.c`

**Interfaces:**
- Consumes: `combo_t`, `NUM_COMBOS`, staged profile helpers
- Produces: `COMMAND_GET_COMBOS = 142`, `COMMAND_SET_COMBOS = 143`; on successful SET call `combo_clear()` (stub ok until Task 3)

- [ ] **Step 1: Extend command enums and staged id**

Mirror macros:

```c
COMMAND_GET_COMBOS, /* 142 */
COMMAND_SET_COMBOS, /* 143 */
```

Add `COMMAND_STAGED_COMBOS` to `command_staged_id_t`. Extend staged union with `combo_t combo` if needed for flush size.

- [ ] **Step 2: Implement handlers**

Follow `COMMAND_GET_MACROS` / `SET_MACROS` byte-offset staging. On SET completion success: `combo_clear()`.

Validate on SET (firmware): drop/reject entries that fail validity; unknown `flags` bits → reject write or clear flags to 0 (choose **clear reserved bits to 0** on accept to be lenient with older hosts writing zeros only).

- [ ] **Step 3: Build**

```bash
python setup.py -k he60 && pio run
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add include/commands.h src/commands.c
git commit -m "feat(combo): add GET/SET_COMBOS HID commands"
```

### Task 3: Combo runtime module (libhmk)

**Objective:** Implement buffering, adaptive longest-match, hold-until-break, and failure flush.

**Files:**
- Create: `include/combo.h`, `src/combo.c`
- Modify: `src/layout.c`, `src/main.c` (if init ownership requires)

**Interfaces:**
- Consumes: `CURRENT_PROFILE.combos`, matrix press state, `layout_register`/`unregister`, `deferred_action_push` for taps
- Produces:
  - `void combo_init(void);`
  - `void combo_clear(void);`
  - `bool combo_wants_key(uint8_t layer, uint8_t key);` — whether press should be buffered
  - `void combo_on_press(uint8_t layer, uint8_t key, uint8_t keycode);`
  - `void combo_on_release(uint8_t key);`
  - `void combo_tick(void);`
  - `bool combo_is_key_suppressed(uint8_t key);` — active/pending consumption for layout skip

- [ ] **Step 1: Write behavioral checklist as comments in `combo.h`**

Document the normative algorithm from Shared Contract (adaptive, longest, flush rules, AK exclusion using `advanced_key_indices` or a callback `bool layout_key_has_advanced_key(uint8_t layer, uint8_t key)` added in `layout.c` if needed).

- [ ] **Step 2: Implement `combo.c`**

Suggested internal state:

```c
typedef struct {
  uint8_t key;
  uint8_t layer;
  uint8_t keycode;
  uint32_t since;
  bool held;
  bool buffered;
} combo_key_buf_t;

typedef struct {
  bool active;
  uint8_t combo_index;
  uint8_t anchor_key;
  uint8_t keycode;
  uint8_t members[COMBO_MAX_KEYS];
  uint8_t member_count;
} combo_active_t;
```

Implementation notes:

- Build eligible combo list filtered by snapshot layer + validity + no AK on any member.
- Longest-match selection among complete candidates.
- Suppress member keys from normal layout while buffered or active.
- On activate: never emit member keycodes; register result once.
- On any member release while active: unregister result (Q7-A), clear active.
- Use `deferred_action` TAP for released-before-resolve keys.
- `combo_clear`: unregister active result if needed; zero state.

- [ ] **Step 3: Wire `layout_task`**

In press branch, before AK/normal:

```c
const uint8_t keycode = layout_get_keycode(current_layer, i);
if (!advanced_key_indices[current_layer][i] &&
    combo_wants_key(current_layer, i)) {
  combo_on_press(current_layer, i, keycode);
  bitmap_set(key_press_states, i, k->is_pressed);
  continue;
}
```

In release branch, call `combo_on_release(i)` first; if `combo_is_key_suppressed(i)` and key was never registered normally, skip normal unregister.

Call `combo_tick()` near `advanced_key_tick`.

On profile set: `combo_clear()` next to `advanced_key_clear()`.

- [ ] **Step 4: Build all keyboards**

```bash
for k in he16 he60 he60-v2 m256-whe; do
  python setup.py -k "$k" && pio run || exit 1
done
```

Expected: all succeed.

- [ ] **Step 5: Commit**

```bash
git add include/combo.h src/combo.c src/layout.c src/main.c
git commit -m "feat(combo): implement combo detection, adaptive fire, flush"
```

### Task 4: Firmware docs (libhmk)

**Objective:** Document the feature for humans.

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add Combo to feature list** — per-combo term, layer-scoped, max 4 keys, configurator support; note AK mutual exclusion.

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: mention Combo feature"
```

### Task 5: Protocol types + commands (hmkconf)

**Objective:** Speak `GET/SET_COMBOS` and parse `numCombos` / feature gate.

**Files:**
- Create: `src/lib/libhmk/combos.ts`, `src/lib/libhmk/commands/combos.ts`
- Modify: `src/lib/libhmk/index.ts`, `commands/index.ts`, `keycodes.ts`
- Modify: `src/lib/keyboard/metadata.ts`, `index.ts`, `hmk-keyboard.svelte.ts`, `demo-keyboard.svelte.ts`

**Interfaces:**
- Consumes: Shared Contract
- Produces: `HMK_Combo`, `getCombos`/`setCombos`, `Keyboard.getCombos`/`setCombos`, demo storage

- [ ] **Step 1: Schema**

```ts
export const HMK_MAX_NUM_COMBOS = 32
export const HMK_COMBO_MAX_KEYS = 4
export const DEFAULT_COMBO_TERM = 50
export const MIN_COMBO_TERM = 10
export const MAX_COMBO_TERM = 1000
export const COMBO_KEY_NONE = 0xff
export const COMBO_FLAG_MUST_HOLD = 1 << 0

export const hmkComboSchema = z.object({
  layer: uint8Schema.max(HMK_MAX_NUM_LAYERS - 1),
  keycode: uint8Schema,
  termMs: uint16Schema.min(MIN_COMBO_TERM).max(MAX_COMBO_TERM),
  keys: z.array(uint8Schema).length(HMK_COMBO_MAX_KEYS),
  flags: uint8Schema,
})
```

Wire order must match C packed layout: `layer, keycode, term_ms le16, keys[4], flags`.

- [ ] **Step 2: Feature + commands**

```ts
// featureVersionMap
combos: 0x0109,

// HMK_Command
GET_COMBOS = 142,
SET_COMBOS = 143,
```

Implement `getCombos`/`setCombos` like `macros.ts` / `advanced-keys.ts` staged transfers. If `!isFeatureAvailable("combos", firmwareVersion)` return empty / no-op.

- [ ] **Step 3: Keyboard iface + DemoKeyboard**

Add methods; demo keeps `combos: HMK_Combo[]` sized `numCombos`, default empty slots.

Metadata: `numCombos` with default 16 for demo.

- [ ] **Step 4: Validate**

```bash
cd /agent/repos/hmkconf && bun install && bun lint && bun check
```

Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add src/lib/libhmk src/lib/keyboard
git commit -m "feat(combo): add combos protocol types and commands"
```

### Task 6: Combos configurator UI (hmkconf)

**Objective:** Dedicated Combos tab to create/edit/delete combos with per-combo term.

**Files:**
- Create: `src/lib/configurator/combos/` (tab, menubar, keyboard highlight, main list, create flow, config panel, delete dialog)
- Create: `src/lib/configurator/queries/combos-query.svelte.ts`
- Modify: `lib/layout.ts`, `context.svelte.ts`, `configurator.svelte`

**Interfaces:**
- Consumes: `getCombos`/`setCombos`, advanced keys query (for AK exclusion), keymap optional for labels
- Produces: user-editable combos persisted to device/demo

- [ ] **Step 1: State + query**

Mirror `advanced-keys-query.svelte.ts`: load/save array; expose `updateCombo(index, combo)`.

Configurator state: selected index, create wizard step (pick 2–4 keys on layer → pick result keycode → term).

- [ ] **Step 2: UI**

Sidebar entry after Advanced Keys:

```ts
{ label: "Combos", value: "combos", icon: /* pick existing lucide, e.g. Combine or GitMerge */ }
```

Config panel fields:

- Layer select
- Trigger keys (2–4) via keyboard picker
- Result keycode via `KeycodeAccordion`
- `termMs` number input (10–1000, default 50) — label as “Combo term (ms)”
- Do not expose `flags` / must_hold in MVP UI

Validations before save:

1. ≥2 distinct keys; unused slots `COMBO_KEY_NONE`
2. No key appears twice
3. No member has an Advanced Key on the same layer
4. Result ≠ `KC_NO` / transparent
5. No duplicate key-set+layer against another non-empty slot (optional but recommended)
6. Warn (or block) when one combo’s key set is equal to another

- [ ] **Step 3: Wire tab in configurator shell**

Lazy-import pattern like other tabs; reset state on profile switch.

- [ ] **Step 4: Validate**

```bash
bun lint && bun check
bun dev
# manual: open http://localhost:5173/demo → Combos tab
```

Manual demo checklist:

1. Create combo layer0 keys A+B → Esc, term 50; appears in list; survives reload of demo state setters.
2. Reject adding a key that already has AK on that layer.
3. Edit term to 80; saved value reads back.
4. Delete combo → slot empty.

- [ ] **Step 5: Commit**

```bash
git add src/lib/configurator src/lib/libhmk/keycodes.ts
git commit -m "feat(combo): add Combos configurator tab"
```

### Task 7: Cross-repo README + pairing notes

**Objective:** Align docs; note pairing `libhmk`/`hmkconf` at combos feature version.

**Files:**
- Modify: `hmkconf/README.md`
- Modify: `libhmk/README.md` if pairing note needed

- [ ] **Step 1: Document** that Combos require firmware ≥ 1.9 (`0x0109`) and matching hmkconf; `/demo` works without hardware.

- [ ] **Step 2: Commit each repo**

```bash
git commit -m "docs: document Combos configurator requirements"
```

## Validation

### libhmk

```bash
pip install -r requirements.txt && pip install --upgrade platformio
export PATH="$HOME/.local/bin:$PATH"
for k in he16 he60 he60-v2 m256-whe; do
  python setup.py -k "$k" && pio run || exit 1
done
```

Expected: all four firmwares build; no assert failures on eeconfig size.

### hmkconf

```bash
bun lint && bun check
```

Expected: clean.

### Manual / hardware (when available)

| # | Scenario | Expected |
|---|----------|----------|
| 1 | Combo `A+B→Esc`, tap both within term | Esc down/up with hold semantics; no A/B |
| 2 | Press A, wait > term, no B | A registers after timeout |
| 3 | Press A, release A before B | A taps; no stuck key |
| 4 | Combos `A+B` and `A+B+C` | A+B+C within term → only longer result |
| 5 | `A+B` complete and no longer candidate | Fires without waiting full term (adaptive) |
| 6 | AK on key D; try combo including D | UI blocks; firmware ignores if forced |
| 7 | Per-combo term 50 vs 200 on two combos | Independent timing |
| 8 | Result `MO(1)` | Layer held while chord held; releases on first member up |
| 9 | Profile switch mid-chord | No stuck HID; state cleared |

## Risks, Tradeoffs, and Open Questions

- **Risk:** Buffering increases latency for combo-member keys even on normal taps — mitigated by adaptive fire + short default 50ms + buffering only candidate-capable keys.
- **Risk:** `layout_task` complexity / interaction with deferred taps — keep combo flush on the existing deferred-action path.
- **Tradeoff:** AK exclusion simplifies MVP but blocks home-row mod combos that QMK supports with mod-tap; future work may adopt Q14-C/`must_hold` carefully.
- **Tradeoff:** Exact layer match (not QMK reference-layer) matches libhmk AK mental model.
- **Forward-compat:** `flags` + `SP_COMBO_*` reserved for enable/disable and must_hold without eeconfig resize.
- **Open:** Whether SET_COMBOS should hard-fail or sanitize invalid entries — plan recommends sanitize empty + clear unknown flags.
- **Open:** PR base branch (`main` vs `dev`) — follow repo contribution norm (`dev` for libhmk per README) when opening implementation PRs.

## Dig Summary (frozen)

| Topic | Decision |
|-------|----------|
| Core | Delayed normal keys on failure |
| Storage | Separate combos table |
| Shape | 4 keys, ≤32 slots (default 16), per-combo `term_ms`, layer |
| AK | Mutually exclusive on same layer |
| Overlap | Longest wins |
| Timer | Adaptive |
| Hold | Until any member releases |
| Layer | Exact current layer |
| Early release | Immediate cancel + tap/press replay |
| Result | Full keycode set |
| Ship | Firmware + Combos tab + demo |
| Buffer | Only keys that can still form a candidate |
| Layer change | Press snapshots |
| MVP cut | No must_hold / CM_* behavior yet |
| Reserve | `flags` + `SP_COMBO_ON/OFF/TOGGLE` |
| Term UX | Per-combo only (no profile global) |
