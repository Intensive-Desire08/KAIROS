import { describe, expect, it } from 'vitest';
import { renderToString } from 'react-dom/server';
import App from './App';

describe('App', () => {
  it('renders the KAIROS SOC dashboard shell', () => {
    const html = renderToString(<App />);
    expect(html).toContain('Security Operations Center');
    expect(html).toContain('Threat telemetry');
  });
});
