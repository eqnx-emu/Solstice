<template>
  <Navbar></Navbar>
  <main id="content" class="container sx-page">
    <header class="sx-page-header">
      <div>
        <h1>{{ $t(needsAttention ? 'index.status_attention' : 'index.status_ready') }}</h1>
        <p class="sx-hint">{{ $t(needsAttention ? 'index.status_attention_desc' : 'index.status_ready_desc') }}</p>
      </div>
      <div class="sx-page-actions">
        <RouterLink class="btn btn-sx" to="/troubleshooting#logs">{{ $t('index.view_logs') }}</RouterLink>
      </div>
    </header>

    <!-- Fatal Errors Alert -->
    <div class="alert alert-danger my-4" v-if="fatalLogs.length">
      <div class="d-flex align-items-center mb-3">
        <alert-circle :size="24" class="icon me-3"></alert-circle>
        <div v-html="$t('index.startup_errors')"></div>
      </div>
      <ul class="mb-3">
        <li v-for="v in fatalLogs" :key="`${v.timestamp}-${v.value}`">{{v.value}}</li>
      </ul>
      <RouterLink class="btn btn-danger" to="/troubleshooting#logs">{{ $t('index.view_logs') }}</RouterLink>
    </div>

    <div class="alert alert-warning" v-if="missingPermissions.length">
      <div class="d-flex align-items-center gap-3 mb-2">
        <alert-triangle :size="20" class="icon"></alert-triangle>
        <strong>{{ $t('index.permissions_missing_title') }}</strong>
      </div>
      <p>{{ $t('index.permissions_missing_desc') }}</p>
      <RouterLink class="btn btn-sx" to="/troubleshooting#permissions">
        {{ $t('index.permissions_review') }}
      </RouterLink>
    </div>

    <!-- Virtual gamepad broker status -->
    <div class="alert my-4" :class="virtualInputNotice.alertClass" v-if="virtualInputNotice">
      <div class="d-flex align-items-start gap-3 mb-3">
        <alert-triangle v-if="virtualInputNotice.warning" :size="20" class="icon flex-shrink-0"></alert-triangle>
        <info v-else :size="20" class="icon flex-shrink-0"></info>
        <div>
          <p class="mb-1"><strong>{{ $t(virtualInputNotice.title) }}</strong></p>
          <p v-for="message in virtualInputNotice.messages" :key="message.key" class="mb-1">
            {{ $t(message.key, message.params || {}) }}
          </p>
        </div>
      </div>
      <RouterLink class="btn btn-sx" :to="virtualInputNotice.to">
        {{ $t(virtualInputNotice.action) }}
      </RouterLink>
    </div>

    <!-- Pending pairing request: the one action that cannot wait -->
    <section class="sx-panel sx-panel-highlight home-pairing" v-if="pendingPairings.length">
      <div class="sx-panel-head">
        <div>
          <h2>{{ $t('index.pairing_request_title', { name: primaryPairing.name || $t('pin.unknown_device') }) }}</h2>
          <p class="sx-hint">{{ $t('index.pairing_request_desc') }}</p>
        </div>
      </div>
      <form class="sx-panel-body d-flex flex-wrap align-items-end gap-3" @submit.prevent="pairPrimary">
        <div class="d-flex flex-column gap-1">
          <label for="home-pin" class="form-label mb-0">{{ $t('navbar.pin') }}</label>
          <input id="home-pin" v-model="pin" class="form-control sx-pin-input sx-pin-inline" type="text"
                 inputmode="numeric" pattern="\d{4}" maxlength="4" placeholder="0000" required>
        </div>
        <button class="btn btn-accent" type="submit">{{ $t('index.pair') }}</button>
        <RouterLink class="btn btn-link" to="/pin" v-if="pendingPairings.length > 1">
          {{ $t('index.other_requests', { count: pendingPairings.length - 1 }) }}
        </RouterLink>
        <p v-if="pairStatus" class="w-100 mb-0" :class="pairStatus.ok ? 'sx-ok' : 'sx-bad'" role="status">
          {{ $t(pairStatus.key) }}
        </p>
      </form>
    </section>

    <div class="sx-split">
      <section class="sx-panel sx-wide">
        <div class="sx-panel-head">
          <h2>{{ $t('index.paired_devices') }}</h2>
          <RouterLink to="/pin">{{ $t('index.manage') }}</RouterLink>
        </div>
        <ul class="sx-rows" v-if="clients.length">
          <li class="sx-row" v-for="client in clients" :key="client.uuid">
            <span class="sx-row-main"><span class="fw-medium">{{ client.name || $t('troubleshooting.unpair_single_unknown') }}</span></span>
            <span class="sx-tag" v-if="client.enabled === false">{{ $t('index.client_disabled') }}</span>
          </li>
        </ul>
        <p class="sx-panel-body sx-hint" v-else>{{ $t('index.no_paired_devices') }}</p>
      </section>

      <section class="sx-panel sx-narrow">
        <div class="sx-panel-head"><h2>{{ $t('index.host') }}</h2></div>
        <ul class="sx-rows">
          <li class="sx-row" v-if="platform">
            <span class="sx-row-main">{{ $t('index.platform') }}</span>
            <span class="sx-mono">{{ platform }}</span>
          </li>
          <li class="sx-row">
            <span class="sx-row-main">{{ $t('index.gamepad') }}</span>
            <span :class="virtualInputNotice && virtualInputNotice.warning ? 'sx-warn' : 'sx-ok'">
              {{ !controllerEnabled || gamepadDriver === 'none' ? $t('index.gamepad_disabled') : (virtualInputNotice && virtualInputNotice.warning ? $t('index.needs_attention') : $t('index.ok')) }}
            </span>
          </li>
          <li class="sx-row">
            <span class="sx-row-main">{{ $t('index.permissions') }}</span>
            <span :class="missingPermissions.length ? 'sx-warn' : 'sx-ok'">
              {{ missingPermissions.length ? $t('index.needs_attention') : $t('index.ok') }}
            </span>
          </li>
          <li class="sx-row">
            <span class="sx-row-main">{{ $t('index.startup') }}</span>
            <span :class="fatalLogs.length ? 'sx-bad' : 'sx-ok'">
              {{ fatalLogs.length ? $t('index.startup_failed') : $t('index.ok') }}
            </span>
          </li>
        </ul>
      </section>
    </div>

    <!-- Version -->
    <section class="sx-dashed home-version" v-if="version">
      <div class="d-flex flex-wrap align-items-center gap-3 p-3">
        <p class="mb-0 flex-grow-1">
          <strong>{{ $t('index.version', { version: version.version }) }}</strong>
          <span class="sx-hint ms-2" v-if="loading">{{ $t('index.loading_latest') }}</span>
          <span class="sx-hint ms-2" v-else-if="buildVersionIsDirty">{{ $t('index.version_dirty') }}</span>
          <span class="sx-hint ms-2" v-else-if="installedVersionNotStable">{{ $t('index.installed_version_not_stable') }}</span>
          <span class="sx-hint ms-2" v-else-if="availableRelease">{{ $t(availableRelease.labelKey) }} <strong>{{ availableRelease.release.name }}</strong></span>
          <span class="sx-hint ms-2" v-else>{{ $t('index.version_latest') }}</span>
        </p>
        <a v-if="availableRelease" class="btn btn-primary" :href="availableRelease.release.html_url" target="_blank" rel="noopener">
          <download :size="18" class="icon"></download>
          {{ $t('index.download') }}
        </a>
      </div>
      <details v-if="availableRelease && availableRelease.release.body" class="px-3 pb-3">
        <summary>{{ $t('index.release_notes') }}</summary>
        <div class="markdown-body release-notes mt-2" v-html="convertMarkdownToHtml(availableRelease.release.body)"></div>
      </details>
    </section>

    <Resource-Card :installed-version-not-stable="installedVersionNotStable"></Resource-Card>
  </main>
