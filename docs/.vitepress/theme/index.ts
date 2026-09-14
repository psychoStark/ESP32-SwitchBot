import DefaultTheme from 'vitepress/theme'
import type { App } from 'vue'
import BrowserWindow from './BrowserWindow.vue'
import TerminalWindow from './TerminalWindow.vue'
import CalibrationSimulator from './CalibrationSimulator.vue'
import './custom.css'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }: { app: App }) {
    app.component('BrowserWindow', BrowserWindow)
    app.component('TerminalWindow', TerminalWindow)
    app.component('CalibrationSimulator', CalibrationSimulator)

    // Tactile haptic feedback for genuinely interactive controls
    if (typeof window !== 'undefined') {
      const triggerHaptic = (pattern: number | number[]) => {
        if ('vibrate' in navigator) {
          try {
            navigator.vibrate(pattern)
          } catch (_) {}
        }
      }

      // Zero-latency haptic feedback for interactive elements and homepage cards
      const handlePointer = (e: Event) => {
        const target = (e.target as HTMLElement)?.closest<HTMLElement>(
          'button, a, .esp-scope button, .esp-scope .nav a, .btn-hold, .VPButton, #btn-revert, .cal-slider, .VPFeature'
        )
        if (!target) return

        // Prevent double vibration within 150ms
        const now = Date.now()
        if ((target as any)._lastHaptic && now - (target as any)._lastHaptic < 150) return
        ;(target as any)._lastHaptic = now

        // Authentic haptic patterns from main/web_pages.h & calibration.h
        if (target.classList.contains('VPFeature')) {
          triggerHaptic(18)
        } else if (target.classList.contains('primary') || target.classList.contains('brand') || target.getAttribute('href') === '/') {
          triggerHaptic([35, 15, 45])
        } else if (target.classList.contains('back') || target.classList.contains('alt')) {
          triggerHaptic(14)
        } else if (target.classList.contains('danger') || target.classList.contains('warn')) {
          triggerHaptic([20, 35, 20])
        } else if (target.id === 'btn-revert') {
          triggerHaptic(25)
        } else if (target.classList.contains('cal-slider')) {
          triggerHaptic(8)
        } else {
          triggerHaptic(24)
        }

        target.classList.add('tactile-pressed')
        setTimeout(() => target.classList.remove('tactile-pressed'), 160)
      }

      window.addEventListener('pointerdown', handlePointer, { passive: true })
      window.addEventListener('click', handlePointer, { passive: true })
    }
  }
}
