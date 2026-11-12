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
    check(menuSelect("首注音"), "initials feature menu");
    ic.type("w1");
    auto list = ic.inputPanel().candidateList();
    bool found = false;
    if (list) for (int i = 0; i < list->size(); ++i) {
      if (list->candidate(i).text().toString() == "台北") {
        list->candidate(i).select(&ic); found = true; break;
      }
    }
    check(found && ic.committed == "台北", "initials candidate click commits 台北");
    reset();
    ic.type("w961"); ic.key(fcitx::Key(FcitxKey_Down));
    list = ic.inputPanel().candidateList();
    found = false;
    if (list) for (int i = 0; i < list->size(); ++i) {
      if (list->candidate(i).text().toString() == "台北") found = true;
    }
    check(found, "unfinished reading completion candidates");
    ic.key(fcitx::Key(FcitxKey_Escape));
    ic.type("o3"); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "台北", "Escape preserves original composition");
    reset();
    ic.type("ri3");
    list = ic.inputPanel().candidateList();
    check(list && list->size() > 0, "invalid reading offers correction panel");
    ic.key(fcitx::Key(FcitxKey_Down)); ic.key(fcitx::Key(FcitxKey_Return));
    check(!ic.committed.empty(), "correction selection commits");
    reset();
    check(menuSelect("常用語"), "toolbar feature menu");
    list = ic.inputPanel().candidateList();
    check(list && list->size() > 0, "toolbar candidates render without clipboard addon");
    ic.key(fcitx::Key(FcitxKey_Escape));
    reset();
    ic.type("su3cl3"); ic.key(fcitx::Key(FcitxKey_Return));
    check(ic.committed == "你好", "normal Bopomofo composition unchanged");
    reset();
    ic.type("d9 vup "); ic.key(fcitx::Key(FcitxKey_Down));  // 開心
    list = ic.inputPanel().candidateList();
    found = false;
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
    ic.focusOut();
    instance.exit();
  });
  instance.exec();
  return failures ? 1 : 0;
}
