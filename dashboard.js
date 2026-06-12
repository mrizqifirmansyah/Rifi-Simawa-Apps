/* dashboard.js — data loading, GSAP staggered reveals, CRUD, export/import */
(() => {
  "use strict";

  const $  = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const esc = s => String(s)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;").replace(/'/g, "&#39;");

  async function api(path, opts = {}) {
    const res = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      credentials: "same-origin", ...opts,
    });
    let data = {};
    try { data = await res.json(); } catch (_) {}
    return { ok: res.ok, status: res.status, data };
  }

  // ---------- toasts (GSAP) ----------
  function toast(type, title, msg) {
    const wrap = $("#toasts");
    const el = document.createElement("div");
    el.className = `toast toast--${type}`;
    const icon = type === "success" ? "✓" : type === "error" ? "!" : "i";
    el.innerHTML = `<span class="icon">${icon}</span>
                    <span class="body"><b>${esc(title)}</b>${esc(msg)}</span>`;
    wrap.appendChild(el);
    gsap.fromTo(el, { x: 420, rotate: 1.5, opacity: 0 },
      { x: 0, rotate: 0, opacity: 1, duration: 0.55, ease: "back.out(1.7)" });
    setTimeout(() => gsap.to(el, { x: 440, opacity: 0, duration: 0.4,
      ease: "power3.in", onComplete: () => el.remove() }), 3600);
  }

  // ---------- current query state ----------
  const state = {
    q: "", searchAlgo: "linear", sortAlgo: "insertion", sortBy: "nama",
  };
  const ALGO_LABEL = {
    linear: "Linear", sequential: "Sequential", binary: "Binary",
    insertion: "Insertion", selection: "Selection", bubble: "Bubble",
    merge: "Merge", shell: "Shell",
    nama: "Nama", nim: "NIM", jurusan: "Jurusan",
  };

  // ---------- render table with staggered fade-up ----------
  function renderRows(list) {
    const tbody = $("#rows");
    tbody.innerHTML = "";
    if (!list.length) {
      tbody.innerHTML = `<tr class="empty-row"><td colspan="8">Tidak ada data yang cocok.</td></tr>`;
      $("#result-meta").textContent = "0 hasil.";
      return;
    }
    list.forEach((s, i) => {
      const tr = document.createElement("tr");
      const tipeCls = s.tipe === "Beasiswa" ? "tag--beasiswa" : "tag--reguler";
      tr.innerHTML = `
        <td class="col-no">${i + 1}</td>
        <td>${esc(s.nim)}</td>
        <td>${esc(s.nama)}</td>
        <td>${esc(s.email)}</td>
        <td>${esc(s.jurusan)}</td>
        <td>${esc(s.angkatan)}</td>
        <td><span class="tag ${tipeCls}">${esc(s.tipe)}</span></td>
        <td class="col-aksi">
          <button class="icon-btn" data-edit="${esc(s.nim)}">Edit</button>
          <button class="icon-btn icon-btn--del" data-del="${esc(s.nim)}">Hapus</button>
        </td>`;
      tbody.appendChild(tr);
    });
    // staggered fade-up animation on (re)load
    gsap.fromTo($$("#rows tr"),
      { y: 16, opacity: 0 },
      { y: 0, opacity: 1, duration: 0.4, stagger: 0.035, ease: "power2.out" });

    $("#result-meta").textContent =
      `${list.length} hasil · search=${ALGO_LABEL[state.searchAlgo]} · sort=${ALGO_LABEL[state.sortAlgo]}(${ALGO_LABEL[state.sortBy]})`;

    // wire row buttons
    $$("#rows [data-edit]").forEach(b => b.addEventListener("click", () => openEdit(b.dataset.edit)));
    $$("#rows [data-del]").forEach(b => b.addEventListener("click", () => removeStudent(b.dataset.del)));
  }

  let cache = []; // last fetched dataset (for edit lookups & PDF/CSV fallback)

  async function loadStudents() {
    const qs = new URLSearchParams({
      q: state.q, searchAlgo: state.searchAlgo,
      sortAlgo: state.sortAlgo, sortBy: state.sortBy,
    });
    const { ok, status, data } = await api(`/api/students?${qs}`);
    if (status === 401) return (window.location.href = "/");
    if (!ok) return toast("error", "Gagal", data.message || "Tidak bisa memuat data.");
    cache = data.data || [];
    renderRows(cache);
    $("#active-search").textContent = ALGO_LABEL[state.searchAlgo];
    $("#active-sort").textContent   = ALGO_LABEL[state.sortAlgo];
    $("#active-key").textContent    = ALGO_LABEL[state.sortBy];
  }

  async function loadStats() {
    const { ok, status, data } = await api("/api/stats");
    if (status === 401) return (window.location.href = "/");
    if (!ok) return;
    const s = data.stats;
    // animate the big number counting up
    const totalEl = $("#stat-total");
    const obj = { v: 0 };
    gsap.to(obj, { v: s.total, duration: 0.8, ease: "power2.out",
      onUpdate: () => totalEl.firstChild.nodeValue = Math.round(obj.v) });
    $("#seg-reg").style.width = s.pctReguler + "%";
    $("#seg-bea").style.width = s.pctBeasiswa + "%";
    $("#lbl-reg").textContent = s.pctReguler + "%";
    $("#lbl-bea").textContent = s.pctBeasiswa + "%";
    $("#cnt-reg").textContent = s.reguler;
    $("#cnt-bea").textContent = s.beasiswa;
    $("#cnt-ti").textContent  = s.informatika;
    $("#cnt-si").textContent  = s.sisteminfo;
  }

  async function refresh() { await Promise.all([loadStudents(), loadStats()]); }

  // ---------- control panel wiring ----------
  let searchDebounce = null;
  $("#search").addEventListener("input", e => {
    state.q = e.target.value;
    clearTimeout(searchDebounce);
    searchDebounce = setTimeout(loadStudents, 220);
  });
  $("#search-algo").addEventListener("change", e => { state.searchAlgo = e.target.value; loadStudents(); });
  $("#sort-algo").addEventListener("change",   e => { state.sortAlgo = e.target.value; loadStudents(); });
  $("#sort-by").addEventListener("change",     e => { state.sortBy = e.target.value; loadStudents(); });

  // ---------- modal (add / edit) ----------
  const overlay = $("#overlay");
  let editingNim = null;

  function openModal(title) {
    $("#modal-title").textContent = title;
    overlay.classList.add("open");
    gsap.fromTo("#modal", { y: 24, opacity: 0, scale: 0.98 },
      { y: 0, opacity: 1, scale: 1, duration: 0.35, ease: "back.out(1.4)" });
  }
  function closeModal() { overlay.classList.remove("open"); }
  $("#modal-close").addEventListener("click", closeModal);
  overlay.addEventListener("click", e => { if (e.target === overlay) closeModal(); });

  function openAdd() {
    editingNim = null;
    $("#f-nim").disabled = false;
    $("#f-nim").value = ""; $("#f-nama").value = ""; $("#f-email").value = "";
    $("#f-jurusan").value = "Teknik Informatika"; $("#f-angkatan").value = ""; $("#f-tipe").value = "Reguler";
    $("#f-nim-hint").textContent = ""; $("#f-email-hint").textContent = "";
    openModal("Tambah Mahasiswa");
  }
  function openEdit(nim) {
    const s = cache.find(x => x.nim === nim);
    if (!s) return;
    editingNim = nim;
    $("#f-nim").value = s.nim; $("#f-nim").disabled = true; // NIM is the key, immutable
    $("#f-nama").value = s.nama; $("#f-email").value = s.email;
    $("#f-jurusan").value = s.jurusan; $("#f-angkatan").value = s.angkatan; $("#f-tipe").value = s.tipe;
    $("#f-nim-hint").textContent = ""; $("#f-email-hint").textContent = "";
    openModal("Edit Mahasiswa");
  }
  $("#btn-add").addEventListener("click", openAdd);

  // live validation inside modal
  function vNim() {
    const ok = /^[0-9]{8,15}$/.test($("#f-nim").value.trim());
    const h = $("#f-nim-hint");
    h.textContent = $("#f-nim").value === "" ? "" : (ok ? "NIM valid" : "Harus 8–15 digit angka");
    h.className = "hint " + ($("#f-nim").value === "" ? "" : ok ? "ok" : "bad");
    return ok;
  }
  function vEmail() {
    const ok = /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test($("#f-email").value.trim());
    const h = $("#f-email-hint");
    h.textContent = $("#f-email").value === "" ? "" : (ok ? "Email valid" : "Format email belum benar");
    h.className = "hint " + ($("#f-email").value === "" ? "" : ok ? "ok" : "bad");
    return ok;
  }
  $("#f-nim").addEventListener("input", vNim);
  $("#f-email").addEventListener("input", vEmail);

  $("#btn-save").addEventListener("click", async () => {
    const payload = {
      nim: $("#f-nim").value.trim(),
      nama: $("#f-nama").value.trim(),
      email: $("#f-email").value.trim(),
      jurusan: $("#f-jurusan").value,
      angkatan: parseInt($("#f-angkatan").value, 10) || 0,
      tipe: $("#f-tipe").value,
    };
    if (!/^[0-9]{8,15}$/.test(payload.nim)) return toast("error", "NIM Salah", "NIM harus 8–15 digit angka.");
    if (!payload.nama) return toast("error", "Nama Kosong", "Nama wajib diisi.");
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(payload.email)) return toast("error", "Email Salah", "Periksa format email.");
    if (payload.angkatan < 1990 || payload.angkatan > 2100) return toast("error", "Angkatan Salah", "Masukkan tahun yang valid.");

    const method = editingNim ? "PUT" : "POST";
    const { ok, data } = await api("/api/students", { method, body: JSON.stringify(payload) });
    if (ok) {
      toast("success", editingNim ? "Diperbarui" : "Ditambahkan", data.message);
      closeModal(); refresh();
    } else {
      toast("error", "Gagal", data.message || "Tidak bisa menyimpan.");
    }
  });

  async function removeStudent(nim) {
    const s = cache.find(x => x.nim === nim);
    if (!confirm(`Hapus mahasiswa "${s ? s.nama : nim}" (NIM ${nim})?`)) return;
    const { ok, data } = await api(`/api/students?nim=${encodeURIComponent(nim)}`, { method: "DELETE" });
    if (ok) { toast("success", "Dihapus", data.message); refresh(); }
    else toast("error", "Gagal", data.message || "Tidak bisa menghapus.");
  }

  // ---------- export / import ----------
  $("#btn-export-csv").addEventListener("click", () => {
    window.location.href = "/api/export/csv";
    toast("info", "Export CSV", "File mahasiswa.csv sedang diunduh.");
  });

  // PDF export = dependency-free print-to-PDF of a brutalist report window
  $("#btn-export-pdf").addEventListener("click", () => {
    if (!cache.length) return toast("error", "Kosong", "Tidak ada data untuk diekspor.");
    const rows = cache.map((s, i) => `<tr>
      <td>${i + 1}</td><td>${esc(s.nim)}</td><td>${esc(s.nama)}</td>
      <td>${esc(s.email)}</td><td>${esc(s.jurusan)}</td>
      <td>${esc(s.angkatan)}</td><td>${esc(s.tipe)}</td></tr>`).join("");
    const html = `<!doctype html><html><head><meta charset="utf-8">
      <title>Laporan Data Mahasiswa</title>
      <style>
        *{font-family:'Courier New',monospace;color:#000}
        h1{text-transform:uppercase;border-bottom:4px solid #000;padding-bottom:8px}
        table{width:100%;border-collapse:collapse;font-size:12px}
        th,td{border:2px solid #000;padding:6px 8px;text-align:left}
        thead th{background:#000;color:#fff}
        .meta{font-size:11px;margin:6px 0 18px}
        @media print{button{display:none}}
      </style></head><body>
      <h1>Data Mahasiswa</h1>
      <p class="meta">Total: ${cache.length} record · Dicetak: ${new Date().toLocaleString("id-ID")}</p>
      <table><thead><tr><th>No</th><th>NIM</th><th>Nama</th><th>Email</th>
      <th>Jurusan</th><th>Angkatan</th><th>Tipe</th></tr></thead>
      <tbody>${rows}</tbody></table>
      <button onclick="window.print()" style="margin-top:16px;padding:8px 14px">Cetak / Simpan PDF</button>
      <script>setTimeout(()=>window.print(),400);<\/script>
      </body></html>`;
    const w = window.open("", "_blank");
    w.document.write(html); w.document.close();
    toast("info", "Export PDF", "Jendela cetak terbuka — pilih 'Save as PDF'.");
  });

  $("#btn-import").addEventListener("click", () => $("#file-import").click());
  $("#file-import").addEventListener("change", async e => {
    const file = e.target.files[0];
    if (!file) return;
    try {
      const text = await file.text();
      let parsed = JSON.parse(text);
      // accept either a bare array or { students: [...] }
      const students = Array.isArray(parsed) ? parsed : parsed.students;
      if (!Array.isArray(students)) throw new Error("format");
      const { ok, data } = await api("/api/import", {
        method: "POST", body: JSON.stringify({ students }),
      });
      if (ok) { toast("success", "Import Selesai", data.message); refresh(); }
      else toast("error", "Import Gagal", data.message || "Format tidak didukung.");
    } catch (_) {
      toast("error", "JSON Tidak Valid", "Pastikan file berisi array mahasiswa.");
    }
    e.target.value = "";
  });

  // ---------- logout ----------
  $("#btn-logout").addEventListener("click", async () => {
    await api("/api/logout", { method: "POST" });
    window.location.href = "/";
  });

  // ---------- boot ----------
  (async function init() {
    const me = await api("/api/me");
    if (!me.ok) return (window.location.href = "/");
    $("#who-email").textContent = me.data.email;
    gsap.from(".cell", { y: 20, opacity: 0, duration: 0.5, stagger: 0.08, ease: "power2.out" });
    gsap.from(".panel", { y: 16, opacity: 0, duration: 0.5, delay: 0.15, ease: "power2.out" });
    await refresh();
  })();
})();
