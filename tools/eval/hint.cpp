// B detection metric: within one reading buffer, count keys that did not add
// a new component (overwrite/ignored). Measures hint recall on English words
// and how many words keep their keys inside a single reading buffer.
#include <fstream>
#include <iostream>
#include <string>
#include "Mandarin.h"
using namespace Formosa::Mandarin;
static int comps(const BPMF& s){return !!s.consonantComponent()+!!s.middleVowelComponent()+!!s.vowelComponent()+!!s.toneMarkerComponent();}
int main(int, char** argv){
  for (auto* L:{BopomofoKeyboardLayout::StandardLayout(),BopomofoKeyboardLayout::ETenLayout(),BopomofoKeyboardLayout::IBMLayout(),BopomofoKeyboardLayout::HsuLayout(),BopomofoKeyboardLayout::ETen26Layout()}){
    for (int minLen : {3,4}) for (int minOv : {1,2}) {
      std::ifstream in(argv[1]); std::string w; int n=0,hit=0,split=0,rank=0,n1=0,h1=0;
      while(std::getline(in,w)){ ++rank; if((int)w.size()<minLen) continue; ++n; if(rank<=2000)++n1;
        BopomofoReadingBuffer b(L); int ov=0; bool ok=true;
        for(char c:w){ if(!b.isValidKey(c)){ok=false;break;} int before=comps(b.syllable()); b.combineKey(c); if(b.hasToneMarker()){ok=false;break;} if(comps(b.syllable())<=before) ++ov; }
        if(!ok){++split;continue;}
        if(ov>=minOv){++hit; if(rank<=2000)++h1;}
      }
      std::printf("%-8s len>=%d ov>=%d: hint %5.1f%% (top2000 %5.1f%%), tone/invalid key mid-word %4.1f%%\n",L->name().c_str(),minLen,minOv,100.0*hit/n,100.0*h1/n1,100.0*split/n);
    }
  }
}
