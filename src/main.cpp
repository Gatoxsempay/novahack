#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <string>

using namespace geode::prelude;

// ── Hack IDs ──────────────────────────────────────────────────────────────────
enum HackID : int {
    H_NOCLIP=0, H_AUTORESTART, H_FASTCOMPLETE, H_FREEZE,
    H_NORESPAWN, H_IGNOREESC, H_PRACTICE, H_NODEATHFX,
    H_FREEZEATTEMPTS,
    H_SHOWHITBOX, H_SHOWLABELS, H_SHOWDEATHS, H_SHOWTIME,
    H_SHOWPOS, H_SHOWVEL, H_HIDEUI, H_SHOWPERCENT,
    H_LOWGRAV, H_HIGHGRAV, H_MOONGRAV, H_ANTIGRAV,
    H_SMALL, H_BIG, H_FORCEMINI, H_AUTOJUMP,
    H_NOGLOW, H_NOTRAIL, H_NOPARTICLES, H_NOWAVETRAIL,
    H_NODEATHSOUND, H_NORESPAWNFLASH, H_TRANSBG, H_MIRROR,
    H_UNLOCKALL, H_NOCHECKLIMIT, H_ALWAYSCHK, H_NOCOINCHK,
    H_BYPASSFILTER, H_FREEPRACTICE,
    H_NOMUSIC, H_NOSFX, H_MUTEUNFOCUS, H_LOWPITCH,
    H_COUNT
};

static const char* HACK_NAMES[H_COUNT] = {
    "Noclip",       "Auto restart",  "Fast complete", "Freeze game",
    "No respawn",   "Ignore ESC",    "Practice mode", "No death fx",
    "Frz attempts",
    "Show hitbox",  "Show labels",   "Show deaths",   "Show time",
    "Show pos",     "Show vel",      "Hide UI",       "Show pct",
    "Low gravity",  "High gravity",  "Moon gravity",  "Anti gravity",
    "Small player", "Big player",    "Force mini",    "Auto jump",
    "No glow",      "No trail",      "No particles",  "No wave trail",
    "No death snd", "No resp flash", "Transp. BG",    "Mirror mode",
    "Unlock all",   "No chkpt lmt",  "Always chkpt",  "No coin chk",
    "Byp. filter",  "Free practice",
    "No music",     "No SFX",        "Mute unfocus",  "Low pitch"
};

// ── Global state ──────────────────────────────────────────────────────────────
static bool         g_hacks[H_COUNT]    = {};
static enumKeyCodes g_hackKeys[H_COUNT] = {};   // 0 = unbound
static float        g_speed             = 1.0f;
static int          g_fpsCap            = 0;
static enumKeyCodes g_panelKey          = enumKeyCodes::KEY_M;
static bool         g_bindingPanel      = false;
static float        g_restartTimer      = -1.f;
static bool         g_levelActive       = false;
static PlayLayer*   g_lastPL            = nullptr;
static int          g_deaths            = 0;
static float        g_levelTime         = 0.f;
static CCPoint      g_lastPos           = {0.f, 0.f};
static int          g_bindingHack       = -1;   // -1 = not binding
static bool         g_fastCompleteFired = false;

static constexpr float        SPEEDS[]       = {0.25f, 1.f, 2.f, 5.f, 10.f};
static constexpr const char*  SPEED_LABELS[] = {"x0.25","x1","x2","x5","x10"};

// ── Persistence ───────────────────────────────────────────────────────────────
static void saveAll() {
    auto* m = Mod::get();
    m->setSavedValue("panelKey", (int)g_panelKey);
    m->setSavedValue("speed",    g_speed);
    m->setSavedValue("fpsCap",   g_fpsCap);
    for (int i = 0; i < H_COUNT; i++) {
        m->setSavedValue("h"  + std::to_string(i), g_hacks[i]);
        m->setSavedValue("hk" + std::to_string(i), (int)g_hackKeys[i]);
    }
}
static void loadAll() {
    auto* m = Mod::get();
    g_panelKey    = (enumKeyCodes)m->getSavedValue<int>("panelKey", (int)enumKeyCodes::KEY_M);
    if ((int)g_panelKey <= 4) g_panelKey = enumKeyCodes::KEY_M;  // migrate old index format
    g_speed       = m->getSavedValue<float>("speed",    1.0f);
    g_fpsCap      = m->getSavedValue<int>  ("fpsCap",   0);
    for (int i = 0; i < H_COUNT; i++) {
        g_hacks[i]    = m->getSavedValue<bool>("h"  + std::to_string(i), false);
        g_hackKeys[i] = (enumKeyCodes)m->getSavedValue<int>("hk" + std::to_string(i), 0);
    }
}

