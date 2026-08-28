#pragma once

#include <stddef.h>
#include <stdint.h>

class Stream {
public:
	virtual ~Stream() = default;
	virtual void print(char) {}
	virtual void print(const char *) {}
	virtual void println() {}
	virtual size_t write(const uint8_t *, size_t len) { return len; }
	virtual size_t readBytes(uint8_t *, size_t len) { return len; }
};
