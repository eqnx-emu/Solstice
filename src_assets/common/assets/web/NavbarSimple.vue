<template>
  <nav class="navbar navbar-sunshine">
    <div class="container-fluid">
      <span class="navbar-brand" title="Solstice">
        <img src="/images/logo-sunshine-45.png" height="45" alt="Solstice">
      </span>
      <ThemeToggle/>
    </div>
  </nav>
</template>

<script>
import ThemeToggle from './ThemeToggle.vue'

export default {
  components: {
    ThemeToggle,
  },
}
</script>