// ── Key name helper ───────────────────────────────────────────────────────────
static std::string keyName(enumKeyCodes k) {
    int v = (int)k;
    if (v == 0) return "-";
    if (v >= 65 && v <= 90) return std::string(1, (char)v);  // A-Z
    if (v >= 48 && v <= 57) return std::string(1, (char)v);  // 0-9
    switch (k) {
        case enumKeyCodes::KEY_F1:    return "F1";
        case enumKeyCodes::KEY_F2:    return "F2";
        case enumKeyCodes::KEY_F3:    return "F3";
        case enumKeyCodes::KEY_F4:    return "F4";
        case enumKeyCodes::KEY_F5:    return "F5";
        case enumKeyCodes::KEY_F6:    return "F6";
        case enumKeyCodes::KEY_F7:    return "F7";
        case enumKeyCodes::KEY_F8:    return "F8";
        case enumKeyCodes::KEY_F9:    return "F9";
        case enumKeyCodes::KEY_F10:   return "F10";
        case enumKeyCodes::KEY_F11:   return "F11";
        case enumKeyCodes::KEY_F12:   return "F12";
        case enumKeyCodes::KEY_Space: return "SPC";
        default: return std::to_string(v);
    }
}

// ── Forward declaration for overlay pointer ───────────────────────────────────
class NovaOverlay;
static NovaOverlay* g_overlay = nullptr;

// ── NovaOverlay ───────────────────────────────────────────────────────────────
class NovaOverlay : public CCLayer {
    static constexpr ccColor3B COL_ON   = {255, 220,  35};  // nova gold
    static constexpr ccColor3B COL_OFF  = {115, 115, 138};  // space dust
    static constexpr ccColor3B COL_BIND = {  0, 230, 255};  // nova cyan
    static constexpr float     ROW_H    = 16.f;
    static constexpr float     HDR_H    = 20.f;
    static constexpr float     PAD      = 4.f;

    CCMenuItemSpriteExtra* m_hackBtns[H_COUNT]   = {};
    CCLabelBMFont*         m_hackBadges[H_COUNT]  = {};
    CCLayerColor*          m_rowBgs[H_COUNT]      = {};
    CCMenuItemSpriteExtra* m_speedBtns[5]         = {};
    CCLabelBMFont*         m_panelKeyBadge        = nullptr;
    CCLabelBMFont*         m_statusLbl            = nullptr;

    // ── refresh helpers ───────────────────────────────────────────────────────
    void refreshToggle(int id) {
        if (!m_hackBtns[id]) return;
        auto* l = static_cast<CCLabelBMFont*>(m_hackBtns[id]->getNormalImage());
        if (l) l->setColor(g_hacks[id] ? COL_ON : COL_OFF);
        if (m_rowBgs[id]) m_rowBgs[id]->setOpacity(g_hacks[id] ? 85 : 0);
    }
    void refreshBadge(int id) {
        if (!m_hackBadges[id]) return;
        bool binding = (g_bindingHack == id);
        bool hasBind = ((int)g_hackKeys[id] != 0);
        m_hackBadges[id]->setString(
            binding ? ">>>" : ("[" + keyName(g_hackKeys[id]) + "]").c_str());
        if (binding)      m_hackBadges[id]->setColor(COL_BIND);
        else if (hasBind) m_hackBadges[id]->setColor({150, 175, 215});
        else              m_hackBadges[id]->setColor(COL_OFF);
    }
    void refreshPanelBadge() {
        if (!m_panelKeyBadge) return;
        if (g_bindingPanel) {
            m_panelKeyBadge->setString(">>>");
            m_panelKeyBadge->setColor(COL_BIND);
        } else {
            m_panelKeyBadge->setString(("[" + keyName(g_panelKey) + "]").c_str());
            m_panelKeyBadge->setColor(COL_OFF);
        }
    }
    void refreshStatus() {
        if (!m_statusLbl) return;
        if (g_bindingPanel)
            m_statusLbl->setString("Binding [Tecla menu] - press key, ESC=cancel");
        else if (g_bindingHack >= 0)
            m_statusLbl->setString(
                ("Binding [" + std::string(HACK_NAMES[g_bindingHack]) + "] - press key, ESC=cancel").c_str());
        else
            m_statusLbl->setString("NovaHack  |  click [key] badge to bind a shortcut");
    }

