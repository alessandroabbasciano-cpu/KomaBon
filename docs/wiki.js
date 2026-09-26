document.addEventListener('DOMContentLoaded', () => {
  // 1. Fetch latest version from latest.json
  const versionBadge = document.querySelector('.version-tag');
  fetch('latest.json', { cache: 'no-store' })
    .then(res => res.json())
    .then(data => {
      if (data && data.version && versionBadge) {
        versionBadge.textContent = `v${data.version}`;
      }
    })
    .catch(() => {
      if (versionBadge) versionBadge.textContent = 'v0.6.0';
    });

  // 2. Mobile sidebar toggle
  const menuToggle = document.querySelector('.menu-toggle');
  const sidebar = document.querySelector('.wiki-sidebar');
  const backdrop = document.querySelector('.sidebar-backdrop');

  if (menuToggle && sidebar && backdrop) {
    menuToggle.addEventListener('click', () => {
      sidebar.classList.toggle('open');
      backdrop.classList.toggle('active');
    });

    backdrop.addEventListener('click', () => {
      sidebar.classList.remove('open');
      backdrop.classList.remove('active');
    });

    // Close on navigation click on mobile
    sidebar.querySelectorAll('a').forEach(link => {
      link.addEventListener('click', () => {
        if (window.innerWidth <= 860) {
          sidebar.classList.remove('open');
          backdrop.classList.remove('active');
        }
      });
    });
  }

  // 3. Copy Code Buttons
  document.querySelectorAll('pre').forEach(pre => {
    const wrapper = document.createElement('div');
    wrapper.className = 'code-wrapper';
    pre.parentNode.insertBefore(wrapper, pre);
    wrapper.appendChild(pre);

    const copyBtn = document.createElement('button');
    copyBtn.className = 'copy-btn';
    copyBtn.textContent = 'Copy';
    copyBtn.setAttribute('aria-label', 'Copy code snippet');
    wrapper.appendChild(copyBtn);

    copyBtn.addEventListener('click', async () => {
      const code = pre.querySelector('code') ? pre.querySelector('code').innerText : pre.innerText;
      try {
        await navigator.clipboard.writeText(code);
        copyBtn.textContent = 'Copied!';
        copyBtn.classList.add('copied');
        setTimeout(() => {
          copyBtn.textContent = 'Copy';
          copyBtn.classList.remove('copied');
        }, 2000);
      } catch (err) {
        copyBtn.textContent = 'Failed';
        setTimeout(() => { copyBtn.textContent = 'Copy'; }, 2000);
      }
    });
  });

  // 4. Client-side Search / Filter
  const searchInput = document.querySelector('.search-input');
  if (searchInput) {
    searchInput.addEventListener('input', (e) => {
      const term = e.target.value.toLowerCase().trim();
      const sections = document.querySelectorAll('.nav-section');

      sections.forEach(sec => {
        let hasVisibleLink = false;
        const links = sec.querySelectorAll('.section-links li');

        links.forEach(li => {
          const text = li.textContent.toLowerCase();
          const match = text.includes(term);
          li.style.display = match ? '' : 'none';
          if (match) hasVisibleLink = true;
        });

        sec.style.display = (hasVisibleLink || term === '') ? '' : 'none';
      });
    });
  }

  // 5. ScrollSpy
  const navLinks = document.querySelectorAll('.section-links a');
  const sections = Array.from(document.querySelectorAll('.doc-section'));

  function updateScrollSpy() {
    const scrollY = window.scrollY + 100;
    let currentId = '';

    for (let i = sections.length - 1; i >= 0; i--) {
      const sec = sections[i];
      if (sec.offsetTop <= scrollY) {
        currentId = sec.id;
        break;
      }
    }

    if (currentId) {
      navLinks.forEach(link => {
        const href = link.getAttribute('href');
        if (href === `#${currentId}`) {
          link.classList.add('active');
        } else {
          link.classList.remove('active');
        }
      });
    }
  }

  window.addEventListener('scroll', updateScrollSpy, { passive: true });
  updateScrollSpy();
});
