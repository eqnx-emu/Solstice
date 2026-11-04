<template>
  <Navbar-Simple></Navbar-Simple>
  <div class="sx-center">
    <main>
      <h1>{{ $t('logout.logged_out') }}</h1>
      <p class="sx-hint">{{ $t('logout.logged_out_desc') }}</p>
      <a class="btn btn-primary align-self-start" href="./">{{ $t('logout.login') }}</a>
    </main>
  </div>
</template>

<script>
  import NavbarSimple from './NavbarSimple.vue'

  export default {
    components: {
      NavbarSimple,
    },
  }
</script>
