<template>
  <section class="sx-panel">
    <div class="sx-panel-head">
      <div>
        <h2 id="unpair">{{ $t('troubleshooting.unpair_title') }}</h2>
        <p class="sx-hint">{{ $t('troubleshooting.unpair_desc') }}</p>
      </div>
      <button class="btn btn-sx-danger" :disabled="unpairAllPressed || !clients.length" @click="unpairAll">
        {{ $t('troubleshooting.unpair_all') }}
      </button>
    </div>
    <div class="sx-panel-body" v-if="showApplyMessage || unpairAllStatus !== null">
      <div class="alert alert-success d-flex align-items-center gap-3 mb-0" v-if="showApplyMessage">
        <div class="flex-grow-1"><b>{{ $t('_common.success') }}</b> {{ $t('troubleshooting.unpair_single_success') }}</div>
        <button class="btn btn-sx btn-sm" @click="showApplyMessage = false">{{ $t('_common.dismiss') }}</button>
      </div>
      <div class="alert alert-success mb-0" v-if="unpairAllStatus === true">{{ $t('troubleshooting.unpair_all_success') }}</div>
      <div class="alert alert-danger mb-0" v-if="unpairAllStatus === false">{{ $t('troubleshooting.unpair_all_error') }}</div>
    </div>
    <ul class="sx-rows" v-if="clients.length">
      <li v-for="client in clients" :key="client.uuid" class="sx-row">
        <span class="sx-row-main fw-medium">{{ client.name !== '' ? client.name : $t('troubleshooting.unpair_single_unknown') }}</span>
        <div class="form-check form-switch mb-0">
          <input class="form-check-input" type="checkbox" role="switch"
                 :id="'toggle-' + client.uuid"
                 :checked="client.enabled"
                 @change="toggleClient(client.uuid, !client.enabled)">
          <label class="form-check-label" :for="'toggle-' + client.uuid">{{ $t('pin.client_enabled') }}</label>
        </div>
        <button class="btn btn-sx" @click="unpairSingle(client.uuid)">{{ $t('pin.remove') }}</button>
      </li>
    </ul>
    <p v-else class="sx-panel-body sx-hint">{{ $t('troubleshooting.unpair_single_no_devices') }}</p>
  </section>
</template>

<script>
import { apiFetch } from './fetch_utils'

/**
 * Paired Moonlight clients with enable, unpair and unpair-all controls.
 */
export default {
  data() {
    return {
      clients: [],
      showApplyMessage: false,
      unpairAllPressed: false,
      unpairAllStatus: null,
    }
  },
  mounted() {
    this.refreshClients()
  },
  methods: {
    /** Reload the paired client list. Call after a new device pairs. */
    refreshClients() {
      return fetch('./api/clients/list')
        .then(response => response.json())
        .then(response => {
          if (response.status === true && response.named_certs && response.named_certs.length) {
            this.clients = response.named_certs.sort((a, b) => {
              return (a.name.toLowerCase() > b.name.toLowerCase() || a.name === '' ? 1 : -1)
            })
          } else {
            this.clients = []
          }
        })
        .catch(error => console.error('Failed to load paired clients', error))
    },
    unpairAll() {
      this.unpairAllPressed = true
      apiFetch('./api/clients/unpair-all', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
      })
        .then(r => r.json())
        .then(r => {
          this.unpairAllPressed = false
          this.unpairAllStatus = r.status
          setTimeout(() => {
            this.unpairAllStatus = null
          }, 5000)
          this.refreshClients()
        })
    },
    unpairSingle(uuid) {
      apiFetch('./api/clients/unpair', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ uuid }),
      }).then(() => {
        this.showApplyMessage = true
        this.refreshClients()
      })
    },
    toggleClient(uuid, enabled) {
      fetch('./api/clients/update', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ uuid, enabled }),
      }).then(() => {
        this.refreshClients()
      })
    },
  },
}
</script>
