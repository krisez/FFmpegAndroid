/*
 * Copyright (c) 2007-2010 Stefano Sabatini
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * simple media prober based on the FFmpeg libraries
 */
#include "ffprobe.h"
#include "config.h"
#include "libavutil/ffversion.h"
#include <string.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/display.h"
#include "libavutil/hash.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/libm.h"
#include "libavutil/parseutils.h"
#include "libavutil/timecode.h"
#include "libavutil/timestamp.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libpostproc/postprocess.h"
#include "cmdutils.h"

#include "libavutil/thread.h"

#if !HAVE_THREADS
#  ifdef pthread_mutex_lock
#    undef pthread_mutex_lock
#  endif
#  define pthread_mutex_lock(a) do{}while(0)
#  ifdef pthread_mutex_unlock
#    undef pthread_mutex_unlock
#  endif
#  define pthread_mutex_unlock(a) do{}while(0)
#endif
#define printf(fmt, ...) yyl_print_callback_json(fmt,## __VA_ARGS__);
typedef struct InputStream {
    AVStream *st;

    AVCodecContext *dec_ctx;
} InputStream;

typedef struct InputFile {
    AVFormatContext *fmt_ctx;

    InputStream *streams;
    int       nb_streams;
} InputFile;

const char program_name_yyl[] = "ffprobe";
const int program_birth_year_yyl = 2007;

static int do_bitexact = 0;
static int do_count_frames = 0;
static int do_count_packets = 0;
static int do_read_frames  = 0;
static int do_read_packets = 0;
static int do_show_chapters = 0;
static int do_show_error   = 0;
static int do_show_format  = 0;
static int do_show_frames  = 0;
static int do_show_packets = 0;
static int do_show_programs = 0;
static int do_show_streams = 0;
static int do_show_stream_disposition = 0;
static int do_show_data    = 0;
static int do_show_program_version  = 0;
static int do_show_library_versions = 0;
static int do_show_pixel_formats = 0;
static int do_show_pixel_format_flags = 0;
static int do_show_pixel_format_components = 0;
static int do_show_log = 0;

static int do_show_chapter_tags = 0;
static int do_show_format_tags = 0;
static int do_show_frame_tags = 0;
static int do_show_program_tags = 0;
static int do_show_stream_tags = 0;
static int do_show_packet_tags = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;
static int show_private_data            = 1;

static char *print_format;
static char *stream_specifier;
static char *show_data_hash;

typedef struct ReadInterval {
    int id;             ///< identifier
    int64_t start, end; ///< start, end in second/AV_TIME_BASE units
    int has_start, has_end;
    int start_is_offset, end_is_offset;
    int duration_frames;
} ReadInterval;

static ReadInterval *read_intervals;
static int read_intervals_nb = 0;

static int find_stream_info  = 1;

/* section structure definition */

#define SECTION_MAX_NB_CHILDREN 10

struct section {
    int id;             ///< unique id identifying a section
    const char *name;

#define SECTION_FLAG_IS_WRAPPER      1 ///< the section only contains other sections, but has no data at its own level
#define SECTION_FLAG_IS_ARRAY        2 ///< the section contains an array of elements of the same type
#define SECTION_FLAG_HAS_VARIABLE_FIELDS 4 ///< the section may contain a variable number of fields with variable keys.
    ///  For these sections the element_name field is mandatory.
    int flags;
    int children_ids[SECTION_MAX_NB_CHILDREN+1]; ///< list of children section IDS, terminated by -1
    const char *element_name; ///< name of the contained element, if provided
    const char *unique_name;  ///< unique section name, in case the name is ambiguous
    AVDictionary *entries_to_show;
    int show_all_entries;
};

typedef enum {
    SECTION_ID_NONE = -1,
    SECTION_ID_CHAPTER,
    SECTION_ID_CHAPTER_TAGS,
    SECTION_ID_CHAPTERS,
    SECTION_ID_ERROR,
    SECTION_ID_FORMAT,
    SECTION_ID_FORMAT_TAGS,
    SECTION_ID_FRAME,
    SECTION_ID_FRAMES,
    SECTION_ID_FRAME_TAGS,
    SECTION_ID_FRAME_SIDE_DATA_LIST,
    SECTION_ID_FRAME_SIDE_DATA,
    SECTION_ID_FRAME_LOG,
    SECTION_ID_FRAME_LOGS,
    SECTION_ID_LIBRARY_VERSION,
    SECTION_ID_LIBRARY_VERSIONS,
    SECTION_ID_PACKET,
    SECTION_ID_PACKET_TAGS,
    SECTION_ID_PACKETS,
    SECTION_ID_PACKETS_AND_FRAMES,
    SECTION_ID_PACKET_SIDE_DATA_LIST,
    SECTION_ID_PACKET_SIDE_DATA,
    SECTION_ID_PIXEL_FORMAT,
    SECTION_ID_PIXEL_FORMAT_FLAGS,
    SECTION_ID_PIXEL_FORMAT_COMPONENT,
    SECTION_ID_PIXEL_FORMAT_COMPONENTS,
    SECTION_ID_PIXEL_FORMATS,
    SECTION_ID_PROGRAM_STREAM_DISPOSITION,
    SECTION_ID_PROGRAM_STREAM_TAGS,
    SECTION_ID_PROGRAM,
    SECTION_ID_PROGRAM_STREAMS,
    SECTION_ID_PROGRAM_STREAM,
    SECTION_ID_PROGRAM_TAGS,
    SECTION_ID_PROGRAM_VERSION,
    SECTION_ID_PROGRAMS,
    SECTION_ID_ROOT,
    SECTION_ID_STREAM,
    SECTION_ID_STREAM_DISPOSITION,
    SECTION_ID_STREAMS,
    SECTION_ID_STREAM_TAGS,
    SECTION_ID_STREAM_SIDE_DATA_LIST,
    SECTION_ID_STREAM_SIDE_DATA,
    SECTION_ID_SUBTITLE,
} SectionID;

