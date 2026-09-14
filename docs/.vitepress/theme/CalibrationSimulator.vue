<script setup lang="ts">
import { ref, computed } from 'vue'

const DEFAULT_REST = 25
const DEFAULT_PRESS = 45
const DEFAULT_DUR = 250

const rest = ref(DEFAULT_REST)
const press = ref(DEFAULT_PRESS)
const dur = ref(DEFAULT_DUR)

// Haptic feedback engine (safe cross-platform wrapper matching main/calibration.h)
function haptic(pattern: number | number[]) {
  if (typeof navigator !== 'undefined' && navigator.vibrate) {
    try {
      navigator.vibrate(pattern)
    } catch {}
  }
}

let lastRestHaptic = rest.value
let lastPressHaptic = press.value
let lastDurHaptic = dur.value

function hapticTick(val: number, prevVal: number) {
  if (val !== prevVal) {
    haptic(6)
  }
}

function calcCoords(angle: number) {
  const phi = ((angle - 90) / 90) * 160
  const rad = phi * (Math.PI / 180)
  const x = (80 + 58 * Math.sin(rad)).toFixed(2)
  const y = (80 - 58 * Math.cos(rad)).toFixed(2)
  const largeArc = angle > 101 ? 1 : 0
  const d = angle <= 0 ? '' : `M 60.16 134.50 A 58 58 0 ${largeArc} 1 ${x} ${y}`
  return { x, y, d }
}

const restCoords = computed(() => calcCoords(Number(rest.value)))
const pressCoords = computed(() => calcCoords(Number(press.value)))

const durFill = computed(() => {
  const pct = ((Number(dur.value) - 50) / (3000 - 50)) * 100
  return `linear-gradient(to right, var(--esp-primary) 0%, var(--esp-primary) ${pct}%, rgba(255,255,255,0.1) ${pct}%, rgba(255,255,255,0.1) 100%)`
})

function getDialAngle(e: PointerEvent | MouseEvent | TouchEvent, currentVal: number, wrapEl: HTMLElement | null) {
  if (!wrapEl) return currentVal
  const rect = wrapEl.getBoundingClientRect()
  const cx = rect.left + rect.width / 2
  const cy = rect.top + rect.height / 2
  let cxEv = (e as MouseEvent).clientX
  let cyEv = (e as MouseEvent).clientY
  if ((e as TouchEvent).touches && (e as TouchEvent).touches.length > 0) {
    cxEv = (e as TouchEvent).touches[0].clientX
    cyEv = (e as TouchEvent).touches[0].clientY
  }
  const dx = cxEv - cx
  const dy = cyEv - cy
  if (dx * dx + dy * dy < 36) return currentVal

  const deg = Math.atan2(dx, -dy) * (180 / Math.PI)
  if (currentVal < 45 && deg > 100) return 0
  if (currentVal > 135 && deg < -100) return 180
  if (Math.abs(deg) > 160) return deg < 0 ? 0 : 180

  let ang = Math.round(90 + (deg / 160) * 90)
  if (ang > 180) ang = 180
  if (ang < 0) ang = 0
  return ang
}

function startRestDrag(e: PointerEvent) {
  if ((e.target as HTMLElement)?.closest('.dial-center')) return
  const wrap = (e.currentTarget as HTMLElement)
  const onMove = (moveEv: PointerEvent) => {
    const nextVal = getDialAngle(moveEv, rest.value, wrap)
    hapticTick(nextVal, lastRestHaptic)
    lastRestHaptic = nextVal
    rest.value = nextVal
  }
  const onUp = () => {
    window.removeEventListener('pointermove', onMove)
    window.removeEventListener('pointerup', onUp)
  }
  window.addEventListener('pointermove', onMove)
  window.addEventListener('pointerup', onUp)
  const initialVal = getDialAngle(e, rest.value, wrap)
  hapticTick(initialVal, lastRestHaptic)
  lastRestHaptic = initialVal
  rest.value = initialVal
}

function startPressDrag(e: PointerEvent) {
  if ((e.target as HTMLElement)?.closest('.dial-center')) return
  const wrap = (e.currentTarget as HTMLElement)
  const onMove = (moveEv: PointerEvent) => {
    const nextVal = getDialAngle(moveEv, press.value, wrap)
    hapticTick(nextVal, lastPressHaptic)
    lastPressHaptic = nextVal
    press.value = nextVal
  }
  const onUp = () => {
    window.removeEventListener('pointermove', onMove)
    window.removeEventListener('pointerup', onUp)
  }
  window.addEventListener('pointermove', onMove)
  window.addEventListener('pointerup', onUp)
  const initialVal = getDialAngle(e, press.value, wrap)
  hapticTick(initialVal, lastPressHaptic)
  lastPressHaptic = initialVal
  press.value = initialVal
}

