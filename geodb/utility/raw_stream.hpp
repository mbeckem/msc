#ifndef GEODB_UTILITY_RAW_STREAM_HPP
#define GEODB_UTILITY_RAW_STREAM_HPP

#include "geodb/common.hpp"
#include "geodb/filesystem.hpp"

#include <tpie/file_accessor/file_accessor.h>

namespace geodb {

/**
 * A raw file stream.
 * Essentially wraps TPIE's raw file accessor.
 */
class raw_stream {
    using stream_size_type = tpie::stream_size_type;

public:
    /// Opens the file in read+write mode, creating it if necessary.
    void open_new(const fs::path& path) {
        m_raw.open_rw_new(path.string());
        m_path = path;
    }

    /// Tries to open the file in read+write mode.
    /// Returns false if the file does not exist.
    bool try_open(const fs::path& path) {
        if (m_raw.try_open_rw(path.string())) {
            m_path = path;
            return true;
        }
        return false;
    }

    /// Tries to open the file in readonly mode.
    void open_readonly(const fs::path& path) {
        m_raw.open_ro(path.string());
        m_path = path;
    }

    /// Tries to open the file in writeonly mode,
    /// creating the file if necessary.
    void open_writeonly(const fs::path& path) {
        m_raw.open_wo(path.string());
        m_path = path;
    }

    /// Closes the file.
    void close() {
        m_raw.close_i();
        m_path.clear();
    }

    /// Truncates the file to the given size.
    void truncate(stream_size_type bytes) {
        m_raw.truncate_i(bytes);
    }

    /// Seeks to the given offset.
    void seek(stream_size_type offset) {
        m_raw.seek_i(offset);
    }

    /// Read a value from the stream at the current offset.
    template<typename T>
    void read(T& value) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "Value must be trivially copyable");
        read(reinterpret_cast<void*>(std::addressof(value)), sizeof(value));
    }

    /// Write a value to the stream at the current offset.
    template<typename T>
    void write(const T& value) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "Value must be trivially copyable");
        write(reinterpret_cast<const void*>(std::addressof(value)), sizeof(value));
    }

    void read(void* data, size_t size) {
        m_raw.read_i(data, size);
    }

    void write(const void* data, size_t size) {
        m_raw.write_i(data, size);
    }

    /// True if the file is open.
    bool is_open() const {
        return m_raw.is_open();
    }

    /// Path the currently open file.
    const fs::path& path() const {
        return m_path;
    }

    /// Returns the size of the file in bytes.
    /// FIXME: function on TPIE's raw accessor is not const.
    stream_size_type size() {
        return m_raw.file_size_i();
    }

private:
    fs::path m_path;
    tpie::file_accessor::raw_file_accessor m_raw;
};

} // namespace geodb

#endif // GEODB_UTILITY_RAW_STREAM_HPP
