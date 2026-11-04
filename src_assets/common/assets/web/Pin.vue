<template>
  <Navbar></Navbar>
  <main id="content" class="container sx-page">
    <header class="sx-page-header">
      <div>
        <h1>{{ $t('pin.pin_pairing') }}</h1>
        <p class="sx-hint">{{ $t('pin.page_desc') }}</p>
      </div>
    </header>

    <form id="form" class="sx-split" @submit.prevent="registerDevice">
      <section class="sx-panel sx-wide">
        <div class="sx-panel-head"><h2>{{ $t('pin.waiting_title') }}</h2></div>
        <fieldset class="sx-panel-body d-flex flex-column gap-2 border-0 m-0">
          <legend class="sx-hint mb-2">{{ $t('pin.select_pairing') }}</legend>
          <label class="sx-pick" v-for="pairing in pendingPairings" :key="pairing.id">
            <input type="radio" name="pairing" :value="pairing.id" v-model="selectedPairingId" required>
            <span class="flex-grow-1">
              <span class="d-block fw-medium">{{ pairing.name || $t('pin.unknown_device') }}</span>
              <span class="sx-mono sx-hint">{{ pairing.address || $t('pin.unknown_address') }}</span>
            </span>
          </label>
          <p v-if="!pendingPairings.length" class="sx-hint mb-0">{{ $t('pin.no_pending_pairings') }}</p>
          <div>
            <button type="button" class="btn btn-sx-danger mt-2" :disabled="!selectedPairingId" @click="cancelSelectedPairing">
              {{ $t('pin.cancel_pairing') }}
            </button>
          </div>
        </fieldset>
      </section>

      <section class="sx-panel sx-narrow">
        <div class="sx-panel-head"><h2>{{ $t('pin.enter_pin') }}</h2></div>
        <div class="sx-panel-body d-flex flex-column gap-3">
          <div>
            <label for="pin-input" class="form-label">{{ $t('pin.pin_from_moonlight') }}</label>
            <input v-model="pin" type="text" pattern="\d{4}" maxlength="4" inputmode="numeric" placeholder="0000"
                   id="pin-input" class="form-control sx-pin-input" required />
          </div>
          <div>
            <label for="name-input" class="form-label">{{ $t('pin.device_name') }}</label>
            <input v-model="name" type="text" id="name-input" class="form-control" required />
          </div>
          <button type="submit" class="btn btn-accent">{{ $t('pin.send') }}</button>
          <div v-if="status" :class="`alert alert-${status.type} mb-0`" role="alert">{{ status.message }}</div>
          <p class="sx-callout"><b>{{ $t('_common.warning') }}</b> {{ $t('pin.warning_msg') }}</p>
        </div>
      </section>
    </form>

    <PairedDevices ref="pairedDevices"></PairedDevices>
  </main>
</template>

<script>
  import Navbar from './Navbar.vue'
  import PairedDevices from './PairedDevices.vue'
  import { apiFetch } from './fetch_utils'

  export default {
    components: {
      Navbar,
      PairedDevices,
    },
    inject: ['i18n'],
    data() {
      return {
        name: '',
        pendingPairings: [],
        pin: '',
        refreshTimer: null,
        selectedPairingId: '',
        status: null,
      };
    },
    watch: {
      /** Offer the requesting device's own name so the operator only types the PIN. */
      selectedPairingId(id) {
        const selected = this.pendingPairings.find((pairing) => pairing.id === id);
        if (selected && !this.name) {
          this.name = selected.name || '';
        }
      },
    },
    mounted() {
      this.loadPendingPairings();
      this.refreshTimer = window.setInterval(() => this.loadPendingPairings(), 2000);
    },
    beforeUnmount() {
      window.clearInterval(this.refreshTimer);
    },
    methods: {
      /**
       * Refresh the authenticated list of pairing requests awaiting approval.
       */
      async loadPendingPairings() {
        try {
          const response = await apiFetch('./api/pin', {method: 'GET'});
          if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
          }

          const body = await response.json();
          this.pendingPairings = body.pairings || [];
          if (!this.pendingPairings.some((pairing) => pairing.id === this.selectedPairingId)) {
            this.selectedPairingId = this.pendingPairings.length === 1 ? this.pendingPairings[0].id : '';
          }
        } catch (error) {
          console.error('Failed to load pending pairing requests', error);
          this.status = {type: 'danger', message: this.i18n.t('pin.load_failure')};
        }
      },

      /**
       * Apply the entered PIN only to the pairing request selected by the operator.
       */
      async registerDevice() {
        this.status = null;
        if (!this.selectedPairingId) {
          this.status = {type: 'danger', message: this.i18n.t('pin.select_pairing_required')};
          return;
        }

        const body = JSON.stringify({
          pairing_id: this.selectedPairingId,
          pin: this.pin,
          name: this.name,
        });
        const response = await apiFetch('./api/pin', {
          method: "POST",
          headers: {
            'Content-Type': 'application/json'
          },
          body,
        });
        const result = await response.json();
        if (result.status === true) {
          this.status = {type: 'success', message: this.i18n.t('pin.pair_success')};
          this.pin = '';
          this.name = '';
          this.$refs.pairedDevices?.refreshClients();
        } else {
          this.status = {type: 'danger', message: this.i18n.t('pin.pair_failure')};
        }
        await this.loadPendingPairings();
      },

      /**
       * Cancel the selected pending request without affecting other clients.
       */
      async cancelSelectedPairing() {
        if (!this.selectedPairingId) {
          return;
        }

        const response = await apiFetch('./api/pin', {
          method: 'DELETE',
          headers: {
            'Content-Type': 'application/json',
          },
          body: JSON.stringify({pairing_id: this.selectedPairingId}),
        });
        const result = await response.json();
        this.status = result.status === true
          ? {type: 'success', message: this.i18n.t('pin.cancel_success')}
          : {type: 'danger', message: this.i18n.t('pin.cancel_failure')};
        await this.loadPendingPairings();
      }
    }
  }
</script>
