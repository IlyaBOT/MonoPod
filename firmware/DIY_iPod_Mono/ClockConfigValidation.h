#pragma once
#include <stddef.h>
#include <string.h>

namespace ClockConfigValidation {
inline bool asciiAlnum(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
inline bool timezone(const char* value){
  if(!value)return false;
  const size_t n=strlen(value);
  if(!n||n>63)return false;
  for(size_t i=0;i<n;i++){
    const char c=value[i];
    if(!(asciiAlnum(c)||c=='+'||c=='-'||c==':'||c==','||c=='.'||c=='<'||c=='>'||c=='/'))return false;
  }
  return true;
}
inline bool ntpServer(const char* value){
  if(!value)return false;
  const size_t n=strlen(value);
  if(!n||n>253)return false;
  size_t label=0;
  for(size_t i=0;i<n;i++){
    const char c=value[i];
    if(c=='.'){
      if(!label||value[i-1]=='-')return false;
      label=0;continue;
    }
    if(!(asciiAlnum(c)||c=='-')||(!label&&c=='-')||++label>63)return false;
  }
  return label>0&&value[n-1]!='-';
}
}