static struct section sections[] = {
        [SECTION_ID_CHAPTERS] =           { SECTION_ID_CHAPTERS, "chapters", SECTION_FLAG_IS_ARRAY, { SECTION_ID_CHAPTER, -1 } },
        [SECTION_ID_CHAPTER] =            { SECTION_ID_CHAPTER, "chapter", 0, { SECTION_ID_CHAPTER_TAGS, -1 } },
        [SECTION_ID_CHAPTER_TAGS] =       { SECTION_ID_CHAPTER_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "chapter_tags" },
        [SECTION_ID_ERROR] =              { SECTION_ID_ERROR, "error", 0, { -1 } },
        [SECTION_ID_FORMAT] =             { SECTION_ID_FORMAT, "format", 0, { SECTION_ID_FORMAT_TAGS, -1 } },
        [SECTION_ID_FORMAT_TAGS] =        { SECTION_ID_FORMAT_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "format_tags" },
        [SECTION_ID_FRAMES] =             { SECTION_ID_FRAMES, "frames", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME, SECTION_ID_SUBTITLE, -1 } },
        [SECTION_ID_FRAME] =              { SECTION_ID_FRAME, "frame", 0, { SECTION_ID_FRAME_TAGS, SECTION_ID_FRAME_SIDE_DATA_LIST, SECTION_ID_FRAME_LOGS, -1 } },
        [SECTION_ID_FRAME_TAGS] =         { SECTION_ID_FRAME_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "frame_tags" },
        [SECTION_ID_FRAME_SIDE_DATA_LIST] ={ SECTION_ID_FRAME_SIDE_DATA_LIST, "side_data_list", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "frame_side_data_list" },
        [SECTION_ID_FRAME_SIDE_DATA] =     { SECTION_ID_FRAME_SIDE_DATA, "side_data", 0, { -1 } },
        [SECTION_ID_FRAME_LOGS] =         { SECTION_ID_FRAME_LOGS, "logs", SECTION_FLAG_IS_ARRAY, { SECTION_ID_FRAME_LOG, -1 } },
        [SECTION_ID_FRAME_LOG] =          { SECTION_ID_FRAME_LOG, "log", 0, { -1 },  },
        [SECTION_ID_LIBRARY_VERSIONS] =   { SECTION_ID_LIBRARY_VERSIONS, "library_versions", SECTION_FLAG_IS_ARRAY, { SECTION_ID_LIBRARY_VERSION, -1 } },
        [SECTION_ID_LIBRARY_VERSION] =    { SECTION_ID_LIBRARY_VERSION, "library_version", 0, { -1 } },
        [SECTION_ID_PACKETS] =            { SECTION_ID_PACKETS, "packets", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET, -1} },
        [SECTION_ID_PACKETS_AND_FRAMES] = { SECTION_ID_PACKETS_AND_FRAMES, "packets_and_frames", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET, -1} },
        [SECTION_ID_PACKET] =             { SECTION_ID_PACKET, "packet", 0, { SECTION_ID_PACKET_TAGS, SECTION_ID_PACKET_SIDE_DATA_LIST, -1 } },
        [SECTION_ID_PACKET_TAGS] =        { SECTION_ID_PACKET_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "packet_tags" },
        [SECTION_ID_PACKET_SIDE_DATA_LIST] ={ SECTION_ID_PACKET_SIDE_DATA_LIST, "side_data_list", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PACKET_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "packet_side_data_list" },
        [SECTION_ID_PACKET_SIDE_DATA] =     { SECTION_ID_PACKET_SIDE_DATA, "side_data", 0, { -1 } },
        [SECTION_ID_PIXEL_FORMATS] =      { SECTION_ID_PIXEL_FORMATS, "pixel_formats", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PIXEL_FORMAT, -1 } },
        [SECTION_ID_PIXEL_FORMAT] =       { SECTION_ID_PIXEL_FORMAT, "pixel_format", 0, { SECTION_ID_PIXEL_FORMAT_FLAGS, SECTION_ID_PIXEL_FORMAT_COMPONENTS, -1 } },
        [SECTION_ID_PIXEL_FORMAT_FLAGS] = { SECTION_ID_PIXEL_FORMAT_FLAGS, "flags", 0, { -1 }, .unique_name = "pixel_format_flags" },
        [SECTION_ID_PIXEL_FORMAT_COMPONENTS] = { SECTION_ID_PIXEL_FORMAT_COMPONENTS, "components", SECTION_FLAG_IS_ARRAY, {SECTION_ID_PIXEL_FORMAT_COMPONENT, -1 }, .unique_name = "pixel_format_components" },
        [SECTION_ID_PIXEL_FORMAT_COMPONENT]  = { SECTION_ID_PIXEL_FORMAT_COMPONENT, "component", 0, { -1 } },
        [SECTION_ID_PROGRAM_STREAM_DISPOSITION] = { SECTION_ID_PROGRAM_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "program_stream_disposition" },
        [SECTION_ID_PROGRAM_STREAM_TAGS] =        { SECTION_ID_PROGRAM_STREAM_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "program_stream_tags" },
        [SECTION_ID_PROGRAM] =                    { SECTION_ID_PROGRAM, "program", 0, { SECTION_ID_PROGRAM_TAGS, SECTION_ID_PROGRAM_STREAMS, -1 } },
        [SECTION_ID_PROGRAM_STREAMS] =            { SECTION_ID_PROGRAM_STREAMS, "streams", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROGRAM_STREAM, -1 }, .unique_name = "program_streams" },
        [SECTION_ID_PROGRAM_STREAM] =             { SECTION_ID_PROGRAM_STREAM, "stream", 0, { SECTION_ID_PROGRAM_STREAM_DISPOSITION, SECTION_ID_PROGRAM_STREAM_TAGS, -1 }, .unique_name = "program_stream" },
        [SECTION_ID_PROGRAM_TAGS] =               { SECTION_ID_PROGRAM_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "program_tags" },
        [SECTION_ID_PROGRAM_VERSION] =    { SECTION_ID_PROGRAM_VERSION, "program_version", 0, { -1 } },
        [SECTION_ID_PROGRAMS] =                   { SECTION_ID_PROGRAMS, "programs", SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROGRAM, -1 } },
        [SECTION_ID_ROOT] =               { SECTION_ID_ROOT, "root", SECTION_FLAG_IS_WRAPPER,
                                            { SECTION_ID_CHAPTERS, SECTION_ID_FORMAT, SECTION_ID_FRAMES, SECTION_ID_PROGRAMS, SECTION_ID_STREAMS,
                                              SECTION_ID_PACKETS, SECTION_ID_ERROR, SECTION_ID_PROGRAM_VERSION, SECTION_ID_LIBRARY_VERSIONS,
                                              SECTION_ID_PIXEL_FORMATS, -1} },
        [SECTION_ID_STREAMS] =            { SECTION_ID_STREAMS, "streams", SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM, -1 } },
        [SECTION_ID_STREAM] =             { SECTION_ID_STREAM, "stream", 0, { SECTION_ID_STREAM_DISPOSITION, SECTION_ID_STREAM_TAGS, SECTION_ID_STREAM_SIDE_DATA_LIST, -1 } },
        [SECTION_ID_STREAM_DISPOSITION] = { SECTION_ID_STREAM_DISPOSITION, "disposition", 0, { -1 }, .unique_name = "stream_disposition" },
        [SECTION_ID_STREAM_TAGS] =        { SECTION_ID_STREAM_TAGS, "tags", SECTION_FLAG_HAS_VARIABLE_FIELDS, { -1 }, .element_name = "tag", .unique_name = "stream_tags" },
        [SECTION_ID_STREAM_SIDE_DATA_LIST] ={ SECTION_ID_STREAM_SIDE_DATA_LIST, "side_data_list", SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAM_SIDE_DATA, -1 }, .element_name = "side_data", .unique_name = "stream_side_data_list" },
        [SECTION_ID_STREAM_SIDE_DATA] =     { SECTION_ID_STREAM_SIDE_DATA, "side_data", 0, { -1 } },
        [SECTION_ID_SUBTITLE] =           { SECTION_ID_SUBTITLE, "subtitle", 0, { -1 } },
};

static const OptionDef *options;

/* FFprobe context */
static const char *input_filename;
static AVInputFormat *iformat = NULL;

static struct AVHashContext *hash;

static const struct {
    double bin_val;
    double dec_val;
    const char *bin_str;
    const char *dec_str;
} si_prefixes[] = {
        { 1.0, 1.0, "", "" },
        { 1.024e3, 1e3, "Ki", "K" },
        { 1.048576e6, 1e6, "Mi", "M" },
        { 1.073741824e9, 1e9, "Gi", "G" },
        { 1.099511627776e12, 1e12, "Ti", "T" },
        { 1.125899906842624e15, 1e15, "Pi", "P" },
};

static const char unit_second_str[]         = "s"    ;
static const char unit_hertz_str[]          = "Hz"   ;
static const char unit_byte_str[]           = "byte" ;
static const char unit_bit_per_second_str[] = "bit/s";

static int nb_streams;
static uint64_t *nb_streams_packets;
static uint64_t *nb_streams_frames;
static int *selected_streams;

#if HAVE_THREADS
pthread_mutex_t log_mutex;
#endif
typedef struct LogBuffer {
    char *context_name;
    int log_level;
    char *log_message;
    AVClassCategory category;
    char *parent_name;
    AVClassCategory parent_category;
}LogBuffer;

static LogBuffer *log_buffer;
static int log_buffer_size;

static void log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    AVClass* avc = ptr ? *(AVClass **) ptr : NULL;
    va_list vl2;
    char line[1024];
    static int print_prefix = 1;
    void *new_log_buffer;

    va_copy(vl2, vl);
    av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);

