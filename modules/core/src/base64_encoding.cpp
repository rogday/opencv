#include "persistence_impl.hpp"
#include "base64_encoding.hpp"

class base64::Base64ContextEmitter
{
public:
    explicit Base64ContextEmitter(cv::FileStorage::Impl * fs)
            : file_storage(fs)
            , binary_buffer(BUFFER_LEN)
            , base64_buffer(base64_encode_buffer_size(BUFFER_LEN))
            , src_beg(0)
            , src_cur(0)
            , src_end(0)
    {
        src_beg = binary_buffer.data();
        src_end = src_beg + BUFFER_LEN;
        src_cur = src_beg;

        CV_Assert(fs->write_mode);

        if ( fs->fmt == cv::FileStorage::Mode::FORMAT_JSON )
        {
            char* ptr = fs->bufferPtr();
            *ptr++ = '\0';
            fs->puts(fs->bufferStart());
            fs->setBufferPtr(fs->bufferStart());
            memset(fs->bufferStart(), 0, static_cast<int>(fs->space));
            fs->puts("\"$base64$");
        }
        else
        {
            fs->flush();
        }
    }

    ~Base64ContextEmitter()
    {
        /* cleaning */
        if (src_cur != src_beg)
            flush();    /* encode the rest binary data to base64 buffer */

        if ( file_storage->fmt == cv::FileStorage::Mode::FORMAT_JSON )
        {
            file_storage->puts("\"");
            file_storage->setBufferPtr(file_storage->bufferStart());
            file_storage->flush();
            memset(file_storage->bufferStart(), 0, static_cast<int>(file_storage->space) );
            file_storage->setBufferPtr(file_storage->bufferStart());
        }
    }

    Base64ContextEmitter & write(const uchar * beg, const uchar * end)
    {
        if (beg >= end)
            return *this;

        while (beg < end) {
            /* collect binary data and copy to binary buffer */
            size_t len = std::min(end - beg, src_end - src_cur);
            std::memcpy(src_cur, beg, len);
            beg     += len;
            src_cur += len;

            if (src_cur >= src_end) {
                /* binary buffer is full. */
                /* encode it to base64 and send result to fs */
                flush();
            }
        }

        return *this;
    }

    /*
     * a convertor must provide :
     * - `operator >> (uchar * & dst)` for writing current binary data to `dst` and moving to next data.
     * - `operator bool` for checking if current loaction is valid and not the end.
     */
    template<typename _to_binary_convertor_t> inline
    Base64ContextEmitter & write(_to_binary_convertor_t & convertor)
    {
        static const size_t BUFFER_MAX_LEN = 1024U;

        std::vector<uchar> buffer(BUFFER_MAX_LEN);
        uchar * beg = buffer.data();
        uchar * end = beg;

        while (convertor) {
            convertor >> end;
            write(beg, end);
            end = beg;
        }

        return *this;
    }

    bool flush()
    {
        /* control line width, so on. */
        size_t len = base64_encode(src_beg, base64_buffer.data(), 0U, src_cur - src_beg);
        if (len == 0U)
            return false;

        src_cur = src_beg;
        {
            if ( file_storage->fmt == cv::FileStorage::Mode::FORMAT_JSON)
            {
                file_storage->puts((const char*)base64_buffer.data());
            }
            else
            {
                const char newline[] = "\n";
                char space[80];
                int ident = file_storage->write_stack.back().indent;
                memset(space, ' ', static_cast<int>(ident));
                space[ident] = '\0';

                file_storage->puts(space);
                file_storage->puts((const char*)base64_buffer.data());
                file_storage->puts(newline);
                file_storage->flush();
            }

        }

        return true;
    }

private:
    /* because of Base64, we must keep its length a multiple of 3 */
    static const size_t BUFFER_LEN = 48U;
    // static_assert(BUFFER_LEN % 3 == 0, "BUFFER_LEN is invalid");

private:
    cv::FileStorage::Impl * file_storage;

    std::vector<uchar> binary_buffer;
    std::vector<uchar> base64_buffer;
    uchar * src_beg;
    uchar * src_cur;
    uchar * src_end;
};

std::string base64::make_base64_header(const char *dt) {
    std::ostringstream oss;
    oss << dt   << ' ';
    std::string buffer(oss.str());
    CV_Assert(buffer.size() < HEADER_SIZE);

    buffer.reserve(HEADER_SIZE);
    while (buffer.size() < HEADER_SIZE)
        buffer += ' ';

    return buffer;
}