function onSliderInput() {
  hapticTick(dur.value, lastDurHaptic)
  lastDurHaptic = dur.value
}

// Smooth Cubic Ease-Out Revert Animation (strictly matching main/calibration.h:685-723)
let revAnim: number | null = null

function animateRevert(targetRest = DEFAULT_REST, targetPress = DEFAULT_PRESS, targetDur = DEFAULT_DUR) {
  if (revAnim !== null) {
    cancelAnimationFrame(revAnim)
    revAnim = null
  }
  const sRest = Number(rest.value) || DEFAULT_REST
  const sPress = Number(press.value) || DEFAULT_PRESS
  const sDur = Number(dur.value) || DEFAULT_DUR

  const dRest = targetRest - sRest
  const dPress = targetPress - sPress
  const dDur = targetDur - sDur

  if (dRest === 0 && dPress === 0 && dDur === 0) {
    return
  }

  haptic(15)
  const start = performance.now()
  const durMs = 400

  function step(now: number) {
    const p = Math.min(1, (now - start) / durMs)
    const ease = 1 - Math.pow(1 - p, 3) // cubic ease-out matching firmware calibration.h

    rest.value = Math.round(sRest + dRest * ease)
    press.value = Math.round(sPress + dPress * ease)
    dur.value = Math.round(sDur + dDur * ease)

    if (p < 1) {
      revAnim = requestAnimationFrame(step)
    } else {
      revAnim = null
      rest.value = targetRest
      press.value = targetPress
      dur.value = targetDur
      lastRestHaptic = targetRest
      lastPressHaptic = targetPress
      lastDurHaptic = targetDur
    }
  }
  revAnim = requestAnimationFrame(step)
}

// Live Hold-to-Press Simulation with 10-Second Safety Watchdog Safeguard
const isHolding = ref(false)
let holdWatchdog: any = null

function onHoldStart(e: Event) {
  e.preventDefault()
  if (isHolding.value) return
  isHolding.value = true
  haptic(20)

  clearTimeout(holdWatchdog)
  holdWatchdog = setTimeout(() => {
    if (isHolding.value) {
      isHolding.value = false
      haptic(12)
    }
  }, 10000) // 10s firmware safeguard limit
}

function onHoldEnd() {
  if (!isHolding.value) return
  isHolding.value = false
  clearTimeout(holdWatchdog)
  haptic(12)
}

// Test Tap Action
const isTesting = ref(false)
function onTestTap() {
  if (isTesting.value) return
  isTesting.value = true
  haptic([25, 20, 25])
  setTimeout(() => {
    isTesting.value = false
  }, 700)
}

// Save Action
function onSave() {
  haptic([30, 30, 50])
}

// Reset Action
function onReset() {
  haptic([20, 40, 20])
  animateRevert(DEFAULT_REST, DEFAULT_PRESS, DEFAULT_DUR)
}
</script>

