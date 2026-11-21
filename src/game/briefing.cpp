#include "briefing.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace xa {

static std::string upper(std::string s) { for (auto& c : s) c = char(std::toupper((unsigned char)c)); return s; }

bool Briefing::loadPath(const std::string& name, Path& p) {
    p = Path{};
    std::vector<uint8_t> raw;
    if (!MissionFile::readFile(*fs_, dir_, pilot_, name, raw)) return false;
    std::istringstream in(decodeText(raw));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string w; Vec3 s, d; float st = 100; bool have = false;
        while (ls >> w) {
            if (w == "SRCPOS") { ls >> s.x >> s.y >> s.z; have = true; }
            else if (w == "DSTPOS") ls >> d.x >> d.y >> d.z;
            else if (w == "SRCTIME") ls >> st;
        }
        if (!have) continue;
        p.src.push_back(s * unit_); p.dst.push_back(d * unit_); p.ticks.push_back(std::max(1.f, st));
    }
    for (size_t i = 0; i + 1 < p.ticks.size(); i++) p.total += p.ticks[i];
    return !p.src.empty();
}

Vec3 Briefing::samplePath(const std::vector<Vec3>& pts, float t) const {
    // Catmull-Rom through the key points; segment i lasts ticks[i]
    if (pts.size() == 1) return pts[0];
    size_t i = 0;
    while (i + 2 < pts.size() && t > path_.ticks[i]) { t -= path_.ticks[i]; i++; }
    float u = std::clamp(t / path_.ticks[i], 0.f, 1.f);
    const Vec3& p0 = pts[i > 0 ? i - 1 : i]; const Vec3& p1 = pts[i];
    const Vec3& p2 = pts[i + 1]; const Vec3& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
    float u2 = u * u, u3 = u2 * u;
    return (p1 * 2.f + (p2 - p0) * u + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * u2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * u3) * 0.5f;
}

std::vector<Vec3> Briefing::loadHeliPath(const std::string& f, std::vector<Vec3>* angles) const {
    std::vector<Vec3> pts;
    if (angles) angles->clear();
    std::vector<uint8_t> raw;
    if (fs_ && MissionFile::readFile(*fs_, dir_, pilot_, f, raw)) {
        std::istringstream in(decodeText(raw)); std::string line;
        while (std::getline(in, line)) {
            std::istringstream l2(line); std::string w; float x, y, z;
            if ((l2 >> w) && upper(w) == "POS" && (l2 >> x >> y >> z)) {
                pts.push_back(Vec3{x, y, z} * unit_);
                float a = 0, b = 0, g = 0; std::string kw;
                if ((l2 >> kw) && upper(kw) == "ANGLE") l2 >> a >> b >> g;
                if (angles) angles->push_back({a, b, g});
            }
        }
    }
    return pts;
}

// Original playback (0x365c8): one recorded point every 30 ticks, linear in between.
void Briefing::heliPose(float t) {
    const float k = 6.2831853f / 4096.f;
    size_t i = size_t(std::max(0.f, t));
    if (i + 1 >= heli_.size()) i = heli_.size() - 2;
    float u = std::clamp(t - float(i), 0.f, 1.f);
    heliPos = heli_[i] + (heli_[i + 1] - heli_[i]) * u;
    if (heliAng_.size() == heli_.size()) {
        auto lerpA = [&](float a, float b) { float d = std::remainder(b - a, 4096.f); return a + d * u; };
        Vec3 a0 = heliAng_[i], a1 = heliAng_[i + 1];
        heliPitch = -lerpA(a0.x, a1.x) * k;
        heliRoll = lerpA(a0.y, a1.y) * k;
        heliYaw = -lerpA(a0.z, a1.z) * k;
    } else heliYaw = heliYawAt(i);
}

void Briefing::skip() {
    // jump to the end: the helicopter goes where the last remaining (or running) helicopter path ends
    std::vector<Vec3> last = heli_, lastAng = heliAng_;
    for (size_t i = pc_; i < lines_.size(); i++) {
        std::istringstream ls(lines_[i]); std::string c, f;
        if ((ls >> c >> f) && (upper(c) == "HELIMOTION" || upper(c) == "HELISUBMOTION")) { std::vector<Vec3> a; auto p = loadHeliPath(f, &a); if (!p.empty()) { last = p; lastAng = a; } }
    }
    if (last.size() >= 2) { heli_ = last; heliAng_ = lastAng; heliPose(float(last.size() - 1)); heliArrived = true; }
    else if (last.size() == 1) { heliPos = last[0]; heliArrived = true; }
    pc_ = lines_.size();
    finish();
}

