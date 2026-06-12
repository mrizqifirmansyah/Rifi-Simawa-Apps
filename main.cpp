// =============================================================================
//  main.cpp  -  HTTP server (Crow) for "Manajemen Data Mahasiswa".
//
//  Wires the pure-C++ core (StudentManager, Auth, Algorithms) to a REST API and
//  serves the static frontend. All shared state is guarded by a single mutex
//  because Crow handles requests on multiple threads.
//
//  Routes
//    Static : GET /                 -> frontend/index.html
//             GET /dashboard        -> frontend/dashboard.html
//             GET /css/* /js/*      -> static assets
//    Auth   : POST /api/register  {email,name,password}
//             POST /api/verify    {email,otp}
//             POST /api/resend-otp {email}
//             POST /api/login     {email,password}     (sets sid cookie)
//             POST /api/logout
//             GET  /api/me
//    Data   : GET    /api/students?q=&searchAlgo=&sortAlgo=&sortBy=
//             POST   /api/students {nim,nama,email,jurusan,angkatan,tipe}
//             PUT    /api/students {nim,...}
//             DELETE /api/students?nim=
//             GET    /api/stats
//             GET    /api/export/csv
//             POST   /api/import   {students:[...]}
// =============================================================================

#include "crow.h"

#include "StudentManager.h"
#include "Auth.h"
#include "Algorithms.h"
#include "Mailer.h"
#include "Utils.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace {

std::mutex g_mtx;   // protects manager_ + auth_

std::string envOr(const char* k, const std::string& def) {
    const char* v = std::getenv(k);
    return (v && *v) ? std::string(v) : def;
}

std::string FRONTEND_DIR;   // resolved in main()
std::string DATA_DIR;

// ---- small helpers ----------------------------------------------------------

crow::response jsonResp(int code, const std::string& jsonBody) {
    crow::response r(code, jsonBody);
    r.set_header("Content-Type", "application/json; charset=utf-8");
    return r;
}

crow::response jsonMsg(int code, bool ok, const std::string& msg) {
    std::ostringstream os;
    os << "{\"ok\":" << (ok ? "true" : "false")
       << ",\"message\":\"" << util::jsonEscape(msg) << "\"}";
    return jsonResp(code, os.str());
}

