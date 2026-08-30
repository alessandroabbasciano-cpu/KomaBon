// Indirizzi di memoria standard ESP32 (Offset)
const FIRMWARE_OFFSET = 65536;   // 0x10000
const LITTLEFS_OFFSET = 5308416; // 0x510000
const BOOTLOADER_OFFSET = 0;     // 0x0
const PARTITIONS_OFFSET = 32768; // 0x8000
const BOOT_APP0_OFFSET = 57344;  // 0xE000

const versionBadge = document.querySelector('[data-version-badge]');
const updateInstaller = document.querySelector('#update-installer');
const factoryInstaller = document.querySelector('#factory-installer');
const errorBox = document.querySelector('[data-installer-error]');
const modeButtons = document.querySelectorAll('.mode-button');
const modePanels = document.querySelectorAll('.mode-panel');

function showError(message) {
  if (errorBox) {
    errorBox.textContent = message;
    errorBox.hidden = false;
  }
}

// Logica per il cambio di Tab (Update vs Factory)
modeButtons.forEach((button) => {
  button.addEventListener('click', () => {
    const targetId = button.dataset.panel;

    modeButtons.forEach((item) => {
      const selected = item === button;
      item.classList.toggle('active', selected);
      item.setAttribute('aria-selected', String(selected));
    });

    modePanels.forEach((panel) => {
      const selected = panel.id === targetId;
      panel.classList.toggle('active', selected);
      panel.hidden = !selected;
    });
  });
});

async function init() {
  let info;
  try {
    const res = await fetch('latest.json', { cache: 'no-store' });
    if (!res.ok) throw new Error(`latest.json HTTP ${res.status}`);
    info = await res.json();
    if (!info || typeof info.version !== 'string' || !info.version) {
      throw new Error('latest.json missing a valid "version" field');
    }
  } catch (err) {
    showError('Could not load the latest KomaBon version. Reload the page or try again later.');
    return;
  }

  const version = info.version;
  if (versionBadge) {
    versionBadge.textContent = `Version ${version}`;
    versionBadge.hidden = false;
  }

  // Manifest 1: Update 
  const updateManifest = {
    name: 'KomaBon',
    version,
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: 'ESP32-S3',
        improv: false,
        parts: [
          { path: new URL(`firmware/firmware-v${version}.bin`, document.baseURI).href, offset: FIRMWARE_OFFSET },
          { path: new URL(`firmware/littlefs-v${version}.bin`, document.baseURI).href, offset: LITTLEFS_OFFSET },
        ],
      },
    ],
  };

  // Manifest 2: Factory (Include Bootloader, Partitions, Firmware, UI Web)
  const factoryManifest = {
    name: 'KomaBon',
    version,
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: 'ESP32-S3',
        improv: false,
        parts: [
          { path: new URL(`firmware/bootloader-v${version}.bin`, document.baseURI).href, offset: BOOTLOADER_OFFSET },
          { path: new URL(`firmware/partitions-v${version}.bin`, document.baseURI).href, offset: PARTITIONS_OFFSET },
          { path: new URL(`firmware/boot_app0-v${version}.bin`, document.baseURI).href, offset: BOOT_APP0_OFFSET },
          { path: new URL(`firmware/firmware-v${version}.bin`, document.baseURI).href, offset: FIRMWARE_OFFSET },
          { path: new URL(`firmware/littlefs-v${version}.bin`, document.baseURI).href, offset: LITTLEFS_OFFSET },
        ],
      },
    ],
  };

  const updateManifestUrl = URL.createObjectURL(new Blob([JSON.stringify(updateManifest)], { type: 'application/json' }));
  const factoryManifestUrl = URL.createObjectURL(new Blob([JSON.stringify(factoryManifest)], { type: 'application/json' }));

  if (updateInstaller) {
    updateInstaller.setAttribute('manifest', updateManifestUrl);
    updateInstaller.manifest = updateManifestUrl;
    updateInstaller.hidden = false;
  }
  
  if (factoryInstaller) {
    factoryInstaller.setAttribute('manifest', factoryManifestUrl);
    factoryInstaller.manifest = factoryManifestUrl;
    factoryInstaller.hidden = false;
  }
}

init();