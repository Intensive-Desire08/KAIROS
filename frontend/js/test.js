window.PQEPage = window.PQEPage || {};

window.PQEPage.test = async function () {
  const form = document.getElementById('entropy-test-form');
  if (!form) return;

  const bytesInput = document.getElementById('entropy-bytes');
  const modeSelect = document.getElementById('entropy-mode');
  const status = document.getElementById('entropy-status');
  const output = document.getElementById('entropy-output');

  function setStatus(message, type = 'secondary') {
    status.className = `alert alert-${type} mt-3`;
    status.textContent = message;
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();

    const bytes = Number(bytesInput.value) || 1024;
    const mode = modeSelect.value || 'quick';
    const submitButton = form.querySelector('button[type="submit"]');

    submitButton.disabled = true;
    setStatus(`Running ${mode} entropy test...`, 'secondary');

    try {
      const result = await window.PQEApi.testEntropy({ bytes, mode });
      output.value = JSON.stringify(result, null, 2);
      setStatus(`Analysis complete (${mode} mode).`, 'success');
    } catch (error) {
      output.value = '';
      setStatus(error.message, 'danger');
    } finally {
      submitButton.disabled = false;
    }
  });
};
