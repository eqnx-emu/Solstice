<template>
  <div>
    <nav class="navbar navbar-expand-lg navbar-sunshine" :aria-label="$t('navbar.main_navigation')">
      <div class="container-fluid px-lg-4">
        <RouterLink class="navbar-brand" to="/">
          <img src="/images/logo-sunshine-45.png" height="28" width="28" alt="">
          Solstice
        </RouterLink>
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarSupportedContent"
                aria-controls="navbarSupportedContent" aria-expanded="false" :aria-label="$t('navbar.toggle_navigation')">
          <span class="navbar-toggler-icon"></span>
        </button>
        <div class="collapse navbar-collapse" id="navbarSupportedContent">
          <ul class="navbar-nav me-auto mb-2 mb-lg-0 gap-lg-1">
            <li class="nav-item" v-for="item in items" :key="item.to">
              <RouterLink class="nav-link" :to="item.to" :class="{ active: isActive(item) }"
                          :aria-current="isActive(item) ? 'page' : null">
                {{ $t(item.label) }}
              </RouterLink>
            </li>
          </ul>
          <ul class="navbar-nav ms-auto mb-2 mb-lg-0 align-items-lg-center">
            <li class="nav-item">
              <ThemeToggle/>
            </li>
            <li class="nav-item dropdown">
              <button class="nav-link dropdown-toggle" type="button" id="navbarUserMenu"
                      data-bs-toggle="dropdown" aria-expanded="false">
                {{ $t('navbar.account') }}
              </button>
              <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="navbarUserMenu">
                <li>
                  <RouterLink class="dropdown-item d-flex align-items-center" to="/password">
                    {{ $t('navbar.password') }}
                  </RouterLink>
                </li>
                <li><hr class="dropdown-divider"></li>
                <li>
                  <button type="button" class="dropdown-item d-flex align-items-center" @click="logout">
                    {{ $t('navbar.logout') }}
                  </button>
                </li>
              </ul>
            </li>
          </ul>
        </div>
      </div>
    </nav>
    <Notification></Notification>
  </div>
</template>

<script>
import ThemeToggle from './ThemeToggle.vue'
import Notification from './Notification.vue'

export default {
  components: {
    ThemeToggle,
    Notification,
  },
  data() {
    return {
      // Featured apps live under Applications, so that item stays active there too.
      items: [
        { to: '/', label: 'navbar.home', exact: true },
        { to: '/apps', label: 'navbar.applications', also: ['/featured'] },
        { to: '/pin', label: 'navbar.pairing' },
        { to: '/config', label: 'navbar.configuration' },
        { to: '/troubleshooting', label: 'navbar.troubleshoot' },
      ],
    }
  },
  methods: {
    isActive(item) {
      const path = this.$route?.path || ''
      if (item.exact) {
        return path === item.to
      }
      return path.startsWith(item.to) || (item.also || []).some(p => path.startsWith(p))
    },
    logout() {
      const cacheBuster = Date.now().toString()
      const logoutPageUrl = new URL('/logout', globalThis.location.href)
      const request = new XMLHttpRequest()
      const finish = () => {
        globalThis.location.replace(logoutPageUrl.toString())
      }

      request.open('GET', '/', true, 'sunshine-logout', cacheBuster)
      request.setRequestHeader('Cache-Control', 'no-store')
      request.onload = finish
      request.onerror = finish
      request.ontimeout = finish
      request.timeout = 5000
      request.send()
    }
  }
}
</script>

<style>
/** @brief Keep the Sunshine toggler icon above Bootstrap's light and dark variants. */
.navbar-sunshine .navbar-toggler .navbar-toggler-icon {
  --bs-navbar-toggler-icon-bg: url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 30 30'%3e%3cpath stroke='rgba%28255, 255, 255, 0.9%29' stroke-linecap='round' stroke-miterlimit='10' stroke-width='2' d='M4 7h22M4 15h22M4 23h22'/%3e%3c/svg%3e");
}
</style>