    // ── callbacks ─────────────────────────────────────────────────────────────
    void onToggle(CCObject* s) {
        int id = s->getTag();
        if (id < 0 || id >= H_COUNT) return;
        g_hacks[id] = !g_hacks[id];
        auto* audio = FMODAudioEngine::sharedEngine();
        if (id == H_NOMUSIC) audio->setBackgroundMusicVolume(g_hacks[id] ? 0.f : 1.f);
        if (id == H_NOSFX)   audio->setEffectsVolume(g_hacks[id]          ? 0.f : 1.f);
        if (id == H_FREEZE) {
            if (g_hacks[id]) audio->pauseAllMusic(true);
            else             audio->resumeAllMusic();
        }
        refreshToggle(id);
        saveAll();
    }
    void onBadge(CCObject* s) {
        int id = s->getTag();
        if (id < 0 || id >= H_COUNT) return;
        g_bindingHack = (g_bindingHack == id) ? -1 : id;
        for (int i = 0; i < H_COUNT; i++) refreshBadge(i);
        refreshStatus();
    }
    void onSpeed(CCObject* s) {
        int i = s->getTag();
        g_speed = SPEEDS[i];
        for (int j = 0; j < 5; j++) {
            if (!m_speedBtns[j]) continue;
            auto* l = static_cast<CCLabelBMFont*>(m_speedBtns[j]->getNormalImage());
            if (l) l->setColor(j == i ? COL_ON : COL_OFF);
        }
        saveAll();
    }
    void onPanelKeyBadge(CCObject*) {
        g_bindingPanel = !g_bindingPanel;
        if (g_bindingPanel) g_bindingHack = -1;  // cancel hack binding if active
        refreshPanelBadge();
        refreshStatus();
    }

    // ── column builder ────────────────────────────────────────────────────────
    void buildColumn(CCMenu* menu, int colIdx, const char* title,
                     const int* ids, int count, float colW, float sY,
                     ccColor4B hdrColor)
    {
        float x0    = colIdx * colW;
        float colH  = HDR_H + count * ROW_H + PAD * 2.f;
        float tSc   = std::max(0.18f, std::min(0.25f, (colW - 28.f) / 480.f));
        float btnCx = x0 + colW * 0.40f;
        float bdgCx = x0 + colW * 0.85f;

        // Deep space column background
        auto* bg = CCLayerColor::create(ccc4(8, 6, 20, 240), colW - 2.f, colH);
        bg->setPosition({x0 + 1.f, sY - colH});
        addChild(bg, 1);

        // Header
        auto* hdr = CCLayerColor::create(hdrColor, colW - 2.f, HDR_H);
        hdr->setPosition({x0 + 1.f, sY - HDR_H});
        addChild(hdr, 3);

        // Header shine – soft white highlight on upper portion
        float shinH = HDR_H * 0.45f;
        auto* shine = CCLayerColor::create(ccc4(255, 255, 255, 20), colW - 2.f, shinH);
        shine->setPosition({x0 + 1.f, sY - shinH});
        addChild(shine, 4);

        // Top accent line – 2 px brighter than header
        auto bri = [](GLubyte c, int d) -> GLubyte { return (GLubyte)std::min(255, (int)c + d); };
        auto* accent = CCLayerColor::create(
            ccc4(bri(hdrColor.r, 75), bri(hdrColor.g, 75), bri(hdrColor.b, 75), 215),
            colW - 2.f, 2.f);
        accent->setPosition({x0 + 1.f, sY - 2.f});
        addChild(accent, 4);

        // Separator line below header
        auto* sep = CCLayerColor::create(
            ccc4(hdrColor.r, hdrColor.g, hdrColor.b, 90), colW - 2.f, 1.f);
        sep->setPosition({x0 + 1.f, sY - HDR_H - 1.f});
        addChild(sep, 4);

        // Category title – uppercase
        std::string titleUp(title);
        for (char& c : titleUp) c = (char)toupper((unsigned char)c);
        auto* hl = CCLabelBMFont::create(titleUp.c_str(), "bigFont.fnt");
        hl->setScale(0.27f);
        hl->setColor({255, 255, 255});
        hl->setAnchorPoint({0.5f, 0.5f});
        hl->setPosition({x0 + colW * 0.5f, sY - HDR_H * 0.5f});
        addChild(hl, 4);

        float cy = sY - HDR_H - PAD - ROW_H * 0.5f;
        for (int n = 0; n < count; n++, cy -= ROW_H) {
            int id = ids[n];

            // Row background – category-tinted glow when hack is ON
            auto* rowBg = CCLayerColor::create(
                ccc4(hdrColor.r / 3, hdrColor.g / 3, hdrColor.b / 3, 0),
                colW - 4.f, ROW_H - 1.f);
            rowBg->setPosition({x0 + 2.f, cy - ROW_H * 0.5f});
            if (g_hacks[id]) rowBg->setOpacity(85);
            addChild(rowBg, 2);
            m_rowBgs[id] = rowBg;

            // Hack name toggle button
            auto* tl = CCLabelBMFont::create(HACK_NAMES[id], "bigFont.fnt");
            tl->setScale(tSc);
            tl->setColor(g_hacks[id] ? COL_ON : COL_OFF);
            auto* tb = CCMenuItemSpriteExtra::create(tl, this, menu_selector(NovaOverlay::onToggle));
            tb->setTag(id);
            tb->setPosition({btnCx, cy});
            menu->addChild(tb);
            m_hackBtns[id] = tb;

            // Keybind badge – cyan=binding, steel blue=bound, gray=unbound
            bool hasBind = ((int)g_hackKeys[id] != 0);
            auto* bl = CCLabelBMFont::create(("[" + keyName(g_hackKeys[id]) + "]").c_str(), "bigFont.fnt");
            bl->setScale(0.19f);
            bl->setColor(hasBind ? ccColor3B{150, 175, 215} : COL_OFF);
            auto* bb = CCMenuItemSpriteExtra::create(bl, this, menu_selector(NovaOverlay::onBadge));
            bb->setTag(id);
            bb->setPosition({bdgCx, cy});
            menu->addChild(bb);
            m_hackBadges[id] = bl;
        }
    }

