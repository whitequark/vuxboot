/* Copyright (c) 2009 Peter Zotov <whitequark@whitequark.ru>
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

#ifndef _PICOOPT_H_
#define _PICOOPT_H_

#include <string>
#include <map>
#include <vector>
#include <set>

namespace picoopt {
  using namespace std;

  class parser {
    public:
      parser();
      void option(char sym, bool has_value=false, bool required=false);

      bool parse(int argc, const char* const* argv);
      bool parse(istream& in);
      
      bool valid();
      
      bool has(char option);
      string get(char option);
      
      vector<string> args();

    private:
      vector<string> strings;

      set<char> valid_options;
      set<char> valued_options;
      
      set<char> options;
      map<char, string> values;
      
      vector<char> requirements;
      
      vector<string> remain;
      
      bool parse(vector<string> options);
      parser(parser&);
  };
};

#endif