#if HAVE_THREADS
    pthread_mutex_lock(&log_mutex);

    new_log_buffer = av_realloc_array(log_buffer, log_buffer_size + 1, sizeof(*log_buffer));
    if (new_log_buffer) {
        char *msg;
        int i;

        log_buffer = new_log_buffer;
        memset(&log_buffer[log_buffer_size], 0, sizeof(log_buffer[log_buffer_size]));
        log_buffer[log_buffer_size].context_name= avc ? av_strdup(avc->item_name(ptr)) : NULL;
        if (avc) {
            if (avc->get_category) log_buffer[log_buffer_size].category = avc->get_category(ptr);
            else                   log_buffer[log_buffer_size].category = avc->category;
        }
        log_buffer[log_buffer_size].log_level   = level;
        msg = log_buffer[log_buffer_size].log_message = av_strdup(line);
        for (i=strlen(msg) - 1; i>=0 && msg[i] == '\n'; i--) {
            msg[i] = 0;
        }
        if (avc && avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass ***) (((uint8_t *) ptr) +
                                               avc->parent_log_context_offset);
            if (parent && *parent) {
                log_buffer[log_buffer_size].parent_name = av_strdup((*parent)->item_name(parent));
                log_buffer[log_buffer_size].parent_category =
                        (*parent)->get_category ? (*parent)->get_category(parent) :(*parent)->category;
            }
        }
        log_buffer_size ++;
    }

    pthread_mutex_unlock(&log_mutex);
#endif
}

static void ffprobe_cleanup(int ret)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(sections); i++)
        av_dict_free(&(sections[i].entries_to_show));

#if HAVE_THREADS
    pthread_mutex_destroy(&log_mutex);
#endif
}

struct unit_value {
    union { double d; long long int i; } val;
    const char *unit;
};

static char *value_string(char *buf, int buf_size, struct unit_value uv)
{
    double vald;
    long long int vali;
    int show_float = 0;

    if (uv.unit == unit_second_str) {
        vald = uv.val.d;
        show_float = 1;
    } else {
        vald = vali = uv.val.i;
    }

    if (uv.unit == unit_second_str && use_value_sexagesimal_format) {
        double secs;
        int hours, mins;
        secs  = vald;
        mins  = (int)secs / 60;
        secs  = secs - mins * 60;
        hours = mins / 60;
        mins %= 60;
        snprintf(buf, buf_size, "%d:%02d:%09.6f", hours, mins, secs);
    } else {
        const char *prefix_string = "";

        if (use_value_prefix && vald > 1) {
            long long int index;

            if (uv.unit == unit_byte_str && use_byte_value_binary_prefix) {
                index = (long long int) (log10(vald)) / 10;
                index = av_clip(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
                vald /= si_prefixes[index].bin_val;
                prefix_string = si_prefixes[index].bin_str;
            } else {
                index = (long long int) (log10(vald)) / 3;
                index = av_clip(index, 0, FF_ARRAY_ELEMS(si_prefixes) - 1);
                vald /= si_prefixes[index].dec_val;
                prefix_string = si_prefixes[index].dec_str;
            }
            vali = vald;
        }

        if (show_float || (use_value_prefix && vald != (long long int)vald))
            snprintf(buf, buf_size, "%f", vald);
        else
            snprintf(buf, buf_size, "%lld", vali);
        av_strlcatf(buf, buf_size, "%s%s%s", *prefix_string || show_value_unit ? " " : "",
                    prefix_string, show_value_unit ? uv.unit : "");
    }

    return buf;
}

/* WRITERS API */

typedef struct WriterContext WriterContext;

#define WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS 1
#define WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER 2

typedef enum {
    WRITER_STRING_VALIDATION_FAIL,
    WRITER_STRING_VALIDATION_REPLACE,
    WRITER_STRING_VALIDATION_IGNORE,
    WRITER_STRING_VALIDATION_NB
} StringValidation;

typedef struct Writer {
    const AVClass *priv_class;      ///< private class of the writer, if any
    int priv_size;                  ///< private size for the writer context
    const char *name;

    int  (*init)  (WriterContext *wctx);
    void (*uninit)(WriterContext *wctx);

    void (*print_section_header)(WriterContext *wctx);
    void (*print_section_footer)(WriterContext *wctx);
    void (*print_integer)       (WriterContext *wctx, const char *, long long int);
    void (*print_rational)      (WriterContext *wctx, AVRational *q, char *sep);
    void (*print_string)        (WriterContext *wctx, const char *, const char *);
    int flags;                  ///< a combination or WRITER_FLAG_*
} Writer;

#define SECTION_MAX_NB_LEVELS 10

struct WriterContext {
    const AVClass *class;           ///< class of the writer
    const Writer *writer;           ///< the Writer of which this is an instance
    char *name;                     ///< name of this writer instance
    void *priv;                     ///< private data for use by the filter

    const struct section *sections; ///< array containing all sections
    int nb_sections;                ///< number of sections

    int level;                      ///< current level, starting from 0

    /** number of the item printed in the given section, starting from 0 */
    unsigned int nb_item[SECTION_MAX_NB_LEVELS];

    /** section per each level */
    const struct section *section[SECTION_MAX_NB_LEVELS];
    AVBPrint section_pbuf[SECTION_MAX_NB_LEVELS]; ///< generic print buffer dedicated to each section,
    ///  used by various writers

    unsigned int nb_section_packet; ///< number of the packet section in case we are in "packets_and_frames" section
    unsigned int nb_section_frame;  ///< number of the frame  section in case we are in "packets_and_frames" section
    unsigned int nb_section_packet_frame; ///< nb_section_packet or nb_section_frame according if is_packets_and_frames

    int string_validation;
    char *string_validation_replacement;
    unsigned int string_validation_utf8_flags;
};

static const char *writer_get_name(void *p)
{
    WriterContext *wctx = p;
    return wctx->writer->name;
}

#define OFFSET(x) offsetof(WriterContext, x)

static const AVOption writer_options[] = {
        { "string_validation", "set string validation mode",
                                                                                       OFFSET(string_validation), AV_OPT_TYPE_INT, {.i64=WRITER_STRING_VALIDATION_REPLACE}, 0, WRITER_STRING_VALIDATION_NB-1, .unit = "sv" },
        { "sv", "set string validation mode",
                                                                                       OFFSET(string_validation), AV_OPT_TYPE_INT, {.i64=WRITER_STRING_VALIDATION_REPLACE}, 0, WRITER_STRING_VALIDATION_NB-1, .unit = "sv" },
        { "ignore",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = WRITER_STRING_VALIDATION_IGNORE},  .unit = "sv" },
        { "replace", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = WRITER_STRING_VALIDATION_REPLACE}, .unit = "sv" },
        { "fail",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = WRITER_STRING_VALIDATION_FAIL},    .unit = "sv" },
        { "string_validation_replacement", "set string validation replacement string", OFFSET(string_validation_replacement), AV_OPT_TYPE_STRING, {.str=""}},
        { "svr", "set string validation replacement string", OFFSET(string_validation_replacement), AV_OPT_TYPE_STRING, {.str="\xEF\xBF\xBD"}},
        { NULL }
};

static void *writer_child_next(void *obj, void *prev)
{
    WriterContext *ctx = obj;
    if (!prev && ctx->writer && ctx->writer->priv_class && ctx->priv)
        return ctx->priv;
    return NULL;
}

static const AVClass writer_class = {
        .class_name = "Writer",
        .item_name  = writer_get_name,
        .option     = writer_options,
        .version    = LIBAVUTIL_VERSION_INT,
        .child_next = writer_child_next,
};

static void writer_close(WriterContext **wctx)
{
    int i;

    if (!*wctx)
        return;

    if ((*wctx)->writer->uninit)
        (*wctx)->writer->uninit(*wctx);
    for (i = 0; i < SECTION_MAX_NB_LEVELS; i++)
        av_bprint_finalize(&(*wctx)->section_pbuf[i], NULL);
    if ((*wctx)->writer->priv_class)
        av_opt_free((*wctx)->priv);
    av_freep(&((*wctx)->priv));
    av_opt_free(*wctx);
    av_freep(wctx);
}

static void bprint_bytes(AVBPrint *bp, const uint8_t *ubuf, size_t ubuf_size)
{
    int i;
    av_bprintf(bp, "0X");
    for (i = 0; i < ubuf_size; i++)
        av_bprintf(bp, "%02X", ubuf[i]);
}