    // ── init ──────────────────────────────────────────────────────────────────
    bool init() override {
        if (!CCLayer::init()) return false;

        auto ws   = CCDirector::sharedDirector()->getWinSize();
        float colW = ws.width / 7.f;
        float sY   = ws.height - 18.f;

        // Deep space dim
        auto* dim = CCLayerColor::create(ccc4(3, 2, 12, 155), ws.width, ws.height);
        dim->setPosition(CCPointZero);
        addChild(dim, 0);

        // ── NovaHack brand bar (top strip) ────────────────────────────────────
        float barH = ws.height - sY;
        auto* brandBar = CCLayerColor::create(ccc4(42, 16, 88, 245), ws.width, barH);
        brandBar->setPosition({0.f, sY});
        addChild(brandBar, 4);
        auto* brandEdge = CCLayerColor::create(ccc4(115, 55, 220, 160), ws.width, 1.f);
        brandEdge->setPosition({0.f, sY});
        addChild(brandEdge, 4);
        auto* brandLbl = CCLabelBMFont::create("NovaHack  v1.2.0", "bigFont.fnt");
        brandLbl->setScale(0.30f);
        brandLbl->setColor({200, 165, 255});
        brandLbl->setAnchorPoint({0.5f, 0.5f});
        brandLbl->setPosition({ws.width * 0.5f, sY + barH * 0.5f});
        addChild(brandLbl, 4);
        // ─────────────────────────────────────────────────────────────────────

        auto* menu = CCMenu::create();
        menu->setPosition(CCPointZero);
        addChild(menu, 5);

        static const ccColor4B HC[] = {
            ccc4(115, 42, 200, 255),  // Gameplay  – nebula purple
            ccc4( 22, 92, 188, 255),  // Visual    – pulsar blue
            ccc4(192, 72,   8, 255),  // Physics   – solar orange
            ccc4(  8,158,  88, 255),  // Cosmetic  – aurora green
            ccc4(192, 18,  42, 255),  // Bypass    – supernova red
            ccc4(  8,152, 188, 255),  // Audio     – quasar teal
            ccc4( 58, 58,  78, 255),  // Settings  – void slate
        };

        static const int GP[] = {H_NOCLIP,H_AUTORESTART,H_FASTCOMPLETE,H_FREEZE,
                                  H_NORESPAWN,H_IGNOREESC,H_PRACTICE,H_NODEATHFX,H_FREEZEATTEMPTS};
        static const int VI[] = {H_SHOWHITBOX,H_SHOWLABELS,H_SHOWDEATHS,H_SHOWTIME,
                                  H_SHOWPOS,H_SHOWVEL,H_HIDEUI,H_SHOWPERCENT};
        static const int PH[] = {H_LOWGRAV,H_HIGHGRAV,H_MOONGRAV,H_ANTIGRAV,
                                  H_SMALL,H_BIG,H_FORCEMINI,H_AUTOJUMP};
        static const int CO[] = {H_NOGLOW,H_NOTRAIL,H_NOPARTICLES,H_NOWAVETRAIL,
                                  H_NODEATHSOUND,H_NORESPAWNFLASH,H_TRANSBG,H_MIRROR};
        static const int BP[] = {H_UNLOCKALL,H_NOCHECKLIMIT,H_ALWAYSCHK,H_NOCOINCHK,
                                  H_BYPASSFILTER,H_FREEPRACTICE};
        static const int AU[] = {H_NOMUSIC,H_NOSFX,H_MUTEUNFOCUS,H_LOWPITCH};

        buildColumn(menu, 0, "Gameplay", GP, 9, colW, sY, HC[0]);
        buildColumn(menu, 1, "Visual",   VI, 8, colW, sY, HC[1]);
        buildColumn(menu, 2, "Physics",  PH, 8, colW, sY, HC[2]);
        buildColumn(menu, 3, "Cosmetic", CO, 8, colW, sY, HC[3]);
        buildColumn(menu, 4, "Bypass",   BP, 6, colW, sY, HC[4]);
        buildColumn(menu, 5, "Audio",    AU, 4, colW, sY, HC[5]);

        // Col 7 – Settings (speed + fps + panel key)
        {
            float x0 = colW * 6.f;
            float y  = sY;

            auto bri = [](GLubyte c, int d) -> GLubyte { return (GLubyte)std::min(255, (int)c + d); };
            auto addHdr = [&](const char* text) {
                auto* hdrBg = CCLayerColor::create(HC[6], colW - 2.f, HDR_H);
                hdrBg->setPosition({x0 + 1.f, y - HDR_H});
                addChild(hdrBg, 3);
                float shinH = HDR_H * 0.45f;
                auto* shine = CCLayerColor::create(ccc4(255, 255, 255, 18), colW - 2.f, shinH);
                shine->setPosition({x0 + 1.f, y - shinH});
                addChild(shine, 4);
                auto* ac = CCLayerColor::create(
                    ccc4(bri(HC[6].r,50), bri(HC[6].g,50), bri(HC[6].b,50), 180),
                    colW - 2.f, 2.f);
                ac->setPosition({x0 + 1.f, y - 2.f});
                addChild(ac, 4);
                std::string txt(text);
                for (char& c : txt) c = (char)toupper((unsigned char)c);
                auto* lbl = CCLabelBMFont::create(txt.c_str(), "bigFont.fnt");
                lbl->setScale(0.27f); lbl->setColor({255, 255, 255});
                lbl->setAnchorPoint({0.5f, 0.5f});
                lbl->setPosition({x0 + colW * 0.5f, y - HDR_H * 0.5f});
                addChild(lbl, 4);
                y -= HDR_H + PAD;
            };

            addHdr("Velocidad");

            int selSpd = 1;
            for (int i = 0; i < 5; i++) if (g_speed == SPEEDS[i]) selSpd = i;
            for (int i = 0; i < 5; i++) {
                auto* lbl = CCLabelBMFont::create(SPEED_LABELS[i], "bigFont.fnt");
                lbl->setScale(0.25f);
                lbl->setColor(i == selSpd ? COL_ON : COL_OFF);
                auto* btn = CCMenuItemSpriteExtra::create(lbl, this, menu_selector(NovaOverlay::onSpeed));
                btn->setTag(i);
                btn->setPosition({x0 + colW * 0.5f, y - ROW_H * 0.5f});
                menu->addChild(btn);
                m_speedBtns[i] = btn;
                y -= ROW_H;
            }

            auto* spdIn = geode::TextInput::create(colW - 20.f, "Custom speed");
            spdIn->setFilter("0123456789."); spdIn->setMaxCharCount(6);
            spdIn->setPosition({x0 + colW * 0.5f, y - 10.f});
            spdIn->setCallback([](const std::string& s) {
                float sp = 0.f;
                try { if (!s.empty()) sp = std::stof(s); } catch (...) {}
                if (sp > 0.f) { g_speed = sp; saveAll(); }
            });
            addChild(spdIn, 5);
            y -= 26.f;

            addHdr("FPS cap");

            auto* fpsIn = geode::TextInput::create(colW - 20.f, "0=unlimited");
            fpsIn->setFilter("0123456789"); fpsIn->setMaxCharCount(6);
            fpsIn->setString(g_fpsCap > 0 ? std::to_string(g_fpsCap) : "");
            fpsIn->setPosition({x0 + colW * 0.5f, y - 10.f});
            fpsIn->setCallback([](const std::string& s) {
                int fps = 0;
                try { if (!s.empty()) fps = std::stoi(s); } catch (...) {}
                g_fpsCap = fps;
                CCDirector::sharedDirector()->setAnimationInterval(fps > 0 ? 1.0/fps : 1.0/10000.0);
                saveAll();
            });
            addChild(fpsIn, 5);
            y -= 26.f;

            addHdr("Tecla menu");

            auto* badgeLbl = CCLabelBMFont::create(
                ("[" + keyName(g_panelKey) + "]").c_str(), "bigFont.fnt");
            badgeLbl->setScale(0.28f);
            badgeLbl->setColor(COL_OFF);
            auto* badgeBtn = CCMenuItemSpriteExtra::create(
                badgeLbl, this, menu_selector(NovaOverlay::onPanelKeyBadge));
            badgeBtn->setPosition({x0 + colW * 0.5f, y - ROW_H * 0.5f});
            menu->addChild(badgeBtn);
            m_panelKeyBadge = badgeLbl;
            y -= ROW_H;

            // Settings column background
            float colH = sY - y + PAD;
            auto* colBg = CCLayerColor::create(ccc4(8, 6, 20, 240), colW - 2.f, colH);
            colBg->setPosition({x0 + 1.f, y});
            addChild(colBg, 1);
        }

        // Status bar background
        auto* statusBg = CCLayerColor::create(ccc4(12, 8, 28, 235), ws.width, 22.f);
        statusBg->setPosition({0.f, 0.f});
        addChild(statusBg, 5);
        auto* statusEdge = CCLayerColor::create(ccc4(100, 50, 200, 120), ws.width, 1.f);
        statusEdge->setPosition({0.f, 22.f});
        addChild(statusEdge, 5);

        m_statusLbl = CCLabelBMFont::create(
            "NovaHack  |  click [key] badge to bind a shortcut", "bigFont.fnt");
        m_statusLbl->setScale(0.28f);
        m_statusLbl->setColor({185, 145, 255});
        m_statusLbl->setAnchorPoint({0.5f, 0.f});
        m_statusLbl->setPosition({ws.width * 0.5f, 5.f});
        addChild(m_statusLbl, 6);

        g_overlay = this;
        return true;
    }

