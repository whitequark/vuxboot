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
#include <cctype>
#include <cstdlib>
#include "picoopt.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

using namespace std;

typedef unsigned char byte;
typedef unsigned short word;

const char* usage[] = {
  "",
  "  VuXprog is a serial programmer for universal and extensible AVR",
  "    serial bootloader VuXboot.",
  "  This software is not affiliated with Atmel in any way.",
  "",
  "  Actions|abbreviations:",
  "    flash_read|fr <filename>",
  "      By default, flash_read dumps only application code. To include",
  "      bootloader code, use -a option.",
  "    flash_write|fw <filename>",
  "    eeprom_read|er <filename>",
  "    eeprom_write|ew <filename>",
  "    reset|r",
  "",
  "  Options:",
  "    -s PORT\tset serial port device; default is /dev/ttyUSB0",
  "    -f FORMAT\tset file format; FORMAT may be ihex (default) or binary",
  "    -i SEQ\tstart bootloader by sending SEQ to port",
  "    -r\t\treset device after successful programming",
  "    -a\t\tdump full flash including bootloader code",
  "    -F\t\tdo things which sane human wouldn't",
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

class input_error: public error {
public:
  input_error(string message) : error(message) {}
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
      throw new input_error("flash page address too big");

    string req = "r";
    req += char(page & 0xff);
    req += char(page >> 8);
    write(req);

    return read(_page_words * 2);
  }

  void write_flash(unsigned page, string words) {
    if(words.length() != _page_words * 2)
      throw new error("flash page size mismatch");

    string req = "w", status;
    req += words;
    req += char(page & 0xff);
    req += char(page >> 8);
    write(req);

    status = read(1);
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
      throw new input_error("eeprom address too big");

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
  ios::openmode flags = ios::in;
  if(format == storage::binary)
    flags |= ios::binary | ios::ate;

  ifstream in(filename.c_str(), flags);
  if(!in)
    throw new io_error("cannot read from data file");

  if(format == storage::binary) {
    unsigned size = (unsigned) in.tellg();
    in.seekg(0);

    char data[size];
    in.read(data, size);

    return string(data, size);
  } else if(format == storage::ihex) {
    byte* image = (byte*) malloc(1);
    unsigned image_len = 0;

    while(in) {
      string hdata;
      getline(in, hdata, '\n');

      if(hdata[hdata.length()-1] == '\r')
        hdata = hdata.substr(0, hdata.length() - 1);

      if(hdata[0] != ':' || hdata.length() < 11 || hdata.length() % 2 != 1)
        throw new input_error("invalid ihex data (format)");

      unsigned blen = (hdata.length()-1)/2;
      byte bdata[blen], chksum = 0;

      for(int j = 1, k = 0; j < hdata.length(); j += 2) {
        string hbyte = hdata.substr(j, 2);
        bdata[k++] = strtoul(hbyte.c_str(), NULL, 16);
      }

      if(blen != bdata[0] + 5)
        throw new input_error("invalid ihex data (payload size)");

      for(int i = 0; i < blen-1; i++)
        chksum += bdata[i];

      if((byte) (chksum + bdata[blen-1]) != 0)
        throw new input_error("invalid ihex data (checksum)");

      byte ihex_len = bdata[0];
      word ihex_addr = (bdata[1] << 8) + bdata[2];
      byte ihex_type = bdata[3];

      if(ihex_type == 0) { // data
        if(image_len < ihex_addr + ihex_len)
          image_len = ihex_addr + ihex_len;
        image = (byte*) realloc(image, image_len);

        for(int i = 0; i < ihex_len; i++)
          image[ihex_addr+i] = bdata[4+i];
      } else if(ihex_type == 1) { // eof
        return string((char*) image, image_len);
      } else if(ihex_type == 3) { // .org
        if(ihex_len != 4)
          throw new input_error("invalid ihex data (invalid .org)");

        unsigned int org = (bdata[4] << 24) + (bdata[5] << 16) +
            (bdata[6] << 8) + bdata[7];

        for(int i = 0; i < org; i++)
          image[i] = 0xff; // erase anything below .org
      } else {
        throw new input_error("invalid ihex data (type)");
      }
    }

    throw new input_error("invalid ihex data (unterminated file)");
  }
}

void write_file(string filename, storage::format format, string data) {
  ios::openmode flags = ios::out;
  if(format == storage::binary)
    flags |= ios::binary;

  ofstream out(filename.c_str(), flags);
  if(!out)
    throw new io_error("cannot write to data file");

  if(format == storage::binary) {
    out << data;
  } else if(format == storage::ihex) {
    // data is always aligned by 16 bytes
    for(int i = 0; i < data.length() / 16; i++) {
      string subdata = data.substr(i * 16, 16);
      if(subdata != string(16, '\xff')) {
        char left[10], right[3];
        word addr = i * 16;
        byte checksum = 0x10 + byte(addr & 0xff) + byte(addr >> 8) + 0x01;

        sprintf(left, ":10%04X01", addr);
        out << left;

        for(int j = 0; j < subdata.length(); j++) {
          char middle[3];
          sprintf(middle, "%02X", (byte) subdata[j]);
          out << middle;

          checksum += subdata[j];
        }

        sprintf(right, "%02X", (byte) (0x100 - checksum));
        out << right;

        out << endl;
      }
    }

    out << ":00000001FF" << endl; // EOF*/
  }
}

int main(int argc, char* argv[]) {
  picoopt::parser opts;
  opts.option('s', true);
  opts.option('f', true);
  opts.option('i', true);
  opts.option('F');
  opts.option('a');
  opts.option('r');

  if(!opts.parse(argc, argv) || opts.has('h') || !opts.valid() || (opts.args().size() != 2 &&
        (opts.args().size() != 1 || (opts.args()[0] != "r" && opts.args()[0] != "reset")))) {
    cout << "Usage: " << argv[0] << " <action> [argument] ..." << endl;
    for(int i = 0; i < sizeof(usage) / sizeof(usage[0]); i++)
        cout << usage[i] << endl;
    return 1;
  }

  bool force = opts.has('F');
  bool do_reset = opts.has('r');
  bool dump_all = opts.has('a');

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

    if(opts.has('i'))
      bl.write(opts.get('i'));

    bl.identify();
    bl.describe();

    string action = opts.args()[0];
    if(action == "flash_read" || action == "fr") {
      string flash;

      unsigned last_page = dump_all ? bl.flash_pages() :
            bl.flash_pages() - bl.boot_pages();

      cout << "Reading flash: " << flush;
      for(int page = 0; page < last_page; page++) {
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

      if(even_pages > bl.flash_pages() - bl.boot_pages()) {
        cerr << "                         / ! \\      / ! \\       / ! \\" << endl;
        if(!force) {
          cerr << "* Image is " << even_pages << " pages long; writing it will "
               << "overwrite the bootloader" << endl << "* at pages "
               << bl.flash_pages() - bl.boot_pages() << "-" << bl.flash_pages() - 1
               << ". "
               << "Pass the -F flag if you really know what are you doing." << endl
               << "* Probably you will just overwrite first page of bootloader "
               << "and then everything" << endl
               << "* will fail, leaving you with a nice brick." << endl;
          return 1;
        } else if(force) {
          cerr << "* Shooting myself in the leg." << endl;
        }
      }
      
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
      do_reset = true;
    } else {
      cerr << "unknown action!" << endl;
      return 1;
    }

    if(do_reset) {
      cout << "Resetting device..." << endl;
      bl.reset();
    }
  } catch(input_error *e) {
    cerr << "input error: " << e->message() << endl;
    return 1;
  } catch(io_error *e) {
    cerr << "i/o error: " << e->message() << endl;
    return 1;
  } catch(protocol_error *e) {
    cerr << "protocol error: " << e->message() << endl;
    return 1;
  } catch(hardware_error *e) {
    cerr << "hardware error: " << e->message() << endl;
    return 1;
  } catch(error *e) {
    cerr << "internal error: " << e->message() << endl;
    return 1;
  }

  return 0;
}
