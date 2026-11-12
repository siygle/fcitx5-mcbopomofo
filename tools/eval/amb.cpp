// A letter run + Space is ambiguous only if the letters are exactly a valid
// tone-1 syllable (no component overwriting) that exists in the LM.
#include <fstream>
#include <iostream>
#include <string>
#include "Mandarin.h"
#include "ParselessLM.h"
using namespace Formosa::Mandarin;
int main(int, char** argv) {
  McBopomofo::ParselessLM lm; lm.open(argv[1]);
  for (auto* layout : {BopomofoKeyboardLayout::StandardLayout(), BopomofoKeyboardLayout::ETenLayout(), BopomofoKeyboardLayout::HsuLayout()}) {
    std::ifstream in(argv[2]); std::string w; int n=0, amb=0, top=0, shown=0; int rank=0;
    while (std::getline(in, w)) {
      ++rank; if (w.size() < 2) continue; ++n;
      auto s = layout->syllableFromKeySequence(w);
      bool exact = layout->keySequenceFromSyllable(s) == w && !s.hasToneMarker();
      if (exact && lm.hasUnigrams(s.composedString())) {
        ++amb; if (rank <= 1000) ++top;
        if (shown++ < 10) std::cout << "   " << w << " = " << s.composedString() << "\n";
      }
    }
    std::cout << layout->name() << ": words>=2 " << n << ", ambiguous " << amb << " (" << 100.0*amb/n << "%), in top-1000: " << top << "\n";
  }
}