float Briefing::heliYawAt(size_t i) const {
    if (heli_.size() < 2) return heliYaw;
    size_t a = std::min(i, heli_.size() - 2);
    Vec3 d = heli_[a + 1] - heli_[a];
    return std::atan2(-d.x, d.y);
}

void Briefing::start(const MissionFile& m, const GameFS& fs, const std::string& levelDir, float unitScale) {
    fs_ = &fs; dir_ = levelDir; pilot_ = m.pilot; unit_ = unitScale;
    lines_ = m.briefing; pc_ = 0; wait_ = 0; waitFor_ = None;
    texts.clear(); selected.clear(); hasCamera = false; pathT_ = -1; orbit_ = false; fade = 0; fadeDir_ = 0;
    heli_.clear(); heliT_ = -1; heliMoving = false; heliArrived = false; inCockpit = false; onMonitor = false;
    active_ = !lines_.empty();
}

void Briefing::update(float dt) {
    if (!active_) return;
    const float ticks = dt * 60.f;
    for (auto& t : texts) {
        t.typed += ticks * charsPerTick;
        if (!t.done && textLength && t.typed >= float(textLength(t.id))) { t.done = true; if (sound) sound("ENDTEXT.WAV"); }
    }
    if (distort > 0) distort = std::max(0.f, distort - ticks);
    if (zoomDir_ != 0) {
        zoom = std::clamp(zoom + zoomDir_ * ticks / (zoomDir_ > 0 ? zoomInTicks : zoomOutTicks), 0.f, 1.f);
        if (zoom <= 0.f || zoom >= 1.f) zoomDir_ = 0;
    }
    fade = std::clamp(fade + fadeDir_ * ticks / 30.f, 0.f, 1.f);
    if (pathT_ >= 0) {
        pathT_ += ticks;
        float t = std::min(pathT_, path_.total);
        camPos = samplePath(path_.src, t); camTarget = samplePath(path_.dst, t);
        hasCamera = true;
    }
    if (heliT_ >= 0 && heli_.size() >= 2) {
        heliT_ += ticks / 30.f;                        // in points
        if (heliT_ >= float(heli_.size() - 1)) { heliPose(float(heli_.size() - 1)); heliT_ = -1; heliMoving = false; heliArrived = true; }
        else heliPose(heliT_);
    }
    if (orbit_) {
        orbitT_ += ticks;
        float k = std::clamp(orbitT_ / std::max(1.f, orbitTime_), 0.f, 1.f);
        k = k * k * (3 - 2 * k);
        Vec3 want = orbitTarget_ + Vec3{std::sin(orbitAngle_) * orbitDist_, -std::cos(orbitAngle_) * orbitDist_, orbitDist_ * 0.45f};
        camPos = orbitFrom_ + (want - orbitFrom_) * k;
        camTarget = orbitFromTarget_ + (orbitTarget_ - orbitFromTarget_) * k;
        hasCamera = true;
    }
    // waiting?
    switch (waitFor_) {
    case Delay: wait_ -= ticks; if (wait_ > 0) return; break;
    case Text: {
        bool typing = false;
        for (const auto& t : texts) if (t.typed < float(textLength ? textLength(t.id) : 0)) typing = true;
        if (voiceBusy && voiceBusy()) typing = true;
        if (typing) return;
        break;
    }
    case Camera: if (pathT_ >= 0 && pathT_ < path_.total) return; if (orbit_ && orbitT_ < orbitTime_) return; break;
    case Heli: if (heliMoving) return; break;
    case Select: wait_ -= ticks; if (wait_ > 0) return; break;
    default: break;
    }
    waitFor_ = None;
    // execute commands until one waits
    while (pc_ < lines_.size()) {
        std::istringstream ls(lines_[pc_++]);
        std::string cmd; if (!(ls >> cmd)) continue;
        std::string C = upper(cmd);
        if (std::getenv("XA_DEBUG")) std::printf("briefing cmd %s\n", C.c_str());
        if (C == "DELAY") { ls >> wait_; waitFor_ = Delay; return; }
        if (C == "TEXT" || C == "TEXT_ALWAYS") {
            Shown s{}; s.always = C == "TEXT_ALWAYS"; ls >> s.id;
            std::string w;
            while (ls >> w) {
                if (upper(w) == "POS") ls >> s.x >> s.y;
                else if (upper(w) == "WIDTH") ls >> s.widthChars;
            }
            if (s.widthChars <= 0) s.widthChars = 32;
            texts.erase(std::remove_if(texts.begin(), texts.end(), [&](const Shown& x) { return x.id == s.id; }), texts.end());
            texts.push_back(s);
            if (sound) sound("OPENTEXT.WAV");
            texts.back().voiced = speakText && speakText(s.id);
            continue;
        }
        if (C == "DISTORT") { float n = 0; ls >> n; if (n > 0) { distort = n; wait_ = n; waitFor_ = Delay; return; } continue; }
        if (C == "TEXT_OFF") { int id; ls >> id; if (sound) sound("CLOSETXT.WAV"); texts.erase(std::remove_if(texts.begin(), texts.end(), [&](const Shown& x) { return x.id == id; }), texts.end()); continue; }
        if (C == "TEXT_WAIT_FOR_END") {
            waitFor_ = Text; return;
        }
        if (C == "SETCAMERA") {
            std::string f; ls >> f;
            if (loadPath(f, path_)) { pathT_ = 0; orbit_ = false; }
            continue;
        }
        if (C == "CAMERA_WAIT_FOR_END") { waitFor_ = Camera; return; }
        if (C == "SELECTTARGET") { std::string n; ls >> n; selected.push_back(n); continue; }
        if (C == "SELECTTARGET_WAIT_FOR_END") { wait_ = 60; waitFor_ = Select; return; }
        if (C == "SETCAMERATO") {
            std::string n, w; ls >> n;
            float zoom = 900, angle = 45, time = 50;
            while (ls >> w) { std::string W = upper(w); if (W == "ZOOM") ls >> zoom; else if (W == "ANGLE") ls >> angle; else if (W == "TIME") ls >> time; }
            Vec3 p; float r = 5;
            if (findObject && findObject(n, p, r)) {
                orbitFrom_ = hasCamera ? camPos : p + Vec3{0, -60, 30};
                orbitFromTarget_ = hasCamera ? camTarget : p;
                orbitTarget_ = p; orbitDist_ = std::max(r * 2.5f, zoom * 0.05f); orbitAngle_ = angle * 3.14159265f / 180.f;
                orbitT_ = 0; orbitTime_ = time; orbit_ = true; pathT_ = -1;
            }
            continue;
        }
        if (C == "HELIMOTION" || C == "HELISUBMOTION") {
            // HELIMOTION: fly the helicopter along the path and wait; HELISUBMOTION: runs alongside
            std::string f; ls >> f;
            heli_ = loadHeliPath(f, &heliAng_);
            if (heli_.size() >= 2) { heliT_ = 0; heliMoving = true; heliPose(0); if (C == "HELIMOTION") { waitFor_ = Heli; return; } }
            else if (heli_.size() == 1) { heliPos = heli_[0]; heliArrived = true; }
            continue;
        }
        if (C == "ZOOM_FROM_COCKPIT_TO_BRIEFING") { onMonitor = true; zoom = 0; zoomDir_ = 1; hasCamera = false; wait_ = zoomInTicks; waitFor_ = Delay; return; }
        // fade out, back to the helicopter, fade in; remaining commands (e.g. HELIMOTION) still run
        if (C == "ZOOM_FROM_BRIEFING_TO_COCKPIT") {
            hasCamera = false; pathT_ = -1; orbit_ = false; texts.clear(); selected.clear(); onMonitor = false;
            zoom = 1; zoomDir_ = -1; wait_ = zoomOutTicks; waitFor_ = Delay; lines_.insert(lines_.begin() + long(pc_), "__COCKPIT__"); return;
        }
        if (C == "__COCKPIT__") { hasCamera = false; pathT_ = -1; orbit_ = false; texts.clear(); selected.clear(); zoom = 0; inCockpit = true; continue; }
        if (std::getenv("XA_DEBUG")) std::printf("briefing: unhandled %s\n", C.c_str());
    }
    finish();
}

} // namespace xa
