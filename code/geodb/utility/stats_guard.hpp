#ifndef GEODB_UTILITY_STATS_GUARD_HPP
#define GEODB_UTILITY_STATS_GUARD_HPP

#include "geodb/common.hpp"

//#include <boost/multi_index_container.hpp>
//#include <boost/multi_index/member.hpp>
//#include <boost/multi_index/ordered_index.hpp>
//#include <boost/multi_index/sequenced_index.hpp>
#include <fmt/format.h>
#include <tpie/tpie.h>
#include <tpie/stats.h>

#include <algorithm>
#include <chrono>
#include <memory>

namespace geodb {

///// Contains the definition of the boost multi index map.
//namespace stats_guard_ {

//using namespace boost::multi_index;
//using boost::multi_index_container;

//struct entry_name {};
//struct insertion_order {};

//template<typename NodePointer>
//struct name_extractor {
//    using result_type = std::string;
//    const std::string& operator()(const NodePointer& node) {
//        geodb_assert(node != nullptr, "node is null.");
//        return node->name;
//    }
//};

// /// Indexed by key and ordered by insertion time.
//template<typename NodePointer>
//using container = multi_index_container<
//    NodePointer,
//    indexed_by<
//        ordered_unique<tag<entry_name>, name_extractor<NodePointer>>,
//        sequenced<tag<insertion_order>>
//    >
//>;

//} // namespace stats_guard_

//class stats_guard {
//    using clock = std::chrono::steady_clock;
//    using time_point = clock::time_point;
//    using double_seconds = std::chrono::duration<double>;

//    struct node {
//        node(std::string name, node* parent)
//            : name(std::move(name))
//            , parent(parent)
//        {}

//        /// The name of this entry in the tree.
//        const std::string name;

//        /// The parent of this node.
//        node* const parent = nullptr;

//        /// Number of times this node was entered (~executed).
//        u32 count = 0;

//        /// Number of bytes read while within this nodes (combined).
//        u64 bytes_read = 0;

//        /// Number of bytes written while within this nodes (combined).
//        u64 bytes_written = 0;

//        /// Time spent in this node (in seconds).
//        double duration = 0;

//        /// The children (if any).
//        stats_guard_::container<std::unique_ptr<node>> children;
//    };

//    struct frame {
//        u64 m_bytes_read_start = 0;
//        u64 m_bytes_written_start = 0;
//        time_point m_time_start{};
//    };

//    struct context {
//        void enter(const std::string& name) {
//            if (!m_root) {
//                // Construct a new root node.
//                geodb_assert(m_top == nullptr, "invalid state.");
//                m_root = std::make_unique<node>(name, nullptr);
//                m_top = m_root.get();
//            } else {
//                geodb_assert(m_top != nullptr, "invalid state.");
//                // See if there is already a child of the current node with that name.
//                auto& index = m_top->children.get<stats_guard_::entry_name>();
//                auto pos = index.find(name);
//                if (pos == index.end()) {
//                    auto ptr = std::make_unique<node>(name, m_top);
//                    m_top = ptr.get();
//                    index.insert(std::move(ptr));
//                } else {
//                    m_top = pos->get();
//                }
//            }

//            // Push a new frame.
//            m_frames.emplace_back(frame{tpie::get_bytes_read(), tpie::get_bytes_written(), clock::now()});
//        }

//        void exit() {
//            geodb_assert(m_root && m_top, "invalid state");
//            geodb_assert(m_frames.size() > 0, "stack is empty");

//            frame& f = m_frames.back();
//            double duration = std::chrono::duration_cast<double_seconds>(
//                        clock::now() - f.m_time_start).count();
//            u64 bytes_written = tpie::get_bytes_written() - f.m_bytes_written_start;
//            u64 bytes_read = tpie::get_bytes_read() - f.m_bytes_read_start;

//            m_top->count + 1;
//            m_top->bytes_read += bytes_read;
//            m_top->bytes_written += bytes_written;
//            m_top->duration += duration;

//            // Pop the frame and move the tree pointer.
//            m_frames.pop_back();
//            m_top = m_top->parent;
//        }

//    private:
//        std::unique_ptr<node> m_root = nullptr; ///< Root of the tree.
//        node* m_top = nullptr;                  ///< Current node in the tree.
//        std::vector<frame> m_frames;            ///< Frame stacks (for recording bytes & durations).
//    };

//public:
//    stats_guard(std::string name)
//    {
//    }

//    ~stats_guard() {

//    }

//    stats_guard(const stats_guard&) = delete;
//    stats_guard& operator=(const stats_guard&) = delete;

//private:
//    void handle_child(stats_entry entry) {

//    }

//private:
//};

#ifdef GEODB_DEBUG_STATS
    // Define a stats guard instance with the given variable name and
    // a format specifier. Example: STATS_GUARD(guard, "Hello {}", "World");
    #define STATS_GUARD(Name, ...) stats_guard Name(fmt::format(__VA_ARGS__))
    #define STATS_PRINT(Name, ...) (Name).print(__VA_ARGS__)
#else
    #define STATS_GUARD(Name, ...) unused(__VA_ARGS__)
    #define STATS_PRINT(Name, ...) unused(__VA_ARGS__)
#endif

class stats_guard {
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using double_seconds = std::chrono::duration<double>;

    static thread_local int t_indent;

    static void print_indented(int indent, const std::string& message) {
        int spaces = indent * 2;

        auto pos = message.begin();
        auto end = message.end();
        if (pos == end)
            return;

        std::string result;
        while (pos != end) {
            auto newline = std::find(pos, end, '\n');
            if (newline != end) {
                ++newline;
            }

            result += "-- ";
            for (int i = 0; i < spaces; ++i) {
                result += ' ';
            }

            result.append(pos, newline);
            pos = newline;
        }
        if (!result.empty() && result.back() != '\n') {
            result += '\n';
        }
        print_impl(result);
    }

    static void print_impl(const std::string& result);

public:
    stats_guard(std::string name)
        : m_indent(t_indent)
        , m_name(std::move(name))
        , m_bytes_read(tpie::get_bytes_read())
        , m_bytes_written(tpie::get_bytes_written())
        , m_time(clock::now())
    {
        print("Entering \"{}\".", m_name);
        ++t_indent;
    }

    ~stats_guard() {
        --t_indent;

        u64 read = tpie::get_bytes_read() - m_bytes_read;
        u64 written = tpie::get_bytes_written() - m_bytes_written;
        u64 block_size = tpie::get_block_size();
        u64 blocks_read = ceil(read, block_size);
        u64 blocks_written = ceil(written, block_size);
        double duration = std::chrono::duration_cast<double_seconds>(
                    clock::now() - m_time).count();
        print("Leaving \"{}\".\n"
              "  * Blocks read: {}\n"
              "  * Blocks written: {}\n"
              "  * Blocks total: {}\n"
              "  * Duration: {:.4f} s",
              m_name,
              blocks_read,
              blocks_written,
              blocks_read + blocks_written,
              duration);
    }

    stats_guard(const stats_guard&) = delete;

    stats_guard& operator=(const stats_guard&) = delete;

    template<typename... Args>
    void print(const char* format, Args&&... args) {
        print_indented(m_indent, fmt::format(format, std::forward<Args>(args)...));
    }

private:

    u64 ceil(u64 n, u64 div) {
        geodb_assert(div > 1, "");
        return (n + div - 1) / div;
    }

private:
    int m_indent;
    std::string m_name;
    u64 m_bytes_read;
    u64 m_bytes_written;
    time_point m_time;
};

} // namespace geodb

#endif // GEODB_UTILITY_STATS_GUARD_HPP
