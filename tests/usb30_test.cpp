// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// USB 3.0 in system_settings.ini (USB A of 14/11): the line added, changed, taken from a comment,
// the section made when it is missing, CRLF kept, and every other byte as it was.
#include "usb30_ini.hpp"

#include <cstdio>
#include <string>

using namespace nxdock;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failed++;
    }
}

int main() {
    const std::string on = "usb30_force_enabled = u8!0x1", off = "usb30_force_enabled = u8!0x0";

    // nothing there yet
    Check(Usb30InIni("") == -1, "an empty file says nothing");
    Check(SetUsb30InIni("", true) == "[usb]\n" + on + "\n", "an empty file gets the section and the line");
    Check(Usb30InIni(SetUsb30InIni("", true)) == 1, "and then it is on");

    // other sections are kept, the new one goes at the end
    const std::string other = "[eupld]\nupload_enabled = u8!0x0\n";
    Check(SetUsb30InIni(other, true) == other + "[usb]\n" + on + "\n", "a file without [usb] keeps all it had");
    Check(SetUsb30InIni("[eupld]\nupload_enabled = u8!0x0", true) == other + "[usb]\n" + on + "\n",
          "a last line without its end gets one before the section");

    // added right under [usb], next to what is there
    const std::string usb = "[usb]\nother = u8!0x2\n[ro]\nease_nro_restriction = u8!0x1\n";
    Check(SetUsb30InIni(usb, true) == "[usb]\n" + on + "\nother = u8!0x2\n[ro]\nease_nro_restriction = u8!0x1\n",
          "added under [usb], the rest as it was");
    Check(Usb30InIni(usb) == -1, "another key of [usb] says nothing");

    // replaced, never twice
    const std::string has = "; a comment\n[usb]\nusb30_force_enabled = u8!0x0\n";
    const std::string turned = SetUsb30InIni(has, true);
    Check(turned == "; a comment\n[usb]\n" + on + "\n", "an existing line is replaced");
    Check(Usb30InIni(has) == 0 && Usb30InIni(turned) == 1, "off and on read back");
    Check(SetUsb30InIni(turned, true) == turned, "turning on what is on changes nothing");
    Check(SetUsb30InIni(turned, false) == "; a comment\n[usb]\n" + off + "\n", "turned off in its place");
    Check(SetUsb30InIni("[usb]\n  usb30_force_enabled=u8!0x1\n", false) == "[usb]\n  " + off + "\n", "its indent is kept");

    // a line commented out is taken from the comment instead of adding another
    const std::string commented = "[usb]\n; usb30_force_enabled = u8!0x1\nx = 1\n";
    Check(Usb30InIni(commented) == -1, "a commented line says nothing");
    Check(SetUsb30InIni(commented, true) == "[usb]\n" + on + "\nx = 1\n", "a commented line is uncommented");
    Check(SetUsb30InIni("[usb]\n#usb30_force_enabled = u8!0x1\n", true) == "[usb]\n" + on + "\n", "with # too");
    // a commented one and a real one: only the real one changes
    const std::string both = "[usb]\n; usb30_force_enabled = u8!0x1\nusb30_force_enabled = u8!0x0\n";
    Check(SetUsb30InIni(both, true) == "[usb]\n; usb30_force_enabled = u8!0x1\n" + on + "\n", "the comment stays when a real line is there");

    // the key in another section is not ours
    const std::string elsewhere = "[misc]\nusb30_force_enabled = u8!0x1\n";
    Check(Usb30InIni(elsewhere) == -1, "the key outside [usb] does not count");
    Check(SetUsb30InIni(elsewhere, true) == elsewhere + "[usb]\n" + on + "\n", "and is left alone");

    // CRLF files stay CRLF
    const std::string crlf = "[usb]\r\nother = u8!0x2\r\n[ro]\r\nx = 1\r\n";
    Check(SetUsb30InIni(crlf, true) == "[usb]\r\n" + on + "\r\nother = u8!0x2\r\n[ro]\r\nx = 1\r\n", "CRLF under [usb]");
    Check(SetUsb30InIni("[eupld]\r\nupload_enabled = u8!0x0\r\n", true) == "[eupld]\r\nupload_enabled = u8!0x0\r\n[usb]\r\n" + on + "\r\n",
          "CRLF for a new section");
    Check(SetUsb30InIni("[usb]\r\nusb30_force_enabled = u8!0x0\r\n", true) == "[usb]\r\n" + on + "\r\n", "CRLF replaced in place");
    Check(Usb30InIni("[USB]\r\nUSB30_Force_Enabled = u8!0x1 ; on\r\n") == 1, "names in any case, a comment after the value");

    if (failed) return 1;
    std::printf("usb30_test: ok\n");
    return 0;
}
