<script setup>
import { ref } from 'vue'
import Checkbox from '../../../Checkbox.vue'

const props = defineProps({
  platform: String,
  config: Object,
})

const config = ref(props.config)
</script>

<template>
  <div class="mb-3" v-if="platform === 'windows'">
    <label for="virtual_display" class="form-label">{{ $t('config.virtual_display') }}</label>
    <select id="virtual_display" class="form-select" v-model="config.virtual_display">
      <option value="disabled">{{ $t('_common.disabled_def') }}</option>
      <option value="apps">{{ $t('config.virtual_display_apps') }}</option>
      <option value="always">{{ $t('config.virtual_display_always') }}</option>
    </select>
    <div class="form-text">{{ $t('config.virtual_display_desc') }}</div>
  </div>
  <div class="mb-3" v-if="platform === 'windows' && config.virtual_display !== 'disabled'">
    <Checkbox id="virtual_display_secondary"
      locale-prefix="config"
      v-model="config.virtual_display_secondary"
      default="false"
    ></Checkbox>
  </div>
</template>