</template>

<script>
  import { marked } from 'marked'
  import Navbar from './Navbar.vue'
  import ResourceCard from './ResourceCard.vue'
  import SunshineVersion from './sunshine_version'
  import { apiFetch } from './fetch_utils'
  import {
    AlertCircle,
    AlertTriangle,
    Info,
    Download,
  } from '@lucide/vue'

  // Configure marked to allow HTML
  marked.setOptions({
    breaks: true,
    gfm: true,
    headerIds: true,
    mangle: false,
    sanitize: false
  });

  console.log("Hello, Sunshine!")
  export default {
    components: {
      Navbar,
      ResourceCard,
      AlertCircle,
      AlertTriangle,
      Info,
      Download,
    },
    data() {
      return {
        version: null,
        githubVersion: null,
        notifyPreReleases: false,
        preReleaseVersion: null,
        loading: true,
        logs: null,
        platform: "",
        controllerEnabled: false,
        gamepadDriver: '',
        virtualhid: null,
        virtualhidLicense: null,
        vigembus: null,
        permissions: [],
        pendingPairings: [],
        clients: [],
        pin: '',
        pairStatus: null,
        pairingTimer: null,
      }
    },
    mounted() {
      this.loadClients();
      this.loadPendingPairings();
      this.pairingTimer = globalThis.setInterval(() => this.loadPendingPairings(), 3000);
    },
    beforeUnmount() {
      globalThis.clearInterval(this.pairingTimer);
    },
    async created() {
      try {
        let config = await fetch("./api/config").then((r) => r.json());
        this.notifyPreReleases = config.notify_pre_releases;
        this.platform = config.platform;
        this.controllerEnabled = config.controller !== "disabled";
        this.gamepadDriver = config.gamepad_driver || '';
        this.version = new SunshineVersion(null, config.version);
        try {
          const response = await fetch('./api/permissions');
          this.permissions = (await response.json()).permissions || [];
        } catch (e) {
          console.error('Failed to fetch permission status:', e);
        }
        console.log("Version: ", this.version.version)
        this.githubVersion = new SunshineVersion(await fetch("https://api.github.com/repos/LizardByte/Sunshine/releases/latest").then((r) => r.json()), null);
        console.log("GitHub Version: ", this.githubVersion.version)
        this.preReleaseVersion = new SunshineVersion((await fetch("https://api.github.com/repos/LizardByte/Sunshine/releases").then((r) => r.json())).find(release => release.prerelease), null);
        console.log("Pre-Release Version: ", this.preReleaseVersion.version)

        // Read the broker version on both platforms to identify development builds.
        if (this.platform === 'windows' || this.platform === 'macos') {
          try {
            const virtualInputStatus = await fetch("./api/virtual-input/status").then((r) => r.json());
            this.virtualhid = virtualInputStatus.virtualhid;
            if (this.platform === 'windows') {
              this.vigembus = virtualInputStatus.vigembus;
            }
          } catch (e) {
            console.error("Failed to fetch virtual input driver status:", e);
          }
        }
        if (this.platform === 'windows' || this.platform === 'macos') {
          try {
            this.virtualhidLicense = await fetch("./api/virtual-input/license").then((r) => r.json());
          } catch (e) {
            console.error("Failed to fetch Virtual HID Broker license status:", e);
          }
        }
      } catch (e) {
        console.error(e);
      }
      try {
        this.logs = (await fetch("./api/logs").then(r => r.text()))
      } catch (e) {
        console.error(e);
      }
      this.loading = false;
    },
    computed: {
      /** Return the oldest pending pairing request, which the home page answers inline. */
      primaryPairing() {
        return this.pendingPairings[0] || {};
      },
      /** Return fatal startup log entries. */
      fatalLogs() {
        return this.fancyLogs.filter(x => x.level === 'Fatal');
      },
      /** Whether something on the host blocks or degrades streaming. */
      needsAttention() {
        return this.fatalLogs.length > 0 || this.missingPermissions.length > 0 || !!this.virtualInputNotice?.warning;
      },
      /** Return the newer release to offer, preferring a pre-release only when the user opted in. */
      availableRelease() {
        if (this.notifyPreReleases && this.preReleaseBuildAvailable) {
          return { release: this.preReleaseVersion.release, labelKey: 'index.new_pre_release' };
        }
        if (this.stableBuildAvailable) {
          return { release: this.githubVersion.release, labelKey: 'index.new_stable' };
        }
        return null;
      },
      /** Return required permissions that the current platform can check and has not granted. */
      missingPermissions() {
        return this.permissions.filter(permission => permission.required && permission.verifiable && permission.status !== 'granted');
      },
      /**
       * Build the virtual-input message shown on the home page.
       * Warn about broker or gamepad driver issues when virtual gamepads are enabled.
       */
      virtualInputNotice() {
        if (!this.controllerEnabled || this.gamepadDriver === 'none') {
          return null;
        }

        if (this.platform === 'macos') {
          return this.buildMacosVirtualInputNotice();
        }

        if (this.platform !== 'windows' || !this.virtualhid || !this.vigembus) {
          return null;
        }

        const vigembusUsable = this.vigembus.installed && this.vigembus.version_compatible;

        if (!this.gamepadDriver) {
          return this.buildVirtualInputNotice(false, 'index.gamepad_driver_choice_title', [{
            key: 'index.gamepad_driver_choice_desc',
          }], {
            action: 'index.choose_gamepad_driver',
            chooseDriver: true,
            to: '/config#gamepad_driver',
          });
        }

        if (this.gamepadDriver === 'vigembus' || this.gamepadDriver === 'viiper') {
          return this.buildVigembusNotice(vigembusUsable);
        }

        if (this.virtualhid.installed) {
          return this.buildInstalledVirtualhidNotice(vigembusUsable);
        }

        if (this.gamepadDriver === 'virtualhid') {
          return this.buildVirtualInputNotice(true, 'index.virtualhid_broker_unavailable_title', [{ key: 'index.virtualhid_required_desc' }]);
        }

        if (this.controllerEnabled && !vigembusUsable) {
          return this.buildVirtualInputNotice(true, 'index.virtual_input_unavailable_title', [{ key: 'index.virtual_input_unavailable_desc' }]);
        }

        return this.buildVirtualInputNotice(
          false,
          'index.virtualhid_optional_title',
          [{ key: this.controllerEnabled ? 'index.virtualhid_optional_vigembus_desc' : 'index.virtualhid_optional_desc' }],
        );
      },
      installedVersionNotStable() {
        if (!this.githubVersion || !this.version) {
          return false;
        }
        return this.version.isGreater(this.githubVersion);
      },
      stableBuildAvailable() {
        if (!this.githubVersion || !this.version) {
          return false;
        }
        return this.githubVersion.isGreater(this.version);
      },
      preReleaseBuildAvailable() {
        if (!this.preReleaseVersion || !this.githubVersion || !this.version) {
          return false;
        }
        return this.preReleaseVersion.isGreater(this.version) && this.preReleaseVersion.isGreater(this.githubVersion);
      },
      buildVersionIsDirty() {
        return this.version.version?.split(".").length === 5 &&
          this.version.version.includes("dirty")
      },
      /** Parse the text errors, calculating the text, the timestamp and the level */
      fancyLogs() {
        if (!this.logs) return [];
        let regex = /(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}]):\s/g;
        let rawLogLines = (this.logs.split(regex)).splice(1);
        let logLines = []
        for (let i = 0; i < rawLogLines.length; i += 2) {
          logLines.push({ timestamp: rawLogLines[i], level: rawLogLines[i + 1].split(":")[0], value: rawLogLines[i + 1] });
        }
        return logLines;
      }
    },
    methods: {
      /**
       * Build the macOS broker notice, prioritizing license and service warnings.
       *
       * @returns {object|null} Warning or development-build notice, when applicable.
       */
      buildMacosVirtualInputNotice() {
        if (this.virtualhidLicense && !this.virtualhidLicense.licensed) {
          return this.virtualhidLicense.service_available
            ? this.buildVirtualInputNotice(true, 'index.virtualhid_macos_license_title', [{ key: 'index.virtualhid_macos_license_desc' }])
            : this.buildVirtualInputNotice(true, 'index.virtualhid_broker_unavailable_title', [{ key: 'index.virtualhid_macos_broker_desc' }]);
        }
        return this.virtualhid?.development_version
          ? this.buildVirtualInputNotice(false, 'index.virtualhid_development_title', [{ key: 'index.virtualhid_development_desc' }])
          : null;
      },
      /**
       * Build a home-page notice for the current virtual-input state.
       *
       * @param {boolean} warning Whether the notice represents an actionable warning.
       * @param {string} title Localization key for the notice title.
       * @param {object[]} messages Localized message descriptors.
       * @param {object} options Optional action and destination overrides.
       * @returns {object} Notice data consumed by the template.
       */
      buildVirtualInputNotice(warning, title, messages, options = {}) {
        return {
          action: options.action || 'index.review_virtual_input',
          alertClass: warning ? 'alert-warning' : 'alert-info',
          buttonClass: warning ? 'btn-warning' : 'btn-info',
          chooseDriver: options.chooseDriver || false,
          to: options.to || '/troubleshooting#virtualhid',
          messages,
          title,
          warning,
        };
      },
      /**
       * Build the notice for an explicitly selected ViGEmBus backend.
       *
       * @param {boolean} vigembusUsable Whether ViGEmBus is installed and compatible.
       * @returns {object|null} Warning data, or no notice when ViGEmBus is usable.
       */
      buildVigembusNotice(vigembusUsable) {
        if (!this.controllerEnabled || vigembusUsable) {
          return null;
        }
        return this.buildVirtualInputNotice(true, 'index.vigembus_required_title', [{
          key: this.vigembus.installed ? 'index.vigembus_outdated_desc' : 'index.vigembus_not_installed_desc',
          params: { version: this.vigembus.version, supported_versions: this.vigembus.supported_versions },
        }]);
      },
      /**
       * Build the notice for an installed Virtual HID Driver.
       *
       * @param {boolean} vigembusUsable Whether ViGEmBus is available as a fallback.
       * @returns {object|null} Warning or informational data, or no notice when fully usable.
       */
      buildInstalledVirtualhidNotice(vigembusUsable) {
        const messages = [];
        if (!this.virtualhid.version_compatible && !this.virtualhid.development_version) {
          messages.push({
            key: 'index.virtualhid_outdated_desc',
            params: { version: this.virtualhid.version, supported_versions: this.virtualhid.supported_versions },
          });
        }
        if (this.virtualhidLicense && !this.virtualhidLicense.licensed) {
          const licenseMessageKey = this.gamepadDriver === 'all' && vigembusUsable ?
            'index.virtualhid_license_invalid_fallback_desc' :
            'index.virtualhid_license_invalid_desc';
          messages.push({ key: licenseMessageKey });
        }
        if (messages.length) {
          return this.buildVirtualInputNotice(true, 'index.virtualhid_attention_title', messages);
        }
        if (this.virtualhid.development_version) {
          return this.buildVirtualInputNotice(false, 'index.virtualhid_development_title', [{ key: 'index.virtualhid_development_desc' }]);
        }
        return null;
      },
      /** Load paired clients for the summary list. */
      async loadClients() {
        try {
          const body = await fetch('./api/clients/list').then(r => r.json());
          this.clients = body.named_certs || [];
        } catch (e) {
          console.error('Failed to load paired clients:', e);
        }
      },
      /** Refresh pairing requests waiting for a PIN. */
      async loadPendingPairings() {
        try {
          const response = await apiFetch('./api/pin', { method: 'GET' });
          if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
          }
          this.pendingPairings = (await response.json()).pairings || [];
        } catch (e) {
          console.error('Failed to load pending pairing requests:', e);
        }
      },
      /** Send the typed PIN for the oldest pending request. */
      async pairPrimary() {
        const pairing = this.primaryPairing;
        if (!pairing.id) {
          return;
        }
        const response = await apiFetch('./api/pin', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ pairing_id: pairing.id, pin: this.pin, name: pairing.name || '' }),
        });
        const result = await response.json();
        this.pairStatus = result.status === true
          ? { ok: true, key: 'pin.pair_success' }
          : { ok: false, key: 'pin.pair_failure' };
        if (result.status === true) {
          this.pin = '';
          await this.loadClients();
        }
        await this.loadPendingPairings();
      },
      convertMarkdownToHtml(markdown) {
        if (!markdown) return '';
        return marked.parse(markdown);
      }
    }
  }
</script>
