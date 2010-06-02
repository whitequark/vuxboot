/* Copyright (c) 2009 Peter Zotov <whitequark@whitequark.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <iostream>
#include <fstream>
#include "picoopt.h"

using namespace picoopt;

parser::parser() {
}

void parser::option(char sym, bool has_value, bool required) {
  valid_options.insert(sym);
  if(has_value)
    valued_options.insert(sym);
  if(required)
    requirements.push_back(sym);
}

bool parser::parse(vector<string> strings) {
  for(int i = 0; i < strings.size(); i++) {
    string s = strings[i];

    if(s[0] == '-') {
      if(s.size() > 2) {
        cerr << "error: option " << s << " is unsupported" << endl;
        return false;
      } else if(s == "--") { // stop parsing
        for(int j = i + 1; j < strings.size(); j++)
          remain.push_back(strings[j]);
        break;
      } else if(valid_options.find(s[1]) != valid_options.end()) {
        bool valued = valued_options.find(s[1]) != valued_options.end();
        if(valued && ((i + 1) == strings.size() || strings[i+1][0] == '-')) {
          cerr << "error: option " << s << " requires an argument" << endl;
          return false;
        }
        if(has(s[1])) {
          cerr << "error: duplicate option " << s << endl;
          return false;
        }
        options.insert(s[1]);
        if(valued) {
          values[s[1]] = strings[i+1];
          i++;
        }
      } else {
        cerr << "error: option " << s << " is unknown" << endl;
        return false;
      }
    } else {
      remain.push_back(s);
    }
  }
  
  return true;
}

bool parser::parse(int argc, const char* const* argv) {
  vector<string> options;
  for(int i = 1; i < argc; i++)
    options.push_back(argv[i]);
  return parse(options);
}

bool parser::parse(istream& in) {
  vector<string> options;
  char opt[128];

  if(!in.good())
    return false;
  while(!in.eof()) {
    in.getline(opt, 128);
    string sopt(string(opt) + " ");
    int start = -1;
    for(int i = 1; i < sopt.length(); i++) {
      if(sopt[i] == ' ') {
        if(start != i-1)
          options.push_back(sopt.substr(start+1, i-start-1));
        start = i;
      }
    }
  }

  return parse(options);
}

bool parser::valid() {
  for(int i = 0; i < requirements.size(); i++) {
    char r = requirements[i];
    if(!has(r)) {
      cerr << "error: required option -" << r << " is not set" << endl;
      return false;
    }
  }
  return true;
}

bool parser::has(char option) {
  return options.find(option) != options.end();
}

string parser::get(char option) {
  return values[option];
}

vector<string> parser::args() {
  return remain;
}