    void onExit() override {
        if (g_overlay == this) g_overlay = nullptr;
        g_bindingHack  = -1;
        g_bindingPanel = false;
        CCLayer::onExit();
    }

public:
    static NovaOverlay* create() {
        auto* r = new NovaOverlay();
        if (r->init()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }

    static void show() {
        if (g_overlay) return;
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto* ov = create();
        if (ov) scene->addChild(ov, 9999, 7654);
    }
    static void hide() {
        if (!g_overlay) return;
        g_overlay->removeFromParentAndCleanup(true);
        // g_overlay cleared in onExit
    }
    static void toggle() {
        if (g_overlay) hide(); else show();
    }

    // Called from keyboard hook when binding the panel key
    static void onPanelKeyBind(enumKeyCodes key) {
        if (key != enumKeyCodes::KEY_Escape && key != enumKeyCodes::KEY_Delete)
            g_panelKey = key;
        g_bindingPanel = false;
        saveAll();
        if (g_overlay) {
            g_overlay->refreshPanelBadge();
            g_overlay->refreshStatus();
        }
    }

    // Called from keyboard hook when in binding mode
    static void onKeyBind(enumKeyCodes key) {
        if (g_bindingHack < 0) return;
        if (key == enumKeyCodes::KEY_Escape || key == enumKeyCodes::KEY_Delete) {
            g_bindingHack = -1;
        } else {
            g_hackKeys[g_bindingHack] = key;
            g_bindingHack = -1;
            saveAll();
        }
        if (g_overlay) {
            for (int i = 0; i < H_COUNT; i++) g_overlay->refreshBadge(i);
            g_overlay->refreshStatus();
        }
    }

    void refreshHack(int id) {
        refreshToggle(id);
        refreshBadge(id);
    }
};

// ── Hooks ─────────────────────────────────────────────────────────────────────

class $modify(PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_hacks[H_NOCLIP] || g_hacks[H_FASTCOMPLETE]) {
            g_deaths++;
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
        g_deaths++;
        if (g_hacks[H_NORESPAWN]) {
            g_levelActive = false;
            this->resetLevel();
        } else if (g_hacks[H_AUTORESTART]) {
            g_restartTimer = 0.5f;
        }
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        if (g_hacks[H_FREEZEATTEMPTS] && this->m_attempts > 0)
            this->m_attempts--;
        g_levelActive       = false;
        g_restartTimer      = -1.f;
        g_levelTime         = 0.f;
        g_fastCompleteFired = false;
    }
};

