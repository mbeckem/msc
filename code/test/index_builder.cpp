#include <catch.hpp>

#include "geodb/irwi/inverted_index.hpp"
#include "geodb/irwi/inverted_index_external.hpp"
#include "geodb/utility/temp_dir.hpp"

using namespace geodb;

constexpr size_t block_size = 512;
constexpr u32 Lambda = 2;

using index_storage = inverted_index_external<block_size>;
using index_type = inverted_index<index_storage, Lambda>;
using list_type = index_type::list_type;
using list_ptr = index_type::list_ptr;
using posting_type = index_type::posting_type;

TEST_CASE("bulk load inverted index") {
    temp_dir dir;
    tpie::temp_file block_file;

    block_collection<block_size> blocks(block_file.path());
    blocks.get_free_block();
    {

        inverted_index_external_builder<block_size, Lambda> builder(dir.path(), blocks);

        list_type& total = builder.total();
        for (int j = 0; j < 10; ++j) {
            posting_type p(j);
            p.count(j + 1);
            total.append(p);
        }

        for (label_type i = 1; i <= 10000; ++i) {
            list_type list = builder.push(i);

            for (int j = 0; j < 10; ++j) {
                posting_type p(j);
                p.count(j * 2);
                list.append(p);
            }
        }

        builder.build();
    }

    index_type index(index_storage(dir.path(), blocks));
    label_type i = 1;
    for (const auto& entry : index) {
        label_type label = entry.label();
        list_ptr list = entry.postings_list();

        if (i != label) {
            FAIL("expected label " << i << " but saw " << label);
        }

        u32 j = 0;
        for (auto&& p : *list) {
            if (p.node() != j) {
                FAIL("expected node " << j << " but saw " << p.node());
            }
            if (p.count() != j * 2) {
                FAIL("expected count " << (j * 2) << " but saw " << p.count());
            }
            ++j;
        }
        if (j != 10) {
            FAIL("expected to read j = 10 but only reached " << j);
        }

        ++i;
    }
    if (i != 10001) {
        FAIL("labels went missing, i = " << i);
    }

    list_ptr total = index.total();
    u32 j = 0;
    for (auto&& p : *total) {
        if (p.node() != j) {
            FAIL("expected node " << j << " but saw " << p.node());
        }
        if (p.count() != j + 1) {
            FAIL("expected count " << (j + 1) << " but saw " << p.count());
        }
        ++j;
    }
}
