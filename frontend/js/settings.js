window.PQEPage = window.PQEPage || {};

window.PQEPage.settings = async function () {
  const form = document.getElementById('settings-form');
  if (!form) return;

  const portInput = document.getElementById('settings-port');
  const baudInput = document.getElementById('settings-baud');
  const sourceInput = document.getElementById('settings-source');
  const status = document.getElementById('settings-status');

  function setStatus(message, type = 'secondary') {
    status.className = `alert alert-${type} mt-3`;
    status.textContent = message;
  }

  try {
    const settings = await window.PQEApi.getSettings();
    portInput.value = settings.port || '';
    baudInput.value = settings.baud_rate || '';
    sourceInput.value = settings.source_type || settings.source_name || '';
    setStatus('Current settings loaded.', 'success');
  } catch (error) {
    setStatus(error.message, 'danger');
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();

    const payload = {
      port: portInput.value.trim(),
      baud_rate: Number(baudInput.value) || 0,
      source_type: sourceInput.value.trim(),
      reseed: false
    };

    const submitButton = form.querySelector('button[type="submit"]');
    submitButton.disabled = true;
    setStatus('Saving settings...', 'secondary');

    try {
      await window.PQEApi.settings(payload);
      setStatus('Settings updated successfully.', 'success');
    } catch (error) {
      setStatus(error.message, 'danger');
    } finally {
      submitButton.disabled = false;
    }
  });
};
