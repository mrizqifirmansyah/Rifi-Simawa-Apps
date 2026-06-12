# Manajemen Data Mahasiswa — Backend C++ + Frontend Brutalist

Aplikasi web manajemen data mahasiswa dengan **backend C++ murni** (framework
[Crow](https://crowcpp.org)) dan **frontend Vanilla JS + GSAP** bergaya
Neo-Brutalist / Internet-Nostalgia. Dibuat untuk mata kuliah **Algoritma dan
Pemrograman**.

Fitur inti: OOP penuh (inheritance + polymorphism), array dinamis berbasis
pointer (tanpa memory leak), **3 algoritma pencarian** & **5 algoritma
pengurutan**, autentikasi dengan **OTP via email**, hashing password (SHA-256
bersalt), session login, sanitasi input anti-injection/XSS, serta persistensi
data ke file CSV.

---

## 1. Struktur Folder

```
manajemen-mahasiswa/
├── CMakeLists.txt              # build script (mengunduh Crow & asio otomatis)
├── README.md
├── contoh-import.json          # contoh file untuk fitur Import JSON
├── backend/
│   ├── include/                # header (.h)
│   │   ├── Student.h            # OOP: base class + 2 subclass
│   │   ├── StudentManager.h     # array dinamis (Student**) + CRUD + File I/O
│   │   ├── Algorithms.h         # 3 search + 5 sort (Big-O di komentar)
│   │   ├── Auth.h               # register / OTP / login / session
│   │   ├── Mailer.h             # pengiriman email OTP (SMTP)
│   │   ├── Sha256.h             # hashing password (mandiri, tanpa lib)
│   │   └── Utils.h              # sanitasi, validasi, escaping
│   ├── src/                    # implementasi (.cpp)
│   │   ├── main.cpp             # server Crow + routing REST API
│   │   └── (Student/StudentManager/Algorithms/Auth/Mailer/Sha256/Utils).cpp
│   └── data/                   # CSV dibuat otomatis saat runtime
└── frontend/
    ├── index.html              # halaman login / daftar / OTP
    ├── dashboard.html          # dashboard bento + tabel + kontrol
    ├── css/style.css           # design system brutalist
    └── js/
        ├── auth.js             # validasi real-time + alur OTP
        └── dashboard.js        # CRUD, search/sort, export/import, GSAP
```

---

## 2. Prasyarat

| Kebutuhan        | Keterangan                                                |
|------------------|-----------------------------------------------------------|
| Compiler C++17   | GCC ≥ 9, Clang ≥ 10, atau MSVC 2019+                      |
| CMake ≥ 3.16     | sistem build                                              |
| Git              | untuk mengunduh dependency (Crow & asio) via FetchContent |
| libcurl (opsional)| hanya jika ingin **kirim email OTP sungguhan** via SMTP  |

> **Dependency otomatis:** Crow dan asio **tidak perlu** dipasang manual —
> `CMakeLists.txt` akan mengunduhnya saat konfigurasi pertama (butuh internet).

### Memasang prasyarat

**Ubuntu / Debian**
```bash
sudo apt update
sudo apt install -y build-essential cmake git libcurl4-openssl-dev
```

**macOS (Homebrew)**
```bash
brew install cmake git curl
```

**Windows** — pasang Visual Studio 2019+ (workload "Desktop C++"), CMake, dan Git.
libcurl bisa lewat vcpkg: `vcpkg install curl`.

---

## 3. Kompilasi & Menjalankan

Dari folder root proyek:

```bash
# 1) konfigurasi (mengunduh Crow + asio pada run pertama)
cmake -S . -B build

# 2) build
cmake --build build -j4

# 3) jalankan (dari dalam folder build agar path frontend/data benar)
cd build
./manajemen_mahasiswa
```

Server berjalan di **http://localhost:18080**. Buka di browser.

Saat pertama dijalankan, backend otomatis membuat `build/data/students.csv`
berisi **12 data mahasiswa dummy**, sehingga tabel langsung terisi.

### Variabel lingkungan (opsional)

| Variabel       | Default     | Fungsi                                   |
|----------------|-------------|------------------------------------------|
| `PORT`         | `18080`     | port HTTP                                |
| `FRONTEND_DIR` | `frontend`  | lokasi folder frontend                   |
| `DATA_DIR`     | `data`      | lokasi penyimpanan CSV                   |

Contoh: `PORT=9000 ./manajemen_mahasiswa`

---

## 4. Routing API (REST)

Semua endpoint data memerlukan **cookie session** (`sid`) hasil login.

### Autentikasi
| Method | Endpoint            | Body / Query                          | Keterangan |
|--------|---------------------|----------------------------------------|------------|
| POST   | `/api/register`     | `{email, name, password}`             | buat akun, kirim OTP |
| POST   | `/api/verify`       | `{email, otp}`                        | verifikasi OTP (aktifkan akun) |
| POST   | `/api/resend-otp`   | `{email}`                             | kirim ulang OTP (tanpa blacklist) |
| POST   | `/api/login`        | `{email, password}`                   | set cookie `sid` |
| POST   | `/api/logout`       | —                                     | hapus session |
| GET    | `/api/me`           | —                                     | cek session aktif |

### Data Mahasiswa
| Method | Endpoint              | Body / Query                                               |
|--------|-----------------------|------------------------------------------------------------|
| GET    | `/api/students`       | query: `q`, `searchAlgo`, `sortAlgo`, `sortBy`            |
| POST   | `/api/students`       | `{nim, nama, email, jurusan, angkatan, tipe}`            |
| PUT    | `/api/students`       | `{nim, nama, email, jurusan, angkatan, tipe}`            |
| DELETE | `/api/students?nim=`  | hapus berdasarkan NIM                                     |
| GET    | `/api/stats`          | statistik untuk dashboard                                 |
| GET    | `/api/export/csv`     | unduh CSV                                                 |
| POST   | `/api/import`         | `{students:[...]}`                                       |

Nilai parameter algoritma:
- `searchAlgo` = `linear` \| `sequential` \| `binary`
- `sortAlgo`   = `insertion` \| `selection` \| `bubble` \| `merge` \| `shell`
- `sortBy`     = `nama` \| `nim` \| `jurusan`

Contoh dengan `curl`:
```bash
# login & simpan cookie
curl -c cookie.txt -X POST localhost:18080/api/login \
  -H "Content-Type: application/json" \
  -d '{"email":"kamu@unpam.ac.id","password":"Passw0rd!"}'

# cari "dewi" pakai binary search + merge sort
curl -b cookie.txt "localhost:18080/api/students?q=dewi&searchAlgo=binary&sortAlgo=merge&sortBy=nama"
```

---

## 5. Integrasi Pengiriman Email (OTP)

OTP berlaku **60 detik**. Tombol **Kirim Ulang** selalu aktif dan menerbitkan
kode baru — email pengguna **tidak pernah** di-blacklist atau dikunci.

### Mode Dev (default, tanpa SMTP)
Jika `libcurl` tidak terpasang **atau** variabel `SMTP_URL` kosong, OTP
**dicetak ke konsol server** sehingga aplikasi tetap bisa diuji tanpa mail
server:

```
================ OTP (DEV MODE) ================
  To   : kamu@unpam.ac.id
  Code : 538573  (valid 60 detik)
===============================================
```

### Mode Produksi (kirim email asli via SMTP)
1. Pastikan dibuild dengan libcurl terpasang (`-- libcurl ditemukan` saat
   `cmake -S . -B build`).
2. Set variabel lingkungan SMTP sebelum menjalankan server.

Contoh **Gmail** (gunakan *App Password*, bukan password biasa — aktifkan 2FA
lalu buat App Password di akun Google):

```bash
export SMTP_URL=smtps://smtp.gmail.com:465
export SMTP_USER=akunkamu@gmail.com
export SMTP_PASS=xxxx_app_password_xxxx
export SMTP_FROM=akunkamu@gmail.com      # opsional, default = SMTP_USER
./manajemen_mahasiswa
```

Provider lain (tinggal sesuaikan host/port):
- Outlook: `smtps://smtp.office365.com:587`
- Mailtrap (sandbox uji): `smtp://sandbox.smtp.mailtrap.io:2525`

---

## 6. Arsitektur C++ (ringkas)

- **OOP & Polymorphism** — `Student` adalah *abstract base class*; `RegularStudent`
  dan `ScholarshipStudent` mewarisinya dan meng-override `getTipe()` & `clone()`.
  Saat tipe mahasiswa diubah, objek dibangun ulang ke subclass yang sesuai.
- **Array Dinamis & Pointer** — `StudentManager` menyimpan `Student** data_` yang
  tumbuh dengan *doubling* (amortized O(1) append). Destructor & `clear()`
  membebaskan setiap objek (sudah diuji **bebas leak** dengan AddressSanitizer).
- **Algoritma** — lihat `Algorithms.cpp`; setiap fungsi diberi anotasi Big-O.
- **Keamanan** — `Utils::sanitize()` membuang byte berbahaya (`; | & $ \` < >`
  dll.) sebelum data disimpan; output di-`htmlEscape()` (anti-XSS); password
  di-hash SHA-256 + salt + 10.000 putaran; cookie session `HttpOnly` + `SameSite=Lax`.

### Cheat Sheet Time Complexity
| Algoritma        | Kompleksitas        |
|------------------|---------------------|
| Insertion Sort   | O(n) – O(n²)        |
| Selection Sort   | O(n²)               |
| Bubble Sort      | O(n) – O(n²)        |
| Merge Sort       | O(n log n)          |
| Shell Sort       | ~O(n log² n)        |
| Linear Search    | O(n)                |
| Sequential Search| O(n)                |
| Binary Search    | O(log n) (data terurut) |

---

## 7. Troubleshooting

- **`cmake` gagal mengunduh Crow/asio** → pastikan ada koneksi internet saat
  konfigurasi pertama. Setelah terunduh, build berikutnya bisa offline.
- **Port sudah dipakai** → jalankan dengan `PORT=9000 ./manajemen_mahasiswa`.
- **Tabel kosong** → hapus `build/data/students.csv` lalu jalankan ulang untuk
  men-generate ulang data dummy.
- **Email OTP tidak terkirim** → cek log server; bila muncul `SMTP gagal`,
  verifikasi `SMTP_URL/USER/PASS`. Tanpa SMTP, gunakan kode dari konsol (mode dev).
