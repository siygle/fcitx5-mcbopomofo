// False-positive check: type every syllable of the phrase dictionary in the
// canonical key order on each layout, plus common typo patterns (a repeated
// key, re-typing the consonant to fix it). Count how often the hint rule
// (>=4 keys, >=2 overwrites, word in English lexicon) would fire.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include "Mandarin.h"
#include "PlusEnglish.h"
using namespace Formosa::Mandarin;
static int comps(const BPMF& s){return !!s.consonantComponent()+!!s.middleVowelComponent()+!!s.vowelComponent()+!!s.toneMarkerComponent();}
struct R { int keys=0, ov=0; std::string typed; };
static R type(const BopomofoKeyboardLayout* L, const std::string& keys){
  BopomofoReadingBuffer b(L); R r;
  for(char c:keys){ if(!b.isValidKey(c)) break; int before=comps(b.syllable()); b.combineKey(c); r.typed+=c; ++r.keys; if(comps(b.syllable())<=before) ++r.ov; if(b.hasToneMarker()) break; }
  return r;
}
int main(int, char** argv){
  McBopomofo::Plus::EnglishLexicon lex; lex.load(argv[2]);
  std::set<std::string> syl;
  { std::ifstream in(argv[1]); std::string line; while(std::getline(in,line)){ if(line.empty()||line[0]=='#'||line[0]=='_') continue; std::string r=line.substr(0,line.find(' ')); std::stringstream ss(r); std::string s; while(std::getline(ss,s,'-')) syl.insert(s);} }
  for (auto* L:{BopomofoKeyboardLayout::StandardLayout(),BopomofoKeyboardLayout::ETenLayout(),BopomofoKeyboardLayout::IBMLayout(),BopomofoKeyboardLayout::HsuLayout(),BopomofoKeyboardLayout::ETen26Layout()}){
    int n=0, fpClean=0, fpTypo=0, typos=0; std::string ex;
    for (auto& s: syl){
      auto bp=BPMF::FromComposedString(s);
      auto keys=L->keySequenceFromSyllable(bp);
      if(keys.empty()) continue;
      // Without the tone key: what the user sees while typing.
      std::string body=keys; if(bp.hasToneMarker()) body.pop_back();
      ++n;
      auto fire=[&](const std::string& k){ auto r=type(L,k); return r.keys>=4 && r.ov>=2 && std::all_of(r.typed.begin(),r.typed.end(),[](char c){return c>='a'&&c<='z';}) && lex.contains(r.typed); };
      if(fire(body)) ++fpClean;
      // Typo patterns: double a key; retype first key after the second.
      for(size_t i=0;i<body.size();++i){ std::string t=body; t.insert(i,1,body[i]); ++typos; if(fire(t)){++fpTypo; if(ex.size()<120) ex+=t+"("+s+") ";} }
      if(body.size()>=2){ std::string t=body.substr(0,2)+body[0]+body.substr(2); ++typos; if(fire(t)){++fpTypo; if(ex.size()<120) ex+=t+"("+s+") ";} }
    }
    std::printf("%-8s clean syllables %d: false hints %d | typo variants %d: false hints %d (%.2f%%) %s\n",L->name().c_str(),n,fpClean,typos,fpTypo,100.0*fpTypo/typos,ex.c_str());
  }
}
