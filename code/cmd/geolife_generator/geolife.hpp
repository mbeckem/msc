#ifndef GEOLIFE_HPP
#define GEOLIFE_HPP

#include "common/common.hpp"

#include "geodb/trajectory.hpp"
#include "geodb/irwi/base.hpp"
#include "geodb/irwi/string_map.hpp"
#include "geodb/irwi/string_map_external.hpp"

#include <tpie/file_stream.h>
#include <tpie/progress_indicator_base.h>

#include <vector>

/// Parses the geolife dataset located at `path`.
/// Maps string labels to integers using the `labels` object.
/// Trajectories are written to `out`.
void parse_geolife(const geodb::fs::path& path, external_string_map& labels,
                   tpie::file_stream<geodb::tree_entry>& out,
                   std::ostream& log,
                   tpie::progress_indicator_base& progress);

#endif // GEOLIFE_HPP