size_t base64::base64_encode(const uint8_t *src, uint8_t *dst, size_t off, size_t cnt) {
    if (!src || !dst || !cnt)
        return 0;

    /* initialize beginning and end */
    uint8_t       * dst_beg = dst;
    uint8_t       * dst_cur = dst_beg;

    uint8_t const * src_beg = src + off;
    uint8_t const * src_cur = src_beg;
    uint8_t const * src_end = src_cur + cnt / 3U * 3U;

    /* integer multiples part */
    while (src_cur < src_end) {
        uint8_t _2 = *src_cur++;
        uint8_t _1 = *src_cur++;
        uint8_t _0 = *src_cur++;
        *dst_cur++ = base64_mapping[ _2          >> 2U];
        *dst_cur++ = base64_mapping[(_1 & 0xF0U) >> 4U | (_2 & 0x03U) << 4U];
        *dst_cur++ = base64_mapping[(_0 & 0xC0U) >> 6U | (_1 & 0x0FU) << 2U];
        *dst_cur++ = base64_mapping[ _0 & 0x3FU];
    }

    /* remainder part */
    size_t rst = src_beg + cnt - src_cur;
    if (rst == 1U) {
        uint8_t _2 = *src_cur++;
        *dst_cur++ = base64_mapping[ _2          >> 2U];
        *dst_cur++ = base64_mapping[(_2 & 0x03U) << 4U];
    } else if (rst == 2U) {
        uint8_t _2 = *src_cur++;
        uint8_t _1 = *src_cur++;
        *dst_cur++ = base64_mapping[ _2          >> 2U];
        *dst_cur++ = base64_mapping[(_2 & 0x03U) << 4U | (_1 & 0xF0U) >> 4U];
        *dst_cur++ = base64_mapping[(_1 & 0x0FU) << 2U];
    }

    /* padding */
    switch (rst)
    {
        case 1U: *dst_cur++ = base64_padding;
            /* fallthrough */
        case 2U: *dst_cur++ = base64_padding;
            /* fallthrough */
        default: *dst_cur   = 0;
            break;
    }

    return static_cast<size_t>(dst_cur - dst_beg);
}

int base64::icvCalcStructSize(const char *dt, int initial_size) {
    int size = cv::fs::calcElemSize( dt, initial_size );
    size_t elem_max_size = 0;
    for ( const char * type = dt; *type != '\0'; type++ ) {
        switch ( *type )
        {
            case 'u': { elem_max_size = std::max( elem_max_size, sizeof(uchar ) ); break; }
            case 'c': { elem_max_size = std::max( elem_max_size, sizeof(schar ) ); break; }
            case 'w': { elem_max_size = std::max( elem_max_size, sizeof(ushort) ); break; }
            case 's': { elem_max_size = std::max( elem_max_size, sizeof(short ) ); break; }
            case 'i': { elem_max_size = std::max( elem_max_size, sizeof(int   ) ); break; }
            case 'f': { elem_max_size = std::max( elem_max_size, sizeof(float ) ); break; }
            case 'd': { elem_max_size = std::max( elem_max_size, sizeof(double) ); break; }
            default: break;
        }
    }
    size = cvAlign( size, static_cast<int>(elem_max_size) );
    return size;
}

size_t base64::base64_encode_buffer_size(size_t cnt, bool is_end_with_zero) {
    size_t additional = static_cast<size_t>(is_end_with_zero == true);
    return (cnt + 2U) / 3U * 4U + additional;
}

base64::Base64Writer::Base64Writer(cv::FileStorage::Impl * fs)
        : emitter(new Base64ContextEmitter(fs))
        , data_type_string()
{
    CV_Assert(fs->write_mode);
}

void base64::Base64Writer::write(const void* _data, size_t len, const char* dt)
{
    check_dt(dt);
    RawDataToBinaryConvertor convertor(_data, static_cast<int>(len), data_type_string);
    emitter->write(convertor);
}

template<typename _to_binary_convertor_t> inline
void base64::Base64Writer::write(_to_binary_convertor_t & convertor, const char* dt)
{
    check_dt(dt);
    emitter->write(convertor);
}

base64::Base64Writer::~Base64Writer()
{
    delete emitter;
}

void base64::Base64Writer::check_dt(const char* dt)
{
    if ( dt == 0 )
        CV_Error( CV_StsBadArg, "Invalid \'dt\'." );
    else if (data_type_string.empty()) {
        data_type_string = dt;

        /* output header */
        std::string buffer = make_base64_header(dt);
        const uchar * beg = reinterpret_cast<const uchar *>(buffer.data());
        const uchar * end = beg + buffer.size();

        emitter->write(beg, end);
    } else if ( data_type_string != dt )
        CV_Error( CV_StsBadArg, "\'dt\' does not match." );
}
