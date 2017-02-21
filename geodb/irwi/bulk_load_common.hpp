#ifndef IRWI_BULK_LOAD_COMMON_HPP
#define IRWI_BULK_LOAD_COMMON_HPP

#include "geodb/common.hpp"
#include "geodb/bounding_box.hpp"
#include "geodb/trajectory.hpp"

#include <tpie/serialization2.h>

namespace geodb {

template<typename State>
class bulk_load_common {
protected:
    using state_type = State;

    using node_ptr = typename state_type::node_ptr;

    using list_summary = typename state_type::list_type::summary_type;

    /// The precomputed summary of a lower-level node.
    struct node_summary {
        node_ptr ptr{};
        bounding_box mbb;
        list_summary total;     ///< Summary of `index->total`
        u64 num_labels = 0;     ///< This many label summaries follow.

        template<typename Dst>
        friend void serialize(Dst& dst, const node_summary& n) {
            using tpie::serialize;
            serialize(dst, n.ptr);
            serialize(dst, n.mbb);
            serialize(dst, n.total);
            serialize(dst, n.num_labels);
        }

        template<typename Dst>
        friend void unserialize(Dst& dst, node_summary& n) {
            using tpie::unserialize;
            unserialize(dst, n.ptr);
            unserialize(dst, n.mbb);
            unserialize(dst, n.total);
            unserialize(dst, n.num_labels);
        }
    };

    /// The precomputed summary of a <label, postings_list> pair.
    struct label_summary {
        label_type label{};
        list_summary summary;

        template<typename Dst>
        friend void serialize(Dst& dst, const label_summary& s) {
            using tpie::serialize;
            serialize(dst, s.label);
            serialize(dst, s.summary);
        }

        template<typename Src>
        friend void unserialize(Src& src, label_summary& s) {
            using tpie::unserialize;
            unserialize(src, s.label);
            unserialize(src, s.summary);
        }
    };
};

} // namespace geodb

#endif // IRWI_BULK_LOAD_COMMON_HPP
