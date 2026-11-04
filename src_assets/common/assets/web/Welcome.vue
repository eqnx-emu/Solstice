<template>
  <Navbar-Simple></Navbar-Simple>
  <Notification></Notification>
  <div class="sx-center">
    <main id="content">
      <div>
        <h1>{{ $t('welcome.greeting') }}</h1>
        <p class="sx-hint mt-2">{{ $t('welcome.create_creds') }}</p>
      </div>
      <div class="sx-callout">{{ $t('welcome.create_creds_alert') }}</div>
      <form @submit.prevent="save" class="sx-panel">
        <div class="sx-panel-body">
          <div class="mb-3">
            <label for="usernameInput" class="form-label">{{ $t('_common.username') }}</label>
            <input type="text" class="form-control" id="usernameInput" autocomplete="username"
              v-model="passwordData.newUsername" />
          </div>
          <div class="mb-3">
            <label for="passwordInput" class="form-label">{{ $t('_common.password') }}</label>
            <input type="password" class="form-control" id="passwordInput" autocomplete="new-password"
              v-model="passwordData.newPassword" required />
          </div>
          <div class="mb-3">
            <label for="confirmPasswordInput" class="form-label">{{ $t('welcome.confirm_password') }}</label>
            <input type="password" class="form-control" id="confirmPasswordInput" autocomplete="new-password"
              v-model="passwordData.confirmNewPassword" required />
          </div>
          <button type="submit" class="btn btn-primary w-100" v-bind:disabled="loading">
            {{ $t('welcome.login') }}
          </button>
          <div class="alert alert-danger mt-3 mb-0" v-if="error"><b>{{ $t('_common.error') }}</b> {{error}}</div>
          <div class="alert alert-success mt-3 mb-0" v-if="success">
            <b>{{ $t('_common.success') }}</b> {{ $t('welcome.welcome_success') }}
          </div>
        </div>
      </form>
      <Resource-Card></Resource-Card>
    </main>
  </div>
</template>

<script>
  import ResourceCard from './ResourceCard.vue'
  import { apiFetch } from './fetch_utils'
  import Notification from './Notification.vue'
  import NavbarSimple from './NavbarSimple.vue'

  export default {
    components: {
      ResourceCard,
      Notification,
      NavbarSimple,
    },
    data() {
      return {
        error: null,
        success: false,
        loading: false,
        passwordData: {
          newUsername: "sunshine",
          newPassword: "",
          confirmNewPassword: "",
        },
      };
    },
    methods: {
      /**
       * @brief Create credentials and reload after a successful response.
       *
       * @return {Promise<void>} Completion of the credential request and response processing.
       */
      async save() {
        this.error = null;
        this.loading = true;
        const response = await apiFetch("./api/password", {
          method: "POST",
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify(this.passwordData),
        });
        this.loading = false;
        if (response.status !== 200) {
          this.error = "Internal Server Error";
          return;
        }

        const result = await response.json();
        this.success = result.status;
        if (this.success === true) {
          setTimeout(() => {
            document.location.reload();
          }, 5000);
        } else {
          this.error = result.error;
        }
      },
    },
  }
</script>
