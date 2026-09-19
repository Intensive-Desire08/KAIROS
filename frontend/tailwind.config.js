/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
  theme: {
    extend: {
      boxShadow: {
        panel: '0 0 0 1px rgba(148, 163, 184, 0.12), 0 24px 60px rgba(2, 6, 23, 0.45)',
      },
      colors: {
        panel: '#0f172a',
        panelAlt: '#111827',
        accent: '#38bdf8',
        warning: '#fbbf24',
        danger: '#f87171',
        success: '#34d399',
      },
    },
  },
  plugins: [],
};
