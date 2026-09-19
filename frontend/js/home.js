window.PQEPage = window.PQEPage || {};

window.PQEPage.home = async function () {
  const output = document.getElementById('status-output');
  if (!output) return;

  output.innerHTML = '<div class="col-12"><div class="alert alert-light border">Loading status...</div></div>';

  try {
    const result = await window.PQEApi.getStatus();
    const entropy = result?.entropy || {};
    const server = result?.server || {};

    output.innerHTML = `
      <div class="col-md-3">
        <div class="card status-card h-100">
          <div class="card-body">
            <div class="label">Status</div>
            <div class="value">${result?.status || 'unknown'}</div>
          </div>
        </div>
      </div>
      <div class="col-md-3">
        <div class="card status-card h-100">
          <div class="card-body">
            <div class="label">Hardware Available</div>
            <div class="value">${String(entropy.hardware_available ?? 'unknown')}</div>
          </div>
        </div>
      </div>
      <div class="col-md-3">
        <div class="card status-card h-100">
          <div class="card-body">
            <div class="label">Entropy Source</div>
            <div class="value">${entropy.source || 'unknown'}</div>
          </div>
        </div>
      </div>
      <div class="col-md-3">
        <div class="card status-card h-100">
          <div class="card-body">
            <div class="label">Pool Size</div>
            <div class="value">${entropy.pool_size ?? '0'} bytes</div>
          </div>
        </div>
      </div>
      <div class="col-12">
        <div class="card">
          <div class="card-body">
            <h5 class="card-title">Detailed Response</h5>
            <pre class="json-output">${JSON.stringify(result, null, 2)}</pre>
          </div>
        </div>
      </div>
    `;
  } catch (error) {
    output.innerHTML = `
      <div class="col-12">
        <div class="alert alert-danger" role="alert">
          Failed to load status: ${error.message}
        </div>
      </div>
    `;
  }
};
