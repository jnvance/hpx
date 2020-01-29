//  Copyright (c) 2007-2017 Hartmut Kaiser
//  Copyright (c)      2011 Bryce Lelbach
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/config.hpp>

#if defined(HPX_HAVE_LOGGING)
#include <hpx/logging.hpp>
#include <hpx/logging/format/destination/defaults.hpp>
#include <hpx/logging/format/named_write.hpp>
#include <hpx/naming_base.hpp>
#include <hpx/runtime/components/console_logging.hpp>
#include <hpx/runtime/get_locality_id.hpp>
#include <hpx/runtime/naming/resolver_client.hpp>
#include <hpx/runtime/naming_fwd.hpp>
#include <hpx/runtime/threads/thread_data.hpp>
#include <hpx/runtime/threads/threadmanager.hpp>
#include <hpx/type_support/static.hpp>
#include <hpx/util/get_entry_as.hpp>
#include <hpx/util/init_logging.hpp>
#include <hpx/util/runtime_configuration.hpp>

#include <boost/config.hpp>
#include <boost/version.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#if defined(ANDROID) || defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(HPX_MSVC_WARNING_PRAGMA)
#pragma warning(push)
#pragma warning (disable: 4250) // 'class1' : inherits 'class2::member' via dominance
#endif

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace util {
    typedef logging::writer::named_write logger_writer_type;

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: shepherd
    struct shepherd_thread_id : logging::formatter::manipulator
    {
        shepherd_thread_id() {}

        void operator()(logging::message& msg) override
        {
            error_code ec(lightweight);
            std::size_t thread_num = hpx::get_worker_thread_num(ec);

            if (std::size_t(-1) != thread_num)
            {
                std::string out = format("{:016x}", thread_num);
                msg.prepend_string(out);
            }
            else
            {
                msg.prepend_string(std::string(16, '-'));
            }
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: locality prefix
    struct locality_prefix : logging::formatter::manipulator
    {
        locality_prefix() {}

        void operator()(logging::message& msg) override
        {
            std::uint32_t locality_id = hpx::get_locality_id();

            if (naming::invalid_locality_id != locality_id)
            {
                std::string out = format("{:08x}", locality_id);
                msg.prepend_string(out);
            }
            else
            {
                // called from outside a HPX thread
                msg.prepend_string(std::string(8, '-'));
            }
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: HPX thread id
    struct thread_id : logging::formatter::manipulator
    {
        void operator()(logging::message& msg) override
        {
            threads::thread_self* self = threads::get_self_ptr();
            if (nullptr != self)
            {
                // called from inside a HPX thread
                threads::thread_id_type id = threads::get_self_id();
                if (id != threads::invalid_thread_id)
                {
                    std::ptrdiff_t value =
                        reinterpret_cast<std::ptrdiff_t>(id.get());
                    std::string out = format("{:016x}", value);
                    msg.prepend_string(out);
                    return;
                }
            }

            // called from outside a HPX thread or invalid thread id
            msg.prepend_string(std::string(16, '-'));
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: HPX thread phase
    struct thread_phase : logging::formatter::manipulator
    {
        void operator()(logging::message& msg) override
        {
            threads::thread_self* self = threads::get_self_ptr();
            if (nullptr != self)
            {
                // called from inside a HPX thread
                std::size_t phase = self->get_thread_phase();
                if (0 != phase)
                {
                    std::string out =
                        format("{:04x}", self->get_thread_phase());
                    msg.prepend_string(out);
                    return;
                }
            }

            // called from outside a HPX thread or no phase given
            msg.prepend_string(std::string(4, '-'));
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: locality prefix of parent thread
    struct parent_thread_locality : logging::formatter::manipulator
    {
        void operator()(logging::message& msg) override
        {
            std::uint32_t parent_locality_id =
                threads::get_parent_locality_id();
            if (naming::invalid_locality_id != parent_locality_id)
            {
                // called from inside a HPX thread
                std::string out = format("{:08x}", parent_locality_id);
                msg.prepend_string(out);
            }
            else
            {
                // called from outside a HPX thread
                msg.prepend_string(std::string(8, '-'));
            }
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: HPX parent thread id
    struct parent_thread_id : logging::formatter::manipulator
    {
        void operator()(logging::message& msg) override
        {
            threads::thread_id_type parent_id = threads::get_parent_id();
            if (nullptr != parent_id && threads::invalid_thread_id != parent_id)
            {
                // called from inside a HPX thread
                std::ptrdiff_t value =
                    reinterpret_cast<std::ptrdiff_t>(parent_id.get());
                std::string out = format("{:016x}", value);
                msg.prepend_string(out);
            }
            else
            {
                // called from outside a HPX thread
                msg.prepend_string(std::string(16, '-'));
            }
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: HPX parent thread phase
    struct parent_thread_phase : logging::formatter::manipulator
    {
        void operator()(logging::message& msg) override
        {
            std::size_t parent_phase = threads::get_parent_phase();
            if (0 != parent_phase)
            {
                // called from inside a HPX thread
                std::string out = format("{:04x}", parent_phase);
                msg.prepend_string(out);
            }
            else
            {
                // called from outside a HPX thread
                msg.prepend_string(std::string(4, '-'));
            }
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom formatter: HPX component id of current thread
    struct thread_component_id : logging::formatter::manipulator
    {
        void operator()(logging::message& msg) override
        {
            std::uint64_t component_id = threads::get_self_component_id();
            if (0 != component_id)
            {
                // called from inside a HPX thread
                std::string out = format("{:016x}", component_id);
                msg.prepend_string(out);
            }
            else
            {
                // called from outside a HPX thread
                msg.prepend_string(std::string(16, '-'));
            }
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // custom log destination: send generated strings to console
    struct console : logging::destination::manipulator
    {
        console(logging::level level, logging_destination dest)
          : level_(level)
          , dest_(dest)
        {
        }

        void operator()(logging::message const& msg) override
        {
            components::console_logging(
                dest_, static_cast<std::size_t>(level_), msg.full_string());
        }

        bool operator==(console const& rhs) const
        {
            return dest_ == rhs.dest_;
        }

        logging::level level_;
        logging_destination dest_;
    };

#if defined(ANDROID) || defined(__ANDROID__)
    // default log destination for Android
    struct android_log : logging::destination::manipulator
    {
        android_log(char const* tag_)
          : tag(tag_)
        {
        }

        void operator()(logging::message const& msg) override
        {
            __android_log_write(
                ANDROID_LOG_DEBUG, tag.c_str(), msg.full_string().c_str());
        }

        bool operator==(android_log const& rhs) const
        {
            return tag == rhs.tag;
        }

        std::string tag;
    };
#endif

    namespace detail {
        // unescape config entry
        static std::string unescape(std::string const& value)
        {
            std::string result;
            std::string::size_type pos = 0;
            std::string::size_type pos1 = value.find_first_of('\\', 0);
            if (std::string::npos != pos1)
            {
                do
                {
                    switch (value[pos1 + 1])
                    {
                    case '\\':
                    case '\"':
                    case '?':
                        result = result + value.substr(pos, pos1 - pos);
                        pos1 = value.find_first_of('\\', (pos = pos1 + 1) + 1);
                        break;

                    case 'n':
                        result = result + value.substr(pos, pos1 - pos) + "\n";
                        pos1 = value.find_first_of('\\', pos = pos1 + 1);
                        ++pos;
                        break;

                    default:
                        result = result + value.substr(pos, pos1 - pos + 1);
                        pos1 = value.find_first_of('\\', pos = pos1 + 1);
                    }

                } while (pos1 != std::string::npos);
                result = result + value.substr(pos);
            }
            else
            {
                // the string doesn't contain any escaped character sequences
                result = value;
            }
            return result;
        }

        static void define_formatters(logger_writer_type& writer)
        {
            writer.set_formatter("osthread", shepherd_thread_id());
            writer.set_formatter("locality", locality_prefix());
            writer.set_formatter("hpxthread", thread_id());
            writer.set_formatter("hpxphase", thread_phase());
            writer.set_formatter("hpxparent", parent_thread_id());
            writer.set_formatter("hpxparentphase", parent_thread_phase());
            writer.set_formatter("parentloc", parent_thread_locality());
            writer.set_formatter("hpxcomponent", thread_component_id());
        }
    }    // namespace detail

    // initialize logging for AGAS
    void init_agas_log(util::section const& ini, bool isconsole)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.agas"))
        {
            util::section const* logini = ini.get_section("hpx.logging.agas");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty())
            {
                logdest = logini->get_entry("destination", empty);
                logformat =
                    detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = agas_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "android_log" : "console";
            agas_logger()->writer().set_destination("android_log",
                android_log("hpx.agas"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "cerr" : "console";
#endif

            writer.set_destination("console", console(lvl, destination_agas)); //-V106
            writer.write(logformat, logdest);
            detail::define_formatters(writer);

            agas_logger()->mark_as_initialized();
            agas_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for the parcel transport
    void init_parcel_log(util::section const& ini, bool isconsole)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.parcel")) {
            util::section const* logini = ini.get_section("hpx.logging.parcel");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel);

        if (hpx::util::logging::level::disable_all != lvl)
        {
           logger_writer_type& writer = parcel_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "android_log" : "console";
            parcel_logger()->writer().set_destination("android_log",
                android_log("hpx.parcel"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "cerr" : "console";
#endif

            writer.set_destination("console",
                console(lvl, destination_parcel)); //-V106
            writer.write(logformat, logdest);
            detail::define_formatters(writer);

            parcel_logger()->mark_as_initialized();
            parcel_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for performance measurements
    void init_timing_log(util::section const& ini, bool isconsole)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.timing")) {
            util::section const* logini = ini.get_section("hpx.logging.timing");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel);

        if (hpx::util::logging::level::disable_all != lvl)
        {
           logger_writer_type& writer = timing_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "android_log" : "console";

            writer.set_destination("android_log",
                android_log("hpx.timing"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "cerr" : "console";
#endif

            writer.set_destination("console", console(lvl, destination_timing)); //-V106
            writer.write(logformat, logdest);
            detail::define_formatters(writer);

            timing_logger()->mark_as_initialized();
            timing_logger()->set_enabled(lvl);
        }
    }

    void init_hpx_logs(util::section const& ini, bool isconsole)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging")) {
            util::section const* logini = ini.get_section("hpx.logging");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel, true);

        logger_writer_type& writer = hpx_logger()->writer();
        logger_writer_type& error_writer = hpx_error_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
        if (logdest.empty())      // ensure minimal defaults
            logdest = isconsole ? "android_log" : "console";

        writer.set_destination("android_log", android_log("hpx"));
        error_writer.set_destination("android_log", android_log("hpx"));
#else
        if (logdest.empty())      // ensure minimal defaults
            logdest = isconsole ? "cerr" : "console";
#endif

        if (hpx::util::logging::level::disable_all != lvl)
        {
            writer.set_destination("console", console(lvl, destination_hpx)); //-V106
            writer.write(logformat, logdest);
            detail::define_formatters(writer);

            hpx_logger()->mark_as_initialized();
            hpx_logger()->set_enabled(lvl);

            // errors are logged to the given destination and to cerr
            error_writer.set_destination("console",
                console(lvl, destination_hpx)); //-V106
#if !defined(ANDROID) && !defined(__ANDROID__)
            if (logdest != "cerr")
                error_writer.write(logformat, logdest + " cerr");
#endif
            detail::define_formatters(error_writer);

            hpx_error_logger()->mark_as_initialized();
            hpx_error_logger()->set_enabled(lvl);
        }
        else {
            // errors are always logged to cerr
            if (!isconsole) {
                error_writer.set_destination("console",
                    console(lvl, destination_hpx)); //-V106
                error_writer.write(logformat, "console");
            }
            else {
#if defined(ANDROID) || defined(__ANDROID__)
                error_writer.write(logformat, "android_log");
#else
                error_writer.write(logformat, "cerr");
#endif
            }
            detail::define_formatters(error_writer);

            hpx_error_logger()->mark_as_initialized();
            hpx_error_logger()->set_enabled(hpx::util::logging::level::fatal);
        }
    }

    // initialize logging for application
    void init_app_logs(util::section const& ini, bool isconsole)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.application")) {
            util::section const* logini = ini.get_section("hpx.logging.application");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = app_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "android_log" : "console";
            writer.set_destination("android_log", android_log("hpx.application"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "cerr" : "console";
#endif

            writer.set_destination("console", console(lvl, destination_app)); //-V106
            writer.write(logformat, logdest);
            detail::define_formatters(writer);

            app_logger()->mark_as_initialized();
            app_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for application
    void init_debuglog_logs(util::section const& ini, bool isconsole)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.debuglog")) {
            util::section const* logini = ini.get_section("hpx.logging.debuglog");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = debuglog_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "android_log" : "console";
            writer.set_destination("android_log", android_log("hpx.debuglog"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = isconsole ? "cerr" : "console";
#endif

            writer.set_destination("console",
                console(lvl, destination_debuglog)); //-V106
            writer.write(logformat, logdest);
            detail::define_formatters(writer);

            debuglog_logger()->mark_as_initialized();
            debuglog_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for AGAS
    void init_agas_console_log(util::section const& ini)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.console.agas")) {
            util::section const* logini = ini.get_section("hpx.logging.console.agas");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel, true);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = agas_console_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = "android_log";
            writer.set_destination("android_log", android_log("hpx.agas"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = "cerr";
#endif

            writer.write(logformat, logdest);

            agas_console_logger()->mark_as_initialized();
            agas_console_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for the parcel transport
    void init_parcel_console_log(util::section const& ini)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.console.parcel")) {
            util::section const* logini =
                ini.get_section("hpx.logging.console.parcel");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel, true);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = parcel_console_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = "android_log";
            writer.set_destination("android_log", android_log("hpx.parcel"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = "cerr";
#endif

            writer.write(logformat, logdest);

            parcel_console_logger()->mark_as_initialized();
            parcel_console_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for performance measurements
    void init_timing_console_log(util::section const& ini)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.console.timing")) {
            util::section const* logini = ini.get_section("hpx.logging.console.timing");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel, true);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = timing_console_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = "android_log";
            writer.set_destination("android_log", android_log("hpx.timing"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = "cerr";
#endif

            writer.write(logformat, logdest);

            timing_console_logger()->mark_as_initialized();
            timing_console_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for HPX runtime
    void init_hpx_console_log(util::section const& ini)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.console")) {
            util::section const* logini = ini.get_section("hpx.logging.console");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel, true);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = hpx_console_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = "android_log";
            writer.set_destination("android_log", android_log("hpx"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = "cerr";
#endif

            writer.write(logformat, logdest);

            hpx_console_logger()->mark_as_initialized();
            hpx_console_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for applications
    void init_app_console_log(util::section const& ini)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.console.application")) {
            util::section const* logini =
                ini.get_section("hpx.logging.console.application");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel, true);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = app_console_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = "android_log";
            writer.set_destination("android_log", android_log("hpx.application"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = "cerr";
#endif

            writer.write(logformat, logdest);

            app_console_logger()->mark_as_initialized();
            app_console_logger()->set_enabled(lvl);
        }
    }

    // initialize logging for applications
    void init_debuglog_console_log(util::section const& ini)
    {
        std::string loglevel, logdest, logformat;

        if (ini.has_section("hpx.logging.console.debuglog")) {
            util::section const* logini =
                ini.get_section("hpx.logging.console.debuglog");
            HPX_ASSERT(nullptr != logini);

            std::string empty;
            loglevel = logini->get_entry("level", empty);
            if (!loglevel.empty()) {
                logdest = logini->get_entry("destination", empty);
                logformat = detail::unescape(logini->get_entry("format", empty));
            }
        }

        auto lvl = hpx::util::logging::level::disable_all;
        if (!loglevel.empty())
            lvl = detail::get_log_level(loglevel, true);

        if (hpx::util::logging::level::disable_all != lvl)
        {
            logger_writer_type& writer = debuglog_console_logger()->writer();

#if defined(ANDROID) || defined(__ANDROID__)
            if (logdest.empty())      // ensure minimal defaults
                logdest = "android_log";
            writer.set_destination("android_log", android_log("hpx.debuglog"));
#else
            if (logdest.empty())      // ensure minimal defaults
                logdest = "cerr";
#endif

            writer.write(logformat, logdest);

            debuglog_console_logger()->mark_as_initialized();
            debuglog_console_logger()->set_enabled(lvl);
        }
    }
}}

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace util { namespace detail
{
    ///////////////////////////////////////////////////////////////////////////
    // the logging_configuration type will be instantiated exactly once
    struct logging_configuration
    {
        logging_configuration();
        std::vector<std::string> prefill_;
    };

// define the format for the generated time stamps
#define HPX_TIMEFORMAT "$hh:$mm.$ss.$mili"

    logging_configuration::logging_configuration()
    {
        try {
            // add default logging configuration as defaults to the ini data
            // this will be overwritten by related entries in the read hpx.ini
            prefill_ = {
                // general logging
                "[hpx.logging]",
                "level = ${HPX_LOGLEVEL:0}",
                "destination = ${HPX_LOGDESTINATION:console}",
                "format = ${HPX_LOGFORMAT:"
                    "(T%locality%/%hpxthread%.%hpxphase%/%hpxcomponent%) "
                    "P%parentloc%/%hpxparent%.%hpxparentphase% %time%("
                    HPX_TIMEFORMAT ") [%idx%]}",

                // general console logging
                "[hpx.logging.console]",
                "level = ${HPX_LOGLEVEL:$[hpx.logging.level]}",
#if defined(ANDROID) || defined(__ANDROID__)
                "destination = ${HPX_CONSOLE_LOGDESTINATION:android_log}",
#else
                "destination = ${HPX_CONSOLE_LOGDESTINATION:"
                    "file(hpx.$[system.pid].log)}",
#endif
                "format = ${HPX_CONSOLE_LOGFORMAT:}",

                // logging related to timing
                "[hpx.logging.timing]",
                "level = ${HPX_TIMING_LOGLEVEL:-1}",
                "destination = ${HPX_TIMING_LOGDESTINATION:console}",
                "format = ${HPX_TIMING_LOGFORMAT:"
                    "(T%locality%/%hpxthread%.%hpxphase%/%hpxcomponent%) "
                    "P%parentloc%/%hpxparent%.%hpxparentphase% %time%("
                    HPX_TIMEFORMAT ") [%idx%] [TIM]}",

                // console logging related to timing
                "[hpx.logging.console.timing]",
                "level = ${HPX_TIMING_LOGLEVEL:$[hpx.logging.timing.level]}",
#if defined(ANDROID) || defined(__ANDROID__)
                "destination = ${HPX_CONSOLE_TIMING_LOGDESTINATION:android_log}",
#else
                "destination = ${HPX_CONSOLE_TIMING_LOGDESTINATION:"
                    "file(hpx.timing.$[system.pid].log)}",
#endif
                "format = ${HPX_CONSOLE_TIMING_LOGFORMAT:}",

                // logging related to AGAS
                "[hpx.logging.agas]",
                "level = ${HPX_AGAS_LOGLEVEL:-1}",
//                     "destination = ${HPX_AGAS_LOGDESTINATION:console}",
                "destination = ${HPX_AGAS_LOGDESTINATION:"
                    "file(hpx.agas.$[system.pid].log)}",
                "format = ${HPX_AGAS_LOGFORMAT:"
                    "(T%locality%/%hpxthread%.%hpxphase%/%hpxcomponent%) "
                    "P%parentloc%/%hpxparent%.%hpxparentphase% %time%("
                    HPX_TIMEFORMAT ") [%idx%][AGAS]}",

                // console logging related to AGAS
                "[hpx.logging.console.agas]",
                "level = ${HPX_AGAS_LOGLEVEL:$[hpx.logging.agas.level]}",
#if defined(ANDROID) || defined(__ANDROID__)
                "destination = ${HPX_CONSOLE_AGAS_LOGDESTINATION:android_log}",
#else
                "destination = ${HPX_CONSOLE_AGAS_LOGDESTINATION:"
                    "file(hpx.agas.$[system.pid].log)}",
#endif
                "format = ${HPX_CONSOLE_AGAS_LOGFORMAT:}",

                // logging related to the parcel transport
                "[hpx.logging.parcel]",
                "level = ${HPX_PARCEL_LOGLEVEL:-1}",
                "destination = ${HPX_PARCEL_LOGDESTINATION:"
                    "file(hpx.parcel.$[system.pid].log)}",
                "format = ${HPX_PARCEL_LOGFORMAT:"
                    "(T%locality%/%hpxthread%.%hpxphase%/%hpxcomponent%) "
                    "P%parentloc%/%hpxparent%.%hpxparentphase% %time%("
                    HPX_TIMEFORMAT ") [%idx%][  PT]}",

                // console logging related to the parcel transport
                "[hpx.logging.console.parcel]",
                "level = ${HPX_PARCEL_LOGLEVEL:$[hpx.logging.parcel.level]}",
#if defined(ANDROID) || defined(__ANDROID__)
                "destination = ${HPX_CONSOLE_PARCEL_LOGDESTINATION:android_log}",
#else
                "destination = ${HPX_CONSOLE_PARCEL_LOGDESTINATION:"
                    "file(hpx.parcel.$[system.pid].log)}",
#endif
                "format = ${HPX_CONSOLE_PARCEL_LOGFORMAT:}",

                // logging related to applications
                "[hpx.logging.application]",
                "level = ${HPX_APP_LOGLEVEL:-1}",
                "destination = ${HPX_APP_LOGDESTINATION:console}",
                "format = ${HPX_APP_LOGFORMAT:"
                    "(T%locality%/%hpxthread%.%hpxphase%/%hpxcomponent%) "
                    "P%parentloc%/%hpxparent%.%hpxparentphase% %time%("
                    HPX_TIMEFORMAT ") [%idx%] [APP]}",

                // console logging related to applications
                "[hpx.logging.console.application]",
                "level = ${HPX_APP_LOGLEVEL:$[hpx.logging.application.level]}",
#if defined(ANDROID) || defined(__ANDROID__)
                "destination = ${HPX_CONSOLE_APP_LOGDESTINATION:android_log}",
#else
                "destination = ${HPX_CONSOLE_APP_LOGDESTINATION:"
                    "file(hpx.application.$[system.pid].log)}",
#endif
                "format = ${HPX_CONSOLE_APP_LOGFORMAT:}",

                // logging of debug channel
                "[hpx.logging.debuglog]",
                "level = ${HPX_DEB_LOGLEVEL:-1}",
                "destination = ${HPX_DEB_LOGDESTINATION:console}",
                "format = ${HPX_DEB_LOGFORMAT:"
                    "(T%locality%/%hpxthread%.%hpxphase%/%hpxcomponent%) "
                    "P%parentloc%/%hpxparent%.%hpxparentphase% %time%("
                    HPX_TIMEFORMAT ") [%idx%] [DEB]}",

                "[hpx.logging.console.debuglog]",
                "level = ${HPX_DEB_LOGLEVEL:$[hpx.logging.debuglog.level]}",
#if defined(ANDROID) || defined(__ANDROID__)
                "destination = ${HPX_CONSOLE_DEB_LOGDESTINATION:android_log}",
#else
                "destination = ${HPX_CONSOLE_DEB_LOGDESTINATION:"
                    "file(hpx.debuglog.$[system.pid].log)}",
#endif
                "format = ${HPX_CONSOLE_DEB_LOGFORMAT:|}"
            };
        }
        catch (std::exception const&) {
            // just in case something goes wrong
            std::cerr << "caught std::exception during initialization"
                      << std::endl;
        }
    }

#undef HPX_TIMEFORMAT

    struct init_logging_tag {};
    std::vector<std::string> const& get_logging_data()
    {
        static_<logging_configuration, init_logging_tag> init;
        return init.get().prefill_;
    }

    ///////////////////////////////////////////////////////////////////////////
    void init_logging(runtime_configuration& ini, bool isconsole)
    {
        // initialize normal logs
        init_agas_log(ini, isconsole);
        init_parcel_log(ini, isconsole);
        init_timing_log(ini, isconsole);
        init_hpx_logs(ini, isconsole);
        init_app_logs(ini, isconsole);
        init_debuglog_logs(ini, isconsole);

        // initialize console logs
        init_agas_console_log(ini);
        init_parcel_console_log(ini);
        init_timing_console_log(ini);
        init_hpx_console_log(ini);
        init_app_console_log(ini);
        init_debuglog_console_log(ini);
    }
}}}

#if defined(HPX_MSVC_WARNING_PRAGMA)
#pragma warning(pop)
#endif

#else  // HPX_HAVE_LOGGING

#include <hpx/util/runtime_configuration.hpp>
#include <hpx/logging.hpp>
#include <hpx/util/get_entry_as.hpp>
#include <hpx/util/init_logging.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace hpx { namespace util { namespace detail
{
    std::vector<std::string> const& get_logging_data()
    {
        static std::vector<std::string> dummy_data;
        return dummy_data;
    }

    void init_logging(runtime_configuration& ini, bool)
    {
        // warn if logging is requested

        if (util::get_entry_as<int>(ini, "hpx.logging.level", -1) > 0 ||
            util::get_entry_as<int>(ini, "hpx.logging.timing.level", -1) > 0 ||
            util::get_entry_as<int>(ini, "hpx.logging.agas.level", -1) > 0 ||
            util::get_entry_as<int>(ini, "hpx.logging.debuglog.level", -1) > 0 ||
            util::get_entry_as<int>(ini, "hpx.logging.application.level", -1) > 0)
        {
            std::cerr << "hpx::init_logging: warning: logging is requested even "
                         "while it was disabled at compile time. If you "
                         "need logging to be functional, please reconfigure and "
                         "rebuild HPX with HPX_WITH_LOGGING set to ON."
                      << std::endl;
        }
    }
}}}

#endif // HPX_HAVE_LOGGING
