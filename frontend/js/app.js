window.PQEPage = window.PQEPage || {};

window.PQEApp = {
  init() {
    const page = document.body.dataset.page || 'home';
    this.setActivePage(page);

    const navLinks = document.querySelectorAll('.nav-link');
    navLinks.forEach((link) => {
      const target = link.getAttribute('href');
      if (target && target.startsWith('#')) {
        link.addEventListener('click', (event) => {
          const id = target.replace('#', '');
          event.preventDefault();
          this.setActivePage(id);
        });
      }
    });

    const refreshButton = document.getElementById('refresh-status');
    if (refreshButton) {
      refreshButton.addEventListener('click', () => {
        if (window.PQEPage.home) {
          window.PQEPage.home();
        }
      });
    }

    Object.keys(window.PQEPage).forEach(key => {
      if (typeof window.PQEPage[key] === 'function') {
        window.PQEPage[key]();
      }
    });
  },

  setActivePage(pageName) {
    const sections = document.querySelectorAll('.page-section');
    sections.forEach((section) => {
      const isActive = section.id === pageName;
      section.classList.toggle('active', isActive);
      section.classList.toggle('d-none', !isActive);
    });

    const links = document.querySelectorAll('.nav-link');
    links.forEach((link) => {
      const isActive = link.getAttribute('href') === `#${pageName}`;
      link.classList.toggle('active', isActive);
      link.setAttribute('aria-current', isActive ? 'page' : 'false');
    });

    document.body.dataset.page = pageName;
  }
};

document.addEventListener('DOMContentLoaded', () => {
  window.PQEApp.init();
});