class $modify(GJBaseGameLayer) {
    void update(float dt) {
        if (g_hacks[H_FREEZE]) return;  // Congelar gameplay sin abrir pausa
        GJBaseGameLayer::update(dt * (g_speed > 0.f ? g_speed : 1.f));

        auto* pl = PlayLayer::get();
        if (!pl) { g_lastPL = nullptr; return; }

        if (pl != g_lastPL) {
            g_lastPL            = pl;
            g_levelActive       = false;
            g_restartTimer      = -1.f;
            g_levelTime         = 0.f;
            g_deaths            = 0;
            g_fastCompleteFired = false;
            if (pl->m_player1) g_lastPos = pl->m_player1->getPosition();
        }
        if (!g_levelActive) g_levelActive = true;
        else                g_levelTime  += dt;

        pl->m_isDebugDrawEnabled = g_hacks[H_SHOWHITBOX];

        if (g_hacks[H_FASTCOMPLETE] && pl->m_player1 && !g_fastCompleteFired) {
            g_fastCompleteFired = true;
            // Diferir fuera del update para evitar crash por re-entrada
            Loader::get()->queueInMainThread([] {
                if (auto* p = PlayLayer::get()) p->levelComplete();
            });
            return;
        }
        if (pl->m_player1) {
            if      (g_hacks[H_LOWGRAV])  pl->m_player1->m_gravityMod = 0.3f;
            else if (g_hacks[H_HIGHGRAV]) pl->m_player1->m_gravityMod = 3.0f;
            else if (g_hacks[H_MOONGRAV]) pl->m_player1->m_gravityMod = 0.12f;
            else if (g_hacks[H_ANTIGRAV]) pl->m_player1->m_gravityMod = -1.0f;
        }
        if (pl->m_player1) {
            if      (g_hacks[H_SMALL] || g_hacks[H_FORCEMINI]) pl->m_player1->setScale(0.5f);
            else if (g_hacks[H_BIG])                            pl->m_player1->setScale(1.8f);
        }
        if (g_hacks[H_AUTOJUMP] && pl->m_player1) {
            pl->m_player1->pushButton(PlayerButton::Jump);
            pl->m_player1->releaseButton(PlayerButton::Jump);
        }
        if (g_hacks[H_PRACTICE])  pl->m_isPracticeMode = true;
        if (pl->m_uiLayer)        pl->m_uiLayer->setVisible(!g_hacks[H_HIDEUI]);
        if (g_hacks[H_TRANSBG]) {
            if (auto* bg = dynamic_cast<CCSprite*>(pl->getChildByTag(1001)))
                bg->setOpacity(0);
        }

        static const int LBL1 = 9876, LBL2 = 9877;
        bool need1 = g_hacks[H_SHOWLABELS]||g_hacks[H_SHOWDEATHS]||
                     g_hacks[H_SHOWTIME]  ||g_hacks[H_SHOWPERCENT];
        bool need2 = g_hacks[H_SHOWPOS]   ||g_hacks[H_SHOWVEL];

        if (pl->m_uiLayer) {
            auto ws = CCDirector::sharedDirector()->getWinSize();
            auto* l1 = static_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByTag(LBL1));
            if (!l1) {
                l1 = CCLabelBMFont::create("", "bigFont.fnt");
                l1->setScale(0.38f); l1->setAnchorPoint({0.f, 1.f});
                pl->m_uiLayer->addChild(l1, 100, LBL1);
            }
            l1->setVisible(need1);
            if (need1) {
                l1->setPosition({6.f, ws.height - 6.f});
                std::string s;
                auto app = [&](const std::string& p){ if (!s.empty()) s += "  "; s += p; };
                if (g_hacks[H_SHOWLABELS]) {
                    int fps = (int)(1.f / CCDirector::sharedDirector()->getDeltaTime() + 0.5f);
                    app("FPS:" + std::to_string(fps) + " ATT:" + std::to_string(pl->m_attempts));
                }
                if (g_hacks[H_SHOWDEATHS])  app("D:" + std::to_string(g_deaths));
                if (g_hacks[H_SHOWPERCENT]) app(std::to_string((int)pl->getCurrentPercent()) + "%");
                if (g_hacks[H_SHOWTIME]) {
                    int mn = (int)(g_levelTime/60), sc = (int)g_levelTime%60;
                    app(std::to_string(mn) + ":" + (sc<10?"0":"") + std::to_string(sc));
                }
                l1->setString(s.c_str());
            }

            auto* l2 = static_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByTag(LBL2));
            if (!l2) {
                l2 = CCLabelBMFont::create("", "bigFont.fnt");
                l2->setScale(0.38f); l2->setAnchorPoint({0.f, 1.f});
                pl->m_uiLayer->addChild(l2, 100, LBL2);
            }
            l2->setVisible(need2);
            if (need2 && pl->m_player1) {
                l2->setPosition({6.f, ws.height - 22.f});
                auto cur = pl->m_player1->getPosition();
                std::string s;
                auto app = [&](const std::string& p){ if (!s.empty()) s += "  "; s += p; };
                if (g_hacks[H_SHOWPOS])
                    app("X:" + std::to_string((int)cur.x) + " Y:" + std::to_string((int)cur.y));
                if (g_hacks[H_SHOWVEL] && dt > 0.f) {
                    int vx = (int)((cur.x - g_lastPos.x)/dt);
                    int vy = (int)((cur.y - g_lastPos.y)/dt);
                    app("VX:" + std::to_string(vx) + " VY:" + std::to_string(vy));
                }
                l2->setString(s.c_str());
                g_lastPos = cur;
            }
        }

        if (g_restartTimer > 0.f) {
            g_restartTimer -= dt;
            if (g_restartTimer <= 0.f) { g_restartTimer = -1.f; pl->resetLevel(); }
        }
    }
};