static int writer_open(WriterContext **wctx, const Writer *writer, const char *args,
                       const struct section *sections, int nb_sections)
{
    int i, ret = 0;

    if (!(*wctx = av_mallocz(sizeof(WriterContext)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!((*wctx)->priv = av_mallocz(writer->priv_size))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    (*wctx)->class = &writer_class;
    (*wctx)->writer = writer;
    (*wctx)->level = -1;
    (*wctx)->sections = sections;
    (*wctx)->nb_sections = nb_sections;

    av_opt_set_defaults(*wctx);

    if (writer->priv_class) {
        void *priv_ctx = (*wctx)->priv;
        *((const AVClass **)priv_ctx) = writer->priv_class;
        av_opt_set_defaults(priv_ctx);
    }

    /* convert options to dictionary */
    if (args) {
        AVDictionary *opts = NULL;
        AVDictionaryEntry *opt = NULL;

        if ((ret = av_dict_parse_string(&opts, args, "=", ":", 0)) < 0) {
            av_log(*wctx, AV_LOG_ERROR, "Failed to parse option string '%s' provided to writer context\n", args);
            av_dict_free(&opts);
            goto fail;
        }

        while ((opt = av_dict_get(opts, "", opt, AV_DICT_IGNORE_SUFFIX))) {
            if ((ret = av_opt_set(*wctx, opt->key, opt->value, AV_OPT_SEARCH_CHILDREN)) < 0) {
                av_log(*wctx, AV_LOG_ERROR, "Failed to set option '%s' with value '%s' provided to writer context\n",
                       opt->key, opt->value);
                av_dict_free(&opts);
                goto fail;
            }
        }

        av_dict_free(&opts);
    }

    /* validate replace string */
    {
        const uint8_t *p = (*wctx)->string_validation_replacement;
        const uint8_t *endp = p + strlen(p);
        while (*p) {
            const uint8_t *p0 = p;
            int32_t code;
            ret = av_utf8_decode(&code, &p, endp, (*wctx)->string_validation_utf8_flags);
            if (ret < 0) {
                AVBPrint bp;
                av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
                bprint_bytes(&bp, p0, p-p0),
                        av_log(wctx, AV_LOG_ERROR,
                               "Invalid UTF8 sequence %s found in string validation replace '%s'\n",
                               bp.str, (*wctx)->string_validation_replacement);
                return ret;
            }
        }
    }

    for (i = 0; i < SECTION_MAX_NB_LEVELS; i++)
        av_bprint_init(&(*wctx)->section_pbuf[i], 1, AV_BPRINT_SIZE_UNLIMITED);

    if ((*wctx)->writer->init)
        ret = (*wctx)->writer->init(*wctx);
    if (ret < 0)
        goto fail;

    return 0;

    fail:
    writer_close(wctx);
    return ret;
}

static inline void writer_print_section_header(WriterContext *wctx,
                                               int section_id)
{
    int parent_section_id;
    wctx->level++;
    av_assert0(wctx->level < SECTION_MAX_NB_LEVELS);
    parent_section_id = wctx->level ?
                        (wctx->section[wctx->level-1])->id : SECTION_ID_NONE;

    wctx->nb_item[wctx->level] = 0;
    wctx->section[wctx->level] = &wctx->sections[section_id];

    if (section_id == SECTION_ID_PACKETS_AND_FRAMES) {
        wctx->nb_section_packet = wctx->nb_section_frame =
        wctx->nb_section_packet_frame = 0;
    } else if (parent_section_id == SECTION_ID_PACKETS_AND_FRAMES) {
        wctx->nb_section_packet_frame = section_id == SECTION_ID_PACKET ?
                                        wctx->nb_section_packet : wctx->nb_section_frame;
    }

    if (wctx->writer->print_section_header)
        wctx->writer->print_section_header(wctx);
}

static inline void writer_print_section_footer(WriterContext *wctx)
{
    int section_id = wctx->section[wctx->level]->id;
    int parent_section_id = wctx->level ?
                            wctx->section[wctx->level-1]->id : SECTION_ID_NONE;

    if (parent_section_id != SECTION_ID_NONE)
        wctx->nb_item[wctx->level-1]++;
    if (parent_section_id == SECTION_ID_PACKETS_AND_FRAMES) {
        if (section_id == SECTION_ID_PACKET) wctx->nb_section_packet++;
        else                                     wctx->nb_section_frame++;
    }
    if (wctx->writer->print_section_footer)
        wctx->writer->print_section_footer(wctx);
    wctx->level--;
}

static inline void writer_print_integer(WriterContext *wctx,
                                        const char *key, long long int val)
{
    const struct section *section = wctx->section[wctx->level];

    if (section->show_all_entries || av_dict_get(section->entries_to_show, key, NULL, 0)) {
        wctx->writer->print_integer(wctx, key, val);
        wctx->nb_item[wctx->level]++;
    }
}

static inline int validate_string(WriterContext *wctx, char **dstp, const char *src)
{
    const uint8_t *p, *endp;
    AVBPrint dstbuf;
    int invalid_chars_nb = 0, ret = 0;

    av_bprint_init(&dstbuf, 0, AV_BPRINT_SIZE_UNLIMITED);

    endp = src + strlen(src);
    for (p = (uint8_t *)src; *p;) {
        uint32_t code;
        int invalid = 0;
        const uint8_t *p0 = p;

        if (av_utf8_decode(&code, &p, endp, wctx->string_validation_utf8_flags) < 0) {
            AVBPrint bp;
            av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
            bprint_bytes(&bp, p0, p-p0);
            av_log(wctx, AV_LOG_DEBUG,
                   "Invalid UTF-8 sequence %s found in string '%s'\n", bp.str, src);
            invalid = 1;
        }

        if (invalid) {
            invalid_chars_nb++;

            switch (wctx->string_validation) {
                case WRITER_STRING_VALIDATION_FAIL:
                    av_log(wctx, AV_LOG_ERROR,
                           "Invalid UTF-8 sequence found in string '%s'\n", src);
                    ret = AVERROR_INVALIDDATA;
                    goto end;
                    break;

                case WRITER_STRING_VALIDATION_REPLACE:
                    av_bprintf(&dstbuf, "%s", wctx->string_validation_replacement);
                    break;
            }
        }

        if (!invalid || wctx->string_validation == WRITER_STRING_VALIDATION_IGNORE)
            av_bprint_append_data(&dstbuf, p0, p-p0);
    }

    if (invalid_chars_nb && wctx->string_validation == WRITER_STRING_VALIDATION_REPLACE) {
        av_log(wctx, AV_LOG_WARNING,
               "%d invalid UTF-8 sequence(s) found in string '%s', replaced with '%s'\n",
               invalid_chars_nb, src, wctx->string_validation_replacement);
    }

    end:
    av_bprint_finalize(&dstbuf, dstp);
    return ret;
}

#define PRINT_STRING_OPT      1
#define PRINT_STRING_VALIDATE 2

static inline int writer_print_string(WriterContext *wctx,
                                      const char *key, const char *val, int flags)
{
    const struct section *section = wctx->section[wctx->level];
    int ret = 0;

    if ((flags & PRINT_STRING_OPT)
        && !(wctx->writer->flags & WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS))
        return 0;

    if (section->show_all_entries || av_dict_get(section->entries_to_show, key, NULL, 0)) {
        if (flags & PRINT_STRING_VALIDATE) {
            char *key1 = NULL, *val1 = NULL;
            ret = validate_string(wctx, &key1, key);
            if (ret < 0) goto end;
            ret = validate_string(wctx, &val1, val);
            if (ret < 0) goto end;
            wctx->writer->print_string(wctx, key1, val1);
            end:
            if (ret < 0) {
                av_log(wctx, AV_LOG_ERROR,
                       "Invalid key=value string combination %s=%s in section %s\n",
                       key, val, section->unique_name);
            }
            av_free(key1);
            av_free(val1);
        } else {
            wctx->writer->print_string(wctx, key, val);
        }

        wctx->nb_item[wctx->level]++;
    }

    return ret;
}

static inline void writer_print_rational(WriterContext *wctx,
                                         const char *key, AVRational q, char sep)
{
    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "%d%c%d", q.num, sep, q.den);
    writer_print_string(wctx, key, buf.str, 0);
}

static void writer_print_time(WriterContext *wctx, const char *key,
                              int64_t ts, const AVRational *time_base, int is_duration)
{
    char buf[128];

    if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0)) {
        writer_print_string(wctx, key, "N/A", PRINT_STRING_OPT);
    } else {
        double d = ts * av_q2d(*time_base);
        struct unit_value uv;
        uv.val.d = d;
        uv.unit = unit_second_str;
        value_string(buf, sizeof(buf), uv);
        writer_print_string(wctx, key, buf, 0);
    }
}