<template>
  <div class="esp-scope">
    <div class="wrap">
      <h1>🎯 Servo Calibration</h1>
      <div style="text-align: center; margin-bottom: 20px;">
        <span class="pill on">Calibrated</span>
      </div>
      <div class="cal-header-row">
        <h3>Configuration Metrics</h3>
        <button
          type="button"
          id="btn-revert"
          title="Revert to saved metrics"
          @click="animateRevert(DEFAULT_REST, DEFAULT_PRESS, DEFAULT_DUR)"
        >
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/>
            <path d="M3 3v5h5"/>
          </svg>
        </button>
      </div>

      <div class="card" style="padding: 0; overflow: hidden;">
        <div class="dial-grid no-copy">
          <!-- Rest Angle Dial -->
          <div class="dial-box">
            <div class="dial-title">Rest Angle</div>
            <div class="dial-sub">Hovering Just Above Button</div>
            <div
              class="dial-svg-wrap"
              @pointerdown="startRestDrag"
              style="touch-action: none; cursor: grab;"
            >
              <svg class="dial-svg" viewBox="0 0 160 160">
                <path class="dial-bg" d="M 60.16 134.50 A 58 58 0 1 1 99.84 134.50"/>
                <path class="dial-bar" :d="restCoords.d"/>
                <circle class="dial-thumb" :cx="restCoords.x" :cy="restCoords.y" r="8.5"/>
              </svg>
              <div class="dial-center">
                <div class="dial-badge">
                  <input
                    type="text"
                    inputmode="numeric"
                    pattern="[0-9]*"
                    v-model.number="rest"
                    class="cal-dial-input"
                    style="width: 3.2ch; text-align: center; font-variant-numeric: tabular-nums;"
                    @click.stop
                    @pointerdown.stop
                  />
                  <span class="dial-deg">°</span>
                </div>
              </div>
              <div class="dial-limits">
                <span>0°</span><span>180°</span>
              </div>
            </div>
          </div>

          <!-- Press Angle Dial -->
          <div class="dial-box">
            <div class="dial-title">Press Angle</div>
            <div class="dial-sub">Pushing Button Fully</div>
            <div
              class="dial-svg-wrap"
              @pointerdown="startPressDrag"
              style="touch-action: none; cursor: grab;"
            >
              <svg class="dial-svg" viewBox="0 0 160 160">
                <path class="dial-bg" d="M 60.16 134.50 A 58 58 0 1 1 99.84 134.50"/>
                <path class="dial-bar" :d="pressCoords.d"/>
                <circle class="dial-thumb" :cx="pressCoords.x" :cy="pressCoords.y" r="8.5"/>
              </svg>
              <div class="dial-center">
                <div class="dial-badge">
                  <input
                    type="text"
                    inputmode="numeric"
                    pattern="[0-9]*"
                    v-model.number="press"
                    class="cal-dial-input"
                    style="width: 3.2ch; text-align: center; font-variant-numeric: tabular-nums;"
                    @click.stop
                    @pointerdown.stop
                  />
                  <span class="dial-deg">°</span>
                </div>
              </div>
              <div class="dial-limits">
                <span>0°</span><span>180°</span>
              </div>
            </div>
          </div>
        </div>

        <!-- Duration Slider -->
        <div class="cal-row no-copy">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
            <div>
              <div style="font-size: 14px; font-weight: 600; color: var(--esp-on-surface);">Press Duration</div>
              <div style="font-size: 11.5px; color: var(--esp-on-surface-v);">Hold Duration During Tap</div>
            </div>
            <div class="cal-num-box">
              <input
                type="text"
                inputmode="numeric"
                pattern="[0-9]*"
                v-model.number="dur"
                class="cal-num-raw"
              />
              <span style="font-size: 12px; font-weight: 600; color: var(--esp-on-surface-v); margin-left: 4px;">ms</span>
            </div>
          </div>
          <div style="margin-top: 4px;">
            <input
              type="range"
              min="50"
              max="3000"
              step="10"
              v-model.number="dur"
              class="cal-slider"
              :style="{ background: durFill }"
              @input="onSliderInput"
            />
          </div>
        </div>
      </div>

      <h3>Live Manual Control</h3>
      <div class="card" style="padding: 20px; text-align: center;">
        <button
          type="button"
          class="btn-hold"
          :class="{ holding: isHolding }"
          @pointerdown="onHoldStart"
          @pointerup="onHoldEnd"
          @pointercancel="onHoldEnd"
          @mouseleave="onHoldEnd"
        >
          👇 Press &amp; Hold
        </button>
        <div style="font-size: 12px; color: var(--esp-on-surface-v); margin-top: 12px; line-height: 1.4;">
          Hold to press servo live to Press Angle<br>Release to return to Rest Angle
        </div>
      </div>

      <h3>Calibration Actions</h3>
      <div class="card" style="padding: 18px 20px;">
        <div class="actions" style="margin-top: 0; margin-bottom: 12px;">
          <button
            type="button"
            class="primary"
            :disabled="isTesting"
            style="background: rgba(138,180,248,0.16); color: var(--esp-primary);"
            @click="onTestTap"
          >
            ▶ Test Tap
          </button>
          <button
            type="button"
            style="background: rgba(52,211,153,0.18); border-color: rgba(52,211,153,0.35); color: var(--esp-success);"
            @click="onSave"
          >
            ✔ Save
          </button>
        </div>
        <div class="actions" style="margin-top: 0;">
          <button type="button" class="danger" @click="onReset">🗑 Reset Calibration</button>
        </div>
      </div>

      <a class="back" href="javascript:void(0)">← Back to Dashboard</a>
    </div>
  </div>
</template>
