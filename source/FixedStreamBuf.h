// FixedStreamBuf.h - A streambuf wrapper for external fixed-size buffers
// Replaces pubsetbuf which is a no-op on MSVC's stringstream.
// Used by STAR to make istringstream/ostringstream use pre-allocated buffers.

#ifndef FIXED_STREAM_BUF_H
#define FIXED_STREAM_BUF_H

#include <streambuf>
#include <istream>
#include <ostream>

// Input streambuf that wraps an external char buffer (no copy)
class FixedInputBuf : public std::streambuf {
public:
    FixedInputBuf() {}

    // Point to external buffer. Can be called repeatedly to update content.
    void setBuffer(char *buf, std::streamsize size) {
        setg(buf, buf, buf + size);
    }

    // Reset read position to beginning
    void rewind() {
        if (eback())
            setg(eback(), eback(), egptr());
    }

protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                     std::ios_base::openmode /*which*/ = std::ios_base::in) override {
        char *newpos;
        if (dir == std::ios_base::beg)
            newpos = eback() + off;
        else if (dir == std::ios_base::cur)
            newpos = gptr() + off;
        else
            newpos = egptr() + off;

        if (newpos < eback() || newpos > egptr())
            return pos_type(off_type(-1));
        setg(eback(), newpos, egptr());
        return pos_type(newpos - eback());
    }

    pos_type seekpos(pos_type pos,
                     std::ios_base::openmode which = std::ios_base::in) override {
        return seekoff(off_type(pos), std::ios_base::beg, which);
    }
};

// Output streambuf that wraps an external char buffer (no copy)
class FixedOutputBuf : public std::streambuf {
public:
    FixedOutputBuf() {}

    void setBuffer(char *buf, std::streamsize size) {
        setp(buf, buf + size);
    }

    void rewind() {
        if (pbase())
            setp(pbase(), epptr());
    }

    // How many bytes have been written
    std::streamsize written() const {
        return pptr() - pbase();
    }

protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                     std::ios_base::openmode /*which*/ = std::ios_base::out) override {
        char *newpos;
        if (dir == std::ios_base::beg)
            newpos = pbase() + off;
        else if (dir == std::ios_base::cur)
            newpos = pptr() + off;
        else
            newpos = epptr() + off;

        if (newpos < pbase() || newpos > epptr())
            return pos_type(off_type(-1));
        setp(pbase(), epptr());
        pbump((int)(newpos - pbase()));
        return pos_type(newpos - pbase());
    }

    pos_type seekpos(pos_type pos,
                     std::ios_base::openmode which = std::ios_base::out) override {
        return seekoff(off_type(pos), std::ios_base::beg, which);
    }
};

// istream that reads from a fixed external buffer
class FixedIStream : public std::istream {
    FixedInputBuf _buf;
public:
    FixedIStream() : std::istream(&_buf) {}

    void setBuffer(char *buf, std::streamsize size) {
        _buf.setBuffer(buf, size);
        clear();
    }

    void rewind() {
        _buf.rewind();
        clear();
    }
};

// ostream that writes to a fixed external buffer
class FixedOStream : public std::ostream {
    FixedOutputBuf _buf;
public:
    FixedOStream() : std::ostream(&_buf) {}

    void setBuffer(char *buf, std::streamsize size) {
        _buf.setBuffer(buf, size);
        clear();
    }

    void rewind() {
        _buf.rewind();
        clear();
    }

    std::streamsize written() const {
        return _buf.written();
    }
};

#endif // FIXED_STREAM_BUF_H
