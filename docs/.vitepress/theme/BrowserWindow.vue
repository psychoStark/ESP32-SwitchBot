<script setup lang="ts">
defineProps<{
  title?: string
}>()
</script>

<template>
  <div class="snapshot-browser">
    <div class="snapshot-header">
      <span class="snapshot-dot red"></span>
      <span class="snapshot-dot yellow"></span>
      <span class="snapshot-dot green"></span>
      <span class="snapshot-title">{{ title || 'Browser' }}</span>
    </div>
    <div class="snapshot-body">
      <slot />
    </div>
  </div>
</template>
