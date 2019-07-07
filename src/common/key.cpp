#include "key.h"

Key::Key(int len) {
    this->len = len;
    this->key = new char[len];
    cybRand(this->key, len);
}

Key::Key(string buffer) {
    this->len = buffer.size();
    this->key = new char[buffer.size()];
    memcpy(this->key, buffer.c_str(), buffer.size());
}

Key::~Key() {
    delete[] this->key;
}

string Key::str(void) {
    return string(this->key, this->len);
}