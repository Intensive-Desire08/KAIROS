window.PQEPage = window.PQEPage || {};

window.PQEPage.encryption = function () {
  const form = document.getElementById('encryption-form');
  if (!form) return;

  const fileInput = document.getElementById('encryption-file');
  const keyInput = document.getElementById('encryption-key');
  const ivInput = document.getElementById('encryption-iv');
  const tagInput = document.getElementById('encryption-tag');
  const modeSelect = document.getElementById('encryption-mode');
  const status = document.getElementById('encryption-status');
  const downloadLink = document.getElementById('encryption-download');

  function setStatus(message, type = 'secondary') {
    status.className = `alert alert-${type} mt-3`;
    status.textContent = message;
  }

  function buf2hex(buffer) {
    return [...new Uint8Array(buffer)].map(x => x.toString(16).padStart(2, '0')).join('');
  }

  function hex2buf(hexString) {
    const bytes = new Uint8Array(Math.ceil(hexString.length / 2));
    for (let i = 0; i < bytes.length; i++) {
      bytes[i] = parseInt(hexString.substr(i * 2, 2), 16);
    }
    return bytes;
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();

    const file = fileInput.files[0];
    if (!file) {
      setStatus('Please select a file first.', 'warning');
      return;
    }

    const key = keyInput.value.trim();
    if (!key) {
      setStatus('Please enter a key.', 'warning');
      return;
    }

    const mode = modeSelect.value;
    const iv = ivInput.value.trim();
    const tag = tagInput.value.trim();

    if (mode === 'decrypt' && (!iv || !tag)) {
      setStatus('Please enter IV and Tag for decryption.', 'warning');
      return;
    }

    setStatus(mode === 'encrypt' ? 'Encrypting...' : 'Decrypting...', 'secondary');

    const submitButton = form.querySelector('button[type="submit"]');
    submitButton.disabled = true;

    try {
      const arrayBuffer = await file.arrayBuffer();
      const hexData = buf2hex(arrayBuffer);

      let payload = { key: key };
      if (mode === 'encrypt') {
        payload.data = hexData;
        if (iv) payload.iv = iv;
      } else {
        payload.ciphertext = hexData;
        payload.iv = iv;
        payload.tag = tag;
      }

      const response = await window.PQEApi.request(mode === 'encrypt' ? '/encrypt' : '/decrypt', {
        method: 'POST',
        body: JSON.stringify(payload)
      });

      if (mode === 'encrypt') {
        setStatus(`Encryption complete. Generated IV: ${response.iv}, Tag: ${response.tag}. SAVE THESE to decrypt!`, 'success');
        const outBuf = hex2buf(response.ciphertext);
        const blob = new Blob([outBuf], { type: 'application/octet-stream' });
        downloadLink.href = URL.createObjectURL(blob);
        downloadLink.download = `${file.name}.enc`;
        downloadLink.classList.remove('d-none');
      } else {
        setStatus('Decryption complete.', 'success');
        const outBuf = hex2buf(response.plaintext);
        const blob = new Blob([outBuf], { type: 'application/octet-stream' });
        downloadLink.href = URL.createObjectURL(blob);
        downloadLink.download = file.name.endsWith('.enc') ? file.name.slice(0, -4) : file.name;
        downloadLink.classList.remove('d-none');
      }
    } catch (error) {
      downloadLink.classList.add('d-none');
      setStatus(error.message, 'danger');
    } finally {
      submitButton.disabled = false;
    }
  });
};