static void writer_print_ts(WriterContext *wctx, const char *key, int64_t ts, int is_duration)
{
    if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0)) {
        writer_print_string(wctx, key, "N/A", PRINT_STRING_OPT);
    } else {
        writer_print_integer(wctx, key, ts);
    }
}

static void writer_print_data(WriterContext *wctx, const char *name,
                              uint8_t *data, int size)
{
    AVBPrint bp;
    int offset = 0, l, i;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&bp, "\n");
    while (size) {
        av_bprintf(&bp, "%08x: ", offset);
        l = FFMIN(size, 16);
        for (i = 0; i < l; i++) {
            av_bprintf(&bp, "%02x", data[i]);
            if (i & 1)
                av_bprintf(&bp, " ");
        }
        av_bprint_chars(&bp, ' ', 41 - 2 * i - i / 2);
        for (i = 0; i < l; i++)
            av_bprint_chars(&bp, data[i] - 32U < 95 ? data[i] : '.', 1);
        av_bprintf(&bp, "\n");
        offset += l;
        data   += l;
        size   -= l;
    }
    writer_print_string(wctx, name, bp.str, 0);
    av_bprint_finalize(&bp, NULL);
}

static void writer_print_data_hash(WriterContext *wctx, const char *name,
                                   uint8_t *data, int size)
{
    char *p, buf[AV_HASH_MAX_SIZE * 2 + 64] = { 0 };

    if (!hash)
        return;
    av_hash_init(hash);
    av_hash_update(hash, data, size);
    snprintf(buf, sizeof(buf), "%s:", av_hash_get_name(hash));
    p = buf + strlen(buf);
    av_hash_final_hex(hash, p, buf + sizeof(buf) - p);
    writer_print_string(wctx, name, buf, 0);
}

static void writer_print_integers(WriterContext *wctx, const char *name,
                                  uint8_t *data, int size, const char *format,
                                  int columns, int bytes, int offset_add)
{
    AVBPrint bp;
    int offset = 0, l, i;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&bp, "\n");
    while (size) {
        av_bprintf(&bp, "%08x: ", offset);
        l = FFMIN(size, columns);
        for (i = 0; i < l; i++) {
            if      (bytes == 1) av_bprintf(&bp, format, *data);
            else if (bytes == 2) av_bprintf(&bp, format, AV_RN16(data));
            else if (bytes == 4) av_bprintf(&bp, format, AV_RN32(data));
            data += bytes;
            size --;
        }
        av_bprintf(&bp, "\n");
        offset += offset_add;
    }
    writer_print_string(wctx, name, bp.str, 0);
    av_bprint_finalize(&bp, NULL);
}

#define MAX_REGISTERED_WRITERS_NB 64

static const Writer *registered_writers[MAX_REGISTERED_WRITERS_NB + 1];

static int writer_register(const Writer *writer)
{
    static int next_registered_writer_idx = 0;

    if (next_registered_writer_idx == MAX_REGISTERED_WRITERS_NB)
        return AVERROR(ENOMEM);

    registered_writers[next_registered_writer_idx++] = writer;
    return 0;
}

static const Writer *writer_get_by_name(const char *name)
{
    int i;

    for (i = 0; registered_writers[i]; i++)
        if (!strcmp(registered_writers[i]->name, name))
            return registered_writers[i];

    return NULL;
}


/* WRITERS */

#define DEFINE_WRITER_CLASS(name)                   \
static const char *name##_get_name(void *ctx)       \
{                                                   \
    return #name ;                                  \
}                                                   \
static const AVClass name##_class = {               \
    .class_name = #name,                            \
    .item_name  = name##_get_name,                  \
    .option     = name##_options                    \
}

/* Default output */

typedef struct DefaultContext {
    const AVClass *class;
    int nokey;
    int noprint_wrappers;
    int nested_section[SECTION_MAX_NB_LEVELS];
} DefaultContext;

#undef OFFSET
#define OFFSET(x) offsetof(DefaultContext, x)

