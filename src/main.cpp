#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCHttpClient.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>

using namespace geode::prelude;

// ==========================================
// CONFIGURATION (CHANGE THESE TO MATCH YOUR SERVER)
// ==========================================
static const std::string SERVER_URL     = "https://node.waifly.com:27243/api/news.php";
static const std::string MOD_SECRET_KEY = "key123";

static bool s_hasCheckedNewsOnStartup = false;

// 1. AUTOMATIC HTTP HEADER INJECTION
class $modify(GDPSHttpClient, cocos2d::extension::CCHttpClient) {
    void send(cocos2d::extension::CCHttpRequest* request) {
        if (request) {
            std::vector<std::string> headers = request->getHeaders();
            headers.push_back("X-GDPS-Mod-Key: " + MOD_SECRET_KEY);
            request->setHeaders(headers);
        }
        cocos2d::extension::CCHttpClient::send(request);
    }
};

// Helper to send new news to server
void postNewsToServer(std::string const& passcode, std::string const& title, std::string const& body) {
    auto req = web::WebRequest();
    req.param("passcode", passcode);
    req.param("title", title);
    req.param("body", body);

    // Geode v5+ Async Spawn API
    async::spawn(
        req.post(SERVER_URL),
        [](web::WebResponse res) {
            if (res.ok()) {
                auto json = res.json().unwrapOr(matjson::Value());
                bool success = json["success"].asBool().unwrapOr(false);
                std::string msg = json["message"].asString().unwrapOr("Unknown response.");

                if (success) {
                    FLAlertLayer::create("Success", msg.c_str(), "OK")->show();
                } else {
                    FLAlertLayer::create("Error", msg.c_str(), "OK")->show();
                }
            } else {
                FLAlertLayer::create("Error", "Could not reach GDPS news server.", "OK")->show();
            }
        }
    );
}

// Function to prompt Admin for passcode and post news
void openAdminPostDialog() {
    geode::createQuickPopup(
        "Admin Passcode",
        "Enter your Admin Passcode to post news:",
        "Cancel", "Submit",
        [](auto self, bool btn2) {
            if (btn2) {
                std::string passcode = "admin1234";
                std::string title = "New Server Update!";
                std::string body = "We added custom levels and weekly contests. Enjoy!";

                postNewsToServer(passcode, title, body);
            }
        }
    );
}

// Fetch news from server and show popup
void fetchAndShowNews(bool forceShow) {
    // Geode v5+ Async Spawn API
    async::spawn(
        web::WebRequest().get(SERVER_URL),
        [forceShow](web::WebResponse res) {
            if (!res.ok()) {
                if (forceShow) {
                    FLAlertLayer::create("News Error", "Failed to fetch news from server.", "OK")->show();
                }
                return;
            }

            auto json = res.json().unwrapOr(matjson::Value());
            int64_t newsId    = json["id"].asInt().unwrapOr(0);
            std::string title = json["title"].asString().unwrapOr("GDPS News");
            std::string body  = json["body"].asString().unwrapOr("No news content.");
            std::string date  = json["date"].asString().unwrapOr("");

            int64_t lastReadId = Mod::get()->getSavedValue<int64_t>("last_read_news_id", 0);

            // Show automatically if news is new, or if manually clicked
            if (forceShow || newsId > lastReadId) {
                Mod::get()->setSavedValue<int64_t>("last_read_news_id", newsId);

                std::string fullText = fmt::format("{}\n\n[Published: {}]", body, date);

                geode::createQuickPopup(
                    title.c_str(),
                    fullText,
                    "Close", "Post News",
                    [](auto self, bool btn2) {
                        if (btn2) {
                            openAdminPostDialog();
                        }
                    }
                );
            }
        }
    );
}

// 2. MAIN MENU HOOK
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        // Add manual News Button to main menu
        auto menu = this->getChildByID("right-side-menu");
        if (!menu) menu = this->getChildByID("main-menu");

        if (menu) {
            auto spr = CCSprite::createWithSpriteFrameName("GJ_chatBtn_001.png");
            auto btn = CCMenuItemSpriteExtra::create(
                spr,
                this,
                menu_selector(MyMenuLayer::onNewsButton)
            );
            btn->setID("gdps-news-btn"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }

        // Auto-open on game launch if new news exists
        if (!s_hasCheckedNewsOnStartup) {
            s_hasCheckedNewsOnStartup = true;
            fetchAndShowNews(false);
        }

        return true;
    }

    void onNewsButton(CCObject* sender) {
        fetchAndShowNews(true);
    }
};
