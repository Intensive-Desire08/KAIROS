window.PQEPage = window.PQEPage || {};

window.PQEPage.keygen = function () {
  const form = document.getElementById('keygen-form');
  if (!form) return;

  const output = document.getElementById('keygen-output');
  const status = document.getElementById('keygen-status');
  const copyButton = document.getElementById('copy-key-btn');

  function setStatus(message, type = 'secondary') {
    status.className = `alert alert-${type} mt-3`;
    status.textContent = message;
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();

    const password = document.getElementById('keygen-password').value.trim();
    const salt = document.getElementById('keygen-salt').value.trim();
    const submitButton = form.querySelector('button[type="submit"]');

    submitButton.disabled = true;
    setStatus('Generating key...', 'secondary');

    try {
      const result = await window.PQEApi.keygen({
        password,
        salt
      });

      const key = result?.key || result?.data || '';
      output.value = key;
      setStatus('Key generated successfully.', 'success');
    } catch (error) {
      output.value = '';
      setStatus(error.message, 'danger');
    } finally {
      submitButton.disabled = false;
    }
  });

  copyButton.addEventListener('click', async () => {
    if (!output.value) {
      setStatus('No key available to copy.', 'warning');
      return;
    }

    try {
      await navigator.clipboard.writeText(output.value);
      setStatus('Key copied to clipboard.', 'success');
    } catch (error) {
      setStatus('Copy failed. You can still copy from the textarea manually.', 'warning');
    }
  });
};
