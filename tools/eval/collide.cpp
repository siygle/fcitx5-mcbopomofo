// Measures: how often typing an English word's letters on the Standard layout
// would compose into valid Bopomofo (collision), so auto-detection is unsafe.
#include <fstream>
#include <iostream>
#include <string>
#include "Mandarin.h"
#include "ParselessLM.h"
using namespace Formosa::Mandarin;
int main(int argc, char** argv) {
  McBopomofo::ParselessLM lm;
  if (!lm.open(argv[1])) return 1;
  const auto* layout = BopomofoKeyboardLayout::StandardLayout();
  std::ifstream in(argv[2]);
  std::string w;
  int total = 0, allValidKeys = 0, fullyValid = 0, invalidEarly = 0;
  int shown = 0;
  while (std::getline(in, w)) {
    if (w.size() < 3) continue;
    ++total;
    BopomofoReadingBuffer buf(layout);
    bool keysOk = true, bad = false;
    std::string out;
    for (char c : w) {
      if (!buf.isValidKey(c)) { keysOk = false; break; }
      buf.combineKey(c);
      if (buf.hasToneMarker()) {
        auto s = buf.syllable().composedString();
        if (!lm.hasUnigrams(s)) { bad = true; break; }
        out += s + " ";
        buf.clear();
      }
    }
    if (!keysOk) continue;
    ++allValidKeys;
    if (bad) { ++invalidEarly; continue; }
    // trailing reading must at least be a valid prefix of something: count as valid
    ++fullyValid;
    if (shown < 12) { std::cout << "  collide: " << w << " -> " << out << buf.composedString() << "\n"; ++shown; }
  }
  std::cout << "words>=3: " << total << "\nall keys are bopomofo keys: " << allValidKeys
            << "\n  hits an invalid syllable (detectable): " << invalidEarly
            << "\n  never invalid (indistinguishable from Zhuyin): " << fullyValid << "\n";
}
