#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCHttpClient.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/InfoLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <matjson.hpp>

using namespace geode::prelude;

static const std::string SERVER_URL     = "http://node.waifly.com:27243/api/news.php";
static const std::string MOD_SECRET_KEY = "key123";

static bool s_hasCheckedNewsOnStartup = false;

// 1. HTTP HEADER INJECTION FOR GDPS
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

// Declare UI classes
class NewsPopup;

// 2. ADMIN POST NEWS DIALOG WITH FORMATTING SHORTCUTS & INPUT FIELDS
class PostNewsPopup : public geode::Popup<> {
protected:
    TextInput* m_titleInput;
    TextInput* m_bodyInput;
    TextInput* m_imageInput;
    TextInput* m_levelInput;
    TextInput* m_passcodeInput;

    bool setup() override {
        this->setTitle("Create News Post");

        auto winSize = m_mainLayer->getContentSize();

        // Title Input
        m_titleInput = TextInput::create(300.f, "Title", "chatFont.fnt");
        m_titleInput->setPosition({winSize.width / 2, winSize.height - 55.f});
        m_mainLayer->addChild(m_titleInput);

        // Body Input
        m_bodyInput = TextInput::create(300.f, "Body / Content", "chatFont.fnt");
        m_bodyInput->setPosition({winSize.width / 2, winSize.height - 95.f});
        m_mainLayer->addChild(m_bodyInput);

        // Formatting Shortcuts Menu
        auto formatMenu = CCMenu::create();
        formatMenu->setPosition({winSize.width / 2, winSize.height - 125.f});

        auto createTagBtn = [this, formatMenu](std::string const& label, std::string const& tag) {
            auto labelNode = CCLabelBMFont::create(label.c_str(), "goldFont.fnt");
            labelNode->setScale(0.5f);
            auto btn = CCMenuItemSpriteExtra::create(
                labelNode, this, menu_selector(PostNewsPopup::onInsertTag)
            );
            btn->setUserObject(CCString::create(tag));
            formatMenu->addChild(btn);
        };

        createTagBtn("[H1]", "# ");
        createTagBtn("[Bold]", "**text**");
        createTagBtn("[Italic]", "*text*");
        createTagBtn("[Image]", "![img](URL)");
        formatMenu->alignItemsHorizontallyWithPadding(10.f);
        m_mainLayer->addChild(formatMenu);

        // Image URL Input (External link to save disk space)
        m_imageInput = TextInput::create(300.f, "Image Direct URL (Optional)", "chatFont.fnt");
        m_imageInput->setPosition({winSize.width / 2, winSize.height - 155.f});
        m_mainLayer->addChild(m_imageInput);

        // Level ID for Comments
        m_levelInput = TextInput::create(300.f, "GDPS Level ID for Comments (Optional)", "chatFont.fnt");
        m_levelInput->setPosition({winSize.width / 2, winSize.height - 195.f});
        m_mainLayer->addChild(m_levelInput);

        // Admin Passcode Input
        m_passcodeInput = TextInput::create(300.f, "Admin Passcode", "chatFont.fnt");
        m_passcodeInput->setPasswordMode(true);
        m_passcodeInput->setPosition({winSize.width / 2, winSize.height - 235.f});
        m_mainLayer->addChild(m_passcodeInput);

        // Submit Button
        auto submitBtnSpr = ButtonSprite::create("Publish", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        auto submitBtn = CCMenuItemSpriteExtra::create(
            submitBtnSpr, this, menu_selector(PostNewsPopup::onSubmit)
        );
        auto menu = CCMenu::create();
        menu->setPosition({winSize.width / 2, 30.f});
        menu->addChild(submitBtn);
        m_mainLayer->addChild(menu);

        return true;
    }

    void onInsertTag(CCObject* sender) {
        if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
            if (auto str = typeinfo_cast<CCString*>(btn->getUserObject())) {
                std::string current = m_bodyInput->getString();
                m_bodyInput->setString(current + str->getCString());
            }
        }
    }