static const AVOption default_options[] = {
        { "noprint_wrappers", "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
        { "nw",               "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
        { "nokey",          "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
        { "nk",             "force no key printing",     OFFSET(nokey),          AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
        {NULL},
};

DEFINE_WRITER_CLASS(default);

/* lame uppercasing routine, assumes the string is lower case ASCII */
static inline char *upcase_string(char *dst, size_t dst_size, const char *src)
{
    int i;
    for (i = 0; src[i] && i < dst_size-1; i++)
        dst[i] = av_toupper(src[i]);
    dst[i] = 0;
    return dst;
}

static void default_print_section_header(WriterContext *wctx)
{
    DefaultContext *def = wctx->priv;
    char buf[32];
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
                                           wctx->section[wctx->level-1] : NULL;

    av_bprint_clear(&wctx->section_pbuf[wctx->level]);
    if (parent_section &&
        !(parent_section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY))) {
        def->nested_section[wctx->level] = 1;
        av_bprintf(&wctx->section_pbuf[wctx->level], "%s%s:",
                   wctx->section_pbuf[wctx->level-1].str,
                   upcase_string(buf, sizeof(buf),
                                 av_x_if_null(section->element_name, section->name)));
    }

    if (def->noprint_wrappers || def->nested_section[wctx->level])
        return;

    if (!(section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
        printf("[%s]\n", upcase_string(buf, sizeof(buf), section->name));
}

static void default_print_section_footer(WriterContext *wctx)
{
    DefaultContext *def = wctx->priv;
    const struct section *section = wctx->section[wctx->level];
    char buf[32];

    if (def->noprint_wrappers || def->nested_section[wctx->level])
        return;

    if (!(section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
        printf("[/%s]\n", upcase_string(buf, sizeof(buf), section->name));
}

static void default_print_str(WriterContext *wctx, const char *key, const char *value)
{
    DefaultContext *def = wctx->priv;

    if (!def->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    printf("%s\n", value);
}

static void default_print_int(WriterContext *wctx, const char *key, long long int value)
{
    DefaultContext *def = wctx->priv;

    if (!def->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    printf("%lld\n", value);
}

static const Writer default_writer = {
        .name                  = "default",
        .priv_size             = sizeof(DefaultContext),
        .print_section_header  = default_print_section_header,
        .print_section_footer  = default_print_section_footer,
        .print_integer         = default_print_int,
        .print_string          = default_print_str,
        .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
        .priv_class            = &default_class,
};

/* Compact output */

/**
 * Apply C-language-like string escaping.
 */
static const char *c_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
            case '\b': av_bprintf(dst, "%s", "\\b");  break;
            case '\f': av_bprintf(dst, "%s", "\\f");  break;
            case '\n': av_bprintf(dst, "%s", "\\n");  break;
            case '\r': av_bprintf(dst, "%s", "\\r");  break;
            case '\\': av_bprintf(dst, "%s", "\\\\"); break;
            default:
                if (*p == sep)
                    av_bprint_chars(dst, '\\', 1);
                av_bprint_chars(dst, *p, 1);
        }
    }
    return dst->str;
}

/**
 * Quote fields containing special characters, check RFC4180.
 */
static const char *csv_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    char meta_chars[] = { sep, '"', '\n', '\r', '\0' };
    int needs_quoting = !!src[strcspn(src, meta_chars)];

    if (needs_quoting)
        av_bprint_chars(dst, '"', 1);

    for (; *src; src++) {
        if (*src == '"')
            av_bprint_chars(dst, '"', 1);
        av_bprint_chars(dst, *src, 1);
    }
    if (needs_quoting)
        av_bprint_chars(dst, '"', 1);
    return dst->str;
}

static const char *none_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    return src;
}

typedef struct CompactContext {
    const AVClass *class;
    char *item_sep_str;
    char item_sep;
    int nokey;
    int print_section;
    char *escape_mode_str;
    const char * (*escape_str)(AVBPrint *dst, const char *src, const char sep, void *log_ctx);
    int nested_section[SECTION_MAX_NB_LEVELS];
    int has_nested_elems[SECTION_MAX_NB_LEVELS];
    int terminate_line[SECTION_MAX_NB_LEVELS];
} CompactContext;

#undef OFFSET
#define OFFSET(x) offsetof(CompactContext, x)

static const AVOption compact_options[]= {
        {"item_sep", "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  CHAR_MIN, CHAR_MAX },
        {"s",        "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str="|"},  CHAR_MIN, CHAR_MAX },
        {"nokey",    "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=0},    0,        1        },
        {"nk",       "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=0},    0,        1        },
        {"escape",   "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  CHAR_MIN, CHAR_MAX },
        {"e",        "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="c"},  CHAR_MIN, CHAR_MAX },
        {"print_section", "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
        {"p",             "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
        {NULL},
};

DEFINE_WRITER_CLASS(compact);

static av_cold int compact_init(WriterContext *wctx)
{
    CompactContext *compact = wctx->priv;

    if (strlen(compact->item_sep_str) != 1) {
        av_log(wctx, AV_LOG_ERROR, "Item separator '%s' specified, but must contain a single character\n",
               compact->item_sep_str);
        return AVERROR(EINVAL);
    }
    compact->item_sep = compact->item_sep_str[0];

    if      (!strcmp(compact->escape_mode_str, "none")) compact->escape_str = none_escape_str;
    else if (!strcmp(compact->escape_mode_str, "c"   )) compact->escape_str = c_escape_str;
    else if (!strcmp(compact->escape_mode_str, "csv" )) compact->escape_str = csv_escape_str;
    else {
        av_log(wctx, AV_LOG_ERROR, "Unknown escape mode '%s'\n", compact->escape_mode_str);
        return AVERROR(EINVAL);
    }

    return 0;
}

static void compact_print_section_header(WriterContext *wctx)
{
    CompactContext *compact = wctx->priv;
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
                                           wctx->section[wctx->level-1] : NULL;
    compact->terminate_line[wctx->level] = 1;
    compact->has_nested_elems[wctx->level] = 0;

    av_bprint_clear(&wctx->section_pbuf[wctx->level]);
    if (!(section->flags & SECTION_FLAG_IS_ARRAY) && parent_section &&
        !(parent_section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY))) {
        compact->nested_section[wctx->level] = 1;
        compact->has_nested_elems[wctx->level-1] = 1;
        av_bprintf(&wctx->section_pbuf[wctx->level], "%s%s:",
                   wctx->section_pbuf[wctx->level-1].str,
                   (char *)av_x_if_null(section->element_name, section->name));
        wctx->nb_item[wctx->level] = wctx->nb_item[wctx->level-1];
    } else {
        if (parent_section && compact->has_nested_elems[wctx->level-1] &&
            (section->flags & SECTION_FLAG_IS_ARRAY)) {
            compact->terminate_line[wctx->level-1] = 0;
            printf("\n");
        }
        if (compact->print_section &&
            !(section->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
            printf("%s%c", section->name, compact->item_sep);
    }
}

static void compact_print_section_footer(WriterContext *wctx)
{
    CompactContext *compact = wctx->priv;

    if (!compact->nested_section[wctx->level] &&
        compact->terminate_line[wctx->level] &&
        !(wctx->section[wctx->level]->flags & (SECTION_FLAG_IS_WRAPPER|SECTION_FLAG_IS_ARRAY)))
        printf("\n");
}

static void compact_print_str(WriterContext *wctx, const char *key, const char *value)
{
    CompactContext *compact = wctx->priv;
    AVBPrint buf;

    if (wctx->nb_item[wctx->level]) printf("%c", compact->item_sep);
    if (!compact->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s", compact->escape_str(&buf, value, compact->item_sep, wctx));
    av_bprint_finalize(&buf, NULL);
}

static void compact_print_int(WriterContext *wctx, const char *key, long long int value)
{
    CompactContext *compact = wctx->priv;

    if (wctx->nb_item[wctx->level]) printf("%c", compact->item_sep);
    if (!compact->nokey)
        printf("%s%s=", wctx->section_pbuf[wctx->level].str, key);
    printf("%lld", value);
}

static const Writer compact_writer = {
        .name                 = "compact",
        .priv_size            = sizeof(CompactContext),
        .init                 = compact_init,
        .print_section_header = compact_print_section_header,
        .print_section_footer = compact_print_section_footer,
        .print_integer        = compact_print_int,
        .print_string         = compact_print_str,
        .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
        .priv_class           = &compact_class,
};

/* CSV output */

#undef OFFSET
#define OFFSET(x) offsetof(CompactContext, x)

static const AVOption csv_options[] = {
        {"item_sep", "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str=","},  CHAR_MIN, CHAR_MAX },
        {"s",        "set item separator",    OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, {.str=","},  CHAR_MIN, CHAR_MAX },
        {"nokey",    "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
        {"nk",       "force no key printing", OFFSET(nokey),           AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
        {"escape",   "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="csv"}, CHAR_MIN, CHAR_MAX },
        {"e",        "set escape mode",       OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, {.str="csv"}, CHAR_MIN, CHAR_MAX },
        {"print_section", "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
        {"p",             "print section name", OFFSET(print_section), AV_OPT_TYPE_BOOL,   {.i64=1},    0,        1        },
        {NULL},
};

DEFINE_WRITER_CLASS(csv);

static const Writer csv_writer = {
        .name                 = "csv",
        .priv_size            = sizeof(CompactContext),
        .init                 = compact_init,
        .print_section_header = compact_print_section_header,
        .print_section_footer = compact_print_section_footer,
        .print_integer        = compact_print_int,
        .print_string         = compact_print_str,
        .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS,
        .priv_class           = &csv_class,
};

/* Flat output */

typedef struct FlatContext {
    const AVClass *class;
    const char *sep_str;
    char sep;
    int hierarchical;
} FlatContext;

#undef OFFSET
#define OFFSET(x) offsetof(FlatContext, x)

static const AVOption flat_options[]= {
        {"sep_char", "set separator",    OFFSET(sep_str),    AV_OPT_TYPE_STRING, {.str="."},  CHAR_MIN, CHAR_MAX },
        {"s",        "set separator",    OFFSET(sep_str),    AV_OPT_TYPE_STRING, {.str="."},  CHAR_MIN, CHAR_MAX },
        {"hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
        {"h",            "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
        {NULL},
};

DEFINE_WRITER_CLASS(flat);

static av_cold int flat_init(WriterContext *wctx)
{
    FlatContext *flat = wctx->priv;

    if (strlen(flat->sep_str) != 1) {
        av_log(wctx, AV_LOG_ERROR, "Item separator '%s' specified, but must contain a single character\n",
               flat->sep_str);
        return AVERROR(EINVAL);
    }
    flat->sep = flat->sep_str[0];

    return 0;
}

static const char *flat_escape_key_str(AVBPrint *dst, const char *src, const char sep)
{
    const char *p;

    for (p = src; *p; p++) {
        if (!((*p >= '0' && *p <= '9') ||
              (*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z')))
            av_bprint_chars(dst, '_', 1);
        else
            av_bprint_chars(dst, *p, 1);
    }
    return dst->str;
}

static const char *flat_escape_value_str(AVBPrint *dst, const char *src)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
            case '\n': av_bprintf(dst, "%s", "\\n");  break;
            case '\r': av_bprintf(dst, "%s", "\\r");  break;
            case '\\': av_bprintf(dst, "%s", "\\\\"); break;
            case '"':  av_bprintf(dst, "%s", "\\\""); break;
            case '`':  av_bprintf(dst, "%s", "\\`");  break;
            case '$':  av_bprintf(dst, "%s", "\\$");  break;
            default:   av_bprint_chars(dst, *p, 1);   break;
        }
    }
    return dst->str;
}

static void flat_print_section_header(WriterContext *wctx)
{
    FlatContext *flat = wctx->priv;
    AVBPrint *buf = &wctx->section_pbuf[wctx->level];
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
                                           wctx->section[wctx->level-1] : NULL;

    /* build section header */
    av_bprint_clear(buf);
    if (!parent_section)
        return;
    av_bprintf(buf, "%s", wctx->section_pbuf[wctx->level-1].str);

    if (flat->hierarchical ||
        !(section->flags & (SECTION_FLAG_IS_ARRAY|SECTION_FLAG_IS_WRAPPER))) {
        av_bprintf(buf, "%s%s", wctx->section[wctx->level]->name, flat->sep_str);

        if (parent_section->flags & SECTION_FLAG_IS_ARRAY) {
            int n = parent_section->id == SECTION_ID_PACKETS_AND_FRAMES ?
                    wctx->nb_section_packet_frame : wctx->nb_item[wctx->level-1];
            av_bprintf(buf, "%d%s", n, flat->sep_str);
        }
    }
}

static void flat_print_int(WriterContext *wctx, const char *key, long long int value)
{
    printf("%s%s=%lld\n", wctx->section_pbuf[wctx->level].str, key, value);
}

static void flat_print_str(WriterContext *wctx, const char *key, const char *value)
{
    FlatContext *flat = wctx->priv;
    AVBPrint buf;

    printf("%s", wctx->section_pbuf[wctx->level].str);
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s=", flat_escape_key_str(&buf, key, flat->sep));
    av_bprint_clear(&buf);
    printf("\"%s\"\n", flat_escape_value_str(&buf, value));
    av_bprint_finalize(&buf, NULL);
}

static const Writer flat_writer = {
        .name                  = "flat",
        .priv_size             = sizeof(FlatContext),
        .init                  = flat_init,
        .print_section_header  = flat_print_section_header,
        .print_integer         = flat_print_int,
        .print_string          = flat_print_str,
        .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS|WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
        .priv_class            = &flat_class,
};

/* INI format output */

typedef struct INIContext {
    const AVClass *class;
    int hierarchical;
} INIContext;

#undef OFFSET
#define OFFSET(x) offsetof(INIContext, x)

static const AVOption ini_options[] = {
        {"hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
        {"h",            "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1 },
        {NULL},
};

DEFINE_WRITER_CLASS(ini);

static char *ini_escape_str(AVBPrint *dst, const char *src)
{
    int i = 0;
    char c = 0;

    while (c = src[i++]) {
        switch (c) {
            case '\b': av_bprintf(dst, "%s", "\\b"); break;
            case '\f': av_bprintf(dst, "%s", "\\f"); break;
            case '\n': av_bprintf(dst, "%s", "\\n"); break;
            case '\r': av_bprintf(dst, "%s", "\\r"); break;
            case '\t': av_bprintf(dst, "%s", "\\t"); break;
            case '\\':
            case '#' :
            case '=' :
            case ':' : av_bprint_chars(dst, '\\', 1);
            default:
                if ((unsigned char)c < 32)
                    av_bprintf(dst, "\\x00%02x", c & 0xff);
                else
                    av_bprint_chars(dst, c, 1);
                break;
        }
    }
    return dst->str;
}

static void ini_print_section_header(WriterContext *wctx)
{
    INIContext *ini = wctx->priv;
    AVBPrint *buf = &wctx->section_pbuf[wctx->level];
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
                                           wctx->section[wctx->level-1] : NULL;

    av_bprint_clear(buf);
    if (!parent_section) {
        printf("# ffprobe output\n\n");
        return;
    }

    if (wctx->nb_item[wctx->level-1])
        printf("\n");

    av_bprintf(buf, "%s", wctx->section_pbuf[wctx->level-1].str);
    if (ini->hierarchical ||
        !(section->flags & (SECTION_FLAG_IS_ARRAY|SECTION_FLAG_IS_WRAPPER))) {
        av_bprintf(buf, "%s%s", buf->str[0] ? "." : "", wctx->section[wctx->level]->name);

        if (parent_section->flags & SECTION_FLAG_IS_ARRAY) {
            int n = parent_section->id == SECTION_ID_PACKETS_AND_FRAMES ?
                    wctx->nb_section_packet_frame : wctx->nb_item[wctx->level-1];
            av_bprintf(buf, ".%d", n);
        }
    }

    if (!(section->flags & (SECTION_FLAG_IS_ARRAY|SECTION_FLAG_IS_WRAPPER)))
        printf("[%s]\n", buf->str);
}

static void ini_print_str(WriterContext *wctx, const char *key, const char *value)
{
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("%s=", ini_escape_str(&buf, key));
    av_bprint_clear(&buf);
    printf("%s\n", ini_escape_str(&buf, value));
    av_bprint_finalize(&buf, NULL);
}

static void ini_print_int(WriterContext *wctx, const char *key, long long int value)
{
    printf("%s=%lld\n", key, value);
}

static const Writer ini_writer = {
        .name                  = "ini",
        .priv_size             = sizeof(INIContext),
        .print_section_header  = ini_print_section_header,
        .print_integer         = ini_print_int,
        .print_string          = ini_print_str,
        .flags = WRITER_FLAG_DISPLAY_OPTIONAL_FIELDS|WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
        .priv_class            = &ini_class,
};

/* JSON output */

typedef struct JSONContext {
    const AVClass *class;
    int indent_level;
    int compact;
    const char *item_sep, *item_start_end;
} JSONContext;

#undef OFFSET
#define OFFSET(x) offsetof(JSONContext, x)

static const AVOption json_options[]= {
        { "compact", "enable compact output", OFFSET(compact), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
        { "c",       "enable compact output", OFFSET(compact), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
        { NULL }
};

DEFINE_WRITER_CLASS(json);

static av_cold int json_init(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;

    json->item_sep       = json->compact ? ", " : ",\n";
    json->item_start_end = json->compact ? " "  : "\n";

    return 0;
}

static const char *json_escape_str(AVBPrint *dst, const char *src, void *log_ctx)
{
    static const char json_escape[] = {'"', '\\', '\b', '\f', '\n', '\r', '\t', 0};
    static const char json_subst[]  = {'"', '\\',  'b',  'f',  'n',  'r',  't', 0};
    const char *p;

    for (p = src; *p; p++) {
        char *s = strchr(json_escape, *p);
        if (s) {
            av_bprint_chars(dst, '\\', 1);
            av_bprint_chars(dst, json_subst[s - json_escape], 1);
        } else if ((unsigned char)*p < 32) {
            av_bprintf(dst, "\\u00%02x", *p & 0xff);
        } else {
            av_bprint_chars(dst, *p, 1);
        }
    }
    return dst->str;
}

#define JSON_INDENT() printf("%*c", json->indent_level * 4, ' ')

static void json_print_section_header(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;
    AVBPrint buf;
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
                                           wctx->section[wctx->level-1] : NULL;

    if (wctx->level && wctx->nb_item[wctx->level-1])
        printf(",\n");

    if (section->flags & SECTION_FLAG_IS_WRAPPER) {
        printf("{\n");
        json->indent_level++;
    } else {
        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        json_escape_str(&buf, section->name, wctx);
        JSON_INDENT();

        json->indent_level++;
        if (section->flags & SECTION_FLAG_IS_ARRAY) {
            printf("\"%s\": [\n", buf.str);
        } else if (parent_section && !(parent_section->flags & SECTION_FLAG_IS_ARRAY)) {
            printf("\"%s\": {%s", buf.str, json->item_start_end);
        } else {
            printf("{%s", json->item_start_end);

            /* this is required so the parser can distinguish between packets and frames */
            if (parent_section && parent_section->id == SECTION_ID_PACKETS_AND_FRAMES) {
                if (!json->compact)
                    JSON_INDENT();
                printf("\"type\": \"%s\"%s", section->name, json->item_sep);
            }
        }
        av_bprint_finalize(&buf, NULL);
    }
}

static void json_print_section_footer(WriterContext *wctx)
{
    JSONContext *json = wctx->priv;
    const struct section *section = wctx->section[wctx->level];

    if (wctx->level == 0) {
        json->indent_level--;
        printf("\n}\n");
    } else if (section->flags & SECTION_FLAG_IS_ARRAY) {
        printf("\n");
        json->indent_level--;
        JSON_INDENT();
        printf("]");
    } else {
        printf("%s", json->item_start_end);
        json->indent_level--;
        if (!json->compact)
            JSON_INDENT();
        printf("}");
    }
}

static inline void json_print_item_str(WriterContext *wctx,
                                       const char *key, const char *value)
{
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("\"%s\":", json_escape_str(&buf, key,   wctx));
    av_bprint_clear(&buf);
    printf(" \"%s\"", json_escape_str(&buf, value, wctx));
    av_bprint_finalize(&buf, NULL);
}

static void json_print_str(WriterContext *wctx, const char *key, const char *value)
{
    JSONContext *json = wctx->priv;

    if (wctx->nb_item[wctx->level])
        printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();
    json_print_item_str(wctx, key, value);
}

static void json_print_int(WriterContext *wctx, const char *key, long long int value)
{
    JSONContext *json = wctx->priv;
    AVBPrint buf;

    if (wctx->nb_item[wctx->level])
        printf("%s", json->item_sep);
    if (!json->compact)
        JSON_INDENT();

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    printf("\"%s\": %lld", json_escape_str(&buf, key, wctx), value);
    av_bprint_finalize(&buf, NULL);
}

static const Writer json_writer = {
        .name                 = "json",
        .priv_size            = sizeof(JSONContext),
        .init                 = json_init,
        .print_section_header = json_print_section_header,
        .print_section_footer = json_print_section_footer,
        .print_integer        = json_print_int,
        .print_string         = json_print_str,
        .flags = WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
        .priv_class           = &json_class,
};

/* XML output */

typedef struct XMLContext {
    const AVClass *class;
    int within_tag;
    int indent_level;
    int fully_qualified;
    int xsd_strict;
} XMLContext;

#undef OFFSET
#define OFFSET(x) offsetof(XMLContext, x)

static const AVOption xml_options[] = {
        {"fully_qualified", "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
        {"q",               "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
        {"xsd_strict",      "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
        {"x",               "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_BOOL, {.i64=0},  0, 1 },
        {NULL},
};

DEFINE_WRITER_CLASS(xml);

static av_cold int xml_init(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;

    if (xml->xsd_strict) {
        xml->fully_qualified = 1;
#define CHECK_COMPLIANCE(opt, opt_name)                                 \
        if (opt) {                                                      \
            av_log(wctx, AV_LOG_ERROR,                                  \
                   "XSD-compliant output selected but option '%s' was selected, XML output may be non-compliant.\n" \
                   "You need to disable such option with '-no%s'\n", opt_name, opt_name); \
            return AVERROR(EINVAL);                                     \
        }
        CHECK_COMPLIANCE(show_private_data, "private");
        CHECK_COMPLIANCE(show_value_unit,   "unit");
        CHECK_COMPLIANCE(use_value_prefix,  "prefix");

        if (do_show_frames && do_show_packets) {
            av_log(wctx, AV_LOG_ERROR,
                   "Interleaved frames and packets are not allowed in XSD. "
                   "Select only one between the -show_frames and the -show_packets options.\n");
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static const char *xml_escape_str(AVBPrint *dst, const char *src, void *log_ctx)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
            case '&' : av_bprintf(dst, "%s", "&amp;");  break;
            case '<' : av_bprintf(dst, "%s", "&lt;");   break;
            case '>' : av_bprintf(dst, "%s", "&gt;");   break;
            case '"' : av_bprintf(dst, "%s", "&quot;"); break;
            case '\'': av_bprintf(dst, "%s", "&apos;"); break;
            default: av_bprint_chars(dst, *p, 1);
        }
    }

    return dst->str;
}

#define XML_INDENT() printf("%*c", xml->indent_level * 4, ' ')

static void xml_print_section_header(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;
    const struct section *section = wctx->section[wctx->level];
    const struct section *parent_section = wctx->level ?
                                           wctx->section[wctx->level-1] : NULL;

    if (wctx->level == 0) {
        const char *qual = " xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance' "
                           "xmlns:ffprobe='http://www.ffmpeg.org/schema/ffprobe' "
                           "xsi:schemaLocation='http://www.ffmpeg.org/schema/ffprobe ffprobe.xsd'";

        printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        printf("<%sffprobe%s>\n",
               xml->fully_qualified ? "ffprobe:" : "",
               xml->fully_qualified ? qual : "");
        return;
    }

    if (xml->within_tag) {
        xml->within_tag = 0;
        printf(">\n");
    }
    if (section->flags & SECTION_FLAG_HAS_VARIABLE_FIELDS) {
        xml->indent_level++;
    } else {
        if (parent_section && (parent_section->flags & SECTION_FLAG_IS_WRAPPER) &&
            wctx->level && wctx->nb_item[wctx->level-1])
            printf("\n");
        xml->indent_level++;

        if (section->flags & SECTION_FLAG_IS_ARRAY) {
            XML_INDENT(); printf("<%s>\n", section->name);
        } else {
            XML_INDENT(); printf("<%s ", section->name);
            xml->within_tag = 1;
        }
    }
}

static void xml_print_section_footer(WriterContext *wctx)
{
    XMLContext *xml = wctx->priv;
    const struct section *section = wctx->section[wctx->level];

    if (wctx->level == 0) {
        printf("</%sffprobe>\n", xml->fully_qualified ? "ffprobe:" : "");
    } else if (xml->within_tag) {
        xml->within_tag = 0;
        printf("/>\n");
        xml->indent_level--;
    } else if (section->flags & SECTION_FLAG_HAS_VARIABLE_FIELDS) {
        xml->indent_level--;
    } else {
        XML_INDENT(); printf("</%s>\n", section->name);
        xml->indent_level--;
    }
}

static void xml_print_str(WriterContext *wctx, const char *key, const char *value)
{
    AVBPrint buf;
    XMLContext *xml = wctx->priv;
    const struct section *section = wctx->section[wctx->level];

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);

    if (section->flags & SECTION_FLAG_HAS_VARIABLE_FIELDS) {
        XML_INDENT();
        printf("<%s key=\"%s\"",
               section->element_name, xml_escape_str(&buf, key, wctx));
        av_bprint_clear(&buf);
        printf(" value=\"%s\"/>\n", xml_escape_str(&buf, value, wctx));
    } else {
        if (wctx->nb_item[wctx->level])
            printf(" ");
        printf("%s=\"%s\"", key, xml_escape_str(&buf, value, wctx));
    }

    av_bprint_finalize(&buf, NULL);
}

static void xml_print_int(WriterContext *wctx, const char *key, long long int value)
{
    if (wctx->nb_item[wctx->level])
        printf(" ");
    printf("%s=\"%lld\"", key, value);
}

static Writer xml_writer = {
        .name                 = "xml",
        .priv_size            = sizeof(XMLContext),
        .init                 = xml_init,
        .print_section_header = xml_print_section_header,
        .print_section_footer = xml_print_section_footer,
        .print_integer        = xml_print_int,
        .print_string         = xml_print_str,
        .flags = WRITER_FLAG_PUT_PACKETS_AND_FRAMES_IN_SAME_CHAPTER,
        .priv_class           = &xml_class,
};

static void writer_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    writer_register(&default_writer);
    writer_register(&compact_writer);
    writer_register(&csv_writer);
    writer_register(&flat_writer);
    writer_register(&ini_writer);
    writer_register(&json_writer);
    writer_register(&xml_writer);
}

#define print_fmt(k, f, ...) do {              \
    av_bprint_clear(&pbuf);                    \
    av_bprintf(&pbuf, f, __VA_ARGS__);         \
    writer_print_string(w, k, pbuf.str, 0);    \
} while (0)

#define print_int(k, v)         writer_print_integer(w, k, v)
#define print_q(k, v, s)        writer_print_rational(w, k, v, s)
#define print_str(k, v)         writer_print_string(w, k, v, 0)
#define print_str_opt(k, v)     writer_print_string(w, k, v, PRINT_STRING_OPT)
#define print_str_validate(k, v) writer_print_string(w, k, v, PRINT_STRING_VALIDATE)
#define print_time(k, v, tb)    writer_print_time(w, k, v, tb, 0)
#define print_ts(k, v)          writer_print_ts(w, k, v, 0)
#define print_duration_time(k, v, tb) writer_print_time(w, k, v, tb, 1)
#define print_duration_ts(k, v)       writer_print_ts(w, k, v, 1)
#define print_val(k, v, u) do {                                     \
    struct unit_value uv;                                           \
    uv.val.i = v;                                                   \
    uv.unit