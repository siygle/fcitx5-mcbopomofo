// SPDX-License-Identifier: MIT
// Runs in a private fcitx instance with a temporary HOME (see script).
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <iostream>
#include <string>

class TestContext final : public fcitx::InputContext {
 public:
  explicit TestContext(fcitx::InputContextManager& manager)
      : InputContext(manager, "mcb-plus-test") { created(); }
  ~TestContext() override { destroy(); }
  const char* frontend() const override { return "mcb-plus-test"; }
  void commitStringImpl(const std::string& s) override { committed += s; }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent&) override {}
  void updatePreeditImpl() override {}
  void key(fcitx::Key k) {
    fcitx::KeyEvent press(this, k, false); keyEvent(press);
    fcitx::KeyEvent release(this, k, true); keyEvent(release);
  }
  void type(const std::string& keys) {
    for (unsigned char c : keys) key(fcitx::Key(static_cast<fcitx::KeySym>(c)));
  }
  std::string committed;
};

int main() {
  char name[] = "mcb-plus-test";
  char disable[] = "--disable=all";
  char enable[] = "--enable=mcbopomofo";
  char* argv[] = {name, disable, enable};
  fcitx::Instance instance(3, argv);
  instance.addonManager().registerDefaultLoader(nullptr);
  fcitx::EventDispatcher dispatcher;
  dispatcher.attach(&instance.eventLoop());
  int failures = 0;
  dispatcher.schedule([&]() {
    TestContext ic(instance.inputContextManager());
    ic.focusIn();
    instance.setCurrentInputMethod(&ic, "mcbopomofo", true);
    auto check = [&](bool ok, const std::string& label) {
      std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
      if (!ok) ++failures;
    };
    auto reset = [&]() { ic.reset(); ic.committed.clear(); };
    auto menuSelect = [&](const std::string& label) {
      ic.key(fcitx::Key(FcitxKey_backslash, fcitx::KeyState::Ctrl));
      auto list = ic.inputPanel().candidateList();
      if (list) for (int i = 0; i < list->size(); ++i) {
        if (list->candidate(i).text().toString().find(label) != std::string::npos) {
          list->candidate(i).select(&ic); return true;
        }
      }
      return false;
    };
    check(instance.inputMethod(&ic) == "mcbopomofo", "load built addon");
    ic.type("=1+2*3"); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "7", "calculator through real fcitx key path");
    reset();
    check(menuSelect("常用語"), "toolbar feature menu");
    auto list = ic.inputPanel().candidateList();
    check(list && list->size() > 0, "toolbar candidates render without clipboard addon");
    ic.key(fcitx::Key(FcitxKey_Escape));
    reset();
    ic.type("su3cl3"); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "你好", "normal Bopomofo composition unchanged");
    reset();
    ic.type("ri3");
    list = ic.inputPanel().candidateList();
    check(!list || list->size() == 0, "invalid reading keeps original behavior (no correction panel)");
    reset();
    ic.type("d9 vup "); ic.key(fcitx::Key(FcitxKey_Down));  // 開心
    list = ic.inputPanel().candidateList();
    bool found = false;
    if (list) for (int i = 0; i < list->size(); ++i) {
      if (list->candidate(i).text().toString() == "開心😊") {
        check(i == 2, "emoji is third candidate");
        list->candidate(i).select(&ic); found = true; break;
      }
    }
    check(found && ic.committed == "開心😊", "emoji click commits entire composition once");
    reset();
    ic.type("su3cl3A");
    check(ic.committed == "你好 A", "spacing at commit boundary within composition");
    reset();
    ic.setCapabilityFlags(fcitx::CapabilityFlag::Password);
    ic.type("=");
    check(ic.inputPanel().preedit().toString().find("計算器") == std::string::npos,
          "Plus disabled in password context");
    // Emoji shortcodes, real key path with the default ";;" double-tap.
    ic.setCapabilityFlags(fcitx::CapabilityFlags{});
    reset();
    ic.type(";;tw");
    list = ic.inputPanel().candidateList();
    check(list && list->size() > 0 &&
              list->candidate(0).text().toString() == "🇹🇼 :tw",
          "double-tap ;; + user shortcode 'tw' -> 🇹🇼 first");
    ic.key(fcitx::Key(FcitxKey_space));
    check(ic.committed == "🇹🇼", "space commits the shortcode emoji");
    reset();
    ic.type("su3cl3;;smile");
    list = ic.inputPanel().candidateList();
    check(list && list->size() > 0 && list->candidate(0).text().toString() == "🙂 :smile",
          "user override 🙂 for :smile ranks first");
    ic.key(fcitx::Key(FcitxKey_space));
    check(ic.committed == "你好🙂", "emoji inserted at cursor, whole composition commits once");
    reset();
    ic.type("w;");  // ';' is ㄤ inside a syllable
    check(ic.inputPanel().preedit().toString() == "ㄊㄤ",
          "prefix key inside a syllable stays Bopomofo");
    ic.key(fcitx::Key(FcitxKey_Escape));
    reset();
    // English buffer mode via the default Ctrl+Shift+Space shortcut.
    const fcitx::Key toggleEn(FcitxKey_space,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift});
    reset();
    ic.type("su3cl3");  // 你好
    ic.key(toggleEn);
    check(ic.inputPanel().preedit().toString() == "你好[EN] ", "Ctrl+Shift+Space enters [EN] keeping 你好");
    ic.type("github");
    list = ic.inputPanel().candidateList();
    check(list && list->size() > 0 && list->candidate(0).text().toString() == "GitHub",
          "github -> GitHub first candidate (built-in SCOWL)");
    ic.key(fcitx::Key(FcitxKey_space));
    check(ic.committed == "你好 GitHub ", "space commits corrected word with auto-spacing");
    reset();
    ic.key(toggleEn);
    ic.type("openvanilla");
    ic.key(toggleEn);  // Toggle again also commits.
    check(ic.committed == "OpenVanilla", "user word list casing; toggle again commits");
    reset();
    ic.type("su3");  // Back in Bopomofo after leaving.
    check(ic.inputPanel().preedit().toString() == "你", "Bopomofo resumes after English mode");
    ic.key(fcitx::Key(FcitxKey_Escape));
    reset();
    ic.key(toggleEn);
    ic.type("abc");
    list = ic.inputPanel().candidateList();
    if (list && list->size() > 1) list->candidate(1).select(&ic);  // pick 2nd
    check(!ic.committed.empty(), "mouse/selection of an English candidate commits");
    // B: hint while typing English letters as Bopomofo, shortcut converts.
    reset();
    ic.type("su3github");
    check(ic.inputPanel().auxDown().toString().find("GitHub") != std::string::npos,
          "hint shows → GitHub without changing input");
    check(ic.inputPanel().preedit().toString().find("GitHub") == std::string::npos,
          "preedit still Bopomofo before shortcut");
    ic.key(toggleEn);
    ic.key(fcitx::Key(FcitxKey_space));
    check(ic.committed == "你 GitHub ", "shortcut converts hinted keys, commits with spacing");
    reset();
    ic.type("vu0");
    check(ic.inputPanel().auxDown().toString().empty(), "no hint for real Zhuyin ㄒㄧㄢ");
    ic.key(fcitx::Key(FcitxKey_Escape));
    // Only the English part is replaced; Chinese around it survives every
    // commit path (regression: Enter/selection used to drop 你好).
    reset();
    ic.type("su3cl3github"); ic.key(toggleEn); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "你好 github", "hint + Enter keeps 你好, commits as typed");
    reset();
    ic.type("su3cl3github"); ic.key(toggleEn);
    list = ic.inputPanel().candidateList();
    if (list && list->size() > 1) list->candidate(0).select(&ic);
    check(ic.committed == "你好 GitHub", "mouse select keeps 你好");
    reset();
    ic.type("su3cl3github"); ic.key(toggleEn);
    ic.key(fcitx::Key(FcitxKey_Down)); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "你好 github", "Down + Enter picks 2nd, keeps 你好");
    reset();
    ic.type("su3cl3"); ic.key(fcitx::Key(FcitxKey_Left));
    ic.key(toggleEn); ic.type("abc"); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "你 abc 好", "English inserted at cursor between 你 and 好");
    reset();
    ic.key(toggleEn); ic.type("me@x.io"); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "me@x.io", "symbols like @ are typed, not candidate keys");
    reset();
    ic.setCapabilityFlags(fcitx::CapabilityFlag::Password);
    ic.key(toggleEn);
    check(ic.inputPanel().preedit().toString().find("[EN]") == std::string::npos,
          "English mode disabled in password context");
    ic.setCapabilityFlags(fcitx::CapabilityFlags{});
    reset();
    ic.focusOut();
    instance.exit();
  });
  instance.exec();
  return failures ? 1 : 0;
}
