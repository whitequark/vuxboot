/*
 * Copyright (c) 2010 Peter Zotov <whitequark@whitequark.org>
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

#include <string>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include "picoopt.h"

using namespace std;

typedef unsigned char byte;

const char* usage[] = {
  "",
  "  VuXprog is a serial programmer for universal and extensible AVR",
  "    serial bootloader VuXboot.",
  "  This software is not affiliated with Atmel in any way.",
  "",
  "  Actions|abbreviations:",
  "    flash_read|fr <filename>",
  "    flash_write|fw <filename>",
  "    eeprom_read|er <filename>",
  "    eeprom_write|ew <filename>",
  "    reset|r",
  "",
  "  Options:",
  "    -s PORT\tset serial port device; default is /dev/ttyUSB0",
  "    -f FORMAT\tset file format; FORMAT may be ihex (default) or binary",
  "    -i SEQ\tstart bootloader by sending SEQ to port",
  ""
};

namespace storage {
  enum format {
    ihex,
    binary
  };
}

class error: public exception {
public:
  error(string message) : _message(message) {}
  string message() { return _message; }
  ~error() throw() { }

private:
  string _message;
};

class io_error: public error {
public:
  io_error(string message) : error(message) {}
};

class feature_error: public error {
public:
  feature_error(string message) : error(message) {}
};

class hardware_error: public error {
public:
  hardware_error(string message) : error(message) {}
};

class protocol_error: public error {
public:
  protocol_error(string info, string node="") : error(make_message(info, node)) {}
  ~protocol_error() throw() {}

private:
  static string make_message(string info, string node) {
    string message = info;
    if(node != "")
      message += ": `" + node + "'";
    return message;
  }
};

class vuxboot {
public:
  vuxboot(string filename, unsigned baud = 0) {
    _fd = open(filename.c_str(), O_RDWR | O_NONBLOCK);
    if(!_fd) throw new io_error("cannot open port");

    // TODO: set baud rate
  }

  ~vuxboot() {
    close(_fd);
  }

  void identify() {
    write("s");

    string signature = read(3);
    if(signature != "VuX")
      throw new protocol_error("wrong signature", signature);

    string s_type = read(1);
    if(s_type != "f" && s_type != "e")
      throw new protocol_error("wrong type", s_type);

    _has_eeprom = (s_type == "e");
    if(_has_eeprom) {
      string s_eesize = read(1);
      _eeprom_bytes = 1 << s_eesize[0];
      s_type += s_eesize;
    }

    string s_flash_sizes = read(3);
    _page_words = s_flash_sizes[0];
    _flash_pages = 1 << s_flash_sizes[1];
    _boot_pages = s_flash_sizes[2];

    string s_checksum = read(1);

    string concat = signature + s_type + s_flash_sizes;
    char checksum = 0;
    for(int i = 0; i < concat.length(); i++)
      checksum += concat[i];

    if(checksum != s_checksum[0])
      throw new protocol_error("bad checksum");
  }

  void describe() {
    cout << "Device capabilities:" << endl;
    if(_has_eeprom)
      cout << "  EEPROM: " << _eeprom_bytes << " bytes." << endl;
    cout << "  Page size: " << _page_words << " words." << endl;
    cout << "  Flash size: " << _flash_pages << " pages." << endl;
    cout << "  Reserved area: " << _boot_pages << " pages (at end)." << endl;
  }

  string read_flash(unsigned page) {
    if(page > flash_pages())
      throw new feature_error("flash page address too big");

    string req = "r";
    req += char(page & 0xff);
    req += char(page >> 8);
    write(req);

    return read(_page_words * 2);
  }

  void write_flash(unsigned page, string words) {
    if(words.length() != _page_words * 2)
      throw new feature_error("flash page size mismatch");

    string req = "w", status;
    req += char(page & 0xff);
    req += char(page >> 8);
    write(req);

    status = read(1);
    if(status != ".")
      throw new hardware_error("cannot erase flash");

    write(words);
    if(status != ".")
      throw new hardware_error("cannot write flash");
  }

  string read_eeprom() {
    if(!_has_eeprom)
      throw new feature_error("no eeprom");

    write("R");
    return read(_eeprom_bytes);
  }

  void write_eeprom(unsigned address, byte b) {
    if(!_has_eeprom)
      throw new feature_error("no eeprom");
    if(address > _eeprom_bytes)
      throw new feature_error("eeprom address too big");

    string req = "W";
    req += char(address & 0xff);
    req += char(address >> 8);
    req += char(b);
    write(req);

    string status = read(1);
    if(status != ".")
      throw new hardware_error("cannot write eeprom");
  }

  void reset() {
    write("q");
  }

  bool has_eeprom() {
    return _has_eeprom;
  }

  unsigned eeprom_bytes() {
    return _eeprom_bytes;
  }

  unsigned flash_pages() {
    return _flash_pages;
  }

  unsigned boot_pages() {
    return _boot_pages;
  }

  unsigned page_words() {
    return _page_words;
  }

private:
  string read(unsigned length, unsigned timeout=5) {
    char data[length];

    unsigned received = 0;
    while(received < length) {
      struct timeval to = {0};
      to.tv_sec = timeout;

      fd_set rfds, efds;
      FD_ZERO(&rfds);
      FD_SET(_fd, &rfds);

      FD_ZERO(&efds);
      FD_SET(_fd, &efds);

      int retval = select(FD_SETSIZE, &rfds, NULL, &efds, &to);
      if(retval == -1) {
        throw new io_error("cannot select()");
      } else if(retval == 0) {
        throw new io_error("read timeout");
      } else if(FD_ISSET(_fd, &efds)) {
        throw new io_error("i/o error");
      }

      retval = ::read(_fd, data + received, length);
      if(retval == -1) {
        throw new io_error("cannot read()");
      }

      received += retval;
    }

    return string(data, received);
  }

  void write(string data) {
    if(::write(_fd, data.c_str(), data.length()) != data.length()) {
      throw new io_error("cannot write()");
    }
  }

private:
  int _fd;

  bool _has_eeprom;
  unsigned _eeprom_bytes;
  unsigned _page_words, _flash_pages, _boot_pages;
};

string read_file(string filename, storage::format format) {
  ios::openmode flags = ios::ate;
  if(format == storage::binary)
    flags |= ios::binary;

  ifstream in(filename.c_str(), flags);
  if(!in)
    throw new io_error("cannot read from data file");

  unsigned size = in.tellg();
  in.seekg(0);

  char data[size];
  in.get(data, size);

  if(format == storage::binary) {
    return string(data, size);
  }
}

bool write_file(string filename, storage::format format, string data) {
  ios::openmode flags;
  if(format == storage::binary)
    flags = ios::binary;

  ofstream out(filename.c_str(), flags);
  if(!out)
    throw new io_error("cannot write to data file");

  if(format == storage::binary) {
    out << data;
  }
}

int main(int argc, char* argv[]) {
  picoopt::parser opts;
  opts.option('s', true);
  opts.option('f', true);
  opts.option('i', true);

  if(!opts.parse(argc, argv) || opts.has('h') || !opts.valid() || (opts.args().size() != 2 &&
        (opts.args().size() != 1 || (opts.args()[0] != "r" && opts.args()[0] != "reset")))) {
    cout << "Usage: " << argv[0] << " <action> [argument] ..." << endl;
    for(int i = 0; i < sizeof(usage) / sizeof(usage[0]); i++)
        cout << usage[i] << endl;
    return 1;
  }

  storage::format format = storage::ihex;
  if(opts.has('f')) {
    string new_format = opts.get('f');
    if(new_format == "ihex") {
      format = storage::ihex;
    } else if(new_format == "binary") {
      format = storage::binary;
    } else {
      cerr << "unknown storage format `" << new_format << "'!" << endl;
      return 1;
    }
  }

  string port = "/dev/ttyUSB0";
  if(opts.has('s'))
    port = opts.get('s');

  try {
    vuxboot bl(port);
    bl.identify();
    bl.describe();

    string action = opts.args()[0];
    if(action == "flash_read" || action == "fr") {
      string flash;

      cout << "Reading flash: " << flush;
      for(int page = 0; page < bl.flash_pages(); page++) {
        flash += bl.read_flash(page);
        if(page % 10 == 1)
          cout << "." << flush;
      }
      cout << endl;

      write_file(opts.args()[1], format, flash);
    } else if(action == "flash_write" || action == "fw") {
      string flash = read_file(opts.args()[1], format);

      unsigned page_bytes = bl.page_words() * 2;
      unsigned even_pages = flash.length() / page_bytes + (flash.length() % page_bytes > 0);
      flash.resize(even_pages * page_bytes, 0xff);
      
      cout << "Writing flash: " << flush;

      unsigned changed = 0;
      for(int page = 0; page < even_pages; page++) {
        string new_page = flash.substr(page * page_bytes, page_bytes);
        if(new_page != string(page_bytes, (char) 0xff)) {
          string old_page = bl.read_flash(page);
          if(old_page != new_page) {
            bl.write_flash(page, new_page);
            if(changed++ % 10 == 0)
              cout << "." << flush;
            if(bl.read_flash(page) != new_page) {
              cerr << "verification failed!" << endl;
              return 1;
            }
          }
        }
      }

      cout << " " << changed << " pages." << endl;
    } else if(action == "eeprom_read" || action == "er") {
      write_file(opts.args()[1], format, bl.read_eeprom());
    } else if(action == "eeprom_write" || action == "ew") {
      string old_eeprom = bl.read_eeprom();
      string new_eeprom = read_file(opts.args()[1], format);

      if(new_eeprom.length() > old_eeprom.length()) {
        cerr << "eeprom image is too big!" << endl;
        return 1;
      }

      new_eeprom.resize(old_eeprom.length(), 0xff);

      cout << "Writing eeprom: " << flush;

      unsigned changed = 0;
      for(int i = 0; i < new_eeprom.length(); i++) {
        if(old_eeprom[i] != new_eeprom[i]) {
          bl.write_eeprom(i, new_eeprom[i]);
          if(changed++ % 10 == 0)
            cout << "." << flush;
        }
      }

      cout << " " << changed << " bytes." << endl;

      if(bl.read_eeprom() != new_eeprom) {
        cerr << "verification failed!" << endl;
        return 1;
      }
    } else if(action == "reset" || action == "r") {
      cout << "Resetting device..." << endl;
      bl.reset();
    } else {
      cerr << "unknown action!" << endl;
      return 1;
    }
  } catch(io_error *e) {
    cerr << "i/o error: " << e->message() << endl;
    return 1;
  } catch(protocol_error *e) {
    cerr << "protocol error: " << e->message() << endl;
    return 1;
  } catch(hardware_error *e) {
    cerr << "hardware error: " << e->message() << endl;
    return 1;
  }

  return 0;
}
