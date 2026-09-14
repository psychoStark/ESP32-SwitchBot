import { defineConfig } from 'vitepress'

export default defineConfig({
  title: "ESP32-SwitchBot",
  description: "High-Performance ESP32 Switch Actuator with Web Dashboard, Terminal TUI & Embedded Tailscale VPN",
  base: process.env.GITHUB_ACTIONS ? '/ESP32-SwitchBot/' : '/',
  appearance: 'force-dark',

  head: [
    ['link', { rel: 'icon', href: 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><text y=".9em" font-size="90">🤖</text></svg>' }],
    ['meta', { name: 'theme-color', content: '#000000' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'ESP32-SwitchBot Documentation' }],
    ['meta', { property: 'og:description', content: 'Technical architecture, setup guide, and REST API specification for ESP32-SwitchBot.' }],
  ],

  themeConfig: {
    siteTitle: 'ESP32-SwitchBot',

    nav: [
      { text: 'Guide', link: '/guide/getting-started' },
      { text: 'Interfaces', link: '/interfaces/web-dashboard' },
      { text: 'Architecture', link: '/architecture/concurrency-power' },
      { text: 'API Reference', link: '/interfaces/rest-api' },
      { text: 'v1.2', link: '/about' }
    ],

    sidebar: [
      {
        text: 'Getting Started',
        items: [
          { text: 'Overview & Hardware', link: '/guide/getting-started' },
          { text: 'Servo Horn Setup & Centering', link: '/guide/servo-setup' },
          { text: 'Configuration & Secrets', link: '/guide/configuration' },
        ]
      },
      {
        text: 'User Interfaces',
        items: [
          { text: 'Web Dashboard & Calibration', link: '/interfaces/web-dashboard' },
          { text: 'Interactive Terminal cURL TUI', link: '/interfaces/terminal-tui' },
          { text: 'REST API & Endpoints', link: '/interfaces/rest-api' },
        ]
      },
      {
        text: 'Architecture Deep-Dive',
        items: [
          { text: 'FreeRTOS Concurrency & Thermals', link: '/architecture/concurrency-power' },
          { text: 'Embedded Tailscale & HA Failover', link: '/architecture/networking-tailscale' },
          { text: 'Zero-Copy Streaming & Memory', link: '/architecture/memory-streaming' },
          { text: 'NVS Wear-Leveling Forensics', link: '/architecture/nvs-wear-leveling' },
          { text: 'Over-The-Air (OTA) & Multi-Network', link: '/architecture/ota-troubleshooting' },
        ]
      },
      {
        text: 'Project Info',
        items: [
          { text: 'About & Changelog (v1.2)', link: '/about' }
        ]
      }
    ],

    search: {
      provider: 'local'
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/psychoStark/ESP32-SwitchBot' }
    ],

    footer: {
      message: 'Released under Apache 2.0 License • 24/7 Operational Reliability',
      copyright: 'Copyright © 2026 psychoStark & Contributors • v1.2'
    }
  }
})
