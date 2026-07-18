function evaluateSelection() {
  const selection = document.getElementById('buildSelector').value;
  const btn = document.getElementById('actionBtn');

  if (selection === 'none') {
    btn.disabled = true;
    btn.classList.add('bg-[#6E2C00]/20', 'text-[#6E2C00]/50', 'cursor-not-allowed', 'border-dashed');
    btn.innerHTML = '<span>Select a Target Build Above</span>';
    return;
  }

  btn.disabled = false;
  btn.classList.remove('bg-[#6E2C00]/20', 'text-[#6E2C00]/50', 'cursor-not-allowed', 'border-dashed');

  if (selection === 'source') {
    btn.innerHTML = '<span>Download Source Repository (.ZIP)</span>';
  } else if (selection === 'iso') {
    btn.innerHTML = '<span>Download Live Kernel Image (.ISO)</span>';
  }
}

function processAction() {
  const selection = document.getElementById('buildSelector').value;

  if (selection === 'source') {
    downloadSourceFolder();
  } else if (selection === 'iso') {
    downloadIso();
  }
}

// Single-file download — no zipping needed, just point the browser at
// GitHub's raw file host directly. The browser can't render an .iso
// inline, so this triggers a normal file download.
function downloadIso() {
  const owner = 'Veeloh';
  const repo = 'SolOS';
  const branch = 'main';
  const isoPath = 'build/solos.iso';

  notifyR4('/led-start');
  // A plain navigation download has no JS "finished" event to hook into,
  // so this is just a rough cosmetic timing rather than a real completion signal.
  setTimeout(() => notifyR4('/led-done'), 4000);

  window.location.href = `https://raw.githubusercontent.com/${owner}/${repo}/${branch}/${isoPath}`;
}

// The R4's local IP — used only for the fun LED-matrix notification,
// not for the actual download (that goes straight to GitHub).
const R4_LOCAL_IP = '192.168.1.117';

// Fire-and-forget pings to the R4 so it can flash its LED matrix while
// the browser is downloading. These are non-critical — if the R4 is
// unreachable or slow, the download just proceeds without the light show.
function notifyR4(path) {
  fetch(`http://${R4_LOCAL_IP}${path}`).catch(() => {
    // Silently ignore — this is cosmetic, never block the real download on it.
  });
}

// Pulls only the src/ folder from GitHub (via the browser directly —
// no ESP32/R4 relay involved) and zips it client-side with JSZip.
async function downloadSourceFolder() {
  const owner = 'Veeloh';
  const repo = 'SolOS';
  const branch = 'main';
  const rootPath = 'src';

  const btn = document.getElementById('actionBtn');
  const originalText = btn.innerHTML;
  btn.disabled = true;

  notifyR4('/led-start');

  try {
    const zip = new JSZip();

    btn.innerHTML = '<span>Fetching file list...</span>';
    await addFolderToZip(zip, owner, repo, branch, rootPath, rootPath, btn);

    btn.innerHTML = '<span>Building zip...</span>';
    const blob = await zip.generateAsync({ type: 'blob' });

    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'solos-src.zip';
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  } catch (err) {
    console.error('Source download failed:', err);
    alert('Could not build the source zip — check the browser console for details.');
  } finally {
    notifyR4('/led-done');
    btn.disabled = false;
    btn.innerHTML = originalText;
  }
}

// Recursively walks a GitHub repo path via the Contents API and adds
// every file it finds into the given JSZip instance, preserving structure.
async function addFolderToZip(zip, owner, repo, branch, path, rootPath, btn) {
  const apiUrl = `https://api.github.com/repos/${owner}/${repo}/contents/${path}?ref=${branch}`;
  const res = await fetch(apiUrl);
  if (!res.ok) {
    throw new Error(`GitHub API error for ${path}: ${res.status}`);
  }
  const items = await res.json();

  for (const item of items) {
    const relativePath = item.path.startsWith(rootPath + '/')
      ? item.path.slice(rootPath.length + 1)
      : item.path;

    if (item.type === 'dir') {
      await addFolderToZip(zip, owner, repo, branch, item.path, rootPath, btn);
    } else if (item.type === 'file') {
      if (btn) btn.innerHTML = `<span>Downloading ${relativePath}...</span>`;
      const fileRes = await fetch(item.download_url);
      const fileBlob = await fileRes.blob();
      zip.file(relativePath, fileBlob);
    }
  }
}