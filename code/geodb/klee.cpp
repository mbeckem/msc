#include "geodb/klee.hpp"

#include "geodb/interval.hpp"
#include "geodb/utility/range_utils.hpp"

#include <boost/functional/hash.hpp>

#include <memory>

namespace geodb {

namespace {

using interval_t = interval<double>;

size_t log2_ceil(size_t value) {
    geodb_assert(value != 0, "invalid value");

    size_t log2 = 0;
    value--;
    while (value != 0) {
        ++log2;
        value /= 2;
    }
    return log2;
}

class segment_tree {
    struct node {
        /// The interval that this node represents.
        interval_t interval;

        /// The current width of the union of all intervals
        /// active in the subtree rooted at this node.
        double union_width = 0;

        /// Number of active intervals that contain this interval.
        size_t count = 0;

        /// True if this node is a leaf node.
        bool leaf = false;
    };

public:
    /// Construct a new, empty segment tree from the given iterator range.
    ///
    /// The range must contain the Universe of endpoints, i.e. every
    /// possible interval end point.
    /// The points must be sorted in ascending order and must be unique.
    /// The range must have at least two points.
    template<typename RndIter>
    segment_tree(RndIter begin, RndIter end)
    {
        reset(begin, end);
    }

    /// Replaces the structure of this tree using the points
    /// in the given sorted range.
    /// Has the same requirements and effects as the constructor,
    /// but reuses the allocated memory.
    template<typename RndIter>
    void reset(RndIter begin, RndIter end) {
        geodb_assert(std::distance(begin, end) >= 2,
                     "Must have at least two endpoints");

        // Allocate enough space for a complete binary tree.
        size_t leaves = std::distance(begin, end) - 1;
        size_t full_leaves = 1 << log2_ceil(leaves);
        size_t max_size = full_leaves * 2 - 1;

        m_nodes.resize(max_size);
        build(at(0), begin, end - 1);
    }

    /// Sets the given interval to "active", i.e. counting it
    /// in the calculation of the union width.
    /// Only intervals with endpoints in the previously registered universe
    /// can be inserted.
    void insert(const interval_t& interval) {
        insert(at(0), interval);
    }

    /// Removes the given interval, no longer counting it
    /// in the calculation of the union width.
    /// Only intervals that have previously been inserted can be removed.
    void remove(const interval_t& interval) {
        remove(at(0), interval);
    }

    double union_width() const {
        return at(0).union_width;
    }

private:
    /// Recursive build function.
    /// Both begin and end are valid iterators, i.e. its an inclusive range.
    /// All intervals [p1, p2] where p1, p2 are in the original input sequence
    /// will be represented by leaf nodes.
    template<typename Iter>
    void build(node& n, Iter begin, Iter end) {
        geodb_assert(begin != end, "Range is empty");

        n.count = 0;
        n.union_width = 0;
        n.leaf = (begin + 1 == end);
        if (n.leaf) {
            n.interval = interval_t(*begin, *end);
        } else {
            node& l = left(n);
            node& r = right(n);

            Iter mid = begin + (end - begin) / 2;
            build(l, begin, mid);
            build(r, mid, end);
            n.interval = interval_t(l.interval.begin(), r.interval.end());
        }
    }

    void insert(node& n, const interval_t& interval) {
        if (interval.contains(n.interval)) {
            n.count += 1;
            if (n.count == 1) {
                n.union_width = n.interval.end() - n.interval.begin();
            }
        } else {
            geodb_assert(!n.leaf, "must not be a leaf");

            node& l = left(n);
            node& r = right(n);

            if (go_left(l.interval, interval)) {
                insert(l, interval);
            }
            if (go_right(r.interval, interval)) {
                insert(r, interval);
            }
            if (n.count == 0) {
                n.union_width = l.union_width + r.union_width;
            }
        }
    }

    void remove(node& n, const interval_t& interval) {
        if (interval.contains(n.interval)) {
            n.count -= 1;
            if (n.count == 0) {
                n.union_width = n.leaf ? 0
                                       : left(n).union_width + right(n).union_width;
            }
        } else {
            geodb_assert(!n.leaf, "must not be a leaf");

            node& l = left(n);
            node& r = right(n);

            if (go_left(l.interval, interval)) {
                remove(l, interval);
            }
            if (go_right(r.interval, interval)) {
                remove(r, interval);
            }
            if (n.count == 0) {
                n.union_width = l.union_width + r.union_width;
            }
        }
    }