std::string contentTypeFor(const std::string& path) {
    auto ends = [&](const char* s){
        size_t n = std::strlen(s);
        return path.size() >= n && path.compare(path.size() - n, n, s) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css"))  return "text/css; charset=utf-8";
    if (ends(".js"))   return "application/javascript; charset=utf-8";
    if (ends(".json")) return "application/json; charset=utf-8";
    if (ends(".svg"))  return "image/svg+xml";
    return "text/plain; charset=utf-8";
}

crow::response serveFile(const std::string& relPath) {
    // Block path traversal.
    if (relPath.find("..") != std::string::npos)
        return crow::response(403, "forbidden");
    std::string full = FRONTEND_DIR + "/" + relPath;
    std::ifstream in(full, std::ios::binary);
    if (!in.is_open()) return crow::response(404, "not found");
    std::ostringstream ss; ss << in.rdbuf();
    crow::response r(200, ss.str());
    r.set_header("Content-Type", contentTypeFor(relPath));
    return r;
}

// Read the "sid" cookie from a request.
std::string cookieToken(const crow::request& req) {
    std::string cookie = req.get_header_value("Cookie");
    const std::string key = "sid=";
    auto p = cookie.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    auto e = cookie.find(';', p);
    return cookie.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

} // namespace

int main() {
    FRONTEND_DIR = envOr("FRONTEND_DIR", "frontend");
    DATA_DIR     = envOr("DATA_DIR", "data");
    int port     = std::atoi(envOr("PORT", "18080").c_str());

    // Ensure the data directory exists so CSV persistence never fails silently,
    // regardless of the current working directory.
    std::error_code ec;
    std::filesystem::create_directories(DATA_DIR, ec);

    StudentManager manager_(DATA_DIR + "/students.csv");
    Auth           auth_(DATA_DIR + "/users.csv");
    manager_.load();   // auto-seeds 12 mock students on first run

    crow::SimpleApp app;

    // ----------------------------- STATIC --------------------------------
    CROW_ROUTE(app, "/")([](){ return serveFile("index.html"); });
    CROW_ROUTE(app, "/dashboard")([](){ return serveFile("dashboard.html"); });
    CROW_ROUTE(app, "/css/<string>")([](const std::string& f){ return serveFile("css/" + f); });
    CROW_ROUTE(app, "/js/<string>")([](const std::string& f){ return serveFile("js/" + f); });

    // ----------------------------- AUTH ----------------------------------
    CROW_ROUTE(app, "/api/register").methods("POST"_method)
    ([&](const crow::request& req){
        auto b = crow::json::load(req.body);
        if (!b) return jsonMsg(400, false, "Body JSON tidak valid.");
        std::string email = b.has("email")    ? std::string(b["email"].s())    : "";
        std::string name  = b.has("name")     ? std::string(b["name"].s())     : "";
        std::string pass  = b.has("password") ? std::string(b["password"].s()) : "";

        std::lock_guard<std::mutex> lk(g_mtx);
        auto r = auth_.registerUser(email, name, pass);
        switch (r.status) {
            case Auth::Status::Ok:
                mailer::sendOtp(util::toLower(util::trim(email)), name, r.otp);
                return jsonMsg(200, true, "Kode OTP telah dikirim ke email Anda.");
            case Auth::Status::EmailTaken:
                return jsonMsg(409, false, "Email sudah terdaftar dan aktif.");
            default:
                return jsonMsg(400, false, "Data tidak valid. Pastikan email benar dan password kuat (min 8 karakter, huruf besar/kecil, angka, simbol).");
        }
    });

    CROW_ROUTE(app, "/api/verify").methods("POST"_method)
    ([&](const crow::request& req){
        auto b = crow::json::load(req.body);
        if (!b) return jsonMsg(400, false, "Body JSON tidak valid.");
        std::string email = b.has("email") ? std::string(b["email"].s()) : "";
        std::string otp   = b.has("otp")   ? std::string(b["otp"].s())   : "";

        std::lock_guard<std::mutex> lk(g_mtx);
        switch (auth_.verifyOtp(email, otp)) {
            case Auth::Status::Ok:           return jsonMsg(200, true, "Verifikasi berhasil. Silakan login.");
            case Auth::Status::OtpExpired:   return jsonMsg(410, false, "OTP kedaluwarsa. Klik Kirim Ulang.");
            case Auth::Status::AlreadyActive:return jsonMsg(409, false, "Akun sudah aktif.");
            case Auth::Status::NotFound:     return jsonMsg(404, false, "Email tidak ditemukan.");
            default:                         return jsonMsg(400, false, "Kode OTP salah.");
        }
    });

    CROW_ROUTE(app, "/api/resend-otp").methods("POST"_method)
    ([&](const crow::request& req){
        auto b = crow::json::load(req.body);
        if (!b) return jsonMsg(400, false, "Body JSON tidak valid.");
        std::string email = b.has("email") ? std::string(b["email"].s()) : "";

        std::lock_guard<std::mutex> lk(g_mtx);
        auto r = auth_.resendOtp(email);
        switch (r.status) {
            case Auth::Status::Ok:
                mailer::sendOtp(util::toLower(util::trim(email)), "", r.otp);
                return jsonMsg(200, true, "OTP baru telah dikirim.");
            case Auth::Status::AlreadyActive: return jsonMsg(409, false, "Akun sudah aktif.");
            default:                          return jsonMsg(404, false, "Email tidak ditemukan.");
        }
    });

    CROW_ROUTE(app, "/api/login").methods("POST"_method)
    ([&](const crow::request& req){
        auto b = crow::json::load(req.body);
        if (!b) return jsonMsg(400, false, "Body JSON tidak valid.");
        std::string email = b.has("email")    ? std::string(b["email"].s())    : "";
        std::string pass  = b.has("password") ? std::string(b["password"].s()) : "";

        std::lock_guard<std::mutex> lk(g_mtx);
        std::string token;
        auto st = auth_.login(email, pass, token);
        if (st == Auth::Status::Ok) {
            crow::response r = jsonMsg(200, true, "Login berhasil.");
            // HttpOnly so JS can't read it (anti-XSS theft); SameSite=Lax (anti-CSRF).
            r.set_header("Set-Cookie",
                "sid=" + token + "; HttpOnly; SameSite=Lax; Path=/; Max-Age=14400");
            return r;
        }
        if (st == Auth::Status::NotActive)
            return jsonMsg(403, false, "Akun belum diverifikasi.");
        return jsonMsg(401, false, "Email atau password salah.");
    });

    CROW_ROUTE(app, "/api/logout").methods("POST"_method)
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        auth_.logout(cookieToken(req));
        crow::response r = jsonMsg(200, true, "Logout berhasil.");
        r.set_header("Set-Cookie", "sid=; HttpOnly; SameSite=Lax; Path=/; Max-Age=0");
        return r;
    });

    CROW_ROUTE(app, "/api/me")
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        std::string email = auth_.emailForToken(cookieToken(req));
        if (email.empty()) return jsonMsg(401, false, "Belum login.");
        return jsonResp(200, "{\"ok\":true,\"email\":\"" + util::jsonEscape(email) + "\"}");
    });

    // ----------------------------- DATA ----------------------------------
    // Guard helper: returns true and fills email if the session is valid.
    auto requireAuth = [&](const crow::request& req, crow::response& deny) -> bool {
        std::string email = auth_.emailForToken(cookieToken(req));
        if (email.empty()) { deny = jsonMsg(401, false, "Sesi tidak valid. Silakan login."); return false; }
        return true;
    };

    CROW_ROUTE(app, "/api/students")
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        crow::response deny; if (!requireAuth(req, deny)) return deny;

        std::string q          = req.url_params.get("q")          ? req.url_params.get("q")          : "";
        std::string searchAlgo = req.url_params.get("searchAlgo") ? req.url_params.get("searchAlgo") : "linear";
        std::string sortAlgo   = req.url_params.get("sortAlgo")   ? req.url_params.get("sortAlgo")   : "";
        std::string sortByStr  = req.url_params.get("sortBy")     ? req.url_params.get("sortBy")     : "nama";

        algo::SortKey key = algo::parseKey(sortByStr);
        int n = manager_.count();
        Student** arr = manager_.raw();

        // Sort first (so search + display share the same chosen ordering).
        if (!sortAlgo.empty()) {
            algo::sortBy(arr, n, sortAlgo, key);
            manager_.save();
        }

        std::vector<int> idx;
        std::string sa = util::toLower(searchAlgo);
        if (sa == "sequential")  idx = algo::sequentialSearch(arr, n, q, key);
        else if (sa == "binary") idx = algo::binarySearch(arr, n, q, key);
        else                     idx = algo::linearSearch(arr, n, q, key);

        std::string body = "{\"ok\":true,\"data\":" + manager_.toJsonArray(&idx) + "}";
        return jsonResp(200, body);
    });

    CROW_ROUTE(app, "/api/students").methods("POST"_method)
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        crow::response deny; if (!requireAuth(req, deny)) return deny;

        auto b = crow::json::load(req.body);
        if (!b) return jsonMsg(400, false, "Body JSON tidak valid.");

        std::string nim     = util::sanitize(b.has("nim")     ? std::string(b["nim"].s())     : "", 15);
        std::string nama    = util::sanitize(b.has("nama")    ? std::string(b["nama"].s())    : "", 80);
        std::string email   = util::sanitize(b.has("email")   ? std::string(b["email"].s())   : "", 100);
        std::string jurusan = util::sanitize(b.has("jurusan") ? std::string(b["jurusan"].s()) : "", 60);
        std::string tipe    = util::sanitize(b.has("tipe")    ? std::string(b["tipe"].s())    : "", 20);
        int angkatan = 0;
        if (b.has("angkatan")) { try { angkatan = (int)b["angkatan"].i(); } catch (...) {} }

        if (!util::isValidNim(nim))       return jsonMsg(400, false, "NIM harus 8-15 digit angka.");
        if (nama.empty())                 return jsonMsg(400, false, "Nama wajib diisi.");
        if (!util::isValidEmail(email))   return jsonMsg(400, false, "Email tidak valid.");
        if (jurusan.empty())              return jsonMsg(400, false, "Jurusan wajib diisi.");
        if (angkatan < 1990 || angkatan > 2100) return jsonMsg(400, false, "Angkatan tidak valid.");

        Student* s = makeStudent(tipe, nim, nama, email, jurusan, angkatan);
        if (!s) return jsonMsg(400, false, "Tipe harus 'Reguler' atau 'Beasiswa'.");
        if (!manager_.add(s)) return jsonMsg(409, false, "NIM sudah terdaftar.");
        manager_.save();
        return jsonMsg(201, true, "Mahasiswa ditambahkan.");
    });

    CROW_ROUTE(app, "/api/students").methods("PUT"_method)
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        crow::response deny; if (!requireAuth(req, deny)) return deny;

        auto b = crow::json::load(req.body);
        if (!b) return jsonMsg(400, false, "Body JSON tidak valid.");

        std::string nim     = util::sanitize(b.has("nim")     ? std::string(b["nim"].s())     : "", 15);
        std::string nama    = util::sanitize(b.has("nama")    ? std::string(b["nama"].s())    : "", 80);
        std::string email   = util::sanitize(b.has("email")   ? std::string(b["email"].s())   : "", 100);
        std::string jurusan = util::sanitize(b.has("jurusan") ? std::string(b["jurusan"].s()) : "", 60);
        std::string tipe    = util::sanitize(b.has("tipe")    ? std::string(b["tipe"].s())    : "", 20);
        int angkatan = 0;
        if (b.has("angkatan")) { try { angkatan = (int)b["angkatan"].i(); } catch (...) {} }

        if (!util::isValidEmail(email)) return jsonMsg(400, false, "Email tidak valid.");
        if (!manager_.update(nim, nama, email, jurusan, angkatan, tipe))
            return jsonMsg(404, false, "Mahasiswa tidak ditemukan / tipe salah.");
        manager_.save();
        return jsonMsg(200, true, "Data diperbarui.");
    });

    CROW_ROUTE(app, "/api/students").methods("DELETE"_method)
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        crow::response deny; if (!requireAuth(req, deny)) return deny;

        std::string nim = req.url_params.get("nim") ? req.url_params.get("nim") : "";
        if (!manager_.remove(util::sanitize(nim, 15)))
            return jsonMsg(404, false, "Mahasiswa tidak ditemukan.");
        manager_.save();
        return jsonMsg(200, true, "Mahasiswa dihapus.");
    });

    CROW_ROUTE(app, "/api/stats")
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        crow::response deny; if (!requireAuth(req, deny)) return deny;
        return jsonResp(200, "{\"ok\":true,\"stats\":" + manager_.statsJson() + "}");
    });

    CROW_ROUTE(app, "/api/export/csv")
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        crow::response deny; if (!requireAuth(req, deny)) return deny;
        crow::response r(200, manager_.toCsvBlob());
        r.set_header("Content-Type", "text/csv; charset=utf-8");
        r.set_header("Content-Disposition", "attachment; filename=\"mahasiswa.csv\"");
        return r;
    });

    CROW_ROUTE(app, "/api/import").methods("POST"_method)
    ([&](const crow::request& req){
        std::lock_guard<std::mutex> lk(g_mtx);
        crow::response deny; if (!requireAuth(req, deny)) return deny;

        auto b = crow::json::load(req.body);
        if (!b || !b.has("students") || b["students"].t() != crow::json::type::List)
            return jsonMsg(400, false, "Format JSON harus { \"students\": [...] }.");

        int added = 0;
        for (const auto& item : b["students"]) {
            std::string nim     = util::sanitize(item.has("nim")     ? std::string(item["nim"].s())     : "", 15);
            std::string nama    = util::sanitize(item.has("nama")    ? std::string(item["nama"].s())    : "", 80);
            std::string email   = util::sanitize(item.has("email")   ? std::string(item["email"].s())   : "", 100);
            std::string jurusan = util::sanitize(item.has("jurusan") ? std::string(item["jurusan"].s()) : "", 60);
            std::string tipe    = util::sanitize(item.has("tipe")    ? std::string(item["tipe"].s())    : "Reguler", 20);
            int angkatan = 0;
            if (item.has("angkatan")) { try { angkatan = (int)item["angkatan"].i(); } catch (...) {} }
            if (!util::isValidNim(nim) || nama.empty() || !util::isValidEmail(email)) continue;
            Student* s = makeStudent(tipe.empty() ? "Reguler" : tipe, nim, nama, email, jurusan, angkatan);
            if (s && manager_.add(s)) ++added;
        }
        manager_.save();
        return jsonMsg(200, true, "Import selesai. " + std::to_string(added) + " data ditambahkan.");
    });

    CROW_CATCHALL_ROUTE(app)([](){ return crow::response(404, "Not found"); });

    std::cout << "Server berjalan di http://localhost:" << port << "\n";
    std::cout << "FRONTEND_DIR=" << FRONTEND_DIR << "  DATA_DIR=" << DATA_DIR << "\n";
    app.port(port).multithreaded().run();
    return 0;
}
