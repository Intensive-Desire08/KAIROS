import { test, expect } from '@playwright/test';

test.describe('PseudoQuantum Entropy Service - Site Audit', () => {
  
  test.beforeEach(async ({ page }) => {
    // Navigate to the base URL configured in playwright.config.ts
    // The server must be running and available at the configured baseURL
    await page.goto('/');
  });

  test('should load the home page and display system status', async ({ page }) => {
    // Assert title
    await expect(page).toHaveTitle(/PseudoQuantum Entropy Service/);
    
    // Check Home section is active
    const homeSection = page.locator('#home');
    await expect(homeSection).toBeVisible();
    await expect(homeSection.locator('h1')).toHaveText('System Status');
    
    // Check refresh button
    await expect(page.locator('#refresh-status')).toBeVisible();

    // Take a screenshot of the home page
    await page.screenshot({ path: 'screenshots/home-page.png', fullPage: true });
  });

  test('should navigate to Encryption page and verify form elements', async ({ page }) => {
    await page.click('a.nav-link[href="#encryption"]');
    
    const encryptionSection = page.locator('#encryption');
    await expect(encryptionSection).toBeVisible();
    await expect(encryptionSection.locator('h1')).toContainText('Encryption');

    // Verify form fields
    await expect(page.locator('#encryption-file')).toBeVisible();
    await expect(page.locator('#encryption-key')).toBeVisible();
    await expect(page.locator('#encryption-iv')).toBeVisible();
    await expect(page.locator('#encryption-tag')).toBeVisible();
    await expect(page.locator('#encryption-mode')).toBeVisible();
    
    await page.screenshot({ path: 'screenshots/encryption-page.png', fullPage: true });
  });

  test('should navigate to Key Generation page and verify form elements', async ({ page }) => {
    await page.click('a.nav-link[href="#keygen"]');
    
    const keygenSection = page.locator('#keygen');
    await expect(keygenSection).toBeVisible();
    await expect(keygenSection.locator('h1')).toContainText('Key Generation');

    // Verify form fields
    await expect(page.locator('#keygen-password')).toBeVisible();
    await expect(page.locator('#keygen-salt')).toBeVisible();
    await expect(page.locator('button', { hasText: 'Generate key' })).toBeVisible();
    await expect(page.locator('#copy-key-btn')).toBeVisible();
    
    await page.screenshot({ path: 'screenshots/keygen-page.png', fullPage: true });
  });

  test('should navigate to Settings page and verify form elements', async ({ page }) => {
    await page.click('a.nav-link[href="#settings"]');
    
    const settingsSection = page.locator('#settings');
    await expect(settingsSection).toBeVisible();
    await expect(settingsSection.locator('h1')).toContainText('Settings');

    // Verify form fields
    await expect(page.locator('#settings-port')).toBeVisible();
    await expect(page.locator('#settings-baud')).toBeVisible();
    await expect(page.locator('#settings-source')).toBeVisible();
    
    await page.screenshot({ path: 'screenshots/settings-page.png', fullPage: true });
  });

  test('should navigate to Entropy Test page and verify form elements', async ({ page }) => {
    await page.click('a.nav-link[href="#test"]');
    
    const testSection = page.locator('#test');
    await expect(testSection).toBeVisible();
    await expect(testSection.locator('h1')).toContainText('Entropy Test');

    // Verify form fields
    await expect(page.locator('#entropy-bytes')).toBeVisible();
    await expect(page.locator('#entropy-mode')).toBeVisible();
    await expect(page.locator('button', { hasText: 'Run test' })).toBeVisible();
    
    await page.screenshot({ path: 'screenshots/entropy-test-page.png', fullPage: true });
  });

  // End-to-end user flow: Generating a key
  test('should execute key generation flow', async ({ page }) => {
    await page.click('a.nav-link[href="#keygen"]');
    
    await page.fill('#keygen-password', 'test-password');
    await page.fill('#keygen-salt', 'test-salt');
    await page.click('button:has-text("Generate key")');
    
    // We expect the status to change and output to be populated.
    // Assuming backend returns success and outputs hex string.
    const statusText = page.locator('#keygen-status');
    // Ensure the status updates from 'Ready.' when generating
    await expect(statusText).not.toHaveText('Ready.', { timeout: 10000 });
    
    // Verification of output field could be done like this if we expect a certain length or non-empty value:
    // const output = page.locator('#keygen-output');
    // await expect(output).not.toBeEmpty(); 
  });
});
