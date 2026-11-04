<template>
  <footer class="sx-footer-links" :aria-label="$t('resource_card.resources')">
    <a href="https://app.lizardbyte.dev" target="_blank" rel="noopener">{{ $t('resource_card.lizardbyte_website') }}</a>
    <a :href="documentationUrl" target="_blank" rel="noopener">{{ $t('resource_card.documentation') }}</a>
    <a href="https://app.lizardbyte.dev/discord" target="_blank" rel="noopener">Discord</a>
    <a href="https://github.com/orgs/LizardByte/discussions" target="_blank" rel="noopener">{{ $t('resource_card.github_discussions') }}</a>
    <a href="https://github.com/LizardByte/Sunshine/blob/master/LICENSE" target="_blank" rel="noopener">{{ $t('resource_card.license') }}</a>
    <a href="https://github.com/LizardByte/Sunshine/blob/master/NOTICE" target="_blank" rel="noopener">{{ $t('resource_card.third_party_notice') }}</a>
  </footer>
</template>

<script>
export default {
  props: {
    installedVersionNotStable: {
      type: Boolean,
      default: false
    }
  },
  computed: {
    documentationUrl() {
      const docsVersion = this.installedVersionNotStable ? 'master' : 'latest'
      return `https://docs.lizardbyte.dev/projects/sunshine/${docsVersion}/`
    }
  }
}
</script>