    void onSubmit(CCObject*) {
        auto req = web::WebRequest();
        req.param("action", "post");
        req.param("passcode", m_passcodeInput->getString());
        req.param("title", m_titleInput->getString());
        req.param("body", m_bodyInput->getString());
        req.param("image_url", m_imageInput->getString());
        req.param("level_id", m_levelInput->getString());

        async::spawn(req.post(SERVER_URL), [this](web::WebResponse res) {
            if (res.ok()) {
                auto json = res.json().unwrapOr(matjson::Value());
                bool success = json["success"].asBool().unwrapOr(false);
                std::string msg = json["message"].asString().unwrapOr("Response error.");

                FLAlertLayer::create(success ? "Success" : "Error", msg.c_str(), "OK")->show();
                if (success) this->onClose(nullptr);
            } else {
                FLAlertLayer::create("Error", "Could not connect to server.", "OK")->show();
            }
        });
    }

public:
    static PostNewsPopup* create() {
        auto ret = new PostNewsPopup();
        if (ret && ret->initAnchored(380.f, 290.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// 3. FULL SCROLLABLE NEWS PAGE POPUP
class NewsPopup : public geode::Popup<matjson::Value const&> {
protected:
    matjson::Value m_posts;
    ScrollLayer* m_scroll;

    bool setup(matjson::Value const& posts) override {
        m_posts = posts;
        this->setTitle("GDPS News & Announcements");

        auto winSize = m_mainLayer->getContentSize();

        // "+ Post News" Button
        auto postSpr = ButtonSprite::create("+ Post", "goldFont.fnt", "GJ_button_02.png", 0.7f);
        auto postBtn = CCMenuItemSpriteExtra::create(
            postSpr, this, menu_selector(NewsPopup::onOpenPostDialog)
        );
        auto topMenu = CCMenu::create();
        topMenu->setPosition({winSize.width - 50.f, winSize.height - 25.f});
        topMenu->addChild(postBtn);
        m_mainLayer->addChild(topMenu);

        // Scrollable List View
        m_scroll = ScrollLayer::create({winSize.width - 40.f, winSize.height - 70.f});
        m_scroll->setPosition({20.f, 20.f});
        m_mainLayer->addChild(m_scroll);

        renderList();
        return true;
    }

    void renderList() {
        m_scroll->m_contentLayer->removeAllChildren();

        float yPos = 0.f;
        float itemWidth = m_scroll->getContentSize().width;

        if (m_posts.isArray()) {
            for (auto const& item : m_posts.asArray().unwrap()) {
                int64_t postId    = item["id"].asInt().unwrapOr(0);
                std::string title = item["title"].asString().unwrapOr("News");
                std::string body  = item["body"].asString().unwrapOr("");
                std::string date  = item["date"].asString().unwrapOr("");
                int likes         = item["likes"].asInt().unwrapOr(0);
                int levelId       = item["level_id"].asInt().unwrapOr(0);

                // Card Container
                auto card = CCLayerColor::create({0, 0, 0, 100}, itemWidth, 90.f);

                // Title & Date
                auto titleLabel = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
                titleLabel->setScale(0.5f);
                titleLabel->setAnchorPoint({0.f, 1.f});
                titleLabel->setPosition({10.f, 82.f});
                card->addChild(titleLabel);

                auto dateLabel = CCLabelBMFont::create(date.c_str(), "chatFont.fnt");
                dateLabel->setScale(0.4f);
                dateLabel->setAnchorPoint({1.f, 1.f});
                dateLabel->setPosition({itemWidth - 10.f, 82.f});
                card->addChild(dateLabel);

                // Body Text Snippet
                auto bodyLabel = CCLabelBMFont::create(body.c_str(), "chatFont.fnt");
                bodyLabel->setScale(0.45f);
                bodyLabel->setAnchorPoint({0.f, 1.f});
                bodyLabel->setPosition({10.f, 58.f});
                card->addChild(bodyLabel);

                // Card Action Menu (Likes & GD Native Comments)
                auto cardMenu = CCMenu::create();
                cardMenu->setPosition({0.f, 0.f});

                // Like Button & Counter
                auto likeStr = fmt::format(" Like ({})", likes);
                auto likeLabel = CCLabelBMFont::create(likeStr.c_str(), "goldFont.fnt");
                likeLabel->setScale(0.4f);
                auto likeBtn = CCMenuItemSpriteExtra::create(
                    likeLabel, this, menu_selector(NewsPopup::onLikePost)
                );
                likeBtn->setTag(static_cast<int>(postId));
                likeBtn->setPosition({itemWidth - 110.f, 18.f});
                cardMenu->addChild(likeBtn);

                // GD Native Comments Integration Button
                if (levelId > 0) {
                    auto commentLabel = CCLabelBMFont::create(" Comments", "bigFont.fnt");
                    commentLabel->setScale(0.35f);
                    auto commentBtn = CCMenuItemSpriteExtra::create(
                        commentLabel, this, menu_selector(NewsPopup::onOpenComments)
                    );
                    commentBtn->setTag(levelId);
                    commentBtn->setPosition({itemWidth - 45.f, 18.f});
                    cardMenu->addChild(commentBtn);
                }

                card->addChild(cardMenu);
                card->setPosition({0.f, yPos});
                m_scroll->m_contentLayer->addChild(card);

                yPos += 95.f;
            }
        }

        m_scroll->m_contentLayer->setContentSize({itemWidth, std::max(yPos, m_scroll->getContentSize().height)});
    }

    void onLikePost(CCObject* sender) {
        int postId = sender->getTag();
        auto req = web::WebRequest();
        req.param("action", "like");
        req.param("id", std::to_string(postId));

        async::spawn(req.post(SERVER_URL), [](web::WebResponse res) {
            if (res.ok()) {
                FLAlertLayer::create("Liked!", "You liked this post.", "OK")->show();
            }
        });
    }

    // OPENS GD'S NATIVE COMMENT SYSTEM DIRECTLY
    void onOpenComments(CCObject* sender) {
        int levelId = sender->getTag();
        if (levelId <= 0) return;

        auto level = GJGameLevel::create();
        level->m_levelID = levelId;
        level->m_levelName = "News Discussion";

        auto info = InfoLayer::create(level, nullptr);
        info->show();
    }

    void onOpenPostDialog(CCObject*) {
        PostNewsPopup::create()->show();
    }

public:
    static NewsPopup* create(matjson::Value const& posts) {
        auto ret = new NewsPopup();
        if (ret && ret->initAnchored(420.f, 280.f, posts)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// 4. FETCH AND DISPLAY LOGIC
void fetchAndShowNews(bool forceShow) {
    auto req = web::WebRequest();
    req.param("action", "list");

    async::spawn(req.get(SERVER_URL), [forceShow](web::WebResponse res) {
        if (!res.ok()) {
            if (forceShow) FLAlertLayer::create("Error", "Could not fetch news.", "OK")->show();
            return;
        }

        auto json = res.json().unwrapOr(matjson::Value());
        if (!json.isArray()) return;

        auto postsArray = json.asArray().unwrap();
        if (postsArray.empty()) return;

        int64_t latestId = postsArray[0]["id"].asInt().unwrapOr(0);
        int64_t lastReadId = Mod::get()->getSavedValue<int64_t>("last_read_news_id", 0);

        if (forceShow || latestId > lastReadId) {
            Mod::get()->setSavedValue<int64_t>("last_read_news_id", latestId);
            NewsPopup::create(json)->show();
        }
    });
}

// 5. MAIN MENU HOOK
class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto menu = this->getChildByID("right-side-menu");
        if (!menu) menu = this->getChildByID("main-menu");

        if (menu) {
            auto spr = CCSprite::createWithSpriteFrameName("GJ_chatBtn_001.png");
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MyMenuLayer::onNewsButton)
            );
            btn->setID("gdps-news-btn"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }

        if (!s_hasCheckedNewsOnStartup) {
            s_hasCheckedNewsOnStartup = true;
            fetchAndShowNews(false);
        }

        return true;
    }

    void onNewsButton(CCObject*) {
        fetchAndShowNews(true);
    }
};