    bool go_left(const interval_t& left, const interval_t& interval) const {
        return interval.begin() < left.end();
    }

    bool go_right(const interval_t& right, const interval_t& interval) const {
        return interval.end() > right.begin();
    }

    node& left(node& n) {
        geodb_assert(!n.leaf, "n has no children");
        return at(index(n) * 2 + 1);
    }

    node& right(node& n) {
        geodb_assert(!n.leaf, "n has no children");
        return at(index(n) * 2 + 2);
    }

    node& at(size_t index) {
        geodb_assert(index < m_nodes.size(), "index out of bounds");
        return m_nodes[index];
    }

    const node& at(size_t index) const {
        geodb_assert(index < m_nodes.size(), "index out of bounds");
        return m_nodes[index];
    }

    size_t index(node& n) {
        return &n - m_nodes.data();
    }

private:
    std::vector<node> m_nodes;
};

enum event_type {
    open, close
};

struct event2d {
    event_type type;
    double x;
    interval_t y;

    event2d(event_type type, double x, const interval_t& y)
        : type(type)
        , x(x)
        , y(y)
    {}

    bool operator<(const event2d& other) const {
        if (x == other.x) {
            return type < other.type;
        }
        return x < other.x;
    }
};

struct event3d {
    event_type type;
    double x;
    rect2d r;

    event3d(event_type type, double x, const rect2d& r)
        : type(type)
        , x(x)
        , r(r)
    {}

    bool operator<(const event3d& other) const {
        if (x == other.x) {
            return type < other.type;
        }
        return x < other.x;
    }
};

} // namespace

/// Constructs the segment tree with the universe of all (unique)
/// rectangle y corner points.
static segment_tree build_segment_tree(const std::vector<rect2d>& rects) {
    std::vector<double> yvalues;
    yvalues.reserve(rects.size() * 2);
    for (const rect2d& rect : rects) {
        if (!rect.empty()) {
            yvalues.push_back(rect.min().y());
            yvalues.push_back(rect.max().y());
        }
    }

    std::sort(yvalues.begin(), yvalues.end());
    yvalues.erase(std::unique(yvalues.begin(), yvalues.end()), yvalues.end());

    return segment_tree(yvalues.begin(), yvalues.end());
}

static std::vector<event2d> rectangle_events(const std::vector<rect2d>& rects) {
    std::vector<event2d> events;
    events.reserve(2 * rects.size());
    for (const rect2d& rect : rects) {
        if (!rect.empty()) {
            interval_t y(rect.min().y(), rect.max().y());
            events.emplace_back(open, rect.min().x(), y);
            events.emplace_back(close, rect.max().x(), y);
        }
    }

    std::sort(events.begin(), events.end());
    return events;
}

static std::vector<event3d> rectangle_events(const std::vector<rect3d>& rects) {
    std::vector<event3d> events;
    events.reserve(2 * rects.size());
    for (const rect3d& rect : rects) {
        if (!rect.empty()) {
            rect2d rect2(vector2d(rect.min().y(), rect.min().z()),
                         vector2d(rect.max().y(), rect.max().z()));
            events.emplace_back(open, rect.min().x(), rect2);
            events.emplace_back(close, rect.max().x(), rect2);
        }
    }

    std::sort(events.begin(), events.end());
    return events;
}

double union_area(const std::vector<rect2d>& rects) {
    if (rects.size() == 0) {
        return 0;
    }

    segment_tree tree = build_segment_tree(rects);
    std::vector<event2d> events = rectangle_events(rects);

    double area = 0.0;
    double lastx = 0;
    for (const event2d& e : events) {
        // Works even for the first iteration because tree.union_width() will be 0.
        area += (e.x - lastx) * tree.union_width();
        if (e.type == open) {
            tree.insert(e.y);
        } else {
            tree.remove(e.y);
        }
        lastx = e.x;
    }
    return area;
}

double union_area(const std::vector<rect3d>& rects) {
    if (rects.size() == 0) {
        return 0;
    }

    std::vector<event3d> events = rectangle_events(rects);
    std::vector<rect2d> active;
    active.reserve(rects.size());

    double area = 0;
    double lastx = 0;
    for (const event3d& e : events) {
        area += (e.x - lastx) * union_area(active);
        if (e.type == open) {
            active.push_back(e.r);
        } else {
            auto pos = std::find(active.begin(), active.end(), e.r);
            geodb_assert(pos != active.end(), "rectangle must be active");
            active.erase(pos);
        }
        lastx = e.x;
    }

    return area;
}

} // namespace geodb
