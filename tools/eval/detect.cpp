// Detection signal: while typing letters, did any key overwrite an existing
// component (or fail to round-trip)? Real Zhuyin typed in order never does.
#include <fstream>
#include <iostream>
#include <string>
#include "Mandarin.h"
using namespace Formosa::Mandarin;
static bool overwritten(const BopomofoKeyboardLayout* L, const std::string& w) {
  BopomofoReadingBuffer b(L); std::string typed;
  for (char c : w) {
    if (!b.isValidKey(c)) return true;          // non-Bopomofo key (e.g. 'q'? no) -> clearly not Zhuyin
    b.combineKey(c); typed += c;
    if (b.hasToneMarker()) return true;         // tone fired mid-word
    if (L->keySequenceFromSyllable(b.syllable()) != typed) return true;
  }
  return false;
}
int main(int, char** argv) {
  for (auto* L : {BopomofoKeyboardLayout::StandardLayout(), BopomofoKeyboardLayout::ETenLayout(), BopomofoKeyboardLayout::IBMLayout(), BopomofoKeyboardLayout::HsuLayout(), BopomofoKeyboardLayout::ETen26Layout()}) {
    std::ifstream in(argv[1]); std::string w; int n=0,d=0,rank=0,n1=0,d1=0;
    while (std::getline(in,w)) { ++rank; if (w.size()<2) continue; ++n; bool x=overwritten(L,w); d+=x; if(rank<=2000){++n1;d1+=x;} }
    std::printf("%-8s detectable: %5.1f%% of %d words (top-2000: %5.1f%%)\n", L->name().c_str(), 100.0*d/n, n, 100.0*d1/n1);
  }
}