class $modify(CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double dt) {
        if (down && !repeat) {
            // Binding mode: panel key
            if (g_bindingPanel) {
                NovaOverlay::onPanelKeyBind(key);
                return true;
            }
            // Binding mode: hack keybind
            if (g_bindingHack >= 0) {
                NovaOverlay::onKeyBind(key);
                return true;
            }

            // Per-hack keybinds (work anywhere, panel doesn't need to be open)
            for (int i = 0; i < H_COUNT; i++) {
                if ((int)g_hackKeys[i] != 0 && key == g_hackKeys[i]) {
                    g_hacks[i] = !g_hacks[i];
                    auto* audio = FMODAudioEngine::sharedEngine();
                    if (i == H_NOMUSIC) audio->setBackgroundMusicVolume(g_hacks[i] ? 0.f : 1.f);
                    if (i == H_NOSFX)   audio->setEffectsVolume(g_hacks[i]          ? 0.f : 1.f);
                    if (i == H_FREEZE) {
                        if (g_hacks[i]) audio->pauseAllMusic(true);
                        else            audio->resumeAllMusic();
                    }
                    saveAll();
                    if (g_overlay) g_overlay->refreshHack(i);
                    return true;
                }
            }

            // Panel toggle
            if (key == g_panelKey) {
                NovaOverlay::toggle();
                return true;
            }

            // Ignore ESC in level
            if (g_hacks[H_IGNOREESC] && PlayLayer::get() && key == enumKeyCodes::KEY_Escape)
                return true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, dt);
    }
};

class $modify(GameManager) {
    bool isIconUnlocked(int iconID, IconType type) {
        if (g_hacks[H_UNLOCKALL]) return true;
        return GameManager::isIconUnlocked(iconID, type);
    }
};

// ── Load settings on mod startup ──────────────────────────────────────────────
$execute {
    loadAll();
    if (g_fpsCap > 0)
        CCDirector::sharedDirector()->setAnimationInterval(1.0 / g_fpsCap);
}
